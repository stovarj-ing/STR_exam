#include "ntc.h"

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_log.h"

#define TAG "NTC"

static float convert_temperature(float celsius, temp_unit_t unit)
{
    // La temperatura interna se mantiene en Celsius y solo se convierte al imprimir.
    switch (unit)
    {
        case TEMP_UNIT_KELVIN:
            return celsius + 273.15f;
        case TEMP_UNIT_FAHRENHEIT:
            return (celsius * 9.0f / 5.0f) + 32.0f;
        case TEMP_UNIT_CELSIUS:
        default:
            return celsius;
    }
}

static const char *temperature_unit_symbol(temp_unit_t unit)
{
    // Simbolo corto usado en el log periodico.
    switch (unit)
    {
        case TEMP_UNIT_KELVIN:
            return "K";
        case TEMP_UNIT_FAHRENHEIT:
            return "F";
        case TEMP_UNIT_CELSIUS:
        default:
            return "C";
    }
}

void adc_init_ntc(adc_handles_t *adc_handles)
{
    // Crea una unidad ADC en modo oneshot: cada lectura se solicita manualmente.
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config,
                                         &adc_handles->oneshot_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };

    // ADC_CHANNEL_1 mide el divisor de tension del NTC.
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handles->oneshot_handle,
                                               ADC_CHANNEL,
                                               &config));

    // ADC_CHANNEL_0 mide el potenciometro usado como limite 0..100 C del LED.
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handles->oneshot_handle,
                                               ADC_CHANNEL_0,
                                               &config));

    // Calibracion para convertir lectura cruda ADC a milivoltios.
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config,
                                                          &adc_handles->cali_handle));

    ESP_LOGI(TAG, "ADC initialized with both channels (NTC: CH1, POT: CH0)");
}

static float read_temperature(system_config_t *config)
{
    int adc_raw = 0;

    // El NTC y el potenciometro comparten ADC; este mutex evita lecturas simultaneas.
    xSemaphoreTake(config->adc_mutex, portMAX_DELAY);

    // Promedia varias muestras para reducir ruido electrico.
    for (int i = 0; i < ADC_SAMPLES; i++)
    {
        int raw = 0;

        ESP_ERROR_CHECK(adc_oneshot_read(config->ntc_adc.oneshot_handle,
                                         ADC_CHANNEL,
                                         &raw));
        adc_raw += raw;
    }

    adc_raw /= ADC_SAMPLES;

    int voltage_mv = 0;

    // Convierte el promedio crudo a milivoltios calibrados.
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(config->ntc_adc.cali_handle,
                                            adc_raw,
                                            &voltage_mv));

    xSemaphoreGive(config->adc_mutex);

    float voltage = voltage_mv / 1000.0f;


    // Evita divisiones inestables si el sensor esta desconectado o saturado.
    if (voltage <= 0.01f || voltage >= 3.29f)
    {
        ESP_LOGE(TAG, "Invalid voltage");
        return -999.0f;
    }

    float resistance = SERIES_RESISTOR * ((3.3f / voltage) - 1.0f);

    // Ecuacion Beta del termistor para convertir resistencia a temperatura.
    float temperature_kelvin =
        1.0f /
        (
            (1.0f / (NOMINAL_TEMPERATURE + 273.15f)) +
            (logf(resistance / NOMINAL_RESISTANCE) / BETA_COEFFICIENT)
        );

    float temperature_celsius = temperature_kelvin - 273.15f;

    return temperature_celsius;
}

void temperature_task(void *pvParameters)
{
    system_config_t *config = (system_config_t *)pvParameters;

    temperature_msg_t temp_msg;
    TickType_t last_log_tick = 0;

    while (1)
    {
        // Lee temperatura continuamente; LOGTIME solo controla la impresion.
        float temp = read_temperature(config);
        temp_msg.temperature = temp;

        xQueueOverwrite(config->temperature_queue, &temp_msg);

        xSemaphoreTake(config->mutex, portMAX_DELAY);
        config->current_temperature = temp;
        // Copia local para imprimir sin mantener bloqueado el mutex.
        temp_unit_t unit = config->temperature_unit;
        uint32_t log_period_ms = config->temperature_log_period_ms;
        xSemaphoreGive(config->mutex);

        TickType_t now = xTaskGetTickCount();

        if ((now - last_log_tick) >= pdMS_TO_TICKS(log_period_ms))
        {
            last_log_tick = now;

            // Log periodico en la unidad seleccionada por UART o pulsador.
            ESP_LOGI(TAG, "Temperature: %.2f %s",
                     convert_temperature(temp, unit),
                     temperature_unit_symbol(unit));
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
