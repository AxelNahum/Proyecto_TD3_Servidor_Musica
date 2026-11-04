/*
 * output.c
 * Mensajes para la consola o eventos para la interfaz grafica (ver output.h).
 */
#include <stdio.h>
#include <stdarg.h>
#include "output.h"

int gui_mode = 0;

void show_message(const char *fmt, ...)
{
    va_list args;

    if (gui_mode)
        return;

    printf("\n");
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n> ");
    fflush(stdout);
}

void show_event(const char *fmt, ...)
{
    va_list args;

    if (!gui_mode)
        return;

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);         /* La interfaz lee por un pipe: hay que mandarlo ya */
}
