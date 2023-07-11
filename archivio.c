#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdlib.h>   // conversioni stringa/numero exit() etc ...
#include <stdbool.h>  // gestisce tipo bool (variabili booleane)
#include <assert.h>   
#include <string.h>   
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <stdint.h>
#include <search.h>
#include <arpa/inet.h>
#include "xerrori.h"  // per termina e per le funzioni xpthread
#include "rw.h"       // per la lock per la tabella

/* --- DEFINIZIONE COSTANTI --- */
#define QUI  __LINE__,__FILE__
#define PC_buffer_len 10
#define Num_elem 1000000
#define Max_sequence_length 2048

/* --- DEFINIZIONE STRUTTURE DATI --- */
// Struttura dei buffer usati.
typedef struct {
    char **buffer;
    int *count;
    int *in;
    int *out;
    pthread_mutex_t *mutex;
    pthread_cond_t *empty;
    pthread_cond_t *full;
} shared_buffer;

// Stutura per consumatori writers e readers.
typedef struct {
    shared_buffer *buffer; 
    rw *lock;
} dati_consumatori_writers;

typedef struct {
    shared_buffer *buffer; 
    FILE *logfile;
    rw *lock;  
    pthread_mutex_t *mutex_file;
} dati_consumatori_readers;

// Stessa struttura per i produttori writer e reader.
typedef struct {
    shared_buffer *buffer;
    int fifo;
    int consumers;
} dati_produttori;

// Struttura per il thread handler.
typedef struct {
    pthread_t *writerprod;
    pthread_t *readerprod;
}dati_handler;

/*--- HASHTABLE --- */
static int htentries = 0; // Il numero totale di entries della htable.

// Crea un oggetto di tipo entry con chiave s e valore n.
ENTRY *crea_entry(char *s, int n){
    ENTRY *e = malloc(sizeof(ENTRY));
    if(e==NULL) termina("errore malloc entry 1");
    e->key = strdup(s); 
    e->data = (int *) malloc(sizeof(int));
    if(e->key==NULL || e->data==NULL)
        termina("errore malloc entry 2");
    *((int *)e->data) = n;
    return e;
}

void distruggi_entry(ENTRY *e){
  free(e->key);
  free(e->data);
  free(e);
}

// Funzioni aggiungi e conta usate dai writers e dai readers rispettivamente.
void aggiungi(char *s){
    ENTRY *new = crea_entry(s,1);
    ENTRY *present = hsearch(*new, FIND);
    if (present == NULL){
        hsearch(*new, ENTER);
        htentries++;
    }else{
        assert(strcmp(new->key, present->key) == 0);
        int *n = (int * ) present->data;
        *n += 1;
        distruggi_entry(new); // Distruggo la entry che non mi serve.
    }
}

int conta(char *s){
    ENTRY *new = crea_entry(s,1);
    ENTRY *present = hsearch(*new, FIND);
    int result;
    if (present == NULL) {
        result = 0;
    }else{
        int *n = (int *)present->data;
        result = *n;
    } 
    distruggi_entry(new); // Distruggo la entry.
    return result;
}

/* --- SIGNAL HANDLER --- */
// Thread che effettua la gestione di tutti i segnali.
void *tgestore(void *args) {
    dati_handler *a = (dati_handler*)args;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask,SIGINT);
    sigaddset(&mask,SIGTERM);
    int s;
    while(true) {
        int e = sigwait(&mask,&s);
        if(e!=0) perror("Errore sigwait");
        if (s == SIGINT){
            fprintf(stderr, "Il numero di stringhe nella tabella è %d \n", htentries);
        }   
        if (s == SIGTERM){
            xpthread_join(*(a->writerprod),NULL, QUI);
            xpthread_join(*(a->readerprod),NULL, QUI);
            fprintf(stdout, "Il numero di stringhe nella tabella è %d \n", htentries);
            return NULL;
        }
    }
  return NULL;
}

