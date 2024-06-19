#include "commons.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

void* registerThreadFunc(void* arg);
void* fifoThreadFunc(void* arg);
void* sendDataThreadFunc(void* arg);
void unregisterClient(int8_t number);
void exitFunc(void);
void sendData(pid_t requester);
void sigusr1Handler(int signo, siginfo_t *si, void *data);

messageCount clients[MAX_CLIENTS];
int8_t clientsCount = 0;
pthread_t registerThread;
pthread_t fifoThread;
pthread_t senderThread;
sem_t *semaphore;
pthread_mutex_t mutex;
pthread_cond_t cond;
pid_t requesterPid;
struct sigaction sa;
bool signaled = false;
int shm_fd;
void *shmPtr;

int main() 
{    
    if (atexit(exitFunc) != 0) {
        fprintf(stderr, "Cannot set exit function\n");
        return 1;
    }
    
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigusr1Handler;

    sigaction(SIGUSR1, &sa, NULL);

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    int createThreadResult;
    createThreadResult = pthread_create(&registerThread, NULL, registerThreadFunc, NULL);
    if (createThreadResult != 0) {
        fprintf(stderr, "Failed to create thread: %d\n", createThreadResult);
        exit(EXIT_FAILURE);
    }
    createThreadResult = pthread_create(&fifoThread, NULL, fifoThreadFunc, NULL);
    if (createThreadResult != 0) {
        fprintf(stderr, "Failed to create thread: %d\n", createThreadResult);
        exit(EXIT_FAILURE);
    }
    createThreadResult = pthread_create(&senderThread, NULL, sendDataThreadFunc, NULL);
    if (createThreadResult != 0) {
        fprintf(stderr, "Failed to create thread: %d\n", createThreadResult);
        exit(EXIT_FAILURE);
    }

    int8_t clientNumber;
    bool found;
    usleep(100000); // To make sure threads start before next message
    while(1)
    {
        found = false;
        printf("Enter the client number:\n");
        scanf("%d", (int*)&clientNumber);
        for(int i = 0; i < clientsCount; i++)
        {
            if(clients[i].c.number == clientNumber)
            {
                found = true;
                printf("Closing the client %d\n", clientNumber);
                kill(clients[i].c.pid, SIGTERM);
                break;
            }
            if (!found)
            {
                printf("Client %d not found\n", clientNumber);
            }
        }
    }

    return 0;
}

void* registerThreadFunc(void* arg) 
{
    printf("Register thread started\n");
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(client);
    attr.mq_curmsgs = 0;

    mqd_t mq = mq_open(QUEUE_NAME, O_CREAT | O_RDONLY, 0666, &attr);
    client *tmp;

    if (mq == (mqd_t)-1) {
        perror("unable to create the message queue");
        exit(-1);
    }

    while (1) {
        if (clientsCount < MAX_CLIENTS)
        {
            tmp = malloc(sizeof(client));
            printf("Waiting for the clients to register, currently %d registered\n", clientsCount);
            if (mq_receive(mq, (char*)tmp, sizeof(client), NULL) == -1) 
            {
                perror("unable to receive the message");
                exit(-1);
            }
            clients[clientsCount].c.number = tmp->number;
            clients[clientsCount].c.pid = tmp->pid;
            clientsCount++;
            printf("Client registered with number %d, pid: %d\n", tmp->number, tmp->pid);
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
        if (clients[i].c.number == number)
        {
            clientsCount--;
            for(j = i; j < clientsCount - 1; j++)
            {
                clients[j].c.number = clients[j+1].c.number;
                clients[j].c.pid = clients[j+1].c.pid;
                clients[j].msgReceived = clients[j+1].msgReceived;
                clients[j].msgSent = clients[j+1].msgSent;
            }
            clients[clientsCount].c.number = 0;
            clients[clientsCount].c.pid = 0;
            clients[clientsCount].msgReceived = 0;
            clients[clientsCount].msgSent = 0;
            break;
        }
    }
}

void* fifoThreadFunc(void* arg)
{
    printf("Fifo thread started\n");
    
    unlink(FIFO_PATH); //remove fifo file if already exists

    semaphore = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (semaphore == SEM_FAILED) {
        perror("unable to create the semaphore");
        exit(-1);
    }

    /* create the shared memory segment */
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("unable to create the shared memory segment");
        exit(-1);
    }
    ftruncate(shm_fd, MAX_CLIENTS * sizeof(messageCount) + sizeof(pid_t));
    shmPtr = mmap(0, MAX_CLIENTS * sizeof(messageCount) + sizeof(pid_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shmPtr == MAP_FAILED) {
        perror("unable to map the shared memory segment");
        exit(-1);
    }

    // Write server PID at the end of shared memory segment
    pid_t *pid = (pid_t*)((void*)shmPtr + MAX_CLIENTS * sizeof(messageCount));
    *pid = getpid();

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
        printf("Unregistering client %d\n", number);
        unregisterClient(number);
        // Reinitialize the FIFO
        close(fd);
        unlink(FIFO_PATH);
        if (mkfifo(FIFO_PATH, 0666) == -1) {
            perror("unable to recreate the FIFO");
            exit(-1);
        }
        fd = open(FIFO_PATH, O_RDONLY);
        if (fd == -1) {
            perror("unable to open the FIFO");
            exit(-1);
        }
    }
}

void exitFunc(void)
{
    int i;
    mq_unlink(QUEUE_NAME);
    unlink(FIFO_PATH);
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
}


void sendData(pid_t requester)
{
    sem_wait(semaphore);
    memcpy(shmPtr, clients, MAX_CLIENTS * sizeof(messageCount));

    kill(requester, SIGUSR1);

    // Releasing after sending the signal to minimise chance of race conditions, as hopefully client is already wairting for this semaphore
    // so other one will not have chance to initiate this process before it will finish copying
    sem_post(semaphore); 
}

void* sendDataThreadFunc(void* arg)
{
    printf("Sender thread started\n");

    while (1)
    {
        pthread_mutex_lock(&mutex);
        while (!signaled)
        {
            pthread_cond_wait(&cond, &mutex);
        }
        signaled = false;
        sendData(requesterPid);
        pthread_mutex_unlock(&mutex);
    }
}

void sigusr1Handler(int signo, siginfo_t *si, void *data)
{
    pthread_mutex_lock(&mutex);
    signaled = true;
    requesterPid = si->si_pid;
    printf("Data requested by %d\n", requesterPid);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}