#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "uart_manager.h"
#include "rgb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#define TAG "UART_MANAGER"

static const char *temp_unit_name(temp_unit_t unit)
{
    // Convierte el enum interno a texto legible para logs.
    switch (unit)
    {
        case TEMP_UNIT_CELSIUS:
            return "Celsius";
        case TEMP_UNIT_KELVIN:
            return "Kelvin";
        case TEMP_UNIT_FAHRENHEIT:
            return "Fahrenheit";
        default:
            return "Unknown";
    }
}

static bool valid_pwm_value(int value)
{
    // PWM de 8 bits: duty valido entre 0 y 255.
    return value >= 0 && value <= RGB_PWM_MAX_DUTY;
}

static bool valid_log_period_seconds(int seconds)
{
    // Limite practico para evitar 0 segundos o esperas absurdamente largas.
    return seconds >= 1 && seconds <= 3600;
}

void uart_init(void)
{
    // UART0 se usa como consola: 115200 8N1 sin control de flujo.
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_driver_install(UART_PORT_NUM,
                        UART_BUFFER_SIZE,
                        0,
                        0,
                        NULL,
                        0);

    uart_param_config(UART_PORT_NUM, &uart_config);

    ESP_LOGI(TAG, "UART initialized");
}

static void process_command(char *command, system_config_t *config)
{
    // Buffers temporales usados por sscanf para extraer parametros.
    char color[16];
    float min;
    float max;

    // SET <COLOR> <MIN> <MAX>: actualiza rangos de temperatura para el RGB.
    if (sscanf(command, "SET %15s %f %f", color, &min, &max) == 3)
    {
        color_config_t color_config;

        // Se copia la configuracion actual antes de modificar un solo color.
        xSemaphoreTake(config->mutex, portMAX_DELAY);
        color_config.red = config->red;
        color_config.green = config->green;
        color_config.blue = config->blue;
        xSemaphoreGive(config->mutex);

        bool send_update = false;

        if (strcmp(color, "RED") == 0)
        {
            color_config.red.min = min;
            color_config.red.max = max;
            ESP_LOGI(TAG, "RED updated: %.2f - %.2f", min, max);
            send_update = true;
        }
        else if (strcmp(color, "GREEN") == 0)
        {
            color_config.green.min = min;
            color_config.green.max = max;
            ESP_LOGI(TAG, "GREEN updated: %.2f - %.2f", min, max);
            send_update = true;
        }
        else if (strcmp(color, "BLUE") == 0)
        {
            color_config.blue.min = min;
            color_config.blue.max = max;
            ESP_LOGI(TAG, "BLUE updated: %.2f - %.2f", min, max);
            send_update = true;
        }
        else
        {
            ESP_LOGW(TAG, "Invalid color");
        }

        if (send_update)
        {
            // Notifica a rgb_task y tambien guarda el estado compartido.
            xQueueOverwrite(config->color_config_queue, &color_config);

            xSemaphoreTake(config->mutex, portMAX_DELAY);
            config->red = color_config.red;
            config->green = color_config.green;
            config->blue = color_config.blue;
            xSemaphoreGive(config->mutex);
        }
    }
    // TEMP C/K/F: cambia la unidad de impresion de temperatura.
    else if (sscanf(command, "TEMP %15s", color) == 1)
    {
        temp_unit_t unit;
        bool valid = true;

        if (strcmp(color, "C") == 0) {
            unit = TEMP_UNIT_CELSIUS;
        } else if (strcmp(color, "K") == 0) {
            unit = TEMP_UNIT_KELVIN;
        } else if (strcmp(color, "F") == 0) {
            unit = TEMP_UNIT_FAHRENHEIT;
        } else {
            valid = false;
        }

        if (valid)
        {
            xSemaphoreTake(config->mutex, portMAX_DELAY);
            config->temperature_unit = unit;
            xSemaphoreGive(config->mutex);

            ESP_LOGI(TAG, "Temperature unit set to %s", temp_unit_name(unit));
        }
        else
        {
            ESP_LOGW(TAG, "Invalid TEMP command. Use: TEMP C, TEMP K or TEMP F");
        }
    }
    // RGB R/G/B <0..255>: cambia la intensidad PWM de un canal.
    else if (sscanf(command, "RGB %15s %f", color, &min) == 2)
    {
        int duty = (int)min;

        // duty != min rechaza valores decimales como 120.5.
        if (duty != min || !valid_pwm_value(duty))
        {
            ESP_LOGW(TAG, "Invalid RGB duty. Use 0 to %d", RGB_PWM_MAX_DUTY);
            return;
        }

        xSemaphoreTake(config->mutex, portMAX_DELAY);
        color_values_t intensity = config->rgb_intensity;

        if (strcmp(color, "R") == 0) {
            intensity.r = (uint8_t)duty;
        } else if (strcmp(color, "G") == 0) {
            intensity.g = (uint8_t)duty;
        } else if (strcmp(color, "B") == 0) {
            intensity.b = (uint8_t)duty;
        } else {
            xSemaphoreGive(config->mutex);
            ESP_LOGW(TAG, "Invalid RGB channel. Use: RGB R/G/B value");
            return;
        }

        config->rgb_intensity = intensity;
        xSemaphoreGive(config->mutex);

        // Actualizacion inmediata del LED RGB de configuracion.
        rgb_set_config(intensity.r, intensity.g, intensity.b);
        ESP_LOGI(TAG, "RGB intensity updated: R=%u G=%u B=%u",
                 intensity.r, intensity.g, intensity.b);
    }
    // POT: imprime el porcentaje actual del potenciometro.
    else if (strcmp(command, "POT") == 0)
    {
        xSemaphoreTake(config->mutex, portMAX_DELAY);
        int pot_percent = config->current_pot_percent;
        int pot_raw = config->current_pot_raw;
        xSemaphoreGive(config->mutex);

        ESP_LOGI(TAG, "Potentiometer: %d%% (raw=%d)", pot_percent, pot_raw);
    }
    // LOGTIME <SEGUNDOS>: periodo de impresion de temperatura.
    else if (sscanf(command, "LOGTIME %f", &min) == 1)
    {
        int seconds = (int)min;

        if (seconds != min || !valid_log_period_seconds(seconds))
        {
            ESP_LOGW(TAG, "Invalid log period. Use: LOGTIME seconds, with 1 <= seconds <= 3600");
            return;
        }

        xSemaphoreTake(config->mutex, portMAX_DELAY);
        config->temperature_log_period_ms = (uint32_t)seconds * 1000U;
        xSemaphoreGive(config->mutex);

        ESP_LOGI(TAG, "Temperature log period set to %d seconds", seconds);
    }
    else
    {
        ESP_LOGW(TAG, "Invalid command. Valid: SET color min max | TEMP C/K/F | RGB R/G/B duty | POT | LOGTIME seconds");
    }
}

void uart_task(void *pvParameters)
{
    // FreeRTOS entrega parametros como void*, por eso se castea al tipo real.
    system_config_t *config = (system_config_t *)pvParameters;

    uint8_t data[128];

    while (1)
    {
        // Lectura no permanente: despierta cada 100 ms aunque no llegue UART.
        int len = uart_read_bytes(UART_PORT_NUM,
                                  data,
                                  sizeof(data) - 1,
                                  pdMS_TO_TICKS(100));

        if (len > 0)
        {
            // Cierra el buffer como string C para poder usar strlen/sscanf.
            data[len] = '\0';

            // Elimina CR/LF enviados por monitores seriales al presionar Enter.
            char *pos = strchr((char *)data, '\r');
            if (pos != NULL) {
                *pos = '\0';
            }

            pos = strchr((char *)data, '\n');
            if (pos != NULL) {
                *pos = '\0';
            }

            if (strlen((char *)data) == 0) {
                continue;
            }

            ESP_LOGI(TAG, "Received: %s", (char *)data);

            process_command((char *)data, config);
        }
    }
}
