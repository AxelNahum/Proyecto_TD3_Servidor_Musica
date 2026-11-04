/*
 * protocol.h
 * Definiciones compartidas entre el cliente y el servidor.
 *
 * Protocolo (texto, cada linea termina en '\n'):
 *
 *   Cliente -> Servidor          Servidor -> Cliente
 *   -------------------          -------------------------------------------
 *   LIST                         OK <n>            y luego n lineas con nombres
 *   GET <nombre>                 OK <tamanio>      y luego <tamanio> bytes del archivo
 *   (cualquier error)            ERR <mensaje>
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <sys/types.h>

#define PORT        3490    /* Puerto donde escucha el servidor */
#define LINE_SIZE   512     /* Largo maximo de una linea del protocolo */
#define BLOCK_SIZE  4096    /* Tamanio de cada bloque de la cancion */

/* Envia los 'len' bytes de 'buf' (send() puede enviar menos de lo pedido).
 * Devuelve 0 si se envio todo, -1 si hubo error. */
int send_all(int fd, const void *buf, size_t len);

/* Envia una linea de texto con formato printf, agregando '\n' al final. */
int send_line(int fd, const char *fmt, ...);

/* Lee una linea del socket hasta '\n' (que no se guarda) y la deja en 'buf'.
 * Lee de a un byte para no consumir los datos binarios que vienen despues.
 * Devuelve 1 si leyo una linea (aunque este vacia), 0 si se cerro la
 * conexion o -1 si hubo error. */
ssize_t read_line(int fd, char *buf, size_t size);

#endif
