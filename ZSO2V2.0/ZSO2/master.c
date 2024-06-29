#include "commons.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

void* registerThreadFunc(void* arg); // thread function for registering slaves
void* fifoThreadFunc(void* arg); // thread function for handling FIFO inputs
void* sendDataThreadFunc(void* arg); // thread function for sending data to slaves
void unregisterSlave(int8_t number); // unregister slave with given number
void exitFunc(void); // function to perform exit actions
void sendData(pid_t requester); // send data to slave with given pid
void sigusr1Handler(int signo, siginfo_t *si, void *data); // signal handler for SIGUSR1
void sigTermHandler(int signo, siginfo_t *si, void *data); // signal handler for SIGTERM
int8_t getNumFromPid(pid_t pid); // get slave number from pid
int getIndexFromNumber(int8_t number); // get index of slave with given number
int getIndexFromPid(pid_t pid); // get index of slave with given pid
void runSlaves(void); // run slaves

messageCount *slaves; // array of slaves data
int8_t slavesCount; // number of registered slaves
int8_t maxSlaves;
pthread_t registerThread; // thread for registering slaves
pthread_t fifoThread; // thread for handling FIFO inputs
pthread_t senderThread; // thread for sending data to slaves
sem_t *semaphore; // semaphore for synchronisation
pthread_mutex_t mutex; // mutex for synchronisation
pthread_cond_t cond; // condition variable for synchronisation
pid_t requesterPid; // pid of slave that requested data
struct sigaction sa; // struct for signal handling containing sigusr1Handler and settings
struct sigaction saTerm; // struct for signal handling containing sigusr2Handler and settings
bool signaled = false; // flag for condition variable
int shm_fd; // shared memory file descriptor
void *shmPtr; // shared memory pointer

bool shouldClose = false;
bool shouldKill = false;
int8_t closeChance = 2; // 10% chance to close master
int8_t killChance = 10; // 10% chance to kill the slave

