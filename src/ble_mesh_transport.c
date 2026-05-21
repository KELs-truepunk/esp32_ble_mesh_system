#include <stdio.h>
#include <string.h>
#include "ble_mesh_transport.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"

static const char *TAG = "BITCHAT_GAP";

// Настройки GAP рекламы под Mesh-вещание
static esp_ble_adv_params_t mesh_adv_params = {
    .adv_int_min = 0x20, // 20 мс — минимально возможный интервал в BLE (максимальная частота спама)
    .adv_int_max = 0x20,
    .adv_type = ADV_TYPE_NONCONN_IND, // НЕПОДКЛЮЧАЕМАЯ реклама. Чистый пушечный выстрел в эфир.
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL, // Лупим во все 3 рекламных канала одновременно
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Единственный колбэк, который нам теперь нужен — обработчик событий GAP
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        // Сырой пакет успешно загружен в контроллер, даем команду: "Огонь в эфир!"
        esp_ble_gap_start_advertising(&mesh_adv_params);
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "Ошибка запуска вещания: код %d", param->adv_start_cmpl.status);
        }
        else
        {
            ESP_LOGI(TAG, "Широковещательный узел успешно запущен! Частота: ~20мс.");
        }
        break;

    default:
        break;
    }
}

// Функция ручной побайтовой сборки низкоуровневого пакета BLE (Максимум 31 байт!)
void pack_mesh_raw_data(void)
{
    uint8_t raw_adv_data[31] = {0};
    uint8_t idx = 0;

    // 1. Блок флагов (Обязателен по спецификации BLE, иначе телефоны проигнорируют пакет)
    raw_adv_data[idx++] = 2;                    // Длина этого подблока (тип + флаг = 2 байта)
    raw_adv_data[idx++] = ESP_BLE_AD_TYPE_FLAG; // Тип подблока: Флаги
    raw_adv_data[idx++] = ESP_BLE_ADV_FLAG_NON_LIMIT_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;

    // 2. Блок имени устройства
    const char *name = DEVICE_NAME;
    uint8_t name_len = strlen(name);
    raw_adv_data[idx++] = name_len + 1;              // Длина подблока (тип + строка)
    raw_adv_data[idx++] = ESP_BLE_AD_TYPE_NAME_CMPL; // Тип подблока: Полное локальное имя
    memcpy(&raw_adv_data[idx], name, name_len);
    idx += name_len;

    // 3. Блок НАШИХ КАСТОМНЫХ ДАННЫХ (Manufacturer Specific Data)
    // Именно сюда на 3 этапе мы заложим заголовки Mesh (ID, TTL, номер куска)
    const char *payload_text = "HELLO_MESH";
    uint8_t payload_len = strlen(payload_text);

    raw_adv_data[idx++] = payload_len + 3; // Длина (текст + 2б Company ID + 1б тип)
    raw_adv_data[idx++] = 0xFF;            // Тип подблока: Данные производителя

    // Первые два байта кастомных данных — идентификатор нашей сети (Company ID)
    raw_adv_data[idx++] = (BITCHAT_COMPANY_ID & 0xFF);
    raw_adv_data[idx++] = ((BITCHAT_COMPANY_ID >> 8) & 0xFF);

    // Копируем сам полезный текст сообщения
    memcpy(&raw_adv_data[idx], payload_text, payload_len);
    idx += payload_len;

    // Выводим в лог, сколько места мы сожрали
    ESP_LOGI(TAG, "Сборка пакета завершена. Размер пакета: %d байт из 31", idx);

    // Скачиваем собранный "бутерброд" прямо в радиоконтроллер
    esp_ble_gap_config_adv_data_raw(raw_adv_data, idx);
}

void ble_mesh_transport_init(void)
{
    ESP_LOGI(TAG, "Перевод радиомодуля в режим чистого GAP вещания...");

    // Высвобождаем память Classic BT под нужды кучи (Heap)
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    // Регистрируем ТОЛЬКО GAP обработчик (GATT-колбэки удалены)
    esp_ble_gap_register_callback(gap_event_handler);

    // Собираем и отправляем в стек наш сырой рекламный пакет
    pack_mesh_raw_data();
}