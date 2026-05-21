#include <stdio.h>
#include <string.h>
#include "ble_mesh_transport.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"

static const char *TAG = "BITCHAT_HYBRID";

#define PROFILE_NUM 1
#define PROFILE_A_APP_ID 0
#define BITCHAT_SVC_UUID 0x00FF
#define BITCHAT_CHAR_UUID 0xFF01

static uint16_t b_char_handle;

// 1. Настройки GAP вещания (Используем подключаемый тип ADV_TYPE_IND, чтобы телефон мог приконнектиться!)
static esp_ble_adv_params_t hybrid_adv_params = {
    .adv_int_min        = 0x20, // 20 мс — максимальная частота
    .adv_int_max        = 0x20, 
    .adv_type           = ADV_TYPE_IND, // ПОДКЛЮЧАЕМАЯ реклама (телефон сможет зацепиться)
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Структура для хранения состояния профиля GATT
struct gatts_profile_inst {
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
    },
};

//Функция сборки нашего кастомного пакета (Убрали имя устройства, освободили место!)
void pack_mesh_raw_data(void) {
    uint8_t raw_adv_data[31] = {0};
    uint8_t idx = 0;

    // Блок флагов
    raw_adv_data[idx++] = 2;                           
    raw_adv_data[idx++] = ESP_BLE_AD_TYPE_FLAG;        
    raw_adv_data[idx++] = ESP_BLE_ADV_FLAG_NON_LIMIT_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;

    // Блок НАШИХ КАСТОМНЫХ ДАННЫХ (Manufacturer Data)
    const char *payload_text = "MESH_INIT_HYBRID_MODE"; 
    uint8_t payload_len = strlen(payload_text);
    
    raw_adv_data[idx++] = payload_len + 3;             
    raw_adv_data[idx++] = 0xFF; // Жесткий префикс 0xFF по стандарту Bluetooth SIG
    
    raw_adv_data[idx++] = (BITCHAT_COMPANY_ID & 0xFF);
    raw_adv_data[idx++] = ((BITCHAT_COMPANY_ID >> 8) & 0xFF);
    
    memcpy(&raw_adv_data[idx], payload_text, payload_len);//куда, что, сколько в новую строку
    idx += payload_len;

    ESP_LOGI(TAG, "Сборка гибридного пакета: %d байт из 31. Свободно: %d б", idx, 31 - idx);
    esp_ble_gap_config_adv_data_raw(raw_adv_data, idx);
}

// 3. Обработчик GAP событий
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&hybrid_adv_params);
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Ошибка старта вещания: %d", param->adv_start_cmpl.status);
            } else {
                ESP_LOGI(TAG, "Гибридный узел вещает в эфир...");
            }
            break;
        default:
            break;
    }
}

// 4. Обработчик GATT событий (Прием сообщений от смартфона через malloc)
static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
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
            ESP_LOGI(TAG, "Смартфон подключился напрямую к вешке на дереве! ID: %d", param->connect.conn_id);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Смартфон отключился. Вешка возвращается в Mesh-режим.");
            // Перезапускаем рекламу, так как при дисконнекте стек BLE её стопает
            esp_ble_gap_start_advertising(&hybrid_adv_params);
            break;

        case ESP_GATTS_WRITE_EVT: {
            // Сюда Kotlin-приложение шлет текст
            uint16_t incoming_len = param->write.len;
            char *dynamic_buffer = (char *)malloc(incoming_len + 1); 
            if (dynamic_buffer != NULL) {
                memcpy(dynamic_buffer, param->write.value, incoming_len);
                dynamic_buffer[incoming_len] = '\0';
                ESP_LOGI(TAG, "Получено СМС с телефона: %s", dynamic_buffer);
                
                // ТУТ В БУДУЩЕМ: Запуск лавины этого сообщения по другим вешкам!
                
                free(dynamic_buffer);
            }
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            }
            break;
        }
        default:
            break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            return;
        }
    }
    if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[PROFILE_A_APP_ID].gatts_if) {
        if (gl_profile_tab[PROFILE_A_APP_ID].gatts_cb) {
            gl_profile_tab[PROFILE_A_APP_ID].gatts_cb(event, gatts_if, param);
        }
    }
}

void ble_mesh_transport_init(void) {
    ESP_LOGI(TAG, "Инициализация гибридного режима (GATT-сервер + GAP-вещатель)...");
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    // Регистрируем ОБА обработчика
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_register_callback(gatts_event_handler);
    
    esp_ble_gatts_app_register(PROFILE_A_APP_ID);

    pack_mesh_raw_data();
}