int main(int argc, char *argv[])
{
    if(argc != 2) // check if there are any arguments
    {
        fprintf(stderr, "[Master] Incorrect arguments number\n");
        return 1;
    }
    maxSlaves = atoi(argv[1]); // get number of slaves from argument
    slaves = malloc(maxSlaves * sizeof(messageCount)); // allocate memory for slaves array

#if DEBUG
    for(int i = 0; i < argc; i++)
    {
        printf("[Master] Argument %d: %s\n", i, argv[i]); // print arguments
    }
#endif

    if (atexit(exitFunc) != 0) // Register exit function
    {
        perror("[Master] Cannot set exit function\n");
        return 1;
    }

    sa.sa_flags = SA_SIGINFO; // set flag for sigaction to use sa_sigaction instead of sa_handler and obtain more info about signal
    sa.sa_sigaction = sigusr1Handler; // set signal handler to sigusr1Handler

    saTerm.sa_flags = SA_SIGINFO; // set flag for sigaction to use sa_sigaction instead of sa_handler and obtain more info about signal
    saTerm.sa_sigaction = sigTermHandler; // set signal handler to sigTermHandler

    sigaction(SIGUSR1, &sa, NULL); // register signal SIGUSR1
    sigaction(SIGTERM, &saTerm, NULL); // register signal SIGTERM

    pthread_mutex_init(&mutex, NULL); // Initialise mutex
    pthread_cond_init(&cond, NULL); // Initialise condition variable

    int createThreadResult; // variable to store result of creating thread
    createThreadResult = pthread_create(&registerThread, NULL, registerThreadFunc, NULL); // create thread for registering slaves
    if (createThreadResult != 0) {
        fprintf(stderr, "[Master] Failed to create thread: %d\n", createThreadResult); // fprintf instead of perror to be able to print custom message
        exit(EXIT_FAILURE);
    }
    createThreadResult = pthread_create(&fifoThread, NULL, fifoThreadFunc, NULL); // create thread for handling FIFO inputs
    if (createThreadResult != 0) {
        fprintf(stderr, "[Master] Failed to create thread: %d\n", createThreadResult); // fprintf instead of perror to be able to print custom message
        exit(EXIT_FAILURE);
    }
    createThreadResult = pthread_create(&senderThread, NULL, sendDataThreadFunc, NULL); // create thread for sending data to slaves
    if (createThreadResult != 0) {
        fprintf(stderr, "[Master] Failed to create thread: %d\n", createThreadResult); // fprintf instead of perror to be able to print custom message
        exit(EXIT_FAILURE);
    }

    int index; // variable to store index of slave in slaves array
    bool found; // variable to store if slave was found

    usleep(100000); // To make sure threads started

    runSlaves(); // run slaves

    usleep(100000); // To make sure slaves start

    srand(time(NULL) - 1); // seed random number generator

    //Main loop
    while(1)
    {
        sleep(rand() % 5 + 1); // sleep for random time between 1 and 5 seconds
        shouldClose = rand() % 100 < closeChance; // set shouldClose to true with 10% chance
        if(shouldClose)
        {
            printf("[Master] Closing the master\n");
            exit(0); // exit if shouldClose is true
        }
        shouldKill = rand() % 100 < killChance; // set shouldKill to true with 10% chance
        if(shouldKill)
        {
            index = rand() % slavesCount; // get random slave index
            printf("[Master] Killing the slave %d\n", slaves[index].s.number);
            
            pthread_mutex_lock(&mutex); // lock mutex so only one signal can be processed at a time
            kill(slaves[index].s.pid, SIGTERM); // send SIGTERM to slave
            pthread_mutex_unlock(&mutex); // unlock mutex so next signal can be processed

            shouldKill = false; // reset shouldKill
        }
    }

/*
    // Main loop
    while(1)
    {
        slaveNumber = 0; // reset slave number
        printf("[Master] Enter the slave number or \"-2\" to exit:\n");
        scanf("%d", (int*)&slaveNumber); // get slave number from user
        if (slaveNumber == -2) exit(0); // exit if -2 entered
        if (slaveNumber == 0) continue; // 0 is not allowed as number, for some reason signal handler causes scanf to run forward, so to avoid empty input
        index = getIndexFromNumber(slaveNumber); // get index of slave with given number
        if (index == -1) // if slave was not found
        {
            printf("[Master] Slave %d not found\n", slaveNumber);
            continue;
        }
        printf("[Master] Closing the slave %d\n", slaveNumber);
        kill(slaves[index].c.pid, SIGTERM); // send SIGTERM to slave
    }
*/

    return 0;
}

void* registerThreadFunc(void* arg)
{
#if DEBUG
    printf("[Master] Register thread started\n");
#endif
    struct mq_attr attr; // message queue attributes
    attr.mq_flags = 0; // flags
    attr.mq_maxmsg = 10; // max number of messages
    attr.mq_msgsize = sizeof(slave); // message size
    attr.mq_curmsgs = 0; // current number of messages

    mqd_t mq = mq_open(QUEUE_NAME, O_CREAT | O_RDONLY, 0666, &attr); // open message queue with flags to create if not exists and read only
    slave *tmp; // temporary slave struct for receiving data
    tmp = malloc(sizeof(slave)); // allocate memory for temporary slave struct

    if (mq == (mqd_t)-1) // check if message queue was created
    {
        perror("[Master] unable to create the message queue");
        exit(-1);
    }

    // main loop of thread
    while (1)
    {
#if DEBUG
        printf("[Master] Waiting for the slaves to register, currently %d registered\n", slavesCount);
#endif
        if (mq_receive(mq, (char *)tmp, sizeof(slave), NULL) == -1) // receive message from message queue
        {
            perror("[Master] unable to receive the message");
            exit(-1);
        }
        if (slavesCount < maxSlaves) // if there is still space for new slave
        {
            pthread_mutex_lock(&mutex); // lock mutex so only one signal can be processed at a time
            slaves[slavesCount].s.number = tmp->number; // copy slave number to slaves array
            slaves[slavesCount].s.pid = tmp->pid; // copy slave pid to slaves array
            slaves[slavesCount].msgReceived = 1; // set number of messages received to 1
            slaves[slavesCount].msgSent = 0; // set number of messages sent to 0
            slavesCount++; // increment number of registered slaves
            pthread_mutex_unlock(&mutex); // unlock mutex so next signal can be processed
            printf("[Master] Slave registered with number %d, pid: %d\n", tmp->number, tmp->pid);
        }
        else // if master is full
        {
            printf("[Master] The master is full\n");
            kill(tmp->pid, SIGUSR2); // Send SIGUSR2 to the slave that cannot register
        }
    }
    free(tmp); // deallocate memory for temporary slave struct
}

