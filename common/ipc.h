/*
 * ipc.h
 * Funciones simples para usar memoria compartida y semaforos System V
 * (shmget, shmat, semget, semop, ...), como en los ejemplos de la catedra.
 */
#ifndef IPC_H
#define IPC_H

#include <stddef.h>

/* ---- Memoria compartida ---- */

/* Crea un segmento de 'size' bytes. Devuelve su id o -1 si hubo error. */
int shared_mem_create(size_t size);

/* Se conecta al segmento y devuelve el puntero, o NULL si hubo error. */
void *shared_mem_attach(int shm_id);

/* Se desconecta del segmento y lo borra del sistema. */
void shared_mem_remove(int shm_id, void *addr);

/* ---- Semaforos ---- */

/* Crea un set de 'count' semaforos. Devuelve su id o -1 si hubo error. */
int semaphore_create(int count);

/* Pone el semaforo numero 'num' del set en 'value'. */
int semaphore_set(int sem_id, int num, int value);

/* Resta 1 al semaforo (se bloquea si esta en 0). Tambien llamado P o wait. */
int semaphore_down(int sem_id, int num);

/* Suma 1 al semaforo (despierta a quien estaba esperando). Tambien llamado V o signal. */
int semaphore_up(int sem_id, int num);

/* Borra el set de semaforos del sistema. */
void semaphore_remove(int sem_id);

#endif
