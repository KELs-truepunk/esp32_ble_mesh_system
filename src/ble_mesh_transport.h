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
// Кастомные UUID для BitChat GATT

#define BITCHAT_SERVICE_UUID      0xFFE0
#define BITCHAT_CHAR_TX_UUID     0xFFE1 // Phone -> ESP32 (Write)
#define BITCHAT_CHAR_RX_UUID     0xFFE2 // ESP32 -> Phone (Notify)

// Прототипы функций для работы с GATT
void gatt_server_init(void);
void gatt_notify_incoming_mesh_pkt(const uint8_t *payload, size_t len);

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

