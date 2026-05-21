#pragma once

#include "esp_gatts_api.h"

void ble_mesh_transport_init(void);

//кастомный 16-битный UUID для Сервиса BitChat (взят просто для примера)
#define BITCHAT_PROFILE_NUM          1
#define BITCHAT_PROFILE_APP_ID       0
#define BITCHAT_SVC_UUID             0x00FF
#define BITCHAT_CHAR_UUID            0xFF01

// Имя устройства, которое отобразится в nRF Connect
#define DEVICE_NAME                  "ESP32_BLE"
