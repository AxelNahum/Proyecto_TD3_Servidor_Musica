/*
 * server_log.c
 * Mensajes del servidor (los usan el padre y los hijos).
 */
#include <stdio.h>
#include <stdarg.h>
#include "protocol.h"
#include "server_log.h"

static int gui_mode = 0;

void log_set_gui_mode(int on)
{
    gui_mode = on;
}

int log_gui_mode(void)
{
    return gui_mode;
}

void server_log(const char *fmt, ...)
{
    char text[LINE_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    if (gui_mode)
        printf("LOG %s\n", text);
    else
        printf("%s\n", text);

    /* Se manda enseguida: si no, la interfaz (que lee por un pipe) los
     * recibiria todos juntos mucho despues */
    fflush(stdout);
}
