#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h" //драйвер NVS (Non-Volatile Storage) энергонезависимой памяти 
#include "esp_log.h"
#include "ble_mesh_transport.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "Запуск...");
    
    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); //если память не запустилась форматируем ее
        ret = nvs_flash_init(); // и заново запускаем
    }
    ESP_ERROR_CHECK(ret); //запустились? иначе рестарт платы

    // Запуск BLE
    ble_mesh_transport_init();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); //когда вышли из ble_mesh_transport_inti() - просто ждем перзагрузки платы
    }
}