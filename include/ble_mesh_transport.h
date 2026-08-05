#ifndef BLE_MESH_TRANSPORT_H
#define BLE_MESH_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>           // Добавили для работы bool
#include "ble_mesh_protocol.h"  // Добавили, чтобы компилятор знал b_mesh_packet_t

// Имя узла в эфире (сделаем коротким, чтобы экономить байты)
#define DEVICE_NAME "BC_Node"

// Кастомный ID компании для Manufacturer Specific Data (0xFFFF — тестовый диапазон)
#define MESH_COMPANY_ID 0xFFFF

#define SEEN_PACKETS_CACHE_SIZE 20


// Прототипы функций для работы с GATT
void gatt_server_init(void);
void gatt_notify_incoming_mesh_pkt(const uint8_t *payload, size_t len);

// Инициализация транспортного уровня
void ble_mesh_transport_init(void);


#endif // BLE_MESH_TRANSPORT_H

