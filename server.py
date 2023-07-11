#!/usr/bin/env python3
import os, struct, socket,threading, argparse, concurrent.futures, subprocess, logging, subprocess, signal

# Host e porta.
HOST = "127.0.0.1"  
PORT = 50621

# Configurazione del logging.
logging.basicConfig(filename= 'server.log',
                    level=logging.DEBUG, datefmt='%d/%m/%y %H:%M:%S',
                    format='%(asctime)s - %(levelname)s - %(message)s')

# Definisco un tipo di intero positivo, per verificare che mi venga mandato un valore
# positivo per il numero di threads lettori e scrittori.
def positive_int(value):
    ivalue = int(value)
    if ivalue <= 0:
        raise argparse.ArgumentTypeError("Il numero di threads DEVE essere positivo.")
    return ivalue

def main(host=HOST,port=PORT):
  # Richiediamo il numero massimo di thread che possiamo usare 'nthread',è obligatorio,
  # e i valori opzionali -r,-w,-v
  parser = argparse.ArgumentParser()
  parser.add_argument("nthread", type=positive_int, help= "Ho bisogno di un intero positivo")
  parser.add_argument("-r", type=positive_int, default=3, help="Numero di thread lettori per archivio")
  parser.add_argument("-w", type=positive_int, default=3, help="Numero di thread scrittori per archivio")
  parser.add_argument("-v", action="store_true", help="Partire con valgrid o no")
  args = parser.parse_args()
  nthread = args.nthread
  readers = args.r 
  writers = args.w
  v = args.v

  # Verifico l'esistenza delle FIFO caposc e capolet
  # se non esistono, le creo.
  caposc_path= "caposc"
  capolet_path= "capolet"
  if not os.path.exists(caposc_path):
    try:
        os.mkfifo(caposc_path)
    except OSError as e:
        print("Errore nel creare FIFO caposc", e)
  if not os.path.exists(capolet_path):
    try:
        os.mkfifo(capolet_path)
    except OSError as e:
        print("Errore nel creare FIFO capolet", e)
  
  # Se abbiamo passato v in input allora facciamo partire l'archivio con valgrind,
  # se no, senza.
  if v:
    p = subprocess.Popen(["valgrind","--leak-check=full", 
                      "--show-leak-kinds=all", 
                      "--log-file=valgrind-%p.log", 
                      "./archivio", str(writers) , str(readers)])
  else:
    p = subprocess.Popen(["./archivio", str(writers), str(readers)])

  # Apro le fifo.
  fifow = os.open(caposc_path, os.O_WRONLY)
  fifor = os.open(capolet_path, os.O_WRONLY)
  # Creo la lock per la scrittura in fifo.
  lock = threading.Lock()
  # Creo il server socket.
  with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    try:  
      s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)            
      s.bind((host, port))
      s.listen()
      with concurrent.futures.ThreadPoolExecutor(nthread) as executor:
        while True:
          # Mi metto in attesa di una connessione.
          conn, addr = s.accept()
          executor.submit(gestisci_connessione, conn,addr,fifor,fifow,lock)
    except KeyboardInterrupt:
      pass
    # Chiudo socket.
    s.shutdown(socket.SHUT_RDWR)
    # Cancello FIFO capolet e caposc.
    os.close(fifor)
    os.close(fifow)
    os.unlink(capolet_path)
    os.unlink(caposc_path)

  # Mando un segnale SIGTERM ad archivio che lo farà terminare.
  p.send_signal(signal.SIGTERM)

# Gestisce una singola connessione con un client
def gestisci_connessione(conn,addr,fifo_r,fifo_w,lock): 
  with conn:  
    # Attendo un byte di tipo bool che varrà mandato dai client
    # se è True sarà client di tipo 1, connessione A, se è False sarà di tipo 2
    # connessione B.
    data = recv_all(conn,1)
    assert len(data)==1
    tipo = struct.unpack("!?",data)[0]

    if tipo:
        # Se di tipo 1 allora manda prima la lunghezza della stringa
        # , poi, prende la lock e scrive la lunghezza della stringa
        # e la stringa stessa nella FIFO capolet.
        d1 = recv_all(conn,2)
        leng = struct.unpack("!H", d1)[0]
        assert(leng<= 2048)
        d2= recv_all(conn, leng)
        try:
            with lock:
              os.write(fifo_r,d1)
              os.write(fifo_r,d2)
        except OSError as e:
           logging.debug(f"error writing to capolet:{e}")
        except Exception as e:
           logging.debug(f"Generic error occured: {type(e).__name__}")
           logging.debug(f"Error message:{str(e)}")
        # Scrivo sul file di logging quanti byte sono stati scritti nella fifo
        # quindi i due byte per la lunghezza e la lunghezza della stringa stessa.
        logging.info(f"Tipo di connessione 1, byte trasmessi = {leng+2}")
    else:
       # Se è di tipo 2, manda una sequenza di stringhe che scriverà nella
       # FIFO caposc, uscirà dal while solo quando manderò una stringa di leng==0
       b = 0 #variabile che ci servirà per dire quanti bytes vengono scritti in totale sulla fifo
       while True:
        d1= recv_all(conn,2)
        leng = struct.unpack("!H", d1)[0]
        assert(leng<= 2048)
        if leng == 0:
            try:
              logging.info(f"Tipo di connessione 2, byte scritti in caposc= {b}")
            except Exception as e:
              logging.debug(f"Generic error occured: {type(e).__name__}")
              logging.debug(f"Error message:{str(e)}")
            break
        b = b + leng + 2
        d2= recv_all(conn, leng)
        try:
          with lock:
            os.write(fifo_w,d1)
            os.write(fifo_w,d2)
        except OSError as e:
          logging.debug(f"error writing to caposc:{e}")
        except Exception as e:
           logging.debug(f"Generic error occured: {type(e).__name__}")
           logging.debug(f"Error message:{str(e)}")
 
def recv_all(conn,n):
  chunks = b''
  bytes_recd = 0
  while bytes_recd < n:
    chunk = conn.recv(min(n - bytes_recd, 1024))
    if len(chunk) == 0:
      raise RuntimeError("Socket connection broken.")
    chunks += chunk
    bytes_recd = bytes_recd + len(chunk)
  return chunks
 
if __name__ == '__main__':
   main()