/* --- WRITERS AND READERS FUNCTIONS  ---*/
//Funzioni per i consumatori writers e consumatori readers
void *writersbody(void *args){
    dati_consumatori_writers *a = (dati_consumatori_writers *)args;

    char *element = NULL; 
    while(true){
        xpthread_mutex_lock(a->buffer->mutex, QUI);
        while(*(a->buffer->count) == 0){
            xpthread_cond_wait(a->buffer->empty, a->buffer->mutex,QUI);
        }
	// Prendo una stringa dal buffer.
        element = a->buffer->buffer[*(a->buffer->out)% PC_buffer_len ]; 
        *(a->buffer->count) -= 1;
        *(a->buffer->out) +=1 ;

        xpthread_cond_signal(a->buffer->full, QUI);
        xpthread_mutex_unlock(a->buffer->mutex, QUI);
	    
	// Se è di lunghezza 0 vuol dire che abbiamo finito.
        if (strlen(element) == 0)
            break;
	// Se non è di lunghezza 0 la aggiungo alla tabella.
        write_lock(a->lock);
        aggiungi(element);
        write_unlock(a->lock);

        free(element);
    }
    pthread_exit(NULL);
}

void *readersbody(void *args){
    dati_consumatori_readers *a = (dati_consumatori_readers *)args;

    char * element = NULL;
    while(true){
        xpthread_mutex_lock(a->buffer->mutex, QUI);
        while(*(a->buffer->count) == 0){
            xpthread_cond_wait(a->buffer->empty, a->buffer->mutex, QUI);
        }
        // Riceviamo la stringa dal buffer.
        element = a->buffer->buffer[*(a->buffer->out)% PC_buffer_len];
        *(a->buffer->count) -= 1;
        *(a->buffer->out) += 1;

        xpthread_cond_signal(a->buffer->full, QUI);
        xpthread_mutex_unlock(a->buffer->mutex, QUI);

        // Se la stringa è di lunghezza 0, vuol dire che abbiamo terminato.
        if (strlen(element) == 0)
            break;

        // Confrontiamo la stringa che abbiamo con quelle nella htable
        // se num = 0; vuol dire che non è stata aggiunta la stringa element
        // altrimenti avremo un numero positivo che rappresenta quante volte è stata
        // aggiunta alla tabella.
        read_lock(a->lock);
        int num = conta(element);
        read_unlock(a->lock);

        // Stampiamo nel file di log element e num.
        xpthread_mutex_lock(a->mutex_file, QUI);
        fprintf(a->logfile,"%s %d\n",element,num);
        xpthread_mutex_unlock(a->mutex_file, QUI);

        free(element);
    }
    pthread_exit(NULL);
}

// Funzione per i produttori writer e reader.
void *pbody(void *args){
    dati_produttori *a = (dati_produttori*) args;

    char *duptoken = NULL;
    char *linea = NULL;
    while(true){
        // Riceviamo dalla FIFO per prima cosa la grandezza della linea.
        unsigned short int bytes;
        ssize_t e = read(a->fifo, &bytes, 2);
        if(e==0) break;
        if(e<0 && e!=2){
            free(duptoken);
            free(linea);
            termina("Errore nella lettura da FIFO");
        }
        // Viene mandato in tipo network, quindi lo trasformo in tipo host.
        bytes = ntohs(bytes);

        // Riceviamo la sequenza vera e propria e aggiungiamo alla fine \0.
        linea = calloc(bytes+1,sizeof(char));
        linea[bytes] = '\0';
        ssize_t er= read(a->fifo, linea, bytes);
        if(er<0 && er!=bytes){
            free(duptoken);
            free(linea);
            termina("Errore lettura da fifo");
        }
        // Tokenizzazione della linea di bytes.
        char *token;
        char *point;
        char delimiters[] = ".,:; \n\r\t";
        token = strtok_r(linea, delimiters, &point);

        while(token!=NULL){
            // Copio la stringa.
            duptoken = strdup(token);
            if(duptoken==NULL){
                free(duptoken);
                free(linea);
                termina("Errore nell'uso di strdup");
            }

            // Mettiamo la stringa nel buffer.
            xpthread_mutex_lock(a->buffer->mutex,QUI);
            while(*(a->buffer->count) == PC_buffer_len)
                xpthread_cond_wait(a->buffer->full,a->buffer->mutex,QUI);
            a->buffer->buffer[*(a->buffer->in)% PC_buffer_len] = duptoken;
            *(a->buffer->in) += 1;
            *(a->buffer->count) +=1;
            
            xpthread_cond_signal(a->buffer->empty, QUI);
            xpthread_mutex_unlock(a->buffer->mutex,QUI);
            
            token = strtok_r(NULL, delimiters, &point);
        }
        free(linea);
    }
    // Comunico ai consumatori che abbiamo terminato.
    for(int i = 0; i<a->consumers; i++){
        xpthread_mutex_lock(a->buffer->mutex,QUI);
        while(*(a->buffer->count) == PC_buffer_len)
            xpthread_cond_wait(a->buffer->full,a->buffer->mutex,QUI);
        a->buffer->buffer[*(a->buffer->in)% PC_buffer_len] = "";
        *(a->buffer->in) += 1;
        *(a->buffer->count) +=1;
            
        xpthread_cond_signal(a->buffer->empty, QUI);
        xpthread_mutex_unlock(a->buffer->mutex,QUI);  
    }
    pthread_exit(NULL);
}

