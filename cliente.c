/*
 * cliente.c
 * Cliente TCP: se conecta al servidor, recibe la palabra y la
 * muestra por terminal.
 *
 * Uso: ./cliente <IP del servidor>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PUERTO  3490    /* Debe coincidir con el del servidor */
#define TAM_BUF 256

int main(int argc, char *argv[])
{
    int sockfd, n;
    char buf[TAM_BUF];
    struct sockaddr_in serv_dir;

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <IP del servidor>\n", argv[0]);
        exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    memset(&serv_dir, 0, sizeof(serv_dir));
    serv_dir.sin_family = AF_INET;
    serv_dir.sin_port   = htons(PUERTO);
    if (inet_pton(AF_INET, argv[1], &serv_dir.sin_addr) != 1) {
        fprintf(stderr, "IP invalida: %s\n", argv[1]);
        exit(1);
    }

    if (connect(sockfd, (struct sockaddr *)&serv_dir, sizeof(serv_dir)) == -1) {
        perror("connect");
        exit(1);
    }

    /* Lee hasta que el servidor cierra la conexion */
    printf("Recibido: ");
    while ((n = recv(sockfd, buf, TAM_BUF - 1, 0)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    printf("\n");

    if (n == -1)
        perror("recv");

    close(sockfd);
    return 0;
}
