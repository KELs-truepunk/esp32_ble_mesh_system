#pragma once

#include <stdint.h>
#include <string.h>

// Типы пакетов в enum вместо макросов
typedef enum {
    PACKET_TYPE_MSG = 0x01,  // Обычное текстовое сообщение
    PACKET_TYPE_SOS = 0x02,  // Экстренный сигнал
    PACKET_TYPE_SYS = 0x03   // Сервисный пакет (например, синхронизация)
} mesh_packet_type_t;

#define PAYLOAD_SIZE 7       // Увеличили размер полезной нагрузки с 5 до 7 байт!

// Упакованная структура пакета (ровно 14 байт)
typedef struct __attribute__((packed)) {
    uint32_t sender_id;              // 4 байта: ID отправителя
    uint16_t seq_num;                // 2 байта: Порядковый номер (бывший message_id)
    
    // Упакованный байт (8 бит суммарно):
    uint8_t packet_type : 4;        // 4 бита: Тип пакета (значения 0..15)
    uint8_t ttl         : 4;        // 4 бита: Time To Live (значения 0..15)
    
    uint8_t payload[PAYLOAD_SIZE];  // 7 байт: Полезная нагрузка
} b_mesh_packet_t;

// Проверка размера структуры на этапе компиляции (защита от сдвигов выравнивания)
_Static_assert(sizeof(b_mesh_packet_t) == 14, "b_mesh_packet_t must be exactly 14 bytes");


static inline void build_mesh_packet(b_mesh_packet_t *packet, 
                                     uint8_t type, 
                                     uint16_t seq_num, 
                                     uint8_t ttl, 
                                     uint32_t sender, 
                                     const uint8_t *data) {
    packet->sender_id = sender;
    packet->seq_num = seq_num;
    packet->packet_type = type & 0x0F; // Ограничиваем 4 битами
    packet->ttl = ttl & 0x0F;         // Ограничиваем 4 битами
    
    // Очищаем payload перед записью
    memset(packet->payload, 0, PAYLOAD_SIZE);
    
    if (data != NULL) {
        memcpy(packet->payload, data, PAYLOAD_SIZE); 
    }
}