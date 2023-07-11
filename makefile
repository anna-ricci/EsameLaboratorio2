# definizione del compilatore e dei flag di compilazione
# che vengono usate dalle regole implicite
CC = gcc
CFLAGS = -g -Wall -O -std=c99
LDLIBS = -lm -lrt -pthread

# elenco degli eseguibili da creare
EXECS = archivio

# file oggetto aggiunti
OBJS = archivio.o xerrori.o rw.o

# primo target: gli eseguibili sono precondizioni del target
# quindi verranno tutti creati
all: $(EXECS)

# regola per la creazione degli eseguibili utilizzando gli oggetti necessari
$(EXECS): $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

# regola per la creazione di file oggetto che dipendono da xerrori.h e rw.h
%.o: %.c xerrori.h rw.h
	$(CC) $(CFLAGS) -c $<

# target che non corrisponde a una compilazione
# ma esegue la cancellazione dei file oggetto e degli eseguibili
clean: 
	rm -f *.o $(EXECS) $(OBJS)
