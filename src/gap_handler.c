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
#include "freertos/queue.h"

static const char *TAG = "GAP_HANDLER";
// семафоры
static SemaphoreHandle_t adv_config_sem = NULL;
static SemaphoreHandle_t adv_stop_sem = NULL;

// Буферы очереди и передачи сырых байт в эфир
static QueueHandle_t mesh_tx_queue = NULL;
static uint8_t pending_adv_data[31];
static uint8_t pending_adv_len = 0;

// Единый кэш дубликатов по 32-битному хешу FNV-1a
static uint32_t r_cache[ROUTER_CACHE_SIZE];
static int r_cache_idx = 0;

// Структура сообщения для очереди отправки
typedef struct
{
    b_mesh_packet_t pkt;
} mesh_tx_msg_t;

// Буфер сборки сегментированных сообщений
typedef struct
{
    uint16_t seq_num;
    uint32_t sender_id;
    uint8_t total_segs;
    uint16_t rx_mask;
    char buffer[15 * PAYLOAD_SIZE + 1];
} mesh_reassembly_t;
static mesh_reassembly_t rx_session = {0};

// Настройка параметров GAP вещания (на передачу)
esp_ble_adv_params_t hybrid_adv_params = {
    .adv_int_min = 0x30,      // 30 мс
    .adv_int_max = 0x40,      // 40 мс
    .adv_type = ADV_TYPE_IND, // Разрешение на входящие подключения
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL, // Каналы 37, 38, 39
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Функция упаковки структуры b_mesh_packet_t в сырой массив BLE ADV (26 байт)
static void prepare_raw_adv_buffer(const b_mesh_packet_t *pkt)
{
    // 1. Блок флагов BLE (3 байта)
    pending_adv_data[0] = 0x02; // Длина блока
    pending_adv_data[1] = 0x01; // Тип: Flags
    pending_adv_data[2] = 0x06; // General Discoverable + BR/EDR Not Supported

    // 2. Блок Manufacturer Specific Data (23 байта)
    // Длина: 1 байт (тип) + 2 байта (Company ID) + 19 байт (payload) = 22 байта (0x16)
    pending_adv_data[3] = 22;
    pending_adv_data[4] = 0xFF;                                     // Тип: Manufacturer Data
    pending_adv_data[5] = (uint8_t)(MESH_COMPANY_ID & 0xFF);        // 0xFF
    pending_adv_data[6] = (uint8_t)((MESH_COMPANY_ID >> 8) & 0xFF); // 0xFF

    // 3. Копируем 19 байт бинарного пакета нашей Mesh-сети
    memcpy(&pending_adv_data[7], pkt, sizeof(b_mesh_packet_t));

    // Итоговый размер пакета вещания: 3 + 1 + 1 + 2 + 19 = 26 байт (<= 31)
    pending_adv_len = 7 + sizeof(b_mesh_packet_t);
}

void mesh_tx_task(void *pvParameters)
{
    mesh_tx_msg_t tx_msg;
    for (;;)
    {
        if (xQueueReceive(mesh_tx_queue, &tx_msg, portMAX_DELAY) == pdTRUE)
        {
            // 1. Упаковываем пакет в сырые байты
            prepare_raw_adv_buffer(&tx_msg.pkt);

            // 2. Отправляем байты в контроллер и ЖДЕМ подтверждения от колбэка
            esp_ble_gap_config_adv_data_raw(pending_adv_data, pending_adv_len);
            xSemaphoreTake(adv_config_sem, portMAX_DELAY);

            // 3. Запускаем вещание
            esp_ble_gap_start_advertising(&hybrid_adv_params);

            // 4. ДЕРЖИМ ПАКЕТ В ЭФИРЕ 120 мс (за это время он уйдет несколько раз)
            vTaskDelay(pdMS_TO_TICKS(120));

            // 5. Останавливаем вещание и ЖДЕМ полной остановки радио
            esp_ble_gap_stop_advertising();
            xSemaphoreTake(adv_stop_sem, portMAX_DELAY);
        }
    }
}

void init_mesh_tx_system(void)
{
    if (mesh_tx_queue == NULL)
    {
        adv_config_sem = xSemaphoreCreateBinary();
        adv_stop_sem = xSemaphoreCreateBinary();

        mesh_tx_queue = xQueueCreate(16, sizeof(mesh_tx_msg_t));
        xTaskCreate(mesh_tx_task, "mesh_tx_task", 3072, NULL, 5, NULL);
    }
}

void add_to_router_cache(uint32_t packet_hash)
{
    r_cache[r_cache_idx] = packet_hash;
    r_cache_idx = (r_cache_idx + 1) % ROUTER_CACHE_SIZE;
}

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

void ble_mesh_broadcast_packet(b_mesh_packet_t *packet)
{
    if (packet == NULL || mesh_tx_queue == NULL)
        return;

    mesh_tx_msg_t msg;
    memcpy(&msg.pkt, packet, sizeof(b_mesh_packet_t));

    if (xQueueSend(mesh_tx_queue, &msg, 0) != pdTRUE)
    {
        ESP_LOGE("MESH_TX", "Переполнение TX-очереди! Пакет Seq=%d потерян", packet->seq_num);
    }
}

void relay_mesh_packet(b_mesh_packet_t *packet)
{
    if (packet == NULL)
        return;

    ble_mesh_broadcast_packet(packet);
}

// Отправка стартового приветственного пакета в сеть при загрузке
void pack_mesh_raw_data(void)
{
    b_mesh_packet_t startup_packet;
    uint8_t hello_msg[PAYLOAD_SIZE] = {'S', 'T', 'A', 'R', 'T', '!', ' ', 'N', 'O', 'D', 'E'};

    build_mesh_packet(&startup_packet, PACKET_TYPE_MSG, 1, DEFAULT_TTL, 1, 0, MY_NODE_SENDER_ID, hello_msg);

    uint32_t init_hash = mesh_calc_hash(MY_NODE_SENDER_ID, 1, 0);
    add_to_router_cache(init_hash);

    ble_mesh_broadcast_packet(&startup_packet);
}

// ОБРАБОТЧИК СОБЫТИЙ GAP
void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        // Радио заглохло — даем зеленый свет таску брать следующий пакет
        if (adv_stop_sem != NULL)
        {
            xSemaphoreGive(adv_stop_sem);
        }
        break;

    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        // Данные записаны — даем зеленый свет таску для запуска ADV
        if (adv_config_sem != NULL)
        {
            xSemaphoreGive(adv_config_sem);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "Ошибка старта ADV: %d", param->adv_start_cmpl.status);
        }
        else
        {
            ESP_LOGI(TAG, "ВЕЩАНИЕ ЗАПУЩЕНО! Пакет ушел в эфир.");
        }
        break;

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(TAG, "ПРИЕМНИК ВКЛЮЧЕН! Слушаем эфир...");
            esp_ble_gap_start_scanning(0);

            // Генерируем стартовый пакет, который запустит цепочку первой отправки
            pack_mesh_raw_data();
        }
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "Ошибка запуска сканера: %d", param->scan_start_cmpl.status);
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

                if (adv_type == 0xFF && payload_len == (2 + sizeof(b_mesh_packet_t)))
                {
                    uint16_t comp_id = (payload[1] << 8) | payload[0];

                    if (comp_id == MESH_COMPANY_ID)
                    {
                        b_mesh_packet_t incoming_packet;
                        memcpy(&incoming_packet, &payload[2], sizeof(b_mesh_packet_t));

                        uint16_t seq_num = incoming_packet.seq_num;
                        uint32_t sender_id = incoming_packet.sender_id;

                        uint32_t pkt_hash = mesh_calc_hash(sender_id, seq_num, incoming_packet.seg_current);

                        if (!is_in_router_cache(pkt_hash))
                        {
                            add_to_router_cache(pkt_hash);

                            if (rx_session.seq_num != incoming_packet.seq_num ||
                                rx_session.sender_id != incoming_packet.sender_id)
                            {
                                rx_session.seq_num = incoming_packet.seq_num;
                                rx_session.sender_id = incoming_packet.sender_id;
                                rx_session.total_segs = incoming_packet.seg_total;
                                rx_session.rx_mask = 0;
                                memset(rx_session.buffer, 0, sizeof(rx_session.buffer));
                            }

                            size_t offset = incoming_packet.seg_current * PAYLOAD_SIZE;
                            if (offset + PAYLOAD_SIZE <= sizeof(rx_session.buffer))
                            {
                                memcpy(rx_session.buffer + offset, incoming_packet.payload, PAYLOAD_SIZE);
                                rx_session.rx_mask |= (1 << incoming_packet.seg_current);
                            }

                            uint16_t target_mask = (uint16_t)((1U << incoming_packet.seg_total) - 1);

                            ESP_LOGI("MESH_ROUTER", "Перехвачен Seg [%d/%d] от MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                                     incoming_packet.seg_current + 1, incoming_packet.seg_total,
                                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

                            if ((rx_session.rx_mask & target_mask) == target_mask)
                            {
                                ESP_LOGW("MESH_ROUTER", "=================================================");
                                ESP_LOGW("MESH_ROUTER", ">>> СООБЩЕНИЕ СОБРАНО: \"%s\" <<<", rx_session.buffer);
                                ESP_LOGW("MESH_ROUTER", "=================================================");

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