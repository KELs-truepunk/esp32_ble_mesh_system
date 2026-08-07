#pragma once

#include <stdint.h>

// Алгоритм FNV-1a для генерации 32-битного хэша пакета
static inline uint32_t mesh_calc_hash(uint32_t sender_id, uint16_t seq_num) {
    uint32_t hash = 2166136261u;
    
    // Хэшируем 4 байта sender_id
    for (int i = 0; i < 4; i++) {
        hash ^= (sender_id >> (i * 8)) & 0xFF;
        hash *= 16777619u;
    }
    
    // Хэшируем 2 байта seq_num
    hash ^= (seq_num & 0xFF);
    hash *= 16777619u;
    hash ^= ((seq_num >> 8) & 0xFF);
    hash *= 16777619u;

    return hash;
}