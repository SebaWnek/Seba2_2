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

// #define MAX_SLAVES 3 // max number of slaves, only 3 for easier testing
#define FIFO_PATH "/tmp/fifo" // path to fifo
#define QUEUE_NAME "/queue" // message queue name
#define SHM_NAME "/shm_name" // shared memory name
#define SEM_NAME "/sem_name" // semaphore name
// #define CLEAR_BUFFER while((tmpChar = getchar()) != '\n' && tmpChar != EOF) // clear buffer macro, to remove input that was left after scanf

#define DEBUG 1 // set to 1 to enable debug messages

// char tmpChar; // used to clear buffer

//struct for holding and passing info about slave
typedef struct {
    pid_t pid; // slave pid
    int8_t number; // slave number assigned by user
} slave;

//struct for holding and passing info about slave and message count
typedef struct {
    slave s;
    int32_t msgReceived; // number of messages received by master from slave
    int32_t msgSent;    // number of messages sent by master to slave
} messageCount;

#endif // COMMONS_H
