#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "ble_mesh_transport.h"

static const char *TAG = "MAIN";

// Таска радиостека BLE Mesh (крутится на Core 1 — APP_CPU)
static void ble_mesh_task(void *pvParameters) {
    ESP_LOGI(TAG, "BLE Mesh task запущена на ядре %d", xPortGetCoreID());

    // Инициализация стека радио
    ble_mesh_transport_init();

    while (1) {
        // Фоновая пауза, основной поток обработается событиями GAP/GATT
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Таска системных служб и будущего Веб-сервера (крутится на Core 0 — PRO_CPU)
static void sys_web_task(void *pvParameters) {
    ESP_LOGI(TAG, "Системная задача (Web/Sys) запущена на ядре %d", xPortGetCoreID());

    while (1) {
        // Заготовка под будущий HTTP-сервер и мониторинг
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Старт инициализации...");

    // Инит пина светодиода индикации
    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 0);
    
    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Создаем таску BLE Mesh с приоритетом 5 на Ядре 1
    xTaskCreatePinnedToCore(
        ble_mesh_task,
        "ble_mesh_task",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    // 2. Создаем системную таску с приоритетом 3 на Ядре 0
    xTaskCreatePinnedToCore(
        sys_web_task,
        "sys_web_task",
        3072,
        NULL,
        3,
        NULL,
        0
    );

    ESP_LOGI(TAG, "Планировщик запущен. Выгружаем app_main из RAM.");
    
    // Убиваем app_main, возвращая память стека системе
    vTaskDelete(NULL);
}