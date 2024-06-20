#ifndef COMMONS_H
#define COMMONS_H

#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdint.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define MAX_CLIENTS 3
#define FIFO_PATH "/tmp/fifo"
#define QUEUE_NAME "/queue"
#define SHM_NAME "/shm_name"
#define SEM_NAME "/sem_name"
#define CLEAR_BUFFER while((tmpChar = getchar()) != '\n' && tmpChar != EOF)

char tmpChar;

typedef struct {
    pid_t pid;
    int8_t number;
} client;

typedef struct {
    client c;
    int32_t msgReceived;
    int32_t msgSent;
} messageCount;

#endif // COMMONS_H