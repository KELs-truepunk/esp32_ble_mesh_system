#include <stdio.h>           // Для стандартного ввода-вывода (printf)
#include <string.h>          // Для работы со строками и памятью (memcpy, strlen)
#include <stdlib.h>          // Для динамического выделения памяти (malloc, free)
#include "nvs_flash.h"       // Для работы с энергонезависимой памятью (Flash)
#include "esp_log.h"         // Для красивых цветных логов в консоли
#include "esp_bt.h"          // Управление контроллером Bluetooth (низкий уровень)
#include "esp_bt_main.h"     // Управление Bluetooth-стеком Bluedroid (верхний уровень)
#include "esp_gap_ble_api.h" // API для GAP (вещание, сканирование)
#include "esp_gatts_api.h"   // API для GATT (подключение телефона, сервисы)

#include "ble_mesh_protocol.h"  // Наша структура бинарного пакета (13 байт)
#include "ble_mesh_transport.h" // Заголовочный файл транспорта

static const char *TAG = "BITCHAT_TRANSPORT";

#define PROFILE_NUM 1
#define PROFILE_A_APP_ID 0
#define BITCHAT_SVC_UUID 0x00FF
#define BITCHAT_CHAR_UUID 0xFF01

// Наш локальный ID узла для жесткого фильтра собственного эха
#define MY_NODE_SENDER_ID 0x77777777 

// --- Единый кэш дубликатов ---
#define ROUTER_CACHE_SIZE 25

typedef struct
{
    uint32_t sender_id;
    uint16_t packet_id;
} router_cache_t;

static router_cache_t r_cache[ROUTER_CACHE_SIZE];
static int r_cache_idx = 0;

static uint16_t b_char_handle = 0; // Хэндл характеристики для работы с Notify смартфона

// Настройка параметров GAP вещания (Передатчик)
static esp_ble_adv_params_t hybrid_adv_params = {
    .adv_int_min = 0x20,      // Минимальный интервал вещания (32 * 0.625 мс = 20 мс)
    .adv_int_max = 0x20,      // Максимальный интервал вещания (20 мс)
    .adv_type = ADV_TYPE_IND, // Разрешаем входящие подключения от телефонов
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL, // Вещаем на всех трех частотных каналах (37, 38, 39)
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Настройка параметров GAP сканирования (Приемник)
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50, // 50 мс
    .scan_window = 0x30,   // 30 мс
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
};

// Структура для управления GATT профилем
struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    uint16_t perm;
    uint16_t property;
};

static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gatts_cb = gatts_profile_a_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
        .conn_id = 0xFFFF, // ФИКС #1: По умолчанию подключения нет!
    },
};

// Вспомогательная функция добавления пакета в кэш дубликатов
static void add_to_router_cache(uint32_t sender_id, uint16_t packet_id)
{
    r_cache[r_cache_idx].sender_id = sender_id;
    r_cache[r_cache_idx].packet_id = packet_id;
    r_cache_idx = (r_cache_idx + 1) % ROUTER_CACHE_SIZE;
}

// Отправка бинарного пакета нашей сети в радиоэфир
void ble_mesh_broadcast_packet(b_mesh_packet_t *packet)
{
    if (packet == NULL) return;

    uint8_t raw_adv_data[31] = {0};
    uint8_t idx = 0;

    // 1. BLE Flags
    raw_adv_data[idx++] = 2;
    raw_adv_data[idx++] = ESP_BLE_AD_TYPE_FLAG;
    raw_adv_data[idx++] = ESP_BLE_ADV_FLAG_NON_LIMIT_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;

    // 2. Manufacturer Specific Data (0xFF)
    raw_adv_data[idx++] = sizeof(b_mesh_packet_t) + 2 + 1;
    raw_adv_data[idx++] = 0xFF;

    // Company ID (0xFFFF)
    raw_adv_data[idx++] = (BITCHAT_COMPANY_ID & 0xFF);
    raw_adv_data[idx++] = ((BITCHAT_COMPANY_ID >> 8) & 0xFF);

    // Копируем пакет
    memcpy(&raw_adv_data[idx], packet, sizeof(b_mesh_packet_t));
    idx += sizeof(b_mesh_packet_t);

    ESP_LOGI(TAG, "Трансляция пакета в эфир: ID=%d, TTL=%d, Type=0x%02X",
             packet->message_id, packet->ttl, packet->packet_type);

    esp_ble_gap_config_adv_data_raw(raw_adv_data, idx);
}

void relay_mesh_packet(b_mesh_packet_t *packet)
{
    ble_mesh_broadcast_packet(packet);
}

void pack_mesh_raw_data(void)
{
    b_mesh_packet_t startup_packet;
    uint8_t hello_msg[5] = {'S', 'T', 'A', 'R', 'T'};

    build_mesh_packet(&startup_packet, PACKET_TYPE_MSG, 1, 4, 0x11111111, hello_msg);
    add_to_router_cache(0x11111111, 1);
    ble_mesh_broadcast_packet(&startup_packet);
}

