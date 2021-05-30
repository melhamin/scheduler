#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>

#include "linked_list.h"

#define ALGO_NAME_LENGTH 10
#define INFILE_LENGTH 30
#define TH_CREATE_ERR "Failed to create the threads."
#define TH_JOIN_ERR "Failed to join the threads."
#define OPEN_FILE_ERR "Failed to open file."
#define KBLU "\x1B[34m"
#define KNRM "\x1B[0m"
#define KRED "\x1b[31m"
#define KGRN "\x1b[32m"

// CPU burst definition
typedef struct
{
    int burst_index;
    int thread_index;
    double length;
    struct timeval start_time;
    double vruntime;
} burst_t;

// Command line arguments definition
typedef struct
{
    size_t n;
    double bCount, minB, minA, avgA, avgB;
    char algo[ALGO_NAME_LENGTH], infile[INFILE_LENGTH];
} arg_t;

// Function definitions
void print_burst(burst_t *burst, bool print_vr);
arg_t parse_args(int argc, char *argv[]);
double rand_expo(double mean, double min);
void exit_on_error(char *msg);

//
void *scheduler(void *arg);
void *worker(void *arg);

node_t *fcfs_scheduler();
node_t *sjf_scheduler();
node_t *prio_scheduler();
node_t *vruntime_scheduler();

void remove_node(node_t *node);
double update_vr(double t, int thread_index);

// Global vars
LinkedList *run_queue;
arg_t args;

pthread_mutex_t runqueue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t new_burst_cond = PTHREAD_COND_INITIALIZER;

int main(int argc, char *argv[])
{
    args = parse_args(argc, argv);

    // Initialize the run queue
    run_queue = list_init(sizeof(burst_t));
    // Init threads array
    int thread_indices[args.n];

    // Thread ids
    pthread_t server_thread;
    pthread_t worker_threads[args.n];

    // Create the worker threads
    for (int i = 0; i < args.n; i++)
    {
        // Directly passing i as the argument would cause
        // incorrect values, since the address to each index
        // is passed and when dereferenced by different threads
        // will result to different values.
        thread_indices[i] = i + 1;
        if (pthread_create(&worker_threads[i], NULL, &worker, &thread_indices[i]) != 0)
            exit_on_error(TH_CREATE_ERR);
    }
    // The server thread
    if (pthread_create(&server_thread, NULL, &scheduler, NULL) != 0)
        exit_on_error(TH_CREATE_ERR);

    for (int i = 0; i < args.n; i++)
        if (pthread_join(worker_threads[i], NULL) != 0)
            exit_on_error(TH_JOIN_ERR);

    if (pthread_join(server_thread, NULL) != 0)
        exit_on_error(TH_JOIN_ERR);

    list_destroy(run_queue);

    pthread_mutex_destroy(&runqueue_lock);
    pthread_cond_destroy(&new_burst_cond);
    return 0;
}

// The thread routine chich creates cpu bursts and
//add them to the run queue.
void *worker(void *arg)
{
    int index = *(int *)arg;
    int burst_index = 1;
    // Keep track of virtual runtime
    double vruntime = 0;

    // If not reading bursts from a file
    if (args.infile[0] == '\0')
    {
        // Generate initial sleep time
        srand(time(NULL) + pthread_self());
        double inter_arrival_time = rand_expo(args.avgA, args.minA);
        printf(KNRM "Initial sleep time: %f -- thread: %d\n", inter_arrival_time, index);
        usleep(inter_arrival_time);

        // Initial wait for each thread
        while (burst_index <= args.bCount)
        {
            // Aquire lock and add new burst to the list
            burst_t burst;
            burst.burst_index = burst_index;
            burst.length = rand_expo(args.avgB, args.minB);
            burst.thread_index = index;
            burst.vruntime = vruntime;
            gettimeofday(&burst.start_time, NULL);

            // Update vruntime
            // Generate random interarrival time
            inter_arrival_time = rand_expo(args.avgA, args.minA);
            pthread_mutex_lock(&runqueue_lock);
            list_add(run_queue, &burst);
            printf(KGRN "New burst from thread %d\n", index);
            pthread_cond_signal(&new_burst_cond);
            pthread_mutex_unlock(&runqueue_lock);
            vruntime += update_vr(burst.length, index);
            burst_index++;

            usleep(inter_arrival_time);
        }
    }
    else
    {
        // construct the filename
        char filename[INFILE_LENGTH + 10];
        sprintf(filename, "%s-%d.txt", args.infile, index);
        double inter_arr_time;
        double length;

        char line[50];
        FILE *fp;
        if ((fp = fopen(filename, "r")) == NULL)
            exit_on_error(OPEN_FILE_ERR);

        while (fgets(line, sizeof(line), fp) != NULL)
        {
            // Parse the inter arrival time and burst length
            char *token;
            char *str = line;
            int i = 0;
            while ((token = strtok_r(str, " ", &str)))
            {
                if (i == 0)
                    sscanf(token, "%lf", &inter_arr_time);
                else
                    sscanf(token, "%lf", &length);
                i++;
            }
            // Sleep for inter arrival time then add the burst
            usleep(inter_arr_time);

            // Aquire lock and add new burst to the list
            burst_t burst;
            burst.burst_index = burst_index;
            burst.length = length;
            burst.thread_index = index;
            burst.vruntime = vruntime;
            gettimeofday(&burst.start_time, NULL);

            // Update vruntime
            vruntime += update_vr(burst.length, index);
            burst_index++;
            // Generate random interarrival time
            pthread_mutex_lock(&runqueue_lock);
            list_add(run_queue, &burst);
            // Wake up the scheduler thread.
            printf(KGRN "New burst from thread %d\n", index);
            pthread_cond_signal(&new_burst_cond);
            pthread_mutex_unlock(&runqueue_lock);
        }

        fclose(fp);
    }
    printf(KBLU "[+] Thread %d is exiting...\n", index);
    pthread_exit(NULL);
}

