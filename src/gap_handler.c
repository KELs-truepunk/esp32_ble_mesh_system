#include <stdio.h>   // Для стандартного ввода-вывода (printf)
#include <string.h>  // Для работы со строками и памятью (memcpy, strlen)
#include "esp_log.h" // Для красивых цветных логов в консоли
#include "gap_handler.h"
#include "legacy_gatt.h"
#include "esp_bt_main.h"
#include "ble_mesh_transport.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static QueueHandle_t mesh_tx_queue = NULL;
static uint8_t pending_adv_data[31];
static uint8_t pending_adv_len = 0;

static const char *TAG = "GAP_HANDLER";

// eдиный кэш дубликатов по 32-битному хешу FNV-1a
static uint32_t r_cache[ROUTER_CACHE_SIZE];
static int r_cache_idx = 0; // счетчик r_cache
// Чтобы передать байты пакета из функции ble_mesh_broadcast_packet в событие остановки рекламы, сохраняем их во временный буфер:
static uint8_t pending_adv_data[31];
static uint8_t pending_adv_len = 0;

// Буфер сборки сегментированных сообщений
typedef struct
{
    uint16_t seq_num;
    uint32_t sender_id;
    uint8_t total_segs;
    uint16_t rx_mask; // Битовая маска собранных сегментов
    char buffer[15 * PAYLOAD_SIZE + 1];
} mesh_reassembly_t;
static mesh_reassembly_t rx_session = {0};

