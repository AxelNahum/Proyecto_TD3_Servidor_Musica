/*
 * server_main.c
 * Servidor TCP concurrente: por cada cliente que se conecta crea un
 * proceso hijo (fork) que atiende sus comandos (ver server_commands.c).
 *
 * Uso: ./bin/server [carpeta]     (por defecto "songs")
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "protocol.h"
#include "server_commands.h"

#define BACKLOG 10      /* Conexiones en espera como maximo */

int main(int argc, char *argv[])
{
    int sockfd, newfd;
    struct sockaddr_in my_addr, client_addr;
    socklen_t addr_len;
    int opt = 1;
    char client[64];    /* "IP:puerto" del cliente, para los mensajes */
    const char *songs_dir = (argc > 1) ? argv[1] : "songs";

    /* Los hijos que terminan no quedan zombies */
    signal(SIGCHLD, SIG_IGN);
    /* Si el cliente se cae mientras le enviamos, send() devuelve error en vez de matar al proceso */
    signal(SIGPIPE, SIG_IGN);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    /* Permite reusar el puerto apenas se cierra el servidor */
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family      = AF_INET;
    my_addr.sin_port        = htons(PORT);
    my_addr.sin_addr.s_addr = INADDR_ANY;  /* Escucha en todas las interfaces */

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1) {
        perror("bind");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    printf("Servidor escuchando en el puerto %d. Carpeta: \"%s\"\n", PORT, songs_dir);

    while (1) {
        addr_len = sizeof(client_addr);
        if ((newfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len)) == -1) {
            perror("accept");
            continue;
        }

        snprintf(client, sizeof(client), "%s:%d",
                 inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        printf("[%s] Conectado\n", client);

        if (fork() == 0) {
            /* Proceso hijo: atiende al cliente */
            close(sockfd);
            handle_client(newfd, songs_dir, client);
            close(newfd);
            exit(0);
        }

        /* Proceso padre: no usa este descriptor */
        close(newfd);
    }

    return 0;
}
