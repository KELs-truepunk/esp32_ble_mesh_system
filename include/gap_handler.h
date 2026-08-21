#pragma once

#include "esp_gap_ble_api.h"
#include "ble_mesh_protocol.h"

#define MY_NODE_SENDER_ID 0x77777777


extern esp_ble_adv_params_t hybrid_adv_params;

// Хэш-кэш для дедупликации
void add_to_router_cache(uint32_t packet_hash);
bool is_in_router_cache(uint32_t packet_hash);
// Функции вещания и ретрансляции
void ble_mesh_broadcast_packet(b_mesh_packet_t *packet);
void relay_mesh_packet(b_mesh_packet_t *packet);            // Логика маршрутизации
void pack_mesh_raw_data(void);
// Основной колбэк события GAP
void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
//таска очереди 
void mesh_tx_task(void *pvParameters);
//инициализация таски с очередью
void init_mesh_tx_system(void);
