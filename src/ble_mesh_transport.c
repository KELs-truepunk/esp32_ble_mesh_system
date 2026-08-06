#include <string.h>          // Для работы со строками и памятью (memcpy, strlen)
#include "esp_log.h"         // Для красивых цветных логов в консоли
#include "esp_bt.h"          // Управление контроллером Bluetooth (низкий уровень)
#include "esp_bt_main.h"     // Управление Bluetooth-стеком Bluedroid (верхний уровень)
#include "ble_mesh_protocol.h"  // обновленная структура бинарного пакета (14 байт)
#include "ble_mesh_transport.h" // Заголовочный файл транспорта
#include "gap_handler.h"
#include "legacy_gatt.h"
#include "nvs_flash.h" //флеш память есп32

static const char *TAG = "BLE_MESH_TRANSPORT";

// Настройка параметров GAP сканирования (Приемник)
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50, // 50 мс
    .scan_window = 0x30,   // 30 мс
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE //фильтр дубликатов отключен для приема уникальных seq_num
};


void ble_mesh_transport_init(void)
{
    ESP_LOGI(TAG, "Запуск и конфигурация BLE стека...");

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT); //освобождаем память от классик блютуза

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    
    //инит прослойки Bluedroid
    esp_bluedroid_init();
    esp_bluedroid_enable();
    
    //регестрация к алл-беков для GAP и GATT
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(PROFILE_A_APP_ID);
    
    //ставим BLE передачу на +9dBm
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9));
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9));
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9));
    
    esp_ble_gap_set_scan_params(&ble_scan_params);

    pack_mesh_raw_data();
}