#include "commons.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

void* registerThreadFunc(void* arg); // thread function for registering clients
void* fifoThreadFunc(void* arg); // thread function for handling FIFO inputs
void* sendDataThreadFunc(void* arg); // thread function for sending data to clients
void unregisterClient(int8_t number); // unregister client with given number
void exitFunc(void); // function to perform exit actions
void sendData(pid_t requester); // send data to client with given pid
void sigusr1Handler(int signo, siginfo_t *si, void *data); // signal handler for SIGUSR1
int8_t getNumFromPid(pid_t pid); // get client number from pid
int getIndexFromNumber(int8_t number); // get index of client with given number
int getIndexFromPid(pid_t pid); // get index of client with given pid
void runClients(void); // run clients

messageCount *clients; // array of clients data
int8_t clientsCount; // number of registered clients
int8_t maxClients;
pthread_t registerThread; // thread for registering clients
pthread_t fifoThread; // thread for handling FIFO inputs
pthread_t senderThread; // thread for sending data to clients
sem_t *semaphore; // semaphore for synchronisation
pthread_mutex_t mutex; // mutex for synchronisation
pthread_cond_t cond; // condition variable for synchronisation
pid_t requesterPid; // pid of client that requested data
struct sigaction sa; // struct for signal handling containing sigusr1Handler and settings
bool signaled = false; // flag for condition variable
int shm_fd; // shared memory file descriptor
void *shmPtr; // shared memory pointer

bool shouldClose = false;
bool shouldKill = false;
int8_t closeChance = 2; // 10% chance to close server
int8_t killChance = 10; // 10% chance to kill the client

int main(int argc, char *argv[]) 
{   
    if(argc != 2) // check if there are any arguments
    {
        fprintf(stderr, "[Server] Incorrect arguments number\n");
        return 1;
    }
    maxClients = atoi(argv[1]); // get number of clients from argument
    clients = malloc(maxClients * sizeof(messageCount)); // allocate memory for clients array

#if DEBUG
    for(int i = 0; i < argc; i++)
    {
        printf("[Server] Argument %d: %s\n", i, argv[i]); // print arguments
    }
#endif

    if (atexit(exitFunc) != 0) // Register exit function
    { 
        perror("[Server] Cannot set exit function\n");
        return 1;
    }
    
    sa.sa_flags = SA_SIGINFO; // set flag for sigaction to use sa_sigaction instead of sa_handler and obtain more info about signal
    sa.sa_sigaction = sigusr1Handler; // set signal handler to sigusr1Handler

    sigaction(SIGUSR1, &sa, NULL); // register signal SIGUSR1

    pthread_mutex_init(&mutex, NULL); // Initialise mutex
    pthread_cond_init(&cond, NULL); // Initialise condition variable

    int createThreadResult; // variable to store result of creating thread
    createThreadResult = pthread_create(&registerThread, NULL, registerThreadFunc, NULL); // create thread for registering clients
    if (createThreadResult != 0) {
        fprintf(stderr, "[Server] Failed to create thread: %d\n", createThreadResult); // fprintf instead of perror to be able to print custom message
        exit(EXIT_FAILURE);
    }
    createThreadResult = pthread_create(&fifoThread, NULL, fifoThreadFunc, NULL); // create thread for handling FIFO inputs
    if (createThreadResult != 0) {
        fprintf(stderr, "[Server] Failed to create thread: %d\n", createThreadResult); // fprintf instead of perror to be able to print custom message
        exit(EXIT_FAILURE);
    }
    createThreadResult = pthread_create(&senderThread, NULL, sendDataThreadFunc, NULL); // create thread for sending data to clients
    if (createThreadResult != 0) {
        fprintf(stderr, "[Server] Failed to create thread: %d\n", createThreadResult); // fprintf instead of perror to be able to print custom message
        exit(EXIT_FAILURE);
    }

    int8_t clientNumber; // variable to store client number
    int index; // variable to store index of client in clients array
    bool found; // variable to store if client was found

    usleep(100000); // To make sure threads started

    runClients(); // run clients

    usleep(100000); // To make sure clients start
    
    srand(time(NULL) - 1); // seed random number generator

    //Main loop
    while(1)
    {
        sleep(rand() % 5 + 1); // sleep for random time between 1 and 5 seconds
        shouldClose = rand() % 100 < closeChance; // set shouldClose to true with 10% chance
        if(shouldClose)
        {
            exit(0); // exit if shouldClose is true
        }
        shouldKill = rand() % 100 < killChance; // set shouldKill to true with 10% chance
        if(shouldKill)
        {
            clientNumber = rand() % maxClients; // get random client number
            index = getIndexFromNumber(clientNumber); // get index of client with given number
            if (index == -1) // if client was not found
            {
                printf("[Server] Client %d not found\n", clientNumber);
                continue;
            }
            printf("[Server] Killing the client %d\n", clientNumber);
            kill(clients[index].c.pid, SIGTERM); // send SIGTERM to client
            shouldKill = false; // reset shouldKill
        }
    }

/*
    // Main loop
    while(1)
    {
        clientNumber = 0; // reset client number
        printf("[Server] Enter the client number or \"-2\" to exit:\n");
        scanf("%d", (int*)&clientNumber); // get client number from user
        if (clientNumber == -2) exit(0); // exit if -2 entered
        if (clientNumber == 0) continue; // 0 is not allowed as number, for some reason signal handler causes scanf to run forward, so to avoid empty input
        index = getIndexFromNumber(clientNumber); // get index of client with given number
        if (index == -1) // if client was not found
        {
            printf("[Server] Client %d not found\n", clientNumber);
            continue;
        }
        printf("[Server] Closing the client %d\n", clientNumber);
        kill(clients[index].c.pid, SIGTERM); // send SIGTERM to client
    }
*/

    return 0;
}