void unregisterSlave(int8_t number)
{
    pthread_mutex_lock(&mutex); // lock mutex so only one signal can be processed at a time
    int j, index = getIndexFromNumber(number); // get index of slave with given number
    if (index == -1) // if slave was not found
    {
        printf("[Master] Slave %d not found\n", number);
        return;
    }

    slavesCount--; // decrement number of registered slaves
    for (j = index; j < slavesCount; j++) // move slaves in array to fill the gap
    {
        slaves[j].s.number = slaves[j + 1].s.number;
        slaves[j].s.pid = slaves[j + 1].s.pid;
        slaves[j].msgReceived = slaves[j + 1].msgReceived;
        slaves[j].msgSent = slaves[j + 1].msgSent;
    }
    // Clear the last slave as it's already copied to previous one
    slaves[slavesCount].s.number = 0;
    slaves[slavesCount].s.pid = 0;
    slaves[slavesCount].msgReceived = 0;
    slaves[slavesCount].msgSent = 0;
    pthread_mutex_unlock(&mutex); // unlock mutex so next signal can be processed
}

void* fifoThreadFunc(void* arg)
{
#if DEBUG
    printf("[Master] Fifo thread started\n");
#endif

    unlink(FIFO_PATH); //remove fifo file if already exists for any reason (i.e. previous run of the program closed unexpectedly without removing it)

    semaphore = sem_open(SEM_NAME, O_CREAT, 0666, 1); // create semaphore
    if (semaphore == SEM_FAILED) // check if semaphore was created
    {
        perror("[Master] Unable to create the semaphore");
        exit(-1);
    }

    // create the shared memory segment
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("[Master] Unable to create the shared memory segment");
        exit(-1);
    }
    ftruncate(shm_fd, maxSlaves * sizeof(messageCount) + sizeof(pid_t)); // set size of shared memory segment
    shmPtr = mmap(0, maxSlaves * sizeof(messageCount) + sizeof(pid_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); // map shared memory segment
    if (shmPtr == MAP_FAILED)
    {
        perror("[Master] Unable to map the shared memory segment");
        exit(-1);
    }

    // Write master PID at the end of shared memory segment
    pid_t *pid = (pid_t*)((void*)shmPtr + maxSlaves * sizeof(messageCount));
    *pid = getpid();

    // Create the FIFO if it doesn't exist
    if (mkfifo(FIFO_PATH, 0666) == -1)
    {
        perror("[Master] Unable to create the FIFO");
        exit(-1);
    }

    // Open the FIFO
    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd == -1) {
        perror("[Master] Unable to open the FIFO");
        exit(-1);
    }

    int8_t number; // variable to store slave number

    // main loop of thread
    while(1)
    {
        if (read(fd, &number, sizeof(int8_t)) == -1)
        {
            perror("[Master] Unable to read from the FIFO");
            exit(-1);
        }

        printf("[Master] Unregistering slave %d\n", number);
        unregisterSlave(number); // unregister slave with given number

        // Reinitialize the FIFO, otherwise we would read the same data indefinitely in a loop
        close(fd); // close the FIFO
        unlink(FIFO_PATH); // remove the FIFO
        if (mkfifo(FIFO_PATH, 0666) == -1) // recreate the FIFO
        {
            perror("[Master] Unable to recreate the FIFO");
            exit(-1);
        }
        fd = open(FIFO_PATH, O_RDONLY); // open the FIFO to read again from another slave
        if (fd == -1) {
            perror("[Master] Unable to open the FIFO");
            exit(-1);
        }
    }
}

void exitFunc(void)
{
    for(int i = 0; i < slavesCount; i++) // send SIGTERM to all slaves
    {
        kill(slaves[i].s.pid, SIGTERM); // send SIGTERM to slave
    }
    mq_unlink(QUEUE_NAME); // remove message queue
    unlink(FIFO_PATH); // remove FIFO
    shm_unlink(SHM_NAME); // remove shared memory
    sem_unlink(SEM_NAME); // remove semaphore
    free(slaves); // deallocate memory for slaves array
}


