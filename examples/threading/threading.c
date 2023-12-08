#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>


// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    useconds_t obtain_time = (useconds_t) thread_func_args->obtain_ms;     // warning: this is casting signed into unsigned
    useconds_t release_time = (useconds_t) thread_func_args->release_ms;   // warning: this is casting signed into unsigned
    if(usleep(100)) {
        perror("threacfunc:usleep");
    }
    
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *td;
    td = (struct thread_data *) malloc(sizeof(struct thread_data));

    if (td == NULL) {
        perror("start_thread_obtaining_mutex: malloc");
        return false;
    }

    // TODO: setup mutex 
    
    // DONE: setup wait arguments
    td->obtain_ms = wait_to_obtain_ms;
    td->release_ms = wait_to_release_ms;

    // TODO pass thread_data to created thread
    
}

