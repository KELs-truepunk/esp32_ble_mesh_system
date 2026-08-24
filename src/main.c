#include "nvs_flash.h"
#include "esp_log.h"
#include "ble_mesh_transport.h"

void wifi_init_softap(void);
void start_web_server(void);

void app_main(void)
{
    // 1. Память
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Сеть и Веб (Работают на Core 1 через конфиги)
    wifi_init_softap();
    start_web_server();

    // 3. BLE Транспорт и запуск mesh_tx_task (Core 0 - радио, Core 1 - логика TX)
    ble_mesh_transport_init();

    ESP_LOGI("MAIN", "Система заложена: Core 0 [BLE Stack/GAP] <---> Core 1 [Web Server + TX Task]");
}