void* registerThreadFunc(void* arg) 
{
#if DEBUG
    printf("[Server] Register thread started\n");
#endif
    struct mq_attr attr; // message queue attributes
    attr.mq_flags = 0; // flags
    attr.mq_maxmsg = 10; // max number of messages
    attr.mq_msgsize = sizeof(client); // message size
    attr.mq_curmsgs = 0; // current number of messages

    mqd_t mq = mq_open(QUEUE_NAME, O_CREAT | O_RDONLY, 0666, &attr); // open message queue with flags to create if not exists and read only
    client *tmp; // temporary client struct for receiving data
    tmp = malloc(sizeof(client)); // allocate memory for temporary client struct

    if (mq == (mqd_t)-1) // check if message queue was created
    {
        perror("[Server] unable to create the message queue");
        exit(-1);
    }

    // main loop of thread
    while (1)
    {
#if DEBUG
        printf("[Server] Waiting for the clients to register, currently %d registered\n", clientsCount);
#endif
        if (mq_receive(mq, (char *)tmp, sizeof(client), NULL) == -1) // receive message from message queue
        {
            perror("[Server] unable to receive the message");
            exit(-1);
        }
        if (clientsCount < maxClients) // if there is still space for new client
        {
            clients[clientsCount].c.number = tmp->number; // copy client number to clients array
            clients[clientsCount].c.pid = tmp->pid; // copy client pid to clients array
            clients[clientsCount].msgReceived = 1; // set number of messages received to 1
            clients[clientsCount].msgSent = 0; // set number of messages sent to 0
            clientsCount++; // increment number of registered clients
            printf("[Server] Client registered with number %d, pid: %d\n", tmp->number, tmp->pid);
        }
        else // if server is full
        {
            printf("[Server] The server is full\n");
            kill(tmp->pid, SIGUSR2); // Send SIGUSR2 to the client that cannot register
        }
    }
    free(tmp); // deallocate memory for temporary client struct
}

void unregisterClient(int8_t number)
{
    int j, index = getIndexFromNumber(number); // get index of client with given number
    if (index == -1) // if client was not found
    {
        printf("[Server] Client %d not found\n", number);
        return;
    }

    clientsCount--; // decrement number of registered clients
    for (j = index; j < clientsCount; j++) // move clients in array to fill the gap
    {
        clients[j].c.number = clients[j + 1].c.number;
        clients[j].c.pid = clients[j + 1].c.pid;
        clients[j].msgReceived = clients[j + 1].msgReceived;
        clients[j].msgSent = clients[j + 1].msgSent;
    }
    // Clear the last client as it's already copied to previous one
    clients[clientsCount].c.number = 0; 
    clients[clientsCount].c.pid = 0;
    clients[clientsCount].msgReceived = 0;
    clients[clientsCount].msgSent = 0;
}

void* fifoThreadFunc(void* arg)
{
#if DEBUG
    printf("[Server] Fifo thread started\n");
#endif

    unlink(FIFO_PATH); //remove fifo file if already exists for any reason (i.e. previous run of the program closed unexpectedly without removing it)

    semaphore = sem_open(SEM_NAME, O_CREAT, 0666, 1); // create semaphore
    if (semaphore == SEM_FAILED) // check if semaphore was created
    {
        perror("[Server] Unable to create the semaphore");
        exit(-1);
    }

    // create the shared memory segment
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) 
    {
        perror("[Server] Unable to create the shared memory segment");
        exit(-1);
    }
    ftruncate(shm_fd, maxClients * sizeof(messageCount) + sizeof(pid_t)); // set size of shared memory segment
    shmPtr = mmap(0, maxClients * sizeof(messageCount) + sizeof(pid_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); // map shared memory segment
    if (shmPtr == MAP_FAILED) 
    {
        perror("[Server] Unable to map the shared memory segment");
        exit(-1);
    }

    // Write server PID at the end of shared memory segment
    pid_t *pid = (pid_t*)((void*)shmPtr + maxClients * sizeof(messageCount));
    *pid = getpid();

    // Create the FIFO if it doesn't exist
    if (mkfifo(FIFO_PATH, 0666) == -1) 
    {
        perror("[Server] Unable to create the FIFO");
        exit(-1);
    }

    // Open the FIFO
    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd == -1) {
        perror("[Server] Unable to open the FIFO");
        exit(-1);
    }

    int8_t number; // variable to store client number

    // main loop of thread
    while(1)
    {
        if (read(fd, &number, sizeof(int8_t)) == -1) 
        {
            perror("[Server] Unable to read from the FIFO");
            exit(-1);
        }

        printf("[Server] Unregistering client %d\n", number);
        unregisterClient(number); // unregister client with given number

        // Reinitialize the FIFO, otherwise we would read the same data indefinitely in a loop
        close(fd); // close the FIFO
        unlink(FIFO_PATH); // remove the FIFO
        if (mkfifo(FIFO_PATH, 0666) == -1) // recreate the FIFO
        {
            perror("[Server] Unable to recreate the FIFO");
            exit(-1);
        }
        fd = open(FIFO_PATH, O_RDONLY); // open the FIFO to read again from another client
        if (fd == -1) {
            perror("[Server] Unable to open the FIFO");
            exit(-1);
        }
    }
}