// The scheduler thread which selects bursts from the runqueue
// and remove them.
void *scheduler(void *arg)
{
    node_t *(*selected_scheduler)();

    // Check the selected algorithm and
    // and select the corresponding scheduler
    if (strcasecmp(args.algo, "fcfs") == 0)
        selected_scheduler = fcfs_scheduler;
    else if (strcasecmp(args.algo, "sjf") == 0)
        selected_scheduler = sjf_scheduler;
    else if (strcasecmp(args.algo, "prio") == 0)
        selected_scheduler = prio_scheduler;
    else if (strcasecmp(args.algo, "vruntime") == 0)
        selected_scheduler = vruntime_scheduler;
    else
    {
        printf(KRED "[-] Invalid scheduling algorithm: %s\n", args.algo);
        printf(KGRN "[+] Using default algorithm (FCFS)\n" KNRM);
        selected_scheduler = fcfs_scheduler;
    }

    //
    unsigned long avg_bursts_wait_time = 0;
    unsigned long num_bursts_processed = 0;
    unsigned long threads_avg_wait_time[args.n + 1];
    for (size_t i = 1; i <= args.n; i++)
        threads_avg_wait_time[i] = 0;

    while (1)
    {
        // Wait until a burst is available
        pthread_mutex_lock(&runqueue_lock);
        if (run_queue->head == NULL)
        {
            // printf(KBLU "---------------- %s Stats -------------------\n", args.algo);
            // printf(KNRM "All bursts avg. time: %f\n", (avg_bursts_wait_time / (args.n * args.bCount) / 1000000));
            // printf("-------------------------------------\n");
            // for (size_t i = 1; i <= args.n; i++)
            // {
            //     printf("Thread %ld avg. wait time: %f\n", i, (threads_avg_wait_time[i] / args.bCount) / 1000000);
            // }
            // // printf(KBLU "-----------------------------------------------\n" KNRM);
            printf(KNRM "List is empty. Waiting...\n");
            pthread_cond_wait(&new_burst_cond, &runqueue_lock);
        }
        pthread_mutex_unlock(&runqueue_lock);

        node_t *node_to_remove = NULL;
        burst_t *burst;
        struct timeval selection_time;
        gettimeofday(&selection_time, NULL);
        pthread_mutex_lock(&runqueue_lock);
        node_to_remove = selected_scheduler();
        pthread_mutex_unlock(&runqueue_lock);

        if (node_to_remove != NULL)
        {
            burst = node_to_remove->data;
            double length = burst->length;
            long long wait_time = (selection_time.tv_sec - burst->start_time.tv_sec) * 1000000 + (selection_time.tv_usec - burst->start_time.tv_usec);
            // Avg. Wait time for all bursts.
            avg_bursts_wait_time += wait_time;
            // Wait time for every burst of each thread
            threads_avg_wait_time[burst->thread_index] += wait_time;
            remove_node(node_to_remove);
            // num_bursts_processed += 1;
            usleep(length);
        }
    }
    pthread_exit(NULL);
}

// Selects a burst from the runqueue according to the FCFS algorithm.
node_t *fcfs_scheduler()
{
    return run_queue->head;
}

// Selects a burst from the runqueue according to the SJF algorithm.
node_t *sjf_scheduler()
{
    // Initialize an array to hold the earliest bursts of each thread.
    node_t *earliest_bursts[args.n + 1];
    for (size_t i = 0; i <= args.n; i++)
        earliest_bursts[i] = NULL;

    // Get the earliest burst of each thread.
    node_t *curr = run_queue->head;
    while (curr)
    {
        burst_t *burst = curr->data;
        if (earliest_bursts[burst->thread_index] == NULL)
            earliest_bursts[burst->thread_index] = curr;
        curr = curr->next;
    }

    // Get the burst with shortest length.
    node_t *curr_node = NULL,
           *result = NULL;
    double min_length = INFINITY;
    curr_node = run_queue->head;
    for (size_t i = 0; i < args.n + 1; i++)
    {
        curr_node = earliest_bursts[i];
        if (curr_node)
        {
            burst_t *burst = curr_node->data;
            if (burst->length < min_length)
            {
                min_length = burst->length;
                result = curr_node;
            }
        }
    }
    return result;
}

