#ifndef NTC_H
#define NTC_H

#include "config.h"

#define ADC_CHANNEL ADC_CHANNEL_1
#define ADC_UNIT    ADC_UNIT_1

// Cantidad de muestras promediadas para reducir ruido de lectura.
#define ADC_SAMPLES 64

// Parametros fisicos del divisor y del termistor NTC.
#define SERIES_RESISTOR      11000.0
#define NOMINAL_RESISTANCE   10000.0
#define NOMINAL_TEMPERATURE  25.0
#define BETA_COEFFICIENT     4000.0

void adc_init_ntc(adc_handles_t *adc_handles);
void temperature_task(void *pvParameters);

#endif
