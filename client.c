#include "commons.h"

void termSignalHandler(int signal);
void registerWithMaster(void);
void getNumber(void);
void sendInfoMessage(void);
void performExit(void);
void sigusr1Handler(int signal);
void printClients(void);

int8_t number = -1;
pid_t pid;
pid_t serverPid;
mqd_t mq;
pthread_mutex_t mutex;
pthread_cond_t cond;
messageCount clients[MAX_CLIENTS];
sem_t *semaphore;
int fd;
bool ready = false;
int shm_fd;
void *shmPtr;

int main() 
{
    if (atexit(performExit) != 0) {
        fprintf(stderr, "Cannot set exit function\n");
        return 1;
    }
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    semaphore = sem_open(SEM_NAME, 0);
    if (semaphore == SEM_FAILED)
    {
        perror("unable to create the semaphore");
        exit(-1);
    }

    // Register signal SIGTERM
    signal(SIGTERM, termSignalHandler);
    signal(SIGUSR1, sigusr1Handler);
    
    pid = getpid();
    getNumber();
    CLEAR_BUFFER;
    
    /* open the shared memory segment */
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("unable to open the shared memory segment");
        exit(-1);
    }
    shmPtr = mmap(0, MAX_CLIENTS * sizeof(messageCount) + sizeof(pid_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shmPtr == MAP_FAILED) {
        perror("unable to map the shared memory segment");
        exit(-1);
    }

    // Get server PID
    serverPid = *((pid_t*)((void*)shmPtr + MAX_CLIENTS * sizeof(messageCount)));
    printf("Server PID: %d\n", serverPid);

    // Register with the master
    registerWithMaster();

    char command;
    while(1)
    {
        printf("type \"m\" to send a message or \"q\" to quit\n");
        scanf("%c", &command);
        if(command == 'm') 
        {
            printf("Invoking sender\n");
            sendInfoMessage();
        }
        else if(command == 'q')
        {
            exit(0);
        }
        else
        {
            printf("Invalid command\n");
        }
        CLEAR_BUFFER;
    }

    return 0;
}

void getNumber(void)
{
    while(number <= 0)
    {
        printf("Enter a positive number, max 127:\n");
        if(scanf("%d", (int*)&number) == 0) 
        {
            printf("Error reading the number, try again\n");
            CLEAR_BUFFER;
            continue;
        }
        if(number < 0)
        {
            printf("The number must be positive, try again\n");
            CLEAR_BUFFER;
        }
    }
    printf("The number is %d\n", number);
}

void registerWithMaster(void) {
    client s;
    s.pid = pid;
    s.number = number;
    
    mq = mq_open(QUEUE_NAME, O_WRONLY, 0666, NULL);
    if (mq == (mqd_t)-1) {
        perror("unable to open the message queue");
        exit(-1);
    }

    if (mq_send(mq, (char*)&s, sizeof(client), 0) == -1) {
        perror("unable to send the message");
        exit(-1);
    }
    
}

void termSignalHandler(int signal) {
    printf("Received signal %d\n", signal);
    exit(0);
}

void sendInfoMessage(void)
{
    printf("Sending signal\n");
    kill(serverPid, SIGUSR1);

    pthread_mutex_lock(&mutex);
    while(!ready)
    {
        pthread_cond_wait(&cond, &mutex);
    }

    //make sure that the server is not writing to the shared memory at the same time for example for other client
    sem_wait(semaphore);
    if (shmPtr != NULL) {
        memcpy(clients, shmPtr, MAX_CLIENTS * sizeof(messageCount));
    } else {
        printf("Shared memory pointer is NULL\n");
    }
    sem_post(semaphore);

    printClients();
    ready = false;
    pthread_mutex_unlock(&mutex);
}

void performExit(void)
{
    fd = open(FIFO_PATH, O_WRONLY);
    if (fd == -1)
    {
        perror("unable to open the FIFO");
        exit(-1);
    }
    printf("Exiting\n");
    write(fd, &number, sizeof(int8_t));
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    mq_close(mq);
    close(fd);
    munmap(shmPtr, MAX_CLIENTS * sizeof(messageCount));
}

void sigusr1Handler(int signal)
{
    pthread_mutex_lock(&mutex);
    ready = true;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}

void printClients(void)
{
    int i;
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].c.number != 0)
        {
            printf("Client %d has received %d messages and sent %d messages\n", clients[i].c.number, clients[i].msgReceived, clients[i].msgSent);
        }
    }
}