// Selects a burst from the runqueue according to the PRIO algorithm.
node_t *prio_scheduler()
{
    // Initialize an array to hold the earliest bursts of each thread.
    // args.n + 1 is used because the thread index starts from 1
    node_t *earliest_bursts[args.n + 1];
    for (size_t i = 0; i <= args.n; i++)
        earliest_bursts[i] = NULL;

    // Get the earliest burst of each thread.
    node_t *curr = run_queue->head;
    while (curr)
    {
        burst_t *burst = curr->data;
        if (earliest_bursts[burst->thread_index] == NULL)
            earliest_bursts[burst->thread_index] = curr;
        curr = curr->next;
    }

    // Get the burst with highest priority
    node_t *curr_node = NULL, *result = NULL;
    burst_t *curr_burst = NULL;
    double max_prio = INFINITY;
    // args.n + 1 is used because the thread index starts from 1
    for (size_t i = 0; i < args.n + 1; i++)
    {
        curr_node = earliest_bursts[i];
        if (curr_node)
        {
            burst_t *burst = curr_node->data;
            if (burst->thread_index < max_prio)
            {
                max_prio = burst->thread_index;
                result = curr_node;
            }
        }
    }
    return result;
}

// Selects a burst from the runqueue according to the virtual runtime algorithm.
node_t *vruntime_scheduler()
{
    // Initialize an array to hold the earliest bursts of each thread.
    node_t *earliest_bursts[args.n + 1];
    for (size_t i = 0; i <= args.n; i++)
        earliest_bursts[i] = NULL;

    // Get the earliest burst of each thread.
    node_t *curr = run_queue->head;
    while (curr)
    {
        burst_t *burst = curr->data;
        if (earliest_bursts[burst->thread_index] == NULL)
            earliest_bursts[burst->thread_index] = curr;
        curr = curr->next;
    }

    // Get the burst with lowest virtual runtime.
    node_t *curr_node = NULL,
           *result = NULL;
    double min_vruntime = INFINITY;
    curr_node = run_queue->head;
    for (size_t i = 0; i < args.n + 1; i++)
    {
        curr_node = earliest_bursts[i];
        if (curr_node)
        {
            burst_t *burst = curr_node->data;
            if (burst->vruntime < min_vruntime)
            {
                min_vruntime = burst->vruntime;
                result = curr_node;
            }
        }
    }
    return result;
}

// ############################################
//               Util functions               #
// ############################################

void remove_node(node_t *node)
{
    pthread_mutex_lock(&runqueue_lock);
    burst_t *burst = node->data;
    int bid = burst->burst_index;
    int tid = burst->thread_index;
    list_remove(run_queue, node);
    printf(KRED "Removed burst %d of thread %d\n", bid, tid);
    pthread_mutex_unlock(&runqueue_lock);
}

// void print_burst(burst_t *burst, bool print_vr)
// {
//     printf(KNRM "---------------------------\n");
//     printf(KNRM "burst_index: %d\n", burst->burst_index);
//     printf(KNRM "thread_index: %d\n", burst->thread_index);
//     printf(KNRM "start_time: %ld\n", burst->start_time.tv_usec);
//     printf(KNRM "length: %.2f\n", burst->length);
//     if (print_vr)
//         printf(KNRM "vruntime: %.2f\n", burst->vruntime);
//     printf(KNRM "---------------------------\n");
// }

// Parses the command line arguments and sets the required values.
arg_t parse_args(int argc, char *argv[])
{
    arg_t args;
    if (argc == 8)
    {
        args.n = atoi(argv[1]);
        args.bCount = atoi(argv[2]);
        args.minA = atoi(argv[3]);
        args.minB = atoi(argv[4]);
        args.avgA = atoi(argv[5]);
        args.avgB = atoi(argv[6]);
        strncpy(args.algo, argv[7], sizeof(argv[7]));
    }
    else if (argc == 5)
    {
        args.n = atoi(argv[1]);
        strncpy(args.algo, argv[2], sizeof(argv[2]));
        strncpy(args.infile, argv[4], sizeof(argv[4]));
    }
    else
    {
        fprintf(stderr, "Invalid arguments passed!\nUsage: schedule <N> <Bcount> <minB> <avgB> <minA> <avgA> <ALG>\nUsage: schedule <N> <ALG> -f <infile>\n");
        exit(EXIT_FAILURE);
    }
    return args;
}

// Generates an exponentially distributed random number
// bigger the given minimum value and with given mean
double rand_expo(double mean, double min)
{
    double uniform, result;
    do
    {
        uniform = rand() / (RAND_MAX + 1.0);
        double lambda = 1.0 / mean;
        result = -log(1 - uniform) / lambda;
    } while (result < min);
    return result;
}

double update_vr(double t, int thread_index)
{
    double new_vr = t * (0.7 + (0.3 * thread_index));
    return new_vr;
}

void exit_on_error(char *msg)
{
    printf(KRED "%s\n", msg);
    exit(EXIT_FAILURE);
}