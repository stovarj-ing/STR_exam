#ifndef COLOR_SETUP_H
#define COLOR_SETUP_H

#include "config.h"

// Inicializa pulsador y LED externo.
// El parametro `config` queda disponible para posibles ampliaciones.
void color_setup_init(system_config_t *config);

// Lee potenciometro, controla LED externo y cambia unidades con el boton.
void color_setup_task(void *pvParameters);

#endif
