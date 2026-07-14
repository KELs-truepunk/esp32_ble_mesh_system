#ifndef BLE_MESH_TRANSPORT_H
#define BLE_MESH_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>           // Добавили для работы bool
#include "ble_mesh_protocol.h"  // Добавили, чтобы компилятор знал b_mesh_packet_t

// Имя узла в эфире (сделаем коротким, чтобы экономить байты)
#define DEVICE_NAME "BC_Node"

// Кастомный ID компании для Manufacturer Specific Data (0xFFFF — тестовый диапазон)
#define BITCHAT_COMPANY_ID 0xFFFF

#define SEEN_PACKETS_CACHE_SIZE 20

typedef struct {
    uint32_t sender_id;
    uint16_t message_id;
} seen_packet_t;

// Инициализация транспортного уровня
void ble_mesh_transport_init(void);

// Проверка и кэширование пакетов
bool is_packet_new(uint32_t sender_id, uint16_t message_id);
void add_packet_to_cache(uint32_t sender_id, uint16_t message_id);

// Логика маршрутизации
void relay_mesh_packet(b_mesh_packet_t *packet);

#endif // BLE_MESH_TRANSPORT_H

