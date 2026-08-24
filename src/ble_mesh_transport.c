#include <string.h>             // Для работы со строками и памятью (memcpy, strlen)
#include "esp_log.h"            // Для красивых цветных логов в консоли
#include "esp_bt.h"             // Управление контроллером Bluetooth (низкий уровень)
#include "esp_bt_main.h"        // Управление Bluetooth-стеком Bluedroid (верхний уровень)
#include "ble_mesh_protocol.h"  // обновленная структура бинарного пакета (14 байт)
#include "ble_mesh_transport.h" // Заголовочный файл транспорта
#include "gap_handler.h"
#include "legacy_gatt.h"
#include "nvs_flash.h" //флеш память есп32

static const char *TAG = "BLE_MESH_TRANSPORT";
static uint16_t global_seq_num = 0;
// Кольцевой буфер для сообщения HTTP-серверу
static mesh_web_msg_t web_msg_ring[WEB_MSG_BUFFER_SIZE];
static int web_msg_head = 0;
static int web_msg_tail = 0;
static portMUX_TYPE web_msg_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Настройка параметров GAP сканирования (Приемник)
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50, // 50 мс
    .scan_window = 0x3C,   // 37.5 мс
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE};

void ble_mesh_transport_init(void)
{
    ESP_LOGI(TAG, "Запуск и конфигурация BLE стека...");
    // Поднимаем очередь и таск ДО запуска стека Bluedroid
    init_mesh_tx_system();
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)); // освобождаем память от классик блютуза

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    // инит прослойки Bluedroid
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // регестрация калл-беков для GAP
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

    // ставим BLE передачу на +9dBm
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9));
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9));
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9));

    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&ble_scan_params));

    pack_mesh_raw_data();
}

esp_err_t ble_mesh_send_text(const char *text)
{
    if (!text)
        return ESP_ERR_INVALID_ARG;
    size_t len = strlen(text);
    if (len == 0)
        return ESP_ERR_INVALID_SIZE;

    uint8_t total_segs = (uint8_t)((len + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE);
    if (total_segs > 15)
        return ESP_ERR_INVALID_SIZE;

    uint16_t current_seq = global_seq_num++;

    for (uint8_t i = 0; i < total_segs; i++)
    {
        b_mesh_packet_t pkt;
        const char *chunk_ptr = text + (i * PAYLOAD_SIZE);

        build_mesh_packet(
            &pkt,
            (total_segs > 1) ? PACKET_TYPE_SEG : PACKET_TYPE_MSG,
            current_seq,
            DEFAULT_TTL,
            total_segs,
            i,
            MY_NODE_SENDER_ID,
            (const uint8_t *)chunk_ptr);

        // Хэшируем собственный пакет и кладем в r_cache, чтобы не зацикливать свой же ADV
        uint32_t self_hash = mesh_calc_hash(MY_NODE_SENDER_ID, current_seq, i);
        add_to_router_cache(self_hash);

        ble_mesh_broadcast_packet(&pkt);
    }
    return ESP_OK;
}

void mesh_push_web_message(uint32_t sender_id, uint16_t seq_num, const char *text)
{
    taskENTER_CRITICAL(&web_msg_spinlock);

    web_msg_ring[web_msg_head].sender_id = sender_id;
    web_msg_ring[web_msg_head].seq_num = seq_num;
    strncpy(web_msg_ring[web_msg_head].text, text, MAX_MESH_MSG_LEN - 1);
    web_msg_ring[web_msg_head].text[MAX_MESH_MSG_LEN - 1] = '\0';

    web_msg_head = (web_msg_head + 1) % WEB_MSG_BUFFER_SIZE;
    if (web_msg_head == web_msg_tail)
    {
        web_msg_tail = (web_msg_tail + 1) % WEB_MSG_BUFFER_SIZE;
    }

    taskEXIT_CRITICAL(&web_msg_spinlock);
}

bool mesh_pop_web_message(mesh_web_msg_t *out_msg)
{
    if (!out_msg)
        return false;

    taskENTER_CRITICAL(&web_msg_spinlock);
    if (web_msg_head == web_msg_tail)
    {
        taskEXIT_CRITICAL(&web_msg_spinlock);
        return false;
    }

    *out_msg = web_msg_ring[web_msg_tail];
    web_msg_tail = (web_msg_tail + 1) % WEB_MSG_BUFFER_SIZE;

    taskEXIT_CRITICAL(&web_msg_spinlock);
    return true;
}