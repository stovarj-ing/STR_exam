#include "color_setup.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "COLOR_SETUP"

#define POT_ADC_CHANNEL      ADC_CHANNEL_0

#define BUTTON_GPIO          GPIO_NUM_10
// El pulsador usa pull-up interno; por eso presionado equivale a nivel bajo.
#define BUTTON_ACTIVE_LEVEL  0
#define DEBOUNCE_MS          50

// Ojo: GPIO12 tambien esta definido como CFG_RGB_GREEN en rgb.h.
#define EXTERNAL_LED_GPIO    GPIO_NUM_12

typedef struct {
    // Estado minimo para detectar pulsaciones con antirrebote por software.
    int last_raw;
    int stable;
    TickType_t last_change_tick;
    bool press_latched;
} button_context_t;

static int adc_raw_to_percent(int raw)
{
    if (raw < 0) {
        raw = 0;
    }
    if (raw > 4095) {
        raw = 4095;
    }

    // Mapea ADC de 12 bits a porcentaje entero: 0..4095 -> 0..100.
    return (raw * 100 + 2047) / 4095;
}

static bool button_pressed_event(button_context_t *ctx)
{
    int raw = gpio_get_level(BUTTON_GPIO);
    TickType_t now = xTaskGetTickCount();

    // Si la lectura cambio, reinicia el tiempo de estabilizacion.
    if (raw != ctx->last_raw)
    {
        ctx->last_raw = raw;
        ctx->last_change_tick = now;
    }

    if ((now - ctx->last_change_tick) >= pdMS_TO_TICKS(DEBOUNCE_MS))
    {
        // Solo acepta el cambio cuando se mantuvo estable por DEBOUNCE_MS.
        if (ctx->stable != raw)
        {
            ctx->stable = raw;

            if (ctx->stable == BUTTON_ACTIVE_LEVEL && !ctx->press_latched)
            {
                ctx->press_latched = true;
                return true;
            }
        }
    }

    if (ctx->stable != BUTTON_ACTIVE_LEVEL) {
        // Permite detectar una nueva pulsacion cuando el boton se suelta.
        ctx->press_latched = false;
    }

    return false;
}

static temp_unit_t next_temperature_unit(temp_unit_t unit)
{
    // Ciclo pedido: Celsius -> Kelvin -> Fahrenheit -> Celsius.
    switch (unit)
    {
        case TEMP_UNIT_CELSIUS:
            return TEMP_UNIT_KELVIN;
        case TEMP_UNIT_KELVIN:
            return TEMP_UNIT_FAHRENHEIT;
        case TEMP_UNIT_FAHRENHEIT:
        default:
            return TEMP_UNIT_CELSIUS;
    }
}

static const char *temperature_unit_name(temp_unit_t unit)
{
    // Texto descriptivo para confirmar el cambio por log.
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

void color_setup_init(system_config_t *config)
{
    (void)config;

    // Entrada con pull-up interno para pulsador a GND.
    gpio_config_t button_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&button_conf));

    // Salida digital: ON cuando 0 C <= temperatura NTC <= porcentaje del pot.
    gpio_config_t led_conf = {
        .pin_bit_mask = 1ULL << EXTERNAL_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&led_conf));
    gpio_set_level(EXTERNAL_LED_GPIO, 0);

    ESP_LOGI(TAG, "Potentiometer percentage, button and external LED initialized");
}

void color_setup_task(void *pvParameters)
{
    system_config_t *config = (system_config_t *)pvParameters;

    // Estado inicial coherente con pull-up: boton suelto = 1.
    button_context_t button_ctx = {
        .last_raw = 1,
        .stable = 1,
        .last_change_tick = 0,
        .press_latched = false
    };

    while (1)
    {
        int raw = 0;

        // Lee el potenciometro. Comparte ADC con el NTC, por eso usa adc_mutex.
        xSemaphoreTake(config->adc_mutex, portMAX_DELAY);
        esp_err_t adc_result = adc_oneshot_read(config->ntc_adc.oneshot_handle,
                                                POT_ADC_CHANNEL,
                                                &raw);
        xSemaphoreGive(config->adc_mutex);

        if (adc_result == ESP_OK)
        {
            int pot_percent = adc_raw_to_percent(raw);
            float temperature_celsius;

            // Guarda el porcentaje en tiempo real para que el comando POT lo reporte.
            xSemaphoreTake(config->mutex, portMAX_DELAY);
            config->current_pot_raw = raw;
            config->current_pot_percent = pot_percent;
            temperature_celsius = config->current_temperature;
            xSemaphoreGive(config->mutex);

            // El porcentaje del pot se interpreta directamente como umbral 0..100 C.
            bool led_on = temperature_celsius >= 0.0f &&
                          temperature_celsius <= (float)pot_percent;

            gpio_set_level(EXTERNAL_LED_GPIO, led_on ? 1 : 0);
        }
        else
        {
            ESP_LOGW(TAG, "Potentiometer ADC read failed: %s", esp_err_to_name(adc_result));
        }

        if (button_pressed_event(&button_ctx))
        {
            // Cada pulsacion cambia la unidad que usa temperature_task al imprimir.
            xSemaphoreTake(config->mutex, portMAX_DELAY);
            config->temperature_unit = next_temperature_unit(config->temperature_unit);
            temp_unit_t unit = config->temperature_unit;
            xSemaphoreGive(config->mutex);

            ESP_LOGI(TAG, "Temperature unit changed by button: %s",
                     temperature_unit_name(unit));
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
