#include "config.h"

#include "uart_manager.h"
#include "rgb.h"
#include "ntc.h"
#include "color_setup.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#define TAG "MAIN"

void app_main(void)
{
    // Configuracion inicial del sistema antes de crear las tareas.
    // El estado compartido se usa para pasar datos entre tareas sin usar
    // variables globales dispersas. Cada tarea actualiza o lee campos de este
    // struct protegido por `mutex`.
    static system_config_t config = {
        .red   = {50, 60},
        .green = {20, 30},
        .blue  = {0, 10},
        .current_temperature = 0,
        .current_color_setup = {0, 0, 0},
        .rgb_intensity = {255, 255, 255},
        .temperature_unit = TEMP_UNIT_CELSIUS,
        .current_pot_raw = 0,
        .current_pot_percent = 0,
        .temperature_log_period_ms = 1000
    };

    // mutex: controla acceso concurrente al estado compartido.
    // adc_mutex: asegura que solo una tarea use el ADC a la vez.
    config.mutex = xSemaphoreCreateMutex();
    config.adc_mutex = xSemaphoreCreateMutex();
    if (config.mutex == NULL || config.adc_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return;
    }

    // Colas de tamano 1: siempre interesa conservar la ultima actualizacion.
    config.color_config_queue = xQueueCreate(1, sizeof(color_config_t));
    if (config.color_config_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create color_config_queue");
        return;
    }

    config.temperature_queue = xQueueCreate(1, sizeof(temperature_msg_t));
    if (config.temperature_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create temperature_queue");
        return;
    }

    // La cola de configuracion de color se crea para poder transmitir la ultima
    // ventana de temperatura sin bloquear a la tarea emisora.
    config.color_setup_queue = xQueueCreate(1, sizeof(color_values_t));
    if (config.color_setup_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create color_setup_queue");
        return;
    }

    // Inicializacion de perifericos antes de arrancar tareas concurrentes.
    uart_init();
    rgb_init();
    adc_init_ntc(&config.ntc_adc);
    color_setup_init(&config);

    ESP_LOGI(TAG, "All peripherals initialized");
    ESP_LOGI(TAG, "Starting tasks...");

    // Cada tarea recibe &config para compartir el estado del sistema.
    // - uart_task: recibe comandos serie y actualiza configuracion.
    // - rgb_task: enciende el RGB segun la temperatura y los rangos actuales.
    // - temperature_task: mide el NTC y publica la temperatura.
    // - color_setup_task: lee el potenciómetro, controla el LED externo y cambia la unidad.
    xTaskCreate(uart_task, "uart_task", 4096, &config, 6, NULL);
    xTaskCreate(rgb_task, "rgb_task", 4096, &config, 3, NULL);
    xTaskCreate(temperature_task, "temperature_task", 4096, &config, 3, NULL);
    xTaskCreate(color_setup_task, "color_setup_task", 4096, &config, 3, NULL);

    ESP_LOGI(TAG, "All tasks created successfully");
}