// ==========================================
// ОБРАБОТЧИК СОБЫТИЙ GAP
// ==========================================
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&hybrid_adv_params);
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
                if (block_len == 0 || (i + block_len + 1 > adv_len)) break;

                uint8_t adv_type = adv_data[i + 1];
                uint8_t *payload = &adv_data[i + 2];
                uint8_t payload_len = block_len - 1;

                if (adv_type == 0xFF && payload_len == 15)
                {
                    uint16_t comp_id = (payload[1] << 8) | payload[0];

                    if (comp_id == 0xFFFF)
                    {
                        b_mesh_packet_t incoming_packet;
                        memcpy(&incoming_packet, &payload[2], sizeof(b_mesh_packet_t));

                        uint8_t packet_ttl = incoming_packet.ttl;
                        uint16_t packet_id = incoming_packet.message_id;
                        uint8_t packet_type = incoming_packet.packet_type;
                        uint32_t sender_id = incoming_packet.sender_id;

                        // 1. Фильтр дубликатов по кэшу
                        bool is_duplicate = false;
                        for (int k = 0; k < ROUTER_CACHE_SIZE; k++)
                        {
                            if (r_cache[k].sender_id == sender_id && r_cache[k].packet_id == packet_id)
                            {
                                is_duplicate = true;
                                break;
                            }
                        }

                        if (!is_duplicate)
                        {
                            // Сохраняем пакет в кэш
                            add_to_router_cache(sender_id, packet_id);

                            // 2. Проброс в GATT только если реальное соединение открыто!
                            if (gl_profile_tab[PROFILE_A_APP_ID].conn_id != 0xFFFF && b_char_handle != 0)
                            {
                                esp_err_t notify_err = esp_ble_gatts_send_indicate(
                                    gl_profile_tab[PROFILE_A_APP_ID].gatts_if,
                                    gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                    b_char_handle,
                                    sizeof(incoming_packet.payload),
                                    incoming_packet.payload,
                                    false
                                );

                                if (notify_err == ESP_OK) {
                                    ESP_LOGI("GATT_BRIDGE", "Пакет #%d проброшен на телефон!", packet_id);
                                }
                            }

                            // Лог перехвата
                            ESP_LOGI("MESH_ROUTER", "=================================================");
                            ESP_LOGI("MESH_ROUTER", "ПАКЕТ от MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
                            ESP_LOGW("MESH_ROUTER", "ID: #%d | TTL: %d | Type: 0x%02X | Sender: 0x%08X",
                                     packet_id, packet_ttl, packet_type, (unsigned int)sender_id);
                            ESP_LOGI("MESH_ROUTER", "Payload: %.5s", incoming_packet.payload);
                            ESP_LOGI("MESH_ROUTER", "=================================================");

                            // 3. Ретрансляция (Relay)
                            if (packet_ttl > 1)
                            {
                                incoming_packet.ttl--;
                                ESP_LOGW("MESH_ROUTER", "Ретрансляция пакета #%d. Новый TTL: %d",
                                         incoming_packet.message_id, incoming_packet.ttl);
                                relay_mesh_packet(&incoming_packet);
                            }
                            else
                            {
                                ESP_LOGI("MESH_ROUTER", "Пакет #%d дропнут: TTL исчерпан", packet_id);
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

// ==========================================
// ОБРАБОТЧИК СОБЫТИЙ GATT-СЕРВЕРА
// ==========================================
static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = BITCHAT_SVC_UUID;
        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, 4);
        break;

    case ESP_GATTS_CREATE_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);

        esp_bt_uuid_t char_uuid = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = BITCHAT_CHAR_UUID};
        esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        b_char_handle = param->add_char.attr_handle;
        break;

    case ESP_GATTS_CONNECT_EVT:
        // ФИКС #2: Запоминаем conn_id активного подключения!
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
        ESP_LOGW(TAG, "Есть подключение со смартфоном вожатого! conn_id=%d", param->connect.conn_id);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        // ФИКС #3: Сбрасываем conn_id и перезапускаем вещание для переподключения
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = 0xFFFF;
        ESP_LOGI(TAG, "Смартфон отключился. Перезапуск ADV...");
        esp_ble_gap_start_advertising(&hybrid_adv_params);
        break;

    case ESP_GATTS_WRITE_EVT:
    {
        uint16_t incoming_len = param->write.len;
        char *dynamic_buffer = (char *)malloc(incoming_len + 1);
        if (dynamic_buffer != NULL)
        {
            memcpy(dynamic_buffer, param->write.value, incoming_len);
            dynamic_buffer[incoming_len] = '\0';

            ESP_LOGI(TAG, "Данные из приложения: \"%s\"", dynamic_buffer);

            b_mesh_packet_t phone_pkt;
            uint8_t payload[5] = {0};

            size_t copy_bytes = (incoming_len > 5) ? 5 : incoming_len;
            memcpy(payload, dynamic_buffer, copy_bytes);

            static uint16_t msg_counter = 500;
            msg_counter++;

            // Собираем пакет
            build_mesh_packet(&phone_pkt, PACKET_TYPE_MSG, msg_counter, 4, MY_NODE_SENDER_ID, payload);

            // Сразу гасим эхо — заносим в собственный кэш перед выстрелом в эфир
            add_to_router_cache(MY_NODE_SENDER_ID, msg_counter);

            // Выплевываем в эфир
            ble_mesh_broadcast_packet(&phone_pkt);

            // Эхо-подтверждение смартфону по GATT
            esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, b_char_handle,
                                        incoming_len, (uint8_t *)dynamic_buffer, false);
            free(dynamic_buffer);
        }

        if (param->write.need_rsp)
        {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    }
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        }
        else
        {
            return;
        }
    }
    if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[PROFILE_A_APP_ID].gatts_if)
    {
        if (gl_profile_tab[PROFILE_A_APP_ID].gatts_cb)
        {
            gl_profile_tab[PROFILE_A_APP_ID].gatts_cb(event, gatts_if, param);
        }
    }
}

void ble_mesh_transport_init(void)
{
    ESP_LOGI(TAG, "Запуск и конфигурация BLE стека...");

    memset(r_cache, 0, sizeof(r_cache));
    r_cache_idx = 0;

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(PROFILE_A_APP_ID);

    esp_ble_gap_set_scan_params(&ble_scan_params);

    pack_mesh_raw_data();
}