#include "commons.h"

void termSignalHandler(int signal); // signal handler for SIGTERM
void sigusr1Handler(int signal); // signal handler for SIGUSR1
void fullMasterSignalHandler(int signal); // signal handler for SIGUSR2
void registerWithMaster(void); // register slave with master
void getNumber(void); // get number from user
void sendInfoMessage(void); // send message to master requesting info
void performExit(void); // function to perform exit actions
void printSlaves(void); // print slaves info

int8_t number = -1; // slave number/designator, assigned by user at startup, must be positive and >0, 0 not allowed
pid_t pid; // slave pid
pid_t masterPid; // master pid
mqd_t mq; // message queue
pthread_mutex_t mutex; // mutex for synchronisation
pthread_cond_t cond; // condition variable for synchronisation
bool ready = false; // flag for condition variable
sem_t *semaphore; // semaphore for synchronisation
struct sigaction sa; // struct for signal handling
int fd; // file descriptor for FIFO
int shm_fd; // shared memory file descriptor
void *shmPtr; // shared memory pointer
messageCount *slaves; // array of slaves for info received from master
bool masterFull = false; // flag for master full
int maxSlaves; // max number of slaves

bool shouldClose = false;
int8_t closeChance = 10; // 10% chance to close the slave

int main(int argc, char *argv[])
{
    number = atoi(argv[1]); // get number from argument
    maxSlaves = atoi(argv[2]); // get max number of slaves from argument

    srand(time(NULL) + number); // seed random number generator

#if DEBUG
    for(int i = 0; i < argc; i++)
    {
        printf("[Slave %d] Argument %d: %s\n", number, i, argv[i]); // print arguments
    }
#endif

    // Register exit function
    if (atexit(performExit) != 0)
    {
        fprintf(stderr, "[Slave %d] Cannot set exit function\n", number);
        return 1;
    }
    // Initialise mutex and condition variable
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    // Create semaphore
    semaphore = sem_open(SEM_NAME, 0);
    if (semaphore == SEM_FAILED)
    {
        fprintf(stderr, "[Slave %d] Unable to create the semaphore", number);
        exit(-1);
    }

    // Register signal SIGTERM
    signal(SIGTERM, termSignalHandler); // this signal is run only once so can use simpler signal() instead of sigaction()
    // Register signal SIGUSR1
    sa.sa_handler = sigusr1Handler;
    sigaction(SIGUSR1, &sa, NULL); // signal() would need to be reinitialised every time due to System V based implementation in Linux, sigaction doesn't
    // Register signal SIGUSR2
    signal(SIGUSR2, fullMasterSignalHandler); // this signal is run only once so can use simpler signal() instead of sigaction()


    pid = getpid(); // get slave pid
    ///getNumber(); // get number from user
    // CLEAR_BUFFER;

    /* open the shared memory segment */
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        fprintf(stderr, "[Slave %d] Unable to open the shared memory segment", number);
        exit(-1);
    }
    shmPtr = mmap(0, maxSlaves * sizeof(messageCount) + sizeof(pid_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shmPtr == MAP_FAILED)
    {
        fprintf(stderr, "[Slave %d] Unable to map the shared memory segment", number);
        exit(-1);
    }

    slaves = (messageCount*)malloc(maxSlaves * sizeof(messageCount)); // allocate memory for slaves array

    // Get master PID
    masterPid = *((pid_t*)((void*)shmPtr + maxSlaves * sizeof(messageCount)));
#if DEBUG
    printf("[Slave %d] Master PID: %d\n", number, masterPid);
#endif

    // Register with the master
    registerWithMaster();

    char command; // command from user
    // main loop of program

    int waitTime;
    while(1)
    {
        waitTime = rand() % 5 + 1; // random wait time between 1 and 5 seconds
        sleep(waitTime); // sleep for random time
        sendInfoMessage(); // send message to master to get info
        shouldClose = rand() % 100 < closeChance; // 10% chance to close the slave
        if(shouldClose)
        {
            printf("[Slave %d] Exiting\n", number);
            exit(0); // exit program, performing exit actions
        }
    }

    /*
    while(1)
    {
        printf("type \"m\" to send a message or \"q\" to quit\n");
        scanf("%c", &command); // get command from user
        if(command == 'm')
        {
#if DEBUG
            printf("Invoking sender\n");
#endif
            sendInfoMessage(); // send message to master to get info
        }
        else if(command == 'q')
        {
            exit(0); // exit program, performing exit actions
        }
        else
        {
            printf("Invalid command\n"); // try again
        }
        CLEAR_BUFFER;
    }
    */

    return 0;
}

