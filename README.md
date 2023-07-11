# --- Progetto esame Laboratorio II ---

L'obiettivo è sviluppare un server in Python per gestire due diverse tipologie di connessione che interagiscono con un archivio. Il server, insieme ai due client, è un eseguibile Python, mentre l'archivio è un programma C eseguito come subprocesso dal server.

I client comunicano con il server per interagire con una tabella hash specifica presente nell'archivio. 

Per comunicare tra server e archivio vengono usate due FIFO: caposc e capolet, rispettivamente per il thread produttore scrittore e per il thread produttore lettore. Il server riceverà e invierà informazioni in bytes. In seguito i produttori comunicheranno con i thread consumatori scrittori o lettori attraverso un buffer circolare di lunghezza fissata. Tutte le connessioni al server vengono registrate nel file "server.log". 

Le operazioni consentono di aggiungere elementi alla tabella o leggere il numero di occorrenze degli elementi già presenti. Le informazioni di lettura vengono scritte in append nel file "lettori.log".

L'archivio è in grado di gestire i segnali SIGINT e SIGTERM. Alla ricezione del segnale SIGINT, il programma scrive su STDERR il numero di righe distinte salvate nella tabella hash e continua l'esecuzione senza terminare. Quando viene ricevuto il segnale SIGTERM, il programma scrive il numero di righe distinte salvate nella tabella hash su STDOUT e termina l'esecuzione in modo pulito.

Il server gestisce anche la ricezione del segnale SIGINT. In tal caso, gestisce l'eccezione e invia un segnale SIGTERM all'archivio per terminarlo correttamente. Se abilitata, al termine dell'esecuzione verrà anche generato un file di log contenente l'output del tool Valgrind.

## --- Istallazione e utilizzo ---

Il progetto deve essere eseguito esclusivamente su ambiente Linux, poiché alcune funzionalità non sono supportate su macOS o Windows.
Per installare il progetto si segua questi comandi:

```shell
git clone git@github.com:bobogoesbrr/EsameLaboratorio2.git test
cd test
```

Il server ha i seguenti parametri:
- `nthreads` è l'unico parametro obligatorio per il server.py, rappresenta il numero di thread che il server può utilizzare per gestire le connessioni.
- `-w` è un parametro facoltativo che rappresenta il numero di thread scrittori, cioè il numero di thread consumatori-scrittori, che verranno usati da archivio per scrivere nella tabella hash. Se mandato viene richiesto che sia un `positive_int`, questo tipo che ho definito in server.py si assicura che non venga mandato un numero negativo come parametro, se questo non viene rispettato verrà dato errore. Se non viene mandato alcun numero allora di default avrà valore 3. In archivio verrà letto e assegnato a `tw` .
- `-r` parametro facoltativo che rappresenta il numero di thread lettori, cioè il numero di thread consumatori-lettori, che verranno usati da archivio per leggere le informazioni dalla tabella hash. Anche questo deve essere di tipo `positive_int`. Se non viene mandato alcun numero allora di default avrà valore 3. In archivio verrà letto e assegnato a `tr`.
- `-v` parametro facoltativo che se presente porterà all'esecuzione di archivio con valgrind, di conseguenza verrà creato un file valgrind che riporterà l'uso della memoria.

Il client1 presenta:
- `filename` è l'argomento unico e necessario per usare client1. Deve essere il nome di un file presente nella directory e ogni sua linea verrà mandata a server.py.

Il client2 presenta:
- `filenames` gli argomenti per il client2, uno è necessario ma è capace di leggerne di più. Ogni file deve essere presente nella directory e verrà letto e mandato a server.py.

L' archivio presenta:
- `tw` sarà il numero mandato a server `-w`, di default sarà 3.
- `tr` sarà il numero mandato a server `-r`, di default sarà 3.

Per eseguire correttamente il programma, è necessario eseguire prima il `makefile` per creare l'eseguibile dell'archivio.

Esempio:

```shell
make
./server.py 5 -r 2 -w 4 -v &        #parte il server in background con 5 threads, facendo partire 
                                    #archivio con valgrid  e con 2 thread consumatori-lettori e 4 consumatori-scrittori
./client2 file1 file2               #client2 parte leggendo file1 e file2
./client1 file3                     #client1 parte leggendo file3

pkill -INT -f server.py             #per avere una terminazione pulita con deallocazioni e messaggio di ritorno con il numero di elementi nella tabella hash
```

## --- Struttura ---

Il progetto è strutturato all'interno di una singola cartella radice, che contiene tutti i file pertinenti al progetto. Di seguito sono elencati i file inclusi:

