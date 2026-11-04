/*
 * server_main.c
 * Servidor TCP concurrente: por cada cliente que se conecta crea un
 * proceso hijo (fork) que atiende sus comandos (ver server_commands.c).
 *
 * Antes de aceptar conexiones crea la tabla de sesiones en memoria
 * compartida (session_table.c), que ven el padre y todos los hijos. Con ella:
 *   - limita la cantidad de clientes conectados a la vez
 *   - muestra estadisticas cada vez que se conecta alguien
 *     (o cuando se quiera, con: kill -USR1 <pid del servidor>)
 *
 * Modo --gui: lo usa la interfaz grafica (gui/server_gui.py), que lanza
 * este programa y lee lo que imprime. En ese modo los mensajes salen como
 * "LOG ..." y la tabla se imprime cada 1 segundo en lineas STATS/CLIENT/END.
 *
 * Uso: ./bin/server [carpeta] [max_clientes] [--gui]   (por defecto "songs" y 5)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include "protocol.h"
#include "server_commands.h"
#include "session_table.h"
#include "server_log.h"

#define BACKLOG     10      /* Conexiones en espera como maximo */
#define MAX_CLIENTS 5       /* Limite de clientes a la vez, por defecto */

static pid_t main_pid;                      /* PID del padre */
static volatile sig_atomic_t show_table = 0;

/* Ctrl+C, o SIGTERM (la interfaz grafica al apretar "Detener"):
 * el padre termina a los hijos y borra la tabla (memoria compartida y
 * semaforo). Los hijos tambien reciben la senial y solo terminan. */
static void on_exit_signal(int sig)
{
    (void)sig;
    if (getpid() == main_pid) {
        table_kill_children();
        table_destroy();
    }
    _exit(0);
}

/* kill -USR1 <pid>: pide mostrar la tabla. Solo se anota; se muestra en el loop. */
static void on_sigusr1(int sig)
{
    (void)sig;
    show_table = 1;
}

int main(int argc, char *argv[])
{
    int sockfd, newfd, row;
    struct sockaddr_in my_addr, client_addr;
    socklen_t addr_len;
    struct sigaction sa;
    struct timeval timeout;
    fd_set readfds;
    int opt = 1;
    pid_t pid;
    char client[64];    /* "IP:puerto" del cliente, para los mensajes */
    const char *songs_dir = "songs";
    int max_clients = MAX_CLIENTS;
    int gui = 0, positional = 0;

    /* Argumentos: [carpeta] [max_clientes] y, en cualquier lugar, --gui */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gui") == 0)
            gui = 1;
        else if (positional++ == 0)
            songs_dir = argv[i];
        else
            max_clients = atoi(argv[i]);
    }

    if (max_clients < 1 || max_clients > MAX_SESSIONS) {
        fprintf(stderr, "El maximo de clientes tiene que estar entre 1 y %d\n", MAX_SESSIONS);
        exit(1);
    }
    log_set_gui_mode(gui);

    /* Los hijos que terminan no quedan zombies */
    signal(SIGCHLD, SIG_IGN);
    /* Si el cliente se cae mientras le enviamos, send() devuelve error en vez de matar al proceso */
    signal(SIGPIPE, SIG_IGN);

    /* Tabla de sesiones: se crea ANTES de los fork para que la vean todos */
    if (table_create(max_clients) == -1) {
        fprintf(stderr, "No se pudo crear la tabla de sesiones\n");
        exit(1);
    }
    main_pid = getpid();
    signal(SIGINT, on_exit_signal);
    signal(SIGTERM, on_exit_signal);

    /* SIGUSR1 sin SA_RESTART: asi interrumpe al select() y la tabla se muestra enseguida */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigusr1;
    sigaction(SIGUSR1, &sa, NULL);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        table_destroy();
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
        table_destroy();
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        table_destroy();
        exit(1);
    }

    if (gui) {
        printf("READY %d %d\n", PORT, max_clients);     /* La interfaz espera esta linea */
        fflush(stdout);
    } else {
        printf("Servidor escuchando en el puerto %d. Carpeta: \"%s\". Maximo: %d clientes\n",
               PORT, songs_dir, max_clients);
        printf("Para ver la tabla en cualquier momento: kill -USR1 %d\n", main_pid);
    }

    while (1) {
        /* Se espera una conexion como mucho 1 segundo: en modo --gui, cada
         * segundo se le manda la tabla actualizada a la interfaz */
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        timeout.tv_sec  = 1;
        timeout.tv_usec = 0;

        int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);

        if (show_table) {                   /* Llego un SIGUSR1 */
            show_table = 0;
            if (gui)
                table_print_gui();
            else
                table_print();
        }
        if (ready <= 0) {                   /* Timeout o senial: no hay conexion nueva */
            if (ready == -1 && errno != EINTR)
                perror("select");
            if (gui)
                table_print_gui();
            continue;
        }

        addr_len = sizeof(client_addr);
        if ((newfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len)) == -1) {
            if (errno != EINTR)
                perror("accept");
            continue;
        }

        snprintf(client, sizeof(client), "%s:%d",
                 inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        /* Limite de conexiones: si la tabla esta llena, se rechaza */
        if ((row = table_add(client)) == -1) {
            server_log("[%s] Rechazado: servidor lleno (%d de %d)", client, table_count(), table_max());
            send_line(newfd, "ERR servidor lleno (maximo %d clientes), proba mas tarde", table_max());
            close(newfd);
            continue;
        }

        server_log("[%s] Conectado", client);

        if ((pid = fork()) == 0) {
            /* Proceso hijo: atiende al cliente */
            close(sockfd);
            handle_client(newfd, songs_dir, client, row);
            close(newfd);
            exit(0);
        }

        if (pid == -1) {
            perror("fork");
            table_remove(row);
        } else {
            table_set_pid(row, pid);
        }

        /* Proceso padre: no usa este descriptor; muestra las estadisticas */
        close(newfd);
        if (gui)
            table_print_gui();
        else
            table_print();
    }

    return 0;
}
