#include "commons.h"
#include <pthread.h>
#include <sys/mman.h>

void* registerThread(void* arg);
void* fifoThread(void* arg);
void unregisterClient(int8_t number);
void exitFunc(void);
void sendData(int8_t number);

messageCount clients[MAX_CLIENTS];
int8_t clientsCount = 0;
pthread_t thread;
int shm_fd;
void *shmPtr;

int main() 
{    
    if (atexit(exitFunc) != 0) {
        fprintf(stderr, "Cannot set exit function\n");
        return 1;
    }
    pthread_create(&thread, NULL, registerThread, NULL);
    pthread_create(&thread, NULL, fifoThread, NULL);

    int8_t clientNumber;
    bool found;
    while(1)
    {
        found = false;
        printf("Enter the client number: ");
        scanf("%d", &clientNumber);
        for(int i = 0; i < clientsCount; i++)
        {
            if(clients[i].c->number == clientNumber)
            {
                found = true;
                printf("Closing the client %d\n", clientNumber);
                kill(clients[i].c->pid, SIGTERM);
                break;
            }
        }
    }

    return 0;
}

void* registerThread(void* arg) {
    client *tmp;
    mqd_t mq = mq_open(QUEUE_NAME, O_CREAT | O_RDONLY, 0666, NULL);

    if (mq == (mqd_t)-1) {
        perror("unable to create the message queue");
        exit(-1);
    }

    while (1) {
        if (clientsCount < MAX_CLIENTS)
        {
            tmp = malloc(sizeof(client));
            if (mq_receive(mq, (char*)tmp, sizeof(client), NULL) == -1) 
            {
                perror("unable to receive the message");
                exit(-1);
            }
            clients[clientsCount].c = tmp;
            clientsCount++;
        }
        else 
        {
            printf("The server is full\n");
        }
    }
}

void unregisterClient(int8_t number)
{
    int i,j;
    for (i = 0; i < clientsCount; i++)
    {
        if (clients[i].c->number == number)
        {
            free(clients[i].c);
            clients[i].c = NULL;
            clientsCount--;
            for(j = i; j < clientsCount - 1; j++)
            {
                clients[j].c = clients[j+1].c;
                clients[j].msgReceived = clients[j+1].msgReceived;
                clients[j].msgSent = clients[j+1].msgSent;
            }
            break;
        }
    }
}

void* fifoThread(void* arg)
{
    /* create the shared memory segment */
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, MAX_CLIENTS * sizeof(messageCount));
    shmPtr = mmap(0, MAX_CLIENTS * sizeof(messageCount), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // Create the FIFO if it doesn't exist
    if (mkfifo(FIFO_PATH, 0666) == -1) {
        perror("unable to create the FIFO");
        exit(-1);
    }

    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd == -1) {
        perror("unable to open the FIFO");
        exit(-1);
    }

    while(1)
    {
        int8_t number;
        if (read(fd, &number, sizeof(int8_t)) == -1) {
            perror("unable to read from the FIFO");
            exit(-1);
        }
        if(number < 0)
        {
            unregisterClient(number);
        }
        if(number > 0)
        {
            sendData(number);
        }
    }
}

void exitFunc(void)
{
    int i;
    for (i = 0; i < clientsCount; i++)
    {
        free(clients[i].c);
    }
    mq_unlink(QUEUE_NAME);
    unlink(FIFO_PATH);
    shm_unlink(SHM_NAME);
}


void sendData(int8_t number)
{
    memcpy(shmPtr, clients, MAX_CLIENTS * sizeof(messageCount));
    for(int i = 0; i < clientsCount; i++)
    {
        if (clients[i].c->number == number)
        {
            kill(clients[i].c->pid, SIGUSR1);
            break;
        }
    }
}