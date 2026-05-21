#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

typedef struct
{
    // Rango de temperatura donde un canal RGB debe activarse.
    float min;
    float max;
} temp_range_t;

typedef struct
{
    // Handles que ESP-IDF usa para leer y calibrar el ADC.
    adc_oneshot_unit_handle_t oneshot_handle;
    adc_cali_handle_t cali_handle;
} adc_handles_t;

typedef struct
{
    // Rangos de temperatura asociados a cada color.
    temp_range_t red;
    temp_range_t green;
    temp_range_t blue;
} color_config_t;

typedef struct
{
    // Intensidad PWM de cada canal RGB: 0 apagado, 255 maximo.
    uint8_t r;
    uint8_t g;
    uint8_t b;
} color_values_t;

typedef struct
{
    // Temperatura base del sistema, guardada en Celsius.
    float temperature;
} temperature_msg_t;

typedef enum
{
    // Unidad usada para imprimir la temperatura por UART/log.
    TEMP_UNIT_CELSIUS = 0,
    TEMP_UNIT_KELVIN,
    TEMP_UNIT_FAHRENHEIT
} temp_unit_t;

typedef struct
{
    // Estado compartido por todas las tareas FreeRTOS del proyecto.
    adc_handles_t ntc_adc;

    // Colas para publicar el ultimo valor/configuracion sin bloquear tareas.
    QueueHandle_t color_config_queue;
    QueueHandle_t temperature_queue;
    QueueHandle_t color_setup_queue;

    // mutex protege estado compartido; adc_mutex serializa lecturas ADC.
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t adc_mutex;

    // Rangos configurables por UART para activar colores por temperatura.
    temp_range_t red;
    temp_range_t green;
    temp_range_t blue;

    // Variables de estado leidas/escritas por varias tareas.
    float current_temperature;
    color_values_t current_color_setup;
    color_values_t rgb_intensity;
    temp_unit_t temperature_unit;
    int current_pot_raw;
    int current_pot_percent;
    uint32_t temperature_log_period_ms;
} system_config_t;

#endif