/* --- MAIN ---*/
int main(int argc, char *argv[]) {
    // Controllo parametri input e inizializzazione.
    if(argc<3) 
        printf("Uso\n\t%s w e r numt\n", argv[0]);
    int tw = 0; // Numero thread scrittori.
    int tr = 0; // Numero thread lettori.
    tw = atoi(argv[1]);
    tr = atoi(argv[2]);
    if(tw<=0 || tr<= 0)
        termina("Errore: dobbiamo avere un numero di thread scrittori e lettori > 0");

    /* -- SIGNALS HANDLING -- */
    // Blocco tutti i segnali.
    sigset_t mask;
    sigfillset(&mask);
    pthread_sigmask(SIG_BLOCK,&mask, NULL);

    /* -- APERTURA FILE -- */
    // Apro le due fifo caposc e capolet.
    int fifo_w = open("caposc",O_RDONLY);
    if(fifo_w<0)
        termina("Errore nell'apertura pipe caposc");

    int fifo_r = open("capolet",O_RDONLY);
    if(fifo_r<0)
        termina("Errore nell'apertura pipe capolet");

    // Creo e apro il file lettori.log che verrà usato dai thread consumatori lettori.
    FILE* logfile = fopen("lettori.log", "w");
    if(logfile==NULL)
        termina("Errore nella creazione logfile");

    /* -- HTABLE -- */
    // Creo Tabella Hash e la variabile di lock che userò per gestirla:
    // questa soluzione favorisce i lettori, e gli scrittori
    // potrebbero essere messi in attesa indefinita se continuano ad arrivare lettori.
    if(hcreate(Num_elem)==0)
        termina("Errore creazione hash-table");
    rw lock;
    rw_init(&lock);

    /* -- MUTEX & COND. VAR- CREATION -- */
    // Due mutex che serviranno per la sincronizzazione con il buffer.
    pthread_mutex_t muW = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t muR = PTHREAD_MUTEX_INITIALIZER;
    // Un mutex per gestire gli accessi al file di logging.
    pthread_mutex_t mufile = PTHREAD_MUTEX_INITIALIZER;

    // Creo due coppie di cond var (empty e full), una coppia per gli scrittori
    // un'altra per i lettori.
    pthread_cond_t emptyW = PTHREAD_COND_INITIALIZER;
    pthread_cond_t fullW = PTHREAD_COND_INITIALIZER;
    pthread_cond_t emptyR = PTHREAD_COND_INITIALIZER;
    pthread_cond_t fullR = PTHREAD_COND_INITIALIZER;

    /* -- BUFFER -- */
    // Creo i due buffer, uno per i consumatori scrittori (w) e uno per quelli lettori (r)
    shared_buffer sharedBuffer_w;
    int cw = 0,iw = 0,ow = 0;
    char *buffer_w[PC_buffer_len];
    sharedBuffer_w.buffer = buffer_w;
    sharedBuffer_w.count = &cw;
    sharedBuffer_w.in = &iw;
    sharedBuffer_w.out = &ow;
    sharedBuffer_w.full = &fullW;
    sharedBuffer_w.empty = &emptyW;
    sharedBuffer_w.mutex = &muW;

    shared_buffer sharedBuffer_r;
    int cr = 0,ir = 0,or = 0;
    char *buffer_r[PC_buffer_len];
    sharedBuffer_r.buffer = buffer_r;
    sharedBuffer_r.count = &cr;
    sharedBuffer_r.in = &ir;
    sharedBuffer_r.out = &or;
    sharedBuffer_r.full = &fullR;
    sharedBuffer_r.empty = &emptyR;
    sharedBuffer_r.mutex = &muR;

    /* -- THREADS INFO -- */
    // Creo le strutture dati per i thread writer e reader, consumatori e produttori.
    dati_consumatori_readers arc[tr];  // Array di struct che passerò ai ThreadReaders (tr).
    dati_consumatori_writers awc[tw];  // Array di struct che passerò ai ThreadWriters (tw).
    dati_produttori dwp;               // Struct del writer produttore.
    dati_produttori drp;               // Struct del reader produttore.
    dati_handler dh;                   // Struct del signal handler.

    pthread_t conw[tw];                // id thread consumatori writers.
    pthread_t conr[tr];                // id thread consumatori readers.
    pthread_t prodW;                   // id thread produttore writer.
    pthread_t prodR;                   // id thread produttore reader.
    pthread_t signaler;                // id thread handler dei segnali.

    /* -- THREADS CREATION -- */
    // Creo il thread che si occupa dei segnali e definisco i suoi campi.
    dh.writerprod = &prodW;
    dh.readerprod = &prodR;
    xpthread_create(&signaler, NULL, &tgestore, &dh, QUI);

    // Definisco i campi del produttore scrittore e lo creo dandogli come funzione da eseguire
    // pbody, stessa cosa per il produttore lettore.
    dwp.buffer = &sharedBuffer_w;
    dwp.fifo = fifo_w;
    dwp.consumers = tw;
    xpthread_create(&prodW, NULL, &pbody, &dwp ,QUI); 

    drp.buffer = &sharedBuffer_r;
    drp.fifo = fifo_r;
    drp.consumers = tr;
    xpthread_create(&prodR, NULL, &pbody, &drp,QUI);

    // Creo i consumatori scrittori.
    for(int i=0;i<tw;i++) {
		awc[i].buffer = &sharedBuffer_w;  
        awc[i].lock = &lock;
        xpthread_create(&conw[i], NULL, &writersbody, &awc[i],QUI);     
    }

    // Creo i consumatori lettori.
    for(int i=0;i<tr;i++) {
		arc[i].buffer = &sharedBuffer_r;  
        arc[i].logfile = logfile;
        arc[i].mutex_file= &mufile;
        arc[i].lock= &lock;
        xpthread_create(&conr[i], NULL, &readersbody, &arc[i],QUI);     
    }

    /* -- CLEAN ENDING -- */
    // Se viene ricevuto il segnale SIGTER, avremo una "clean ending":
    // libero tutto lo spazio assegnato.
    if(xpthread_join(signaler, NULL,QUI) == 0){
        // Attendo i consumatori writers.
        for(int i=0;i<tw;i++) 
            xpthread_join(conw[i],NULL,QUI);
        
        // Attendo i consumatori readers.
        for (int i = 0; i < tr; i++) 
            xpthread_join(conr[i], NULL, QUI);

        // Distruggo la tabella.
        hdestroy();

        /* -- DEALLOCAZIONE MEMORIA -- */
        xpthread_mutex_destroy(&muW,QUI);
        xpthread_cond_destroy(&emptyW,QUI);
        xpthread_cond_destroy(&fullW,QUI);
        xpthread_mutex_destroy(&muR,QUI);
        xpthread_cond_destroy(&emptyR,QUI);
        xpthread_cond_destroy(&fullR,QUI);

        // Chiudo i file.
        if(fclose(logfile)!=0) termina("Errore chiusura file di log.");
        if(close(fifo_r)!= 0) termina("Errore chiusura fifo caposc.");
        if(close(fifo_w)!= 0) termina("Errore chiusura fifo capolet.");
    }
    return 0;
}