void sendData(pid_t requester)
{
    sem_wait(semaphore); // wait for semaphore to make sure any slave is not reading so we won't corrupt data during that time
    memcpy(shmPtr, slaves, maxSlaves * sizeof(messageCount)); // copy data to shared memory
    kill(requester, SIGUSR1); // send signal to requester to inform data is ready to be read
    // Releasing after sending the signal to minimise chance of race conditions, as requestor slave should be already waiting for this semaphore
    // so no other will have chance to initiate this process before it will finish copying
    sem_post(semaphore);
}

void* sendDataThreadFunc(void* arg)
{
#if DEBUG
    printf("[Master] Sender thread started\n");
#endif

    // main loop of thread
    while (1)
    {
        pthread_mutex_lock(&mutex); // lock mutex so only one signal can be processed at a time
        while (!signaled)
        {
            pthread_cond_wait(&cond, &mutex); // wait for signal and condition variable so signal handler can finish before we process the data
        }

        signaled = false; // reset signaled flag for next read
        sendData(requesterPid); // send data to slave with given pid
        slaves[getIndexFromPid(requesterPid)].msgSent++; // increment number of messages sent
        pthread_mutex_unlock(&mutex); // unlock mutex so next signal can be processed
    }
}

void sigusr1Handler(int signo, siginfo_t *si, void *data)
{
    int index; // variable to store index of slave in slaves array
    pthread_mutex_lock(&mutex); // lock mutex so only one signal can be processed at a time
    signaled = true; // set signaled flag
    requesterPid = si->si_pid; // get pid of slave that requested data
    index = getIndexFromPid(requesterPid); // get index of slave with given pid
#if DEBUG
    printf("[Master] Data requested by %d\n", slaves[index].s.number);
#endif
    slaves[index].msgReceived++; // increment number of messages received
    pthread_cond_signal(&cond); // signal condition variable to release waiting thread
    pthread_mutex_unlock(&mutex); // unlock mutex so next signal can be processed
}

int8_t getNumFromPid(pid_t pid)
{
    for (int i = 0; i < slavesCount; i++) // search for slave number with given pid
    {
        if (slaves[i].s.pid == pid)
        {
            return slaves[i].s.number;
        }
    }
    return -1;
}

int getIndexFromNumber(int8_t number)
{
    for (int i = 0; i < slavesCount; i++) // search for index of slave with given number
    {
        if (slaves[i].s.number == number)
        {
            return i;
        }
    }
    return -1;
}

int getIndexFromPid(pid_t pid)
{
    for (int i = 0; i < slavesCount; i++) // search for index of slave with given pid
    {
        if (slaves[i].s.pid == pid)
        {
            return i;
        }
    }
    return -1;
}

void runSlaves(void)
{
    pid_t id; // variable to store slave pid
    char *idStr; // variable to store slave human-friendly number
    char *slavesCountStr; // variable to store maxSlaves

    slavesCountStr = malloc(4); // allocate memory for slavesCountStr, 3 characters + null terminator
    sprintf(slavesCountStr, "%d", maxSlaves); // convert maxSlaves to string

    idStr = malloc(4); // allocate memory for idStr, 3 characters + null terminator
    for (int i = 0; i < maxSlaves; i++)
    {
        sprintf(idStr, "%d", i + 1); // convert i to string
        if (id = fork() == 0)
        {
            printf("[Master] Starting slave %s\n", idStr);
            execl("./slave", "./slave", idStr, slavesCountStr, NULL); // run slave instead of master
            if (id == -1) // check if fork was successful
            {
                perror("Cannot fork\n");
                return;
            }
        }
    }
    free(idStr); // deallocate memory for idStr
    free(slavesCountStr); // deallocate memory for slavesCountStr
}

void sigTermHandler(int signo, siginfo_t *si, void *data)
{
    printf("[Master] Received SIGTERM from: %d, error: %d, syscall: %d \n", si->si_pid, si->si_errno, si->si_syscall);
    exit(0);
}