// настройка параметров GAP вещания (на передачу)
esp_ble_adv_params_t hybrid_adv_params = {
    .adv_int_min = 0x30,      // мин интервал вещания (32 * 0.625 мс = 20 мс)
    .adv_int_max = 0x40,      // макс интервал вещания (20 мс)
    .adv_type = ADV_TYPE_IND, // разрешение на входящие подключения от телефонов
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL, // вещание на всех трех частотных каналах (37, 38, 39)
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
void mesh_tx_task(void *pvParameters)
{
    b_mesh_packet_t pkt;

    while (1)
    {
        if (xQueueReceive(mesh_tx_queue, &pkt, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI("MESH_TX", "Извлечен из очереди Seg [%d/%d], Seq=%d",
                     pkt.seg_current + 1, pkt.seg_total, pkt.seq_num);

            uint8_t idx = 0;
            memset(pending_adv_data, 0, sizeof(pending_adv_data));

            // Собираем стандартный BLE ADV payload
            pending_adv_data[idx++] = 2;
            pending_adv_data[idx++] = ESP_BLE_AD_TYPE_FLAG;
            pending_adv_data[idx++] = ESP_BLE_ADV_FLAG_NON_LIMIT_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;

            pending_adv_data[idx++] = sizeof(b_mesh_packet_t) + 3; // +2 байта CID, +1 байт Type 0xFF
            pending_adv_data[idx++] = 0xFF;                        // Manufacturer Specific Data

            pending_adv_data[idx++] = (MESH_COMPANY_ID & 0xFF);
            pending_adv_data[idx++] = ((MESH_COMPANY_ID >> 8) & 0xFF);

            memcpy(&pending_adv_data[idx], &pkt, sizeof(b_mesh_packet_t));
            idx += sizeof(b_mesh_packet_t);
            pending_adv_len = idx;

            // Триггерим цепочку событий: Stop ADV -> Config RAW Data -> Start ADV
            esp_ble_gap_stop_advertising();

            // Даем пакету покрутиться в эфире 150 мс перед следующим сегментом
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }
}

void init_mesh_tx_system(void)
{
    if (mesh_tx_queue == NULL)
    {
        mesh_tx_queue = xQueueCreate(16, sizeof(b_mesh_packet_t));
        xTaskCreate(mesh_tx_task, "mesh_tx_task", 3072, NULL, 5, NULL);
        ESP_LOGI("MESH_TX", "Система TX-очереди инициализирована");
    }
}
// Вспомогательная функция добавления хеша пакета в кэш дубликатов
void add_to_router_cache(uint32_t packet_hash)
{
    r_cache[r_cache_idx] = packet_hash;
    r_cache_idx = (r_cache_idx + 1) % ROUTER_CACHE_SIZE;
}

// Проверка наличия хеша в кэше
bool is_in_router_cache(uint32_t packet_hash)
{
    for (int k = 0; k < ROUTER_CACHE_SIZE; k++)
    {
        if (r_cache[k] == packet_hash)
        {
            return true;
        }
    }
    return false;
}

// Отправка бинарного пакета нашей сети в очередь
void ble_mesh_broadcast_packet(b_mesh_packet_t *packet)
{
    if (packet == NULL || mesh_tx_queue == NULL) return;

    if (xQueueSend(mesh_tx_queue, packet, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGE("MESH_TX", "Переполнение TX-очереди! Пакет Seq=%d потерян", packet->seq_num);
    }
}
// Логика маршрутизации
void relay_mesh_packet(b_mesh_packet_t *packet)
{
    if (packet == NULL)
        return;

    // Рандомный Jitter от 10 до 50 мс для защиты от коллизий при одновременной ретрансляции
    uint32_t jitter_ms = 10 + (esp_random() % 41);
    vTaskDelay(pdMS_TO_TICKS(jitter_ms));

    ble_mesh_broadcast_packet(packet);
}

void pack_mesh_raw_data(void)
{
    b_mesh_packet_t startup_packet;
    uint8_t hello_msg[PAYLOAD_SIZE] = {'S', 'T', 'A', 'R', 'T', '!', ' ', 'N', 'O', 'D', 'E'};

    build_mesh_packet(&startup_packet, PACKET_TYPE_MSG, 1, DEFAULT_TTL, 1, 0, 0x11111111, hello_msg);

    uint32_t init_hash = mesh_calc_hash(0x11111111, 1, 0);
    add_to_router_cache(init_hash);

    ble_mesh_broadcast_packet(&startup_packet);
}

// ОБРАБОТЧИК СОБЫТИЙ GAP
void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {

    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        // Шаг 3: Байты записаны в RAM! Запускаем рекламу
        esp_ble_gap_start_advertising(&hybrid_adv_params);
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        // Шаг 2: Радио заглушено! Теперь безопасно загружаем новые байты
        if (pending_adv_len > 0)
        {
            esp_ble_gap_config_adv_data_raw(pending_adv_data, pending_adv_len);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "Ошибка старта вещания: %d", param->adv_start_cmpl.status);
        }
        else
        {
            ESP_LOGI(TAG, "МЕШ-УЗЕЛ В ЭФИРЕ! Пакет обновлен / запущен.");
        }
        break;

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        esp_ble_gap_start_scanning(0);
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "Ошибка запуска сканера: %d", param->scan_start_cmpl.status);
        }
        else
        {
            ESP_LOGI(TAG, "ПРИЕМНИК ВКЛЮЧЕН! Слушаем эфир...");
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;

        if (scan_result->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT)
        {
            uint8_t *adv_data = scan_result->scan_rst.ble_adv;
            uint8_t adv_len = scan_result->scan_rst.adv_data_len;
            uint8_t *bda = scan_result->scan_rst.bda;

            uint8_t i = 0;
            while (i < adv_len)
            {
                uint8_t block_len = adv_data[i];
                if (block_len == 0 || (i + block_len + 1 > adv_len))
                    break;

                uint8_t adv_type = adv_data[i + 1];
                uint8_t *payload = &adv_data[i + 2];
                uint8_t payload_len = block_len - 1;

                //  b_mesh_packet_t = 21 байт
                if (adv_type == 0xFF && payload_len == (2 + sizeof(b_mesh_packet_t)))
                {
                    uint16_t comp_id = (payload[1] << 8) | payload[0];

                    if (comp_id == MESH_COMPANY_ID)
                    {
                        b_mesh_packet_t incoming_packet;
                        memcpy(&incoming_packet, &payload[2], sizeof(b_mesh_packet_t));

                        // uint8_t packet_ttl = incoming_packet.ttl;
                        uint16_t seq_num = incoming_packet.seq_num;
                        // uint8_t packet_type = incoming_packet.packet_type;
                        uint32_t sender_id = incoming_packet.sender_id;

                        // Считаем FNV-1a хеш пакета
                        uint32_t pkt_hash = mesh_calc_hash(sender_id, seq_num, incoming_packet.seg_current);

                        // 1. Фильтр дубликатов по хешу
                        if (!is_in_router_cache(pkt_hash))
                        {
                            // Сохраняем хеш в кэш
                            add_to_router_cache(pkt_hash);

                            // 1. СБОРКА СЕГМЕНТОВ (Reassembly)
                            if (rx_session.seq_num != incoming_packet.seq_num ||
                                rx_session.sender_id != incoming_packet.sender_id)
                            {
                                // Новый seq_num или отправитель — сбрасываем буфер
                                rx_session.seq_num = incoming_packet.seq_num;
                                rx_session.sender_id = incoming_packet.sender_id;
                                rx_session.total_segs = incoming_packet.seg_total;
                                rx_session.rx_mask = 0;
                                memset(rx_session.buffer, 0, sizeof(rx_session.buffer));
                            }

                            // Пишем сегмент в нужный офсет общего буфера
                            size_t offset = incoming_packet.seg_current * PAYLOAD_SIZE;
                            if (offset + PAYLOAD_SIZE <= sizeof(rx_session.buffer))
                            {
                                memcpy(rx_session.buffer + offset, incoming_packet.payload, PAYLOAD_SIZE);
                                rx_session.rx_mask |= (1 << incoming_packet.seg_current);
                            }

                            uint16_t target_mask = (1 << incoming_packet.seg_total) - 1;

                            // Лог перехвата отдельного сегмента
                            ESP_LOGI("MESH_ROUTER", "Перехвачен Seg [%d/%d] от MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                                     incoming_packet.seg_current + 1, incoming_packet.seg_total,
                                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

                            // 2. Если ВСЕ сегменты пакета собраны (маска совпала)
                            if ((rx_session.rx_mask & target_mask) == target_mask)
                            {
                                ESP_LOGW("MESH_ROUTER", "=================================================");
                                ESP_LOGW("MESH_ROUTER", ">>> СООБЩЕНИЕ СОБРАНО: \"%s\" <<<", rx_session.buffer);
                                ESP_LOGW("MESH_ROUTER", "=================================================");

                                // Пробрасываем ЦЕЛЬНОЕ собранное сообщение в смартфон по GATT
                                if (gl_profile_tab[PROFILE_A_APP_ID].conn_id != 0xFFFF && b_char_handle != 0)
                                {
                                    esp_ble_gatts_send_indicate(
                                        gl_profile_tab[PROFILE_A_APP_ID].gatts_if,
                                        gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                        b_char_handle,
                                        strlen(rx_session.buffer),
                                        (uint8_t *)rx_session.buffer,
                                        false);
                                }
                            }

                            // 3. Ретрансляция (Relay)
                            if (incoming_packet.ttl > 1)
                            {
                                incoming_packet.ttl--;
                                ESP_LOGW("MESH_ROUTER", "Ретрансляция пакета #%d. Новый TTL: %d",
                                         incoming_packet.seq_num, incoming_packet.ttl);
                                relay_mesh_packet(&incoming_packet);
                            }
                        }
                    }
                }
                i += block_len + 1;
            }
        }
        break;
    }
    default:
        break;
    }
}