void exitFunc(void)
{
    for(int i = 0; i < clientsCount; i++) // send SIGTERM to all clients
    {
        kill(clients[i].c.pid, SIGTERM); // send SIGTERM to client
    }
    mq_unlink(QUEUE_NAME); // remove message queue
    unlink(FIFO_PATH); // remove FIFO
    shm_unlink(SHM_NAME); // remove shared memory
    sem_unlink(SEM_NAME); // remove semaphore
}


void sendData(pid_t requester)
{
    sem_wait(semaphore); // wait for semaphore to make sure any client is not reading so we won't corrupt data during that time
    memcpy(shmPtr, clients, maxClients * sizeof(messageCount)); // copy data to shared memory

    kill(requester, SIGUSR1); // send signal to requester to inform data is ready to be read

    // Releasing after sending the signal to minimise chance of race conditions, as requestor client should be already waiting for this semaphore
    // so no other will have chance to initiate this process before it will finish copying
    sem_post(semaphore); 
}

void* sendDataThreadFunc(void* arg)
{
#if DEBUG
    printf("[Server] Sender thread started\n");
#endif

    // main loop of thread
    while (1)
    {
        pthread_mutex_lock(&mutex); // lock mutex so only one signal can be processed at a time
        while (!signaled)
        {
            printf("waiting");
            pthread_cond_wait(&cond, &mutex); // wait for signal and condition variable so signal handler can finish before we process the data
            printf("checking %d", signaled);
        }
        
        printf("dupa");
        signaled = false; // reset signaled flag for next read
        sendData(requesterPid); // send data to client with given pid
        clients[getIndexFromPid(requesterPid)].msgSent++; // increment number of messages sent
        pthread_mutex_unlock(&mutex); // unlock mutex so next signal can be processed
    }
}

void sigusr1Handler(int signo, siginfo_t *si, void *data)
{
    int index; // variable to store index of client in clients array
    pthread_mutex_lock(&mutex); // lock mutex so only one signal can be processed at a time
    signaled = true; // set signaled flag
    requesterPid = si->si_pid; // get pid of client that requested data
    index = getIndexFromPid(requesterPid); // get index of client with given pid
#if DEBUG
    //printf("[Server] Data requested by %d\n", clients[index].c.number);
#endif
        //printf("dupa0");
    clients[index].msgReceived++; // increment number of messages received
        //printf("dupa1");
    pthread_cond_signal(&cond); // signal condition variable to release waiting thread
        //printf("dupa2");
    pthread_mutex_unlock(&mutex); // unlock mutex so next signal can be processed
        //printf("dupa3");
}

int8_t getNumFromPid(pid_t pid)
{
    for (int i = 0; i < clientsCount; i++) // search for client number with given pid
    {
        if (clients[i].c.pid == pid)
        {
            return clients[i].c.number;
        }
    }
    return -1;
}

int getIndexFromNumber(int8_t number)
{
    for (int i = 0; i < clientsCount; i++) // search for index of client with given number
    {
        if (clients[i].c.number == number)
        {
            return i;
        }
    }
    return -1;
}

int getIndexFromPid(pid_t pid)
{
    for (int i = 0; i < clientsCount; i++) // search for index of client with given pid
    {
        if (clients[i].c.pid == pid)
        {
            return i;
        }
    }
    return -1;
}

void runClients(void)
{
    pid_t id; // variable to store client pid
    char *idStr; // variable to store client human-friendly number
    char *clientsCountStr; // variable to store maxClients

    clientsCountStr = malloc(4); // allocate memory for clientsCountStr, 3 characters + null terminator
    sprintf(clientsCountStr, "%d", maxClients); // convert maxClients to string

    idStr = malloc(4); // allocate memory for idStr, 3 characters + null terminator
    for (int i = 0; i < maxClients; i++)
    {
        sprintf(idStr, "%d", i + 1); // convert i to string
        if (id = fork() == 0)
        {
            printf("[Server] Starting client %s\n", idStr);
            execl("./client", "./client", idStr, clientsCountStr, NULL); // run client instead of server
            if (id == -1) // check if fork was successful
            {
                perror("Cannot fork\n");
                return;
            }
        }
    }
    free(idStr); // deallocate memory for idStr
    free(clientsCountStr); // deallocate memory for clientsCountStr
}