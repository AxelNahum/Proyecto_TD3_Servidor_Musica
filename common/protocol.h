/*
 * protocol.h
 * Definiciones compartidas entre el cliente y el servidor.
 *
 * Protocolo (texto, cada linea termina en '\n'):
 *
 *   Cliente -> Servidor          Servidor -> Cliente
 *   -------------------          -------------------------------------------
 *   LIST                         SONGS <n>           y luego n lineas con nombres
 *   GET <nombre>                 START <tamanio> <nombre>
 *                                DATA <n>            y luego n bytes de la cancion
 *                                DATA <n> ...        (muchos, hasta terminar)
 *                                END                 termino la cancion
 *   PAUSE                        PAUSED              el servidor deja de mandar DATA
 *   RESUME                       RESUMED             el servidor sigue mandando DATA
 *   SEEK <byte>                  SEEKED <byte>       sigue mandando DATA desde ese byte
 *   QUIT                         STOPPED             saca la cancion
 *   (cualquier error)            ERR <mensaje>
 *
 * El audio va en paquetes DATA para que el cliente pueda recibir otros
 * mensajes (respuestas a comandos) en medio de la cancion.
 *
 * GET, SEEK y QUIT responden SIEMPRE con exactamente una "marca" (START,
 * SEEKED o STOPPED). Si hay un error mandan "ERR ..." seguido de STOPPED.
 * El cliente cuenta las marcas para saber que audio ya no corresponde.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <sys/types.h>

#define PORT        3490    /* Puerto donde escucha el servidor */
#define LINE_SIZE   512     /* Largo maximo de una linea del protocolo */
#define BLOCK_SIZE  4096    /* Tamanio maximo de cada paquete DATA */

/* Envia los 'len' bytes de 'buf' (send() puede enviar menos de lo pedido).
 * Devuelve 0 si se envio todo, -1 si hubo error. */
int send_all(int fd, const void *buf, size_t len);

/* Recibe exactamente 'len' bytes en 'buf' (recv() puede devolver menos).
 * Devuelve 0 si llego todo, -1 si se corto la conexion o hubo error. */
int recv_all(int fd, void *buf, size_t len);

/* Envia una linea de texto con formato printf, agregando '\n' al final. */
int send_line(int fd, const char *fmt, ...);

/* Lee una linea del socket hasta '\n' (que no se guarda) y la deja en 'buf'.
 * Lee de a un byte para no consumir los datos binarios que vienen despues.
 * Devuelve 1 si leyo una linea (aunque este vacia), 0 si se cerro la
 * conexion o -1 si hubo error. */
ssize_t read_line(int fd, char *buf, size_t size);

#endif
