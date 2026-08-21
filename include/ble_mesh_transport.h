#ifndef BLE_MESH_TRANSPORT_H
#define BLE_MESH_TRANSPORT_H

#include <stdint.h>
#include <stddef.h> // Добавлен для size_t
#include <stdbool.h>
#include "ble_mesh_protocol.h"
#include "mesh_config.h" // DEVICE_NAME и MESH_COMPANY_ID берутся отсюда

// Убраны дублирующиеся #define DEVICE_NAME и MESH_COMPANY_ID

void gatt_server_init(void);
void gatt_notify_incoming_mesh_pkt(const uint8_t *payload, size_t len);
void ble_mesh_transport_init(void);

#endif // BLE_MESH_TRANSPORT_H
