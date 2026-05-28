#include "rgb.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"

#define TAG "RGB"

static const ledc_channel_t temp_channels[3] = {
    // Canales PWM usados por el RGB que representa la temperatura.
    LEDC_CHANNEL_0,
    LEDC_CHANNEL_1,
    LEDC_CHANNEL_2
};

static const ledc_channel_t cfg_channels[3] = {
    // Canales PWM usados por el RGB de configuracion/intensidad.
    LEDC_CHANNEL_3,
    LEDC_CHANNEL_4,
    LEDC_CHANNEL_5
};

static void ledc_setup_channel(gpio_num_t pin, ledc_channel_t channel)
{
    // Asocia un GPIO fisico a un canal LEDC de PWM.
    ledc_channel_config_t ch = {
        .gpio_num   = pin,
        .speed_mode  = LEDC_LOW_SPEED_MODE,
        .channel     = channel,
        .intr_type   = LEDC_INTR_DISABLE,
        .timer_sel   = LEDC_TIMER_0,
        .duty        = 0,
        .hpoint      = 0
    };

    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

static void ledc_set_rgb(ledc_mode_t mode,
                         ledc_channel_t r_ch,
                         ledc_channel_t g_ch,
                         ledc_channel_t b_ch,
                         uint8_t r,
                         uint8_t g,
                         uint8_t b)
{
    // Cada set_duty debe confirmarse con update_duty para verse en el pin.
    ESP_ERROR_CHECK(ledc_set_duty(mode, r_ch, r));
    ESP_ERROR_CHECK(ledc_update_duty(mode, r_ch));

    ESP_ERROR_CHECK(ledc_set_duty(mode, g_ch, g));
    ESP_ERROR_CHECK(ledc_update_duty(mode, g_ch));

    ESP_ERROR_CHECK(ledc_set_duty(mode, b_ch, b));
    ESP_ERROR_CHECK(ledc_update_duty(mode, b_ch));
}

void rgb_init(void)
{
    // Timer compartido por todos los canales PWM RGB.
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = RGB_PWM_RES,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = RGB_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    // Grupo RGB controlado automaticamente por rangos de temperatura.
    ledc_setup_channel(TEMP_RGB_RED,   temp_channels[0]);
    ledc_setup_channel(TEMP_RGB_GREEN, temp_channels[1]);
    ledc_setup_channel(TEMP_RGB_BLUE,  temp_channels[2]);

    // Grupo RGB que se actualiza inmediatamente con comandos RGB R/G/B.
    ledc_setup_channel(CFG_RGB_RED,    cfg_channels[0]);
    ledc_setup_channel(CFG_RGB_GREEN,  cfg_channels[1]);
    ledc_setup_channel(CFG_RGB_BLUE,   cfg_channels[2]);

    rgb_set_temp(0, 0, 0);
    rgb_set_config(0, 0, 0);

    ESP_LOGI(TAG, "RGB PWM initialized");
}

void rgb_set_temp(uint8_t r, uint8_t g, uint8_t b)
{
    ledc_set_rgb(LEDC_LOW_SPEED_MODE,
                 temp_channels[0],
                 temp_channels[1],
                 temp_channels[2],
                 r, g, b);
}

void rgb_set_config(uint8_t r, uint8_t g, uint8_t b)
{
    ledc_set_rgb(LEDC_LOW_SPEED_MODE,
                 cfg_channels[0],
                 cfg_channels[1],
                 cfg_channels[2],
                 r, g, b);
}

void rgb_task(void *pvParameters)
{
    system_config_t *config = (system_config_t *)pvParameters;

    // Copia local de rangos; se actualiza cuando UART envia SET.
    // Esto evita mantener el mutex bloqueado durante el ciclo de control PWM.
    color_config_t color_config;
    color_config.red = config->red;
    color_config.green = config->green;
    color_config.blue = config->blue;

    while (1)
    {
        // Recibe nuevas ventanas de temperatura para los colores.
        if (xQueueReceive(config->color_config_queue, &color_config, 0) == pdPASS)
        {
            ESP_LOGI(TAG, "Received color config update from queue");
        }

        float temp;
        temp_range_t red;
        temp_range_t green;
        temp_range_t blue;
        color_values_t intensity;

        xSemaphoreTake(config->mutex, portMAX_DELAY);
        // Copia valores compartidos para decidir el color fuera del mutex.
        temp = config->current_temperature;
        intensity = config->rgb_intensity;
        xSemaphoreGive(config->mutex);

        red = color_config.red;
        green = color_config.green;
        blue = color_config.blue;

        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;

        // Si la temperatura cae en un rango, usa la intensidad configurada.
        if (temp >= red.min && temp <= red.max) {
            r = intensity.r;
        }

        if (temp >= green.min && temp <= green.max) {
            g = intensity.g;
        }

        if (temp >= blue.min && temp <= blue.max) {
            b = intensity.b;
        }

        rgb_set_temp(r, g, b);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
