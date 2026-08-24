#include "esp_http_server.h"
#include "ble_mesh_transport.h"
#include "esp_log.h"
#include "index_html_autogen.h"
static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;


static esp_err_t root_get_handler(httpd_req_t *req)
{
    // sizeof(index_html) - 1, чтобы не отправлять терминирующий \0
    httpd_resp_send(req, index_html, sizeof(index_html) - 1);
    return ESP_OK;
}
static esp_err_t get_messages_handler(httpd_req_t *req)
{
    mesh_web_msg_t msg;
    httpd_resp_set_type(req, "application/json");

    if (mesh_pop_web_message(&msg))
    {
        char json[256];
        snprintf(json, sizeof(json), "{\"sender\":\"0x%08X\",\"seq\":%d,\"text\":\"%s\"}",
                 (unsigned int)msg.sender_id, msg.seq_num, msg.text);
        return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, "{}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t post_send_handler(httpd_req_t *req)
{
    char buf[165] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ble_mesh_send_text(buf);
    httpd_resp_send(req, "{\"status\":\"OK\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
        httpd_uri_t uri_get = {.uri = "/api/messages", .method = HTTP_GET, .handler = get_messages_handler};
        httpd_uri_t uri_post = {.uri = "/api/send", .method = HTTP_POST, .handler = post_send_handler};

        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);

        ESP_LOGI(TAG, "HTTP Сервер запущен!");
    }
}