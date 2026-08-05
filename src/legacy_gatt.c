#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "legacy_gatt.h"
#include "gap_handler.h"
#include "ble_mesh_protocol.h"

static const char *TAG = "LEGACY_GATT";

uint16_t b_char_handle = 0;

static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gatts_cb = gatts_profile_a_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
        .conn_id = 0xFFFF,
    },
};

static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = BITCHAT_SVC_UUID;
        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, 4);
        break;

    case ESP_GATTS_CREATE_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);

        esp_bt_uuid_t char_uuid = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = BITCHAT_CHAR_UUID};
        esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        b_char_handle = param->add_char.attr_handle;
        break;

    case ESP_GATTS_CONNECT_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
        ESP_LOGW(TAG, "Есть подключение со смартфоном! conn_id=%d", param->connect.conn_id);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = 0xFFFF;
        ESP_LOGI(TAG, "Смартфон отключился. Перезапуск ADV...");
        esp_ble_gap_start_advertising(&hybrid_adv_params);
        break;

    case ESP_GATTS_WRITE_EVT:
    {
        uint16_t incoming_len = param->write.len;
        char *dynamic_buffer = (char *)malloc(incoming_len + 1);
        if (dynamic_buffer != NULL)
        {
            memcpy(dynamic_buffer, param->write.value, incoming_len);
            dynamic_buffer[incoming_len] = '\0';

            ESP_LOGI(TAG, "Данные из приложения: \"%s\"", dynamic_buffer);

            b_mesh_packet_t phone_pkt;
            uint8_t payload[PAYLOAD_SIZE] = {0};

            size_t copy_bytes = (incoming_len > PAYLOAD_SIZE) ? PAYLOAD_SIZE : incoming_len;
            memcpy(payload, dynamic_buffer, copy_bytes);

            static uint16_t seq_counter = 500;
            seq_counter++;

            build_mesh_packet(&phone_pkt, PACKET_TYPE_MSG, seq_counter, 4, MY_NODE_SENDER_ID, payload);

            uint32_t self_hash = mesh_protocol_calc_hash(MY_NODE_SENDER_ID, seq_counter);
            add_to_router_cache(self_hash);

            ble_mesh_broadcast_packet(&phone_pkt);

            esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, b_char_handle,
                                        incoming_len, (uint8_t *)dynamic_buffer, false);
            free(dynamic_buffer);
        }

        if (param->write.need_rsp)
        {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    }
    default:
        break;
    }
}

void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        }
        else
        {
            return;
        }
    }
    if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[PROFILE_A_APP_ID].gatts_if)
    {
        if (gl_profile_tab[PROFILE_A_APP_ID].gatts_cb)
        {
            gl_profile_tab[PROFILE_A_APP_ID].gatts_cb(event, gatts_if, param);
        }
    }
}