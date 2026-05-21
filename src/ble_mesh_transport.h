#ifndef BLE_MESH_TRANSPORT_H
#define BLE_MESH_TRANSPORT_H

#include <stdint.h>

// Инициализация широковещательного узла
void ble_mesh_transport_init(void);

// Имя узла в эфире (сделаем коротким, чтобы экономить байты)
#define DEVICE_NAME "BC_Node"

// Кастомный ID компании для Manufacturer Specific Data (0xFFFF — тестовый диапазон)
#define BITCHAT_COMPANY_ID 0xFFFF

#endif