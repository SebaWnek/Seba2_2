#ifndef COMMONS_H
#define COMMONS_H

#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <stdint.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_CLIENTS 10
#define FIFO_PATH "/tmp/fifo"
#define QUEUE_NAME "/tmp/queue"
#define SHM_NAME "/tmp/shm_name"

typedef struct {
    pid_t pid;
    int8_t number;
} client;

typedef struct {
    client *c;
    int32_t msgReceived;
    int32_t msgSent;
} messageCount;

#endif // COMMONS_H