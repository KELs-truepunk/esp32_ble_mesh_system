#include <stdio.h>
#include <string.h>
#include "ble_mesh_transport.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"    
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"

static const char *TAG = "BLE_TRANSPORT";

// Настройки широковещательного пакета (Adv), чтобы телефон нас видел
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20, // Минимальный интервал рекламы (20ms)
    .adv_int_max        = 0x40, // Максимальный интервал (40ms)
    .adv_type           = ADV_TYPE_IND, // Разрешены подключения со стороны телефона
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL, // Рекламировать на всех 3-х BLE каналах
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Хэндлы (ID), которые нам выдаст система при создании сервиса и характеристики
static uint16_t b_service_handle;
static uint16_t b_char_handle;

// 1. Колбэк для GAP (Управление эфиром: реклама, сканирование)
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            // Как только пакет рекламы сформирован — запускаем её в эфир
            esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Ошибка запуска рекламы!");
            } else {
                ESP_LOGI(TAG, "Реклама успешно запущена. Ищи '%s'", DEVICE_NAME);
            }
            break;
        default:
            break;
    }
}

// 2. Колбэк для GATTS (Обработка данных: чтение, запись, подключения)
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            // Как только наш профиль зарегистрирован в стеке, задаем имя устройства
            esp_ble_gap_set_device_name(DEVICE_NAME);
            
            // Настраиваем данные для рекламы (чтобы телефон видел имя при сканировании)
            esp_ble_adv_data_t adv_data = {
                .set_scan_rsp = false,
                .include_name = true, // Включаем имя устройства в пакет
                .include_txpower = true,
                .min_interval = 0x0006,
                .max_interval = 0x0010,
            };
            esp_ble_gap_config_adv_data(&adv_data);

            // Создаем наш Сервис
            esp_gatt_srvc_id_t service_id = {
                .is_primary = true,
                .id.inst_id = 0x00,
                .id.uuid.len = ESP_UUID_LEN_16,
                .id.uuid.uuid.uuid16 = BITCHAT_SVC_UUID,
            };
            esp_ble_gatts_create_service(gatts_if, &service_id, 4); // 4 — это количество хэндлов (места под атрибуты)
            break;
        }
        case ESP_GATTS_CREATE_EVT:
            // Сервис создался, сохраняем его хэндл и запускаем его
            b_service_handle = param->create.service_handle;
            esp_ble_gatts_start_service(b_service_handle);

            // Сразу же добавляем внутрь этого сервиса Характеристику для сообщений
            esp_bt_uuid_t char_uuid = {
                .len = ESP_UUID_LEN_16,
                .uuid.uuid16 = BITCHAT_CHAR_UUID,
            };
            esp_ble_gatts_add_char(b_service_handle, &char_uuid,
                                   ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, // Права: чтение и запись
                                   ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY, // Свойства
                                   NULL, NULL);
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            // Характеристика успешно добавилась, сохраняем её хэндл
            b_char_handle = param->add_char.attr_handle;
            ESP_LOGI(TAG, "GATT Сервер успешно настроен!");
            break;

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "Телефон подключился! ID соединения: %d", param->connect.conn_id);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Телефон отключился. Перезапускаем рекламу...");
            esp_ble_gap_start_advertising(&adv_params); // Снова уходим в эфир, чтобы можно было переподключиться
            break;

        case ESP_GATTS_WRITE_EVT: {
            //ТЕЛЕФОН НАМ ЧТО-ТО НАПИСАЛ!
            ESP_LOGI(TAG, "Получены данные от телефона (длина %d байт)", param->write.len);
            
            uint16_t incoming_len = param->write.len;
    
            //Выделяем память в куче (Heap) ESP32 под конкретный размер + 1 байт для \0
            char *dynamic_buffer = (char *)malloc(incoming_len + 1); 
    
            if (dynamic_buffer != NULL) {
                //Копируем сырые данные в наш буфер
                memcpy(dynamic_buffer, param->write.value, incoming_len);
                dynamic_buffer[incoming_len] = '\0'; // Безопасно закрываем строку

                ESP_LOGI(TAG, "Текст сообщения из кучи: %s", dynamic_buffer);
                free(dynamic_buffer);//отправили - удалили

            } else {
                ESP_LOGE(TAG, "Ошибка malloc: не удалось выделить память под буфер!");
            }
            // Делаем ЭХО: отправляем эти же данные обратно телефону через механизм Notify
            // Здесь мы используем оригинальный указатель из прерывания (param->write.value),
            // так как нам не нужен нуль-терминатор для отправки сырых байт в BLE.
            esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, b_char_handle,
                                        param->write.len, param->write.value, false);
            ESP_LOGI(TAG, "Отправлено эхо обратно на телефон.");
            
            // Отвечаем серверу, что запись прошла успешно (требование протокола BLE GATT)
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            }
            break;
        }
        default:
            break;
        }
    }
    //ее вызываем из мэйн()
void ble_mesh_transport_init(void) {
    ESP_LOGI(TAG, "Запуск базовой инициализации BLE...");

    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) return;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&bt_cfg) != ESP_OK) return;
    if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) return;
    if (esp_bluedroid_init() != ESP_OK) return;
    if (esp_bluedroid_enable() != ESP_OK) return;


    // Регистрируем обработчик событий GAP (эфира) в Bluedroid
    esp_ble_gap_register_callback(gap_event_handler);
    
    // Регистрируем обработчик событий GATT (данных) в Bluedroid
    esp_ble_gatts_register_callback(gatts_event_handler);

    // Регистрируем наше приложение на GATT-сервере (App ID = 0)
    esp_ble_gatts_app_register(BITCHAT_PROFILE_APP_ID);

    ESP_LOGI(TAG, "Регистрация интерфейсов BLE завершена.");
}