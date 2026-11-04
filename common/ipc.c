/*
 * ipc.c
 * Memoria compartida y semaforos System V.
 *
 * Se usa IPC_PRIVATE en vez de ftok(): crea siempre un segmento nuevo, sin
 * nombre, que solo pueden usar este proceso y los hijos que cree con fork().
 * Asi dos clientes corriendo en la misma PC no comparten el buffer sin querer.
 */
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "ipc.h"

/* Linux pide que esta union la defina el programa (ver "man semctl") */
union semun {
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
};

/* ---- Memoria compartida ---- */

int shared_mem_create(size_t size)
{
    int shm_id = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
    if (shm_id == -1)
        perror("shmget");
    return shm_id;
}

void *shared_mem_attach(int shm_id)
{
    void *addr = shmat(shm_id, NULL, 0);
    if (addr == (void *)-1) {
        perror("shmat");
        return NULL;
    }
    return addr;
}

void shared_mem_remove(int shm_id, void *addr)
{
    if (addr != NULL)
        shmdt(addr);
    shmctl(shm_id, IPC_RMID, NULL);
}

/* ---- Semaforos ---- */

int semaphore_create(int count)
{
    int sem_id = semget(IPC_PRIVATE, count, IPC_CREAT | 0600);
    if (sem_id == -1)
        perror("semget");
    return sem_id;
}

int semaphore_set(int sem_id, int num, int value)
{
    union semun arg;

    arg.val = value;
    if (semctl(sem_id, num, SETVAL, arg) == -1) {
        perror("semctl SETVAL");
        return -1;
    }
    return 0;
}

/* Hace semop con +1 o -1 sobre un semaforo */
static int semaphore_op(int sem_id, int num, int delta)
{
    struct sembuf op;

    op.sem_num = num;
    op.sem_op  = delta;
    op.sem_flg = 0;

    while (semop(sem_id, &op, 1) == -1) {
        if (errno != EINTR) {       /* Si lo interrumpio una senial, se reintenta */
            perror("semop");
            return -1;
        }
    }
    return 0;
}

int semaphore_down(int sem_id, int num)
{
    return semaphore_op(sem_id, num, -1);
}

int semaphore_up(int sem_id, int num)
{
    return semaphore_op(sem_id, num, +1);
}

void semaphore_remove(int sem_id)
{
    semctl(sem_id, 0, IPC_RMID);
}
