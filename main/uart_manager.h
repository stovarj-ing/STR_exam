#ifndef UART_MANAGER_H
#define UART_MANAGER_H

#include "config.h"

#include "driver/uart.h"

// UART0 funciona como consola serie para recibir comandos del usuario.
// El buffer se usa para leer lineas completas de comando desde el puerto.
#define UART_PORT_NUM      UART_NUM_0
#define UART_BAUD_RATE     115200
#define UART_BUFFER_SIZE   1024

void uart_init(void);

void uart_task(void *pvParameters);

#endif
