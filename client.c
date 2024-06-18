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
mqd_t mq;
pthread_mutex_t mutex;
pthread_cond_t cond;
bool ready = false;
messageCount clients[MAX_CLIENTS];
int shm_fd;
void *shmPtr;

int main() 
{
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    // Register signal SIGTERM
    signal(SIGTERM, termSignalHandler);
    signal(SIGUSR1, sigusr1Handler);
    pid = getpid();
    getNumber();

    /* create the shared memory segment */
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    frtuncate(shm_fd, MAX_CLIENTS * sizeof(messageCount));
    shmPtr = mmap(0, MAX_CLIENTS * sizeof(messageCount), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // Register with the master
    registerWithMaster();

    int fd = open(FIFO_PATH, O_WRONLY);
    if (fd == -1) {
        perror("unable to open the FIFO");
        exit(-1);
    }

    char command;
    while(1)
    {
        printf("type \"m\" to send a message or \"q\" to quit\n");
        scanf("%c", &command);
        if(command == 'm') 
        {
            sendInfoMessage();
        }
        else if(command == 'q')
        {
            performExit();
        }
        else
        {
            printf("Invalid command\n");
        }
    }

    return 0;
}

void getNumber(void)
{
    while(number <= 0)
    {
        // Get the slave number
        if(scanf("%d", &number) == 0) 
        {
            printf("Error reading the number, try again\n");
            return 1;
        }
        if(number < 0)
        {
            printf("The number must be positive, try again\n");
        }
    }
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
    performExit();
}

void sendInfoMessage(void)
{
    if(mq_send(mq, (char*)&number, sizeof(int8_t), 0) == -1)
    {
        perror("unable to send the message");
        exit(-1);
    }
    pthread_mutex_lock(&mutex);
    while(!ready)
    {
        pthread_cond_wait(&cond, &mutex);
    }
    memcpy(clients, shmPtr, MAX_CLIENTS * sizeof(messageCount));
    printClients();
    ready = false;
    pthread_mutex_unlock(&mutex);
}

void performExit(void)
{
    int8_t msg = -1 * number;
    if(mq_send(mq, (char*)&msg, sizeof(int8_t), 0) == -1)
    {
        perror("unable to send the message");
        exit(-1);
    }
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    mq_close(mq);
    close(QUEUE_NAME);
    munmap(shmPtr, MAX_CLIENTS * sizeof(messageCount));
    exit(0);
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
        if(clients[i].c->number != -1)
        {
            printf("Client %d has received %d messages and sent %d messages\n", clients[i].c->number, clients[i].msgReceived, clients[i].msgSent);
        }
    }
}