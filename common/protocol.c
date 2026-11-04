/*
 * protocol.c
 * Funciones de envio y recepcion usadas por el cliente y el servidor.
 */
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/socket.h>
#include "protocol.h"

int send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;

    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p   += n;
        len -= n;
    }
    return 0;
}

int send_line(int fd, const char *fmt, ...)
{
    char line[LINE_SIZE];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(line, sizeof(line) - 1, fmt, args);
    va_end(args);

    if (n < 0)
        return -1;
    if (n > (int)sizeof(line) - 2)      /* Recorta si no entra */
        n = sizeof(line) - 2;
    line[n++] = '\n';

    return send_all(fd, line, n);
}

ssize_t read_line(int fd, char *buf, size_t size)
{
    size_t i = 0;
    char c;

    while (1) {
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0)
            return 0;                   /* Conexion cerrada */
        if (n == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (c == '\n')
            break;
        if (c != '\r' && i < size - 1)  /* Ignora '\r' y lo que no entra */
            buf[i++] = c;
    }
    buf[i] = '\0';
    return 1;
}
