#pragma once

#include <stdint.h>
#include <string.h>

// Макросы для типов пакетов (чтобы не использовать магические числа в коде)
#define PACKET_TYPE_MSG   0x01  // Обычное текстовое сообщение
#define PACKET_TYPE_SOS   0x02  // Экстренный сигнал
#define PACKET_TYPE_SYS   0x03  // Сервисный пакет (например, синхронизация)

#define PAYLOAD_SIZE      5     // Размер полезной нагрузки

// Жестко упакованная структура пакета (ровно 13 байт)
typedef struct __attribute__((packed)) {
    uint8_t  packet_type;           // 1 байт: Тип пакета
    uint16_t message_id;            // 2 байта: Уникальный ID сообщения
    uint8_t  ttl;                   // 1 байт: Time To Live (ограничение прыжков)
    uint32_t sender_id;             // 4 байта: ID отправителя (хэш или часть MAC)
    uint8_t  payload[PAYLOAD_SIZE]; // 5 байт: Полезная нагрузка (символы)
} b_mesh_packet_t;

// Функция-помощник для быстрой сборки пакета в оперативной памяти
// inline используется, чтобы не тратить время на вызов функции, а встроить код напрямую
static inline void build_mesh_packet(b_mesh_packet_t *packet, 
                                     uint8_t type, 
                                     uint16_t msg_id, 
                                     uint8_t ttl, 
                                     uint32_t sender, 
                                     const uint8_t *data) {
    packet->packet_type = type;
    packet->message_id = msg_id;
    packet->ttl = ttl;
    packet->sender_id = sender;
    
    // Заполняем payload нулями, чтобы не отправить мусор из памяти
    memset(packet->payload, 0, PAYLOAD_SIZE);
    
    // Если данные есть, копируем их в пакет
    if (data != NULL) {
        memcpy(packet->payload, data, PAYLOAD_SIZE); 
    }
}