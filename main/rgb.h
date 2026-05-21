#ifndef RGB_H
#define RGB_H

#include <stdint.h>
#include "config.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#define RGB_PWM_FREQ_HZ     5000
// Resolucion PWM de 8 bits: duty entre 0 y 255.
#define RGB_PWM_RES         LEDC_TIMER_8_BIT
#define RGB_PWM_MAX_DUTY    255

// Pines del RGB que cambia automaticamente con la temperatura.
#define TEMP_RGB_RED        GPIO_NUM_4
#define TEMP_RGB_GREEN      GPIO_NUM_5
#define TEMP_RGB_BLUE       GPIO_NUM_6

// Pines del RGB usado para visualizar intensidad/configuracion por UART.
#define CFG_RGB_RED         GPIO_NUM_11
#define CFG_RGB_GREEN       GPIO_NUM_12
#define CFG_RGB_BLUE        GPIO_NUM_13

void rgb_init(void);

void rgb_set_temp(uint8_t r, uint8_t g, uint8_t b);
void rgb_set_config(uint8_t r, uint8_t g, uint8_t b);

void rgb_task(void *pvParameters);

#endif