// Function definitions

/*
void getNumber(void)
{
    // repeat until correct number entered
    while(number <= 0)
    {
        printf("Enter a positive number, min 1, max 127:\n");
        if(scanf("%d", (int*)&number) == 0) // checking if scanf was successful
        {
            printf("Error reading the number, try again\n");
            CLEAR_BUFFER;
            continue;
        }
        if(number <= 0) // checking if number is positive and >0
        {
            printf("The number must be positive and >0, try again\n");
            CLEAR_BUFFER;
        }
    }
#if DEBUG
    printf("The number is %d\n", number);
#endif
}
*/

void registerWithMaster(void) {
    slave s; // slave struct for sending to master
    s.pid = pid;
    s.number = number;

    mq = mq_open(QUEUE_NAME, O_WRONLY, 0666, NULL); // open message queue
    if (mq == (mqd_t)-1)
    {
        fprintf(stderr, "[Slave %d] Unable to open the message queue", number);
        exit(-1);
    }

    if (mq_send(mq, (char*)&s, sizeof(slave), 0) == -1) // send slave struct to master

    {
        fprintf(stderr, "[Slave %d] Unable to send the message", number);
        exit(-1);
    }

}

void termSignalHandler(int signal)
{
#if DEBUG
    printf("[Slave %d] Received signal %d\n", number, signal);
#endif
    exit(0);
}

void sendInfoMessage(void)
{
#if DEBUG
    printf("[Slave %d] Sending signal\n", number);
#endif
    kill(masterPid, SIGUSR1); // send signal to master to request info

    pthread_mutex_lock(&mutex); // lock mutex so that potential another signal will have to wait during this part before the data is obtained and printed
    while(!ready)
    {
        pthread_cond_wait(&cond, &mutex); // wait for master to signal that data is ready to be read
    }

    //make sure that the master is not writing to the shared memory at the same time for example for other slave
    sem_wait(semaphore); // wait for semaphore to be released
    if (shmPtr != NULL)
    {
        memcpy(slaves, shmPtr, maxSlaves * sizeof(messageCount)); // copy data from shared memory to local array
    }
    else
    {
        printf("[Slave %d] Shared memory pointer is NULL\n", number);
    }
    ready = false; // reset ready flag
    sem_post(semaphore); // release semaphore so master and other slaves can access shared memory again
    printSlaves(); // print received data
    pthread_mutex_unlock(&mutex); // unlock mutex
}

void performExit(void)
{
    if(!masterFull) // no need to unregister if master informed it is full
    {
        // slave informs master it's exiting
        fd = open(FIFO_PATH, O_WRONLY); // open FIFO for writing
        if (fd == -1) // check if open was successful
        {
            fprintf(stderr, "[Slave %d] Unable to open the FIFO, check if master running", number);
            masterFull = true; // to make sure we won't get infinite loop here
            exit(-1);
        }
        write(fd, &number, sizeof(int8_t)); // write slave number to FIFO
        close(fd); // close FIFO
    }
    pthread_mutex_destroy(&mutex); // destroy mutex
    pthread_cond_destroy(&cond); // destroy condition variable
    mq_close(mq); // close message queue
    munmap(shmPtr, maxSlaves * sizeof(messageCount)); // unmap shared memory
#if DEBUG
    printf("[Slave %d] Exiting\n", number);
#endif
}

void sigusr1Handler(int signal)
{
    printf("[Slave %d] Received signal %d\n", number, signal);
    pthread_mutex_lock(&mutex); // lock mutex, so we won't interrupt the process of copying data from shared memory if already working
    ready = true; // set ready flag
    pthread_cond_signal(&cond); // signal that data is ready to release waiting thread
    pthread_mutex_unlock(&mutex); // unlock mutex
}

void printSlaves(void) // too obvious to comment :)
{
    int i;
    for(i = 0; i < maxSlaves; i++)
    {
        if(slaves[i].s.number != 0)
        {
            printf("[Slave %d] Slave %d: master received %d messages and sent %d messages\n", number, slaves[i].s.number, slaves[i].msgReceived, slaves[i].msgSent);
        }
    }
}

void fullMasterSignalHandler(int signal)
{
    printf("[Slave %d] Master is full, exiting\n", number);
    masterFull = true; // set flag that master is full
    exit(1);
}
