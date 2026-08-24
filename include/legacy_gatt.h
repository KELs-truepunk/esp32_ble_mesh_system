#pragma once
//LEGACY
#include "esp_gatts_api.h"   // API для GATT (подключение телефона, сервисы)
#include "esp_gap_ble_api.h" // API для GAP (вещание, сканирование)
#include <stdint.h>

#define PROFILE_NUM 1
#define PROFILE_A_APP_ID 0
#define BITCHAT_SVC_UUID 0x00FF
#define BITCHAT_CHAR_UUID 0xFF01

// Кастомные UUID для BitChat GATT
#define BITCHAT_SERVICE_UUID 0xFFE0
#define BITCHAT_CHAR_TX_UUID 0xFFE1 // Phone -> ESP32 (Write)
#define BITCHAT_CHAR_RX_UUID 0xFFE2 // ESP32 -> Phone (Notify)

// Структура для управления GATT профилем
struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;

    // Вместо одного char_handle разделили под TX/RX и добавили CCCD
    uint16_t char_tx_handle; // Phone -> ESP32 (Write)
    uint16_t char_rx_handle; // ESP32 -> Phone (Notify)
    uint16_t cccd_handle;    // Client Characteristic Configuration Descriptor

    esp_bt_uuid_t char_uuid;
    uint16_t perm;
    uint16_t property;
};

extern struct gatts_profile_inst gl_profile_tab[PROFILE_NUM];
extern uint16_t b_char_handle;

void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
