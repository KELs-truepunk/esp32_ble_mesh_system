#pragma once

#include <stdint.h>
#include <string.h>
#include "mesh_config.h"
#include "mesh_hash.h"

// Типы пакетов в enum вместо макросов
typedef enum
{
    PACKET_TYPE_MSG = 0x01, // Обычное текстовое сообщение
    PACKET_TYPE_SOS = 0x02, // Экстренный сигнал
    PACKET_TYPE_SYS = 0x03, // Сервисный пакет (например, синхронизация)
    PACKET_TYPE_SEG = 0x04  // Сегментированный пакет
} mesh_packet_type_t;

#define PAYLOAD_SIZE 11 // Увеличили размер полезной нагрузки с 5 до 11 байт!

// Упакованная структура пакета (ровно 19 байт)
typedef struct __attribute__((packed))
{
    uint32_t sender_id; // 4 байта: ID отправителя
    uint16_t seq_num;   // 2 байта: Порядковый номер

    // Упакованный байт 1:
    uint8_t packet_type : 4; // 4 бита: Тип пакета
    uint8_t ttl : 4;         // 4 бита: TTL (0..15)

    // Упакованный байт 2 (Сегментация):
    uint8_t seg_total : 4;   // 4 бита: Всего сегментов (до 15)
    uint8_t seg_current : 4; // 4 бита: Текущий сегмент (0..14)

    uint8_t payload[PAYLOAD_SIZE]; // 11 байт
} b_mesh_packet_t;

// Проверка размера структуры на этапе компиляции (защита от сдвигов выравнивания)
_Static_assert(sizeof(b_mesh_packet_t) == 19, "b_mesh_packet_t must be exactly 19 bytes");

static inline void build_mesh_packet(b_mesh_packet_t *packet,
                                     uint8_t type,
                                     uint16_t seq_num,
                                     uint8_t ttl,
                                     uint8_t seg_total,
                                     uint8_t seg_current,
                                     uint32_t sender,
                                     const uint8_t *data)
{
    packet->sender_id = sender;
    packet->seq_num = seq_num;
    packet->packet_type = type & 0x0F;
    packet->ttl = ttl & 0x0F;
    packet->seg_total = seg_total & 0x0F;
    packet->seg_current = seg_current & 0x0F;

    memset(packet->payload, 0, PAYLOAD_SIZE);
    if (data != NULL)
    {
        // Проверяем фактическую длину, чтобы не читать память за границей строки
        size_t data_len = strlen((const char *)data);
        if (data_len > PAYLOAD_SIZE)
        {
            data_len = PAYLOAD_SIZE;
        }
        memcpy(packet->payload, data, data_len);
    }
}