#include "esp_http_server.h"
#include "ble_mesh_transport.h"
#include "esp_log.h"

static httpd_handle_t server = NULL;

// GET /api/messages — забирает новое собранное сообщение из mesh-сети
static esp_err_t get_messages_handler(httpd_req_t *req)
{
    mesh_web_msg_t msg;
    if (mesh_pop_web_message(&msg)) {
        char json[300];
        snprintf(json, sizeof(json), "{\"sender\":\"0x%08X\",\"seq\":%d,\"text\":\"%s\"}", 
                 msg.sender_id, msg.seq_num, msg.text);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, "{}", HTTPD_RESP_USE_STRLEN);
}

// POST /api/send — принимает JSON/text от веб-интерфейса и отправляет в Mesh
static esp_err_t post_send_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;

    ble_mesh_send_text(buf);
    httpd_resp_send_chunk(req, "{\"status\":\"OK\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/api/messages", .method = HTTP_GET, .handler = get_messages_handler };
        httpd_uri_t uri_post = { .uri = "/api/send", .method = HTTP_POST, .handler = post_send_handler };
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);
    }
}