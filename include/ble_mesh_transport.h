#pragma once
#include <stdint.h>
#include <stdbool.h>

#define WEB_MSG_BUFFER_SIZE 10
#define MAX_MESH_MSG_LEN 128 // или сколько у тебя там влезает в пейлоад

// Структура для кольцевого буфера
typedef struct {
    uint8_t sender_id;
    uint16_t seq_num;
    char text[MAX_MESH_MSG_LEN];
} mesh_web_msg_t;

// Прототипы функций
bool mesh_pop_web_message(mesh_web_msg_t *out_msg);
void ble_mesh_transport_init(void);
void mesh_push_web_message(uint32_t sender_id, uint16_t seq_num, const char *text);
esp_err_t ble_mesh_send_text(const char *text);