//uart2.h
#ifndef _UART2_H
#define _UART2_H

#include <inttypes.h>
#include <stdio.h>

extern FILE _uart2io;
#define uart2io (&_uart2io)

void uart2_init(uint32_t baudRate);

// Next received byte (0-255), or -1 when none: what fgetc(uart2io) returns, without the stdio layer
int uart2_getchar(FILE *stream);

#endif //_UART2_H
