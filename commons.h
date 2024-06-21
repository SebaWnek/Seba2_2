#ifndef COMMONS_H
#define COMMONS_H

#define _XOPEN_SOURCE 700 // needed for sigaction

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

#define MAX_CLIENTS 3 // max number of clients, only 3 for easier testing
#define FIFO_PATH "/tmp/fifo" // path to fifo
#define QUEUE_NAME "/queue" // message queue name
#define SHM_NAME "/shm_name" // shared memory name
#define SEM_NAME "/sem_name" // semaphore name
#define CLEAR_BUFFER while((tmpChar = getchar()) != '\n' && tmpChar != EOF) // clear buffer macro, to remove input that was left after scanf

#define DEBUG 1 // set to 1 to enable debug messages

char tmpChar; // used to clear buffer

//struct for holding and passing info about client
typedef struct {
    pid_t pid; // client pid
    int8_t number; // client number assigned by user
} client;

//struct for holding and passing info about client and message count
typedef struct {
    client c;
    int32_t msgReceived; // number of messages received by server from client
    int32_t msgSent;    // number of messages sent by server to client
} messageCount;

#endif // COMMONS_H