- `server.py`= il file per la creazione e la gestione del server, eseguibile Python.
- `client1`=  il file che contiene il codice sorgente del primo client, usato per stabilire una connessione di tipo A, eseguibile Python.
- `client2`= il file che contiene il codice sorgente del secondo client, usato per stabilire una connessione di tipo B, eseguibile Python.
- `archivio.c`= codice sorgente dell'eseguibile `archivio` ottenuto dal `makefile` e avviato come sottoprocesso da `server.py`.
- `rw.c` e `rw.h` = libreria che espone le funzioni per la variabile per assicurare la mutua esclusione per la lettura e scrittura della tabella hash
- `xerrori.c` `xerrori.h` = libreria che espone le funzioni per la creazioni di thread, conditional variables e funzioni per terminazione in caso di errore.
- `file1`, `file2` e `file3`: file di testo da passare ai client per verificare il corretto funzionamento del programma.


## --- Scelte implementative ---

- Il file `server.py` e `archivio.c` sono realizzati rispettivamente in Python e C, con `server.py` che è un eseguibile Python. I due client, `client1` e `client2`, sono anch'essi eseguibili Python. La scelta di utilizzare Python e C per il progetto è stata fatta in quanto entrambi erano disponibili come opzioni per il progetto ridotto.
- Com'è stato detto in precedenza, è stato creato un tipo in server, `positive_int` per controllare direttamente in input che gli interi passati a linea di comando al server siano positivi.
- Per avere mutua esclusione nella tabella hash è stato scelto di implementare le variabili lock della libreria `rw.c` avendo uno schema scrittori/lettori sbilanciato a favore dei lettori.
- Per la creazione e la gestione di thread e conditional variables è stato scelto di implementare la libreria `xerrori.c` che in caso di errore rende più semplici l'individuazione di esso.
- La struttura del buffer include non solo il buffer di puntatori a stringhe stesso e le variabili che servono per indicare a produttori e consumatori dove poter rispettivamente scrivere e leggere, ma anche il mutex e le conditional variables usate per la mutua esclusione, questa scelta è stat fatta per semplificare la creazione dei thread (e anche per rendere più comprensibile le loro strutture).

```shell
typedef struct {
    char **buffer;              \\buffer di puntatori a char.
    int *count;                 \\numero di elementi presenti.
    int *in;                    \\indice che indica il primo elemento su cui può scrivere il producer.
    int *out;                   \\indice che indica il primo elemento che i consumatori possono leggere.
    pthread_mutex_t *mutex;     \\mutex per la mutua esclusione sul buffer.
    pthread_cond_t *empty;      \\conditional variable su cui si fermeranno i consumatori.
    pthread_cond_t *full;       \\conditional variable su cui si fermerà il producer.
} shared_buffer;
```

- Per il contatore del numero di entrys nella tabella hash è stata usata una variabile globale `hentries`.
- Viene utilizzata la funzione `strtok_r()` la funzione è una versione thread-safe di `strtok()`. Questo ci consente di utilizzare `strtok_r` in più thread contemporaneamente, o di iterare su più stringhe senza incorrere in problemi di interferenza.
- Nell'handler vengono usate chiamate ad `fprintf`, nonostante queste funzioni siano non-signal-safe,vengono usate perchè restiamo in ascolto dei segnali solo attraverso la funzione `sigwait`, quindi non avremo il problema di interruzione della gestione del segnale perché ne riceviamo un altro.

## --- Protocollo di rete ---

Il protocollo di rete utilizzato per la comunicazione tra il server e i client prevede l'apertura di un socket TCP da parte del server, che rimane in attesa di connessioni dai client. I client, a loro volta, possono stabilire due tipi di connessioni: A e B.
Entrambi i client mandano pacchetti di bytes, il server li leggerà e li metterà in bytes nelle FIFO per comunicare con archivio. 

### Connessione di tipo A.
La connesione di tipo A serve per interrogare la tabella hash ed è utilizzata dal client1:
- Il client apre una connessione di tipo A inviando un pacchetto di connessione contenente l'identificatore 'True', seguito dalla lunghezza di una riga(max 2048 bytes) e dalla riga.
- Dopo l'invio del pacchetto, la connessione viene chiusa.
- Se il client1 deve inviare più stringhe, deve aprire una nuova connessione di tipo A per ciascuna stringa.

### Connessione di tipo B.
La connessione di tipo B server per aggiungere elementi alla tabella hash ed è utilizzata dal client2:
- Il client riceve più files e, a differenza del client1, potrà usare più threads, ognuno che gestisce un file diverso.
- Il client apre una connessione di tipo B inviando un pacchetto di connessione contenente l'identificatore 'False', seguito dalla lunghezza della riga(max 2048) e dalla riga effettiva.
- L'identificatore viene indicato solo ad apertura della connessione, per ogni altra stringa da inviare si invia lunghezza e riga.
- La connessione viene aperta quando apre un file e viene chiusa quando ha finito con tale file.
- Quando il client2 ha terminato l'invio dei messaggi, la connessione viene chiusa.

Il server, ogni volta che viene ricevuta e chiusa una connessione viene scritto su server.log che tipo di connessione ha ricevuto, il numero di bytes scritti sulle FIFO, incluso i due byte per rappresentare la lunghezza della riga e il numero di bytes della lunghezza della riga stessa.
