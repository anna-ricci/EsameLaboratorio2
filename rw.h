
#define _GNU_SOURCE   // permette di usare estensioni GNU
#include <stdio.h>    // permette di usare scanf printf etc ...
#include <stdlib.h>   // conversioni stringa exit() etc ...
#include <stdbool.h>  // gestisce tipo bool
#include <assert.h>   // permette di usare la funzione ass
#include <string.h>   // funzioni per stringhe
#include <errno.h>    // richiesto per usare errno
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <pthread.h>
#include "xerrori.h"
#define QUI __LINE__,__FILE__

typedef struct {
  int readers;
  bool writing;
  pthread_cond_t cond;   // condition variable
  pthread_mutex_t mutex; // mutex associato alla condition variable
} rw;


void rw_init(rw *z);
void read_lock(rw *z);
void read_unlock(rw *z);
void write_lock(rw *z);
void write_unlock(rw *z);