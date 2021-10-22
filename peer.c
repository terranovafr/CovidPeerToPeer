#include "utility/peer/timeUtility.h"
#include "utility/peer/vars.h"
#include "utility/peer/connectionUtility.h"
#include "utility/peer/commandsUtility.h"
#include "utility/peer/registerUtility.h"

//numero massimo di peer nel network oltre il quale si utilizza il meccanismo delle query ad orizzonte limitato
#define MAX_NUM_PEERS 100
#define TIMEOUT_HOUR 18

char buffer[1024];
int MyPort;
int DSsd;
int udpsd;
int tcpsd;
struct sockaddr_in my_addr, DS_addr;
fd_set readfds, master;
int fdmax;
int advertisedGet;
int connected;

//lista dei peer per i quali non si dispone entry (flooding)
struct peer* peerList;

time_t t;
struct tm tm;

struct neighbors neighbors[NUM_NEIGHBORS];
//registro quotidiano
struct entry* todayRegister;
struct totalEntry totalEntry;
struct diffEntry diffEntry;
struct sumEntry sumEntry;
struct fileEntry fileEntry;

/* checkPort: controlla la validità del numero di porta */

int checkPort(int port){
	if(port > 65535 || port < 1024)
		return -1;
	return 0;
}

/* validSender: verifica che le due strutture sockaddr_in siano relative allo stesso host */

int validSender(struct sockaddr_in src_addr, struct sockaddr_in valid_addr){
	if(src_addr.sin_port == valid_addr.sin_port && src_addr.sin_addr.s_addr == valid_addr.sin_addr.s_addr)
		return 1;
	else return 0;
}

/* updateNeighbors: Aggiorna le informazioni sui propri neighbors
	Chiude un eventuale connessione TCP aperta coi neighbors precedenti e instaura una
	connessione TCP coi nuovi neighbors */

void updateNeighbors(int index, int port, char* IP){
    struct sockaddr_in src_addr;
    socklen_t len;
    int ret;
    neighbors[index].port = port;
    strcpy(neighbors[index].IP, IP);
	// se l'aggiornamento mi dice che non ho più nessun vicino per quell'indice
    if(strcmp(neighbors[index].IP, "0.0.0.0") == 0 && neighbors[index].port == 0){
    	close(neighbors[index].sd);
		neighbors[index].sd = 0;
        return;
    }
    memset(&neighbors[index].neighbor_addr, 0, sizeof(neighbors[index].neighbor_addr));
	neighbors[index].neighbor_addr.sin_family = AF_INET;
	neighbors[index].neighbor_addr.sin_port = htons(neighbors[index].port);
	inet_pton(AF_INET, neighbors[index].IP, &neighbors[index].neighbor_addr.sin_addr);
	// chiude un eventuale socket aperto con il vecchio neighbor
    if(neighbors[index].sd != 0)
        close(neighbors[index].sd);
    if(index == 0){
		// neighbor 0
        len = sizeof(my_addr);
        ret = accept(tcpsd, (struct sockaddr*)&src_addr, &len);
        if(ret == -1){
            perror("Errore");
            return;
        } else neighbors[0].sd = ret;
    } else if(index == 1){
		// neighbor 1
        neighbors[1].sd = socket(AF_INET, SOCK_STREAM, 0);
        if(neighbors[1].sd == -1){
            perror("Errore");
            return;
        }
        len = sizeof(my_addr);
        ret = connect(neighbors[1].sd, (struct sockaddr*)&neighbors[1].neighbor_addr, len);
        if(ret == -1){
            perror("Errore");
            return;
        }
    }
}

/* updateMaster: Aggiorna in maniera consistente il set di descrittori */

void updateMaster(){
	FD_ZERO(&master);
	FD_SET(fileno(stdin), &master);
	fdmax = 0;
	if(DSsd != 0){
		FD_SET(DSsd, &master);
		fdmax = DSsd;
	}
	if(neighbors[0].sd != 0){
		FD_SET(neighbors[0].sd, &master);
   		if(neighbors[0].sd > fdmax) fdmax = neighbors[0].sd;
	}
	if(neighbors[1].sd != 0){
		FD_SET(neighbors[1].sd, &master);
    	if(neighbors[1].sd > fdmax) fdmax = neighbors[1].sd;
	}
}

/* boot: Effettua il boot con il DS server
   Gestisce la ritrasmissione nel caso in cui non si riceve risposta entro un certo timeout
   Se è in corso il calcolo di un'aggregazione invita a riprovare più tardi
   altrimenti si sarà associato al DS server e riceve le informazioni sui neighbors*/

void boot(){
	int ret, len;
	socklen_t addrlen;
	uint16_t lmsg;
	struct timeval timeout;
	struct sockaddr_in src_addr;
	fd_set sockfds;
	addrlen = sizeof(DS_addr);
    char IP0[16];
    char IP1[16];
    int port0;
    int port1;
	int status;
	while(1){
		strcpy(buffer, "BOOT");
		len = strlen(buffer);
		lmsg = htons(len);
		// invio il BOOT al DS server
		ret = sendto(udpsd, (void*)&lmsg, sizeof(uint16_t), 0, (struct sockaddr*)&DS_addr, addrlen);
		if(ret < sizeof(uint16_t)){
				perror("Errore");
				continue;
		}
		ret = sendto(udpsd, (void*)buffer, len, 0, (struct sockaddr*)&DS_addr, addrlen);
		if(ret < len){
			perror("Errore");
			continue;
		}
		FD_SET(udpsd, &sockfds);
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;
		// utilizzo un meccanismo di ritrasmissione nel caso in cui non ricevo risposta
		ret = select(udpsd+1, &sockfds, NULL, NULL, &timeout);
		if(ret > 0){
			addrlen = sizeof(src_addr);
			ret = recvfrom(udpsd, (void*)&lmsg, sizeof(uint16_t), 0, (struct sockaddr*)&src_addr, &addrlen);
			// verifica che la risposta sia da parte del DS_server
			if(!validSender(src_addr, DS_addr)){
				printf("Qualcuno che non è il DS_server sta cercando di inviarmi messaggi, riprovo!\n");
				continue;
			}
			if(ret < sizeof(uint16_t)){
				perror("Errore");
				continue;
			}
			len = ntohs(lmsg);
			ret = recvfrom(udpsd, (void*)buffer, len, 0, (struct sockaddr*)&src_addr, &addrlen);
			if(ret < len){
				perror("Errore");
				continue;
			}
			buffer[len] = '\0';
			if(!validSender(src_addr, DS_addr)){
				printf("Qualcuno che non è il DS_server sta cercando di inviarmi messaggi, riprovo!\n");
				continue;
			}
			// leggo il codice di stato inviato dal DS server
			sscanf(buffer, "BOOT %d\n", &status);
			if(status == 1){
				// il DS server sta già gestendo un'altra richiesta
				printf("Il DS server sta già servendo un'altra richiesta, riprovo!\n");
				sleep(3);
				continue;
			} else if(status == 2){
				// calcolo di un'aggregazione in corso
				printf("Connessione non andata a buon fine, c'è il calcolo di un'aggregazione in corso! Riprova più tardi!\n");
				close(udpsd);
				udpsd = 0;
				return;
			} else if(status == 0){
				printf("Ho ricevuto risposta dal server\n");
				// inizializzo la connessione TCP
				DSsd = socket(AF_INET, SOCK_STREAM, 0);
				if(DSsd == -1){
					perror("Errore");
					return;
				}
				addrlen = sizeof(DS_addr);
				ret = connect(DSsd,(struct sockaddr*)&DS_addr, addrlen);
				if(ret == -1){
					perror("Errore");
					return;
				}
				ret = recv(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
				if(ret < sizeof(uint16_t)){
					perror("Errore");
					continue;
				}
				len = ntohs(lmsg);
				ret = recv(DSsd, (void*)buffer, len, 0);
				if(ret < len){
					perror("Errore");
					continue;
				}
				buffer[len] = '\0';
				// ottengo le informazioni sui neighbors
				sscanf(buffer, "UPDATE\npeer: %s %d\npeer: %s %d", IP0, &port0, IP1, &port1);
				if(strcmp(IP1, neighbors[1].IP) != 0 || port1 != neighbors[1].port)
						updateNeighbors(1, port1, IP1);
				if(strcmp(IP0, neighbors[0].IP) != 0 || port0 != neighbors[0].port)
						updateNeighbors(0, port0, IP0);
				break;
			} else printf("Messaggio ricevuto non riconosciuto\n");
		} else {  
			// il timeout è scaduto senza che abbia ricevuto una risposta
			printf("Nessuna risposta dal DS_server, ri-invio la richiesta di connessione\n");
			continue;
		}
	}
	connected = 1;
	printf("Ho ricevuto dal DS_server le informazioni sui due neighbors:\n%s:%d\n%s:%d\n", neighbors[0].IP, neighbors[0].port, neighbors[1].IP, neighbors[1].port);
	updateMaster();
	close(udpsd);
}


/* start: verifica che il peer non sia già connesso ad un DS server, crea il socket UDP per la connessione e invoca la boot*/

void start(char* addr, int port){
	int ret;
	if(checkPort(port) == -1){
		printf("Porta inserita non valida, scegliere un valore appartenente a [1024,65535]\n");
		return;
	}
	if(connected == 1){
		printf("Ti sei già associato ad un DS_server! Riprova pià tardi!\n");
		return;
	}
	if(udpsd != 0){
		return;
	}
	ret = startUDPConnection(addr, port);
	if(ret == -1)
		return;
	printf("Invio al DS_server in ascolto all'indirizzo %s:%d la richiesta di connessione\n", addr, port);
	boot();
}

/* add: aggiunge una entry al registro giornaliero (in memoria) */

void add(char* type, int quantity){
	struct entry* newEntry;
	struct entry* e;
	// controllo parametri
	if(strcmp(type, "N") != 0 && strcmp(type, "T") != 0){
		printf("Formato del tipo non valido, riprova!\n");
		return;
	}
	if(quantity == 0){
		printf("Hai inserito una quantità non valida, riprova!\n");
		return;
	}
	newEntry = malloc(sizeof(struct entry));
	newEntry->type = *type;
	newEntry->quantity = quantity;
	newEntry->next = 0;
	if(todayRegister == NULL){
		printf("Ho inserito la entry nel registro di oggi!\n");
		todayRegister = newEntry;
		return;
	}
	for(e = todayRegister; e->next != 0; e = e->next);
	e->next = newEntry;
	printf("Ho inserito la entry nel registro di oggi!\n");
}


/* readFile: funzione di supporto per leggere il contenuto del file binario */

void readFile(char* day){
	char file[60];
	FILE* fday;
	sprintf(file, "registers/%d/%s.bin", MyPort, day);
	fday = fopen(file, "rb");
	if(fday == NULL)
		return;
	if(strcmp(day, "total") == 0){
		while(fread(&totalEntry, sizeof(totalEntry), 1, fday))
			printf("%s %c %d\n", totalEntry.date, totalEntry.type, totalEntry.quantity);
	} else if(strcmp(day, "sum") == 0){
		while(fread(&sumEntry, sizeof(sumEntry), 1, fday))
			printf("%s %s %c %d\n", sumEntry.lower, sumEntry.upper, sumEntry.type, sumEntry.quantity);
	} else if(strcmp(day, "diff") == 0){
		while(fread(&diffEntry, sizeof(diffEntry), 1, fday))
			printf("%s %s %c %s\n", diffEntry.lower, diffEntry.upper, diffEntry.type, diffEntry.quantity);
	} else {
		while(fread(&fileEntry, sizeof(struct fileEntry), 1, fday))
			printf("%s %d %c %d\n", fileEntry.IP, fileEntry.port, fileEntry.type, fileEntry.quantity);
	}
	fclose(fday);
}

/* getNumPeer: Richiede al DS Server il numero di peer attualmente connessi al network */

int getNumPeer(){
	int len, ret;
	uint16_t lmsg;
	int numPeer;
	strcpy(buffer, "GETNUM");
	len = strlen(buffer);
	lmsg = htons(len);
	ret = send(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return -1;
	}
	ret = send(DSsd, (void*)buffer, len, 0);
	if(ret < len){
		perror("Errore");
		return -1;
	}
	//Ricevo il numero di peer attualmente connessi al network
	ret = recv(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return -1;
	}
	len = ntohs(lmsg);
	ret = recv(DSsd, (void*)buffer, len, 0);;
	if(ret < len){
		perror("Errore");
		return -1;
	}
	buffer[len] = '\0';
	sscanf(buffer, "GETNUM 0\nnum: %d", &numPeer);
	return numPeer;
}

/* discriminate: elimina dalla lista le entry che ha a disposizione e restituisce il numero di entry che non ha a disposizione */

int discriminate(char* filename, struct peer** list){
	FILE* f;
	int numPeer;
	struct peer* p;
	struct peer* q;
	numPeer = 0;
	if(access(filename,F_OK)!=0){
		// se non ho quel file non ho nessuna entry
		goto end;
	}
	f = fopen(filename, "rb");
	if(f == NULL){
		printf("Impossibile aprire il file di oggi!\n");
		return -1;
	}
	while(fread(&fileEntry, sizeof(fileEntry), 1, f)){
		for(p = *list, q = p->next; q != 0; p = q, q = q->next){
			if(fileEntry.port == q->port && strcmp(fileEntry.IP,q->IP) == 0){
				p->next = q->next;
				printf("Tra le entry che mi ha inviato ho quella relativa a %s:%d\n", q->IP, q->port);
			}
		}
		p = *list;
		if(fileEntry.port == p->port && strcmp(fileEntry.IP,p->IP) == 0){
			printf("Tra le entry che mi ha inviato ho quella relativa a %s:%d\n", p->IP, p->port);
			if(p->next != 0)
				*list = p->next;
			else *list = 0;
		}
		if(*list == 0)
			break; 
		fseek(f, sizeof(fileEntry), SEEK_CUR);	
	}
	fclose(f);
end: for(p = *list; p != 0; numPeer++, p = p->next);
	return numPeer;
}

/* propagateFlooding: Propaga la risposta ad un flooding */

void propagateFlooding(int index){
	uint16_t lmsg;
	int len, ret;
	len = strlen(buffer);
	lmsg = htons(len);
	printf("\nIl peer %s:%d ha risposto al flooding! Lo propago all'indietro!\n", neighbors[index].IP, neighbors[index].port);
	ret = send(neighbors[index].sd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return;
	}
	ret = send(neighbors[index].sd, (void*)buffer, len, 0);
	if(ret < len){
		perror("Errore");
		return;
	}
}

/* waitAfterFlooding: Funzione invocata dopo aver iniziato un flooding
   Si mette in attesa per un numero di secondi dipendente dal numero di peer a cui ha inviato il flooding 
   Se riceve delle entry da un peer verifica di non averla già ricevute da un altro peer e nel caso le memorizza
   Attende rifiutando le richieste di altri peer anche se ha finito così da non farli bloccare in attesa di essere contattati
*/

int waitAfterFlooding(int numPeer, char* filename, int dayPeers){
	int ret;
	fd_set floodset, floodread;
	int floodmax = 0;
	struct stat statRes;
	int ended;
	struct timeval timeout;
	struct sockaddr_in peer_addr;
	int peer_sd;
	char mex[200];
	int len;
	uint16_t lmsg;
	char IP[16];
	int port;
	int i = 0;
	int index;
	char* s;
	FILE* f;
	FILE* fr;
	int numEntries;
	char type;
	int porta;
	char indIP[8];
	int quantity;
	writePermissions(1, filename);
	f = fopen(filename, "ab+");
	if(f == NULL){
		printf("Errore nell'apertura del file!\n");
		return -1;
	}
	// uso un descrittore dei file per la lettura diverso così da poter successivamente utilizzare fseek 
	// e riportare l'I/O pointer all'inizio del file
	fr = fopen(filename, "rb");
	if(f == NULL){
		printf("Errore nell'apertura del file!\n");
		return -1;
	}
	FD_ZERO(&floodset);
	FD_ZERO(&floodread);
	if(neighbors[0].sd != 0){
		FD_SET(neighbors[0].sd, &floodset);
		if(neighbors[0].sd > floodmax) floodmax = neighbors[0].sd;
	}
	if(neighbors[1].sd != 0){
		FD_SET(neighbors[1].sd, &floodset);
		if(neighbors[1].sd > floodmax) floodmax = neighbors[1].sd;
	}
	while(1){
		timeout.tv_sec = 3 * numPeer; 
		timeout.tv_usec = 0;
		//aspetto un numero di secondi pari al numero di peer per una costante
		floodread = floodset;
		printf("Attendo per al più %d*3 secondi..(per tener conto delle sleep)\n", numPeer);
		ret = select(floodmax+1, &floodread, NULL, NULL, &timeout);
		if(ret > 0){
			for(i = 0; i <= floodmax; ++i){
				if(FD_ISSET(i, &floodread)){
					if(i == neighbors[0].sd)
						index = 0;
					else if(i == neighbors[1].sd)
						index = 1;
					else break;
					ret = recv(neighbors[index].sd, (void*)&lmsg, sizeof(uint16_t), 0);
					if(ret < sizeof(uint16_t)){
						perror("Errore");
						continue;
					}
					len = ntohs(lmsg);
					ret = recv(neighbors[index].sd, (void*)buffer, len, 0);
					if(ret < len){
						perror("Errore");
						continue;
					}
					buffer[len] = '\0';
					//se il neighbor mi ha inviato un messaggio propagato all'indietro
					//se quindi un peer ha a disposizione delle entries
					if(strncmp(buffer, "HAVE_ENTRIES", 12) == 0){
						sscanf(buffer, "HAVE_ENTRIES %s %d",IP,&port);
						printf("Rispondo al peer %s %d che vuole inviarmi delle entry..\n", IP, port);
						sleep(1);
						//Inizializzo una connessione col peer
						memset(&peer_addr, 0, sizeof(peer_addr));
						peer_addr.sin_family = AF_INET;
						peer_addr.sin_port = htons(port);
						inet_pton(AF_INET, IP, &peer_addr.sin_addr);
						peer_sd = socket(AF_INET, SOCK_STREAM, 0);
						if(peer_sd == -1){
							perror("Errore");
							goto fine;
						}
						ret = connect(peer_sd, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
						if(ret == -1){
							perror("Errore");
							goto fine;
						}
						strcpy(mex, "REQ_ENTRIES");
						len = strlen(mex);
						lmsg = htons(len);
						ret = send(peer_sd, (void*)&lmsg, sizeof(uint16_t), 0);
						if(ret < sizeof(uint16_t)){
							perror("Errore");
							continue;
						}
						ret = send(peer_sd, (void*)mex, len, 0);
						if(ret < len){
							perror("Errore");
							continue;
						}
						//ricevo le entry
						ret = recv(peer_sd, (void*)&lmsg, sizeof(uint16_t), 0);
						if(ret < sizeof(uint16_t)){
							perror("Errore");
							continue;
						}
						len = ntohs(lmsg);
						ret = recv(peer_sd, (void*)buffer, len, 0);
						if(ret < len){
							perror("Errore");
							continue;
						}
						close(peer_sd);
						printf("Il peer %s:%d ha delle entry che mi servono!\n", IP, port);
						sleep(1);
						printf("Ho ricevuto le seguenti entry:\n");
						buffer[len] = '\0';
						s = strtok(buffer, "\n");
						s = strtok(NULL,"\n");
						while(s	!= NULL){
							printf("%s\n", s);
							sscanf(s, "%s %d %c %d\n", indIP, &porta, &type, &quantity);
							while(fread(&fileEntry, sizeof(struct fileEntry), 1, fr)){
								if(fileEntry.port == porta && strcmp(fileEntry.IP,indIP) == 0 && fileEntry.type == type && fileEntry.quantity == quantity){
									// ho già la entry quindi non la memorizzo
									printf("Ho già questa entry che mi vuole dare il peer, non la memorizzo!\n");
									goto next;
								}
							}
							// altrimenti memorizzo la entry
							fileEntry.type = type;
							fileEntry.port = porta;
							fileEntry.quantity = quantity;
							strncpy(fileEntry.IP, indIP, 8);
							fwrite(&fileEntry, sizeof(struct fileEntry), 1, f);
							next: fseek(fr, SEEK_SET, 0); 
							s = strtok(NULL, "\n");
							fflush(f);
						}
						sleep(2);
						if(stat(filename, &statRes) < 0) printf("Errore!\n");
						numEntries = statRes.st_size / (2*sizeof(fileEntry));
						printf("Ho adesso disposizione %d peer entries e quel giorno erano collegati %d peers!\n", numEntries, dayPeers);
						sleep(1);
						if(numEntries == dayPeers){
							printf("Ho adesso tutti i dati!\nAspetto comunque nel caso qualche altro peer mi stia contattando e scarto la richiesta!\n");
							ended = 1;
						}
					}
				}
			}
		} else { 
			printf("Il timeout è scaduto e nessun altro peer mi ha contattato!\n");
			sleep(1);
fine:		fclose(f);
			fclose(fr);
			writePermissions(0, filename);
			if(ended == 1){
				printf("Procedo con il calcolo!\n");
				return 0;
			} else {
				printf("Non ho tutti i dati e quindi non posso calcolare il dato aggregato!\n");
				return -1;
			}
		}
	}
}

/* startFlooding: Funzione invocata quando si vuole iniziare un flooding
	Gestisce i giusti campi del contatore della query per realizzare il meccanismo di query ad orizzonte limitato
	Invia il flood ai suoi vicini e si mette in attesa di eventuali peer che hanno a disposizione qualche entry
*/

int startFlooding(char* day, char type, int dayPeers){
	int numPeer;
	struct peer* p;
	char mex[200];
	char name[100];
	char temp[30];
	char filename[50];
	int len, ret;
	uint16_t lmsg;
	int cont;

	sprintf(filename, "registers/%d/%s.bin", MyPort, day);
	//chiedo il numero di peer connessi attualmente al network così da gestire anche il caso di query ad orizzonte limitato
	numPeer = getNumPeer();
	printf("Ci sono attualmente %d peer nel network incluso me!\n", numPeer);
	//mi adatto in funzione del numero di peer connessi al network
	if(numPeer == 1){
		printf("Ci sono solo io nel network, non ha senso fare flooding!\n");
		return -1;
	} 
	numPeer--; //mi escludo
	if(numPeer > MAX_NUM_PEERS){
			// query ad orizzonte limitato
			printf("Ci sono più di %d peer nel network e quindi uso delle query ad orizzonte limitato!\n", MAX_NUM_PEERS);
			numPeer = MAX_NUM_PEERS;
	}
	cont = (int)(numPeer/2);
	if(cont == 0) cont = 1; // caso due peer
	sleep(1);
	// se non ho neighbors
	if(strcmp(neighbors[0].IP, "0.0.0.0") == 0 && neighbors[0].port == 0 &&
		strcmp(neighbors[1].IP, "0.0.0.0") == 0 && neighbors[1].port == 0)
		return -1;
	//prendo la lista di entry che mi mancano
	memset(temp, 0, sizeof(temp));
	memset(name, 0, sizeof(name));
	for(p = peerList; p != 0; p = p->next){
		sprintf(temp, " %s %d", p->IP, p->port);
		strcat(name, temp);
	}
	printf("Invio il flooding ai miei vicini...\n");
	if(strcmp(neighbors[0].IP, "0.0.0.0") != 0 && neighbors[0].port != 0){
		// invio il flooding al mio vicino 0
		printf("Invio il flooding al mio vicino %s:%d\n", neighbors[0].IP, neighbors[0].port);
		sprintf(mex, "FLOOD_FOR_ENTRIES\nday: %s\ncont: %d\ntype: %c\nentries:", day, cont, type);
		strcat(mex, name);
		len = strlen(mex);
		lmsg = htons(len);
		ret = send(neighbors[0].sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return -1;
		}
		ret = send(neighbors[0].sd, (void*)mex, len, 0);
		if(ret < len){
			perror("Errore");
			return -1;
		}
	}
	sleep(2);
	if(strcmp(neighbors[1].IP, "0.0.0.0") != 0 && neighbors[1].port != 0){
		// invio il flooding al mio vicino 1
		printf("Invio il flooding al mio vicino %s:%d\n", neighbors[1].IP, neighbors[1].port);
		if(numPeer % 2 == 1 && numPeer > 1) // se il numero di peer è dispari
			sprintf(mex, "FLOOD_FOR_ENTRIES\nday: %s\ncont: %d\ntype: %c\nentries:", day, cont+1, type);
		else sprintf(mex, "FLOOD_FOR_ENTRIES\nday: %s\ncont: %d\ntype: %c\nentries:", day, cont, type);
		strcat(mex, name);
		len = strlen(mex);
		lmsg = htons(len);
		ret = send(neighbors[1].sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return -1;
		}
		ret = send(neighbors[1].sd, (void*)mex, len, 0);
		if(ret < len){
			perror("Errore");
			return -1;
		}
	}
	// mi metto in attesa di eventuali peer che hanno a disposizione qualche entry
	ret = waitAfterFlooding(numPeer, filename, dayPeers);
	return ret;
}

/* getFlood: Funzione invocata quando si riceve un flooding
	Si determina il numero di entry tra quelle richieste che ho a disposizione
	Se ne ho qualcuna a disposizione invio un messaggio a ritroso e fornisco le entry al peer
	Dopodichè se ho inviato tutte le entry al peer smette di propagare al flooding
	Se invece non ho inviato tutte le entry al peer propago il flooding all'altro vicino a meno 
	che non siamo arrivati alla fine dell'orizzonte limitato */

void getFlood(int index){
	int ret, len;
	uint16_t lmsg;
	int new_sd;
	char* s;
	char* w;
	int i, numBegin, numEnd;
	char type;
	int contatore;
	struct peer* r;
	struct peer* q;
	struct peer* p;
	struct peer* tmplist;
	struct sockaddr_in src_addr;
	socklen_t addrlen;
	char day[9];
	char file[70];
	char mex[200];
	char name[40];
	FILE* f;
	printf("\nHo ricevuto un flooding da %s:%d\n", neighbors[index].IP, neighbors[index].port);
	sleep(1);
	sscanf(buffer, "FLOOD_FOR_ENTRIES\nday: %s\ncont: %d\ntype: %c\n", day, &contatore, &type);
	printf("Il flooding è relativo al giorno %s per il tipo %c con valore del contatore pari a %d\n",day, type, contatore);
	sleep(1);
	s = strtok(buffer, "\n");
	s = strtok(NULL,"\n");
	s = strtok(NULL,"\n");
	s = strtok(NULL,"\n");
	s = strtok(NULL,"\n");
	tmplist = 0;
	peerList = 0;
	numBegin = 0;
	numEnd = 0;
	w = strtok(s," ");
	w = strtok(NULL, " ");
	// determino le entry richieste
	for(i = 0; w != NULL; i++, numBegin++){
		r = malloc(sizeof(struct peer));
		q = malloc(sizeof(struct peer));
		strcpy(r->IP,w);
		w = strtok(NULL," ");
		r->port = atoi(w);
		strcpy(q->IP, r->IP);
		q->port = r->port;
		r->next = peerList;
		q->next = tmplist;
		tmplist = q;
		peerList = r;
		w = strtok(NULL," ");
  	}
	printf("Il flooding richiede le seguenti entry:\n");
	for(p = peerList; p != 0; p = p->next)
		printf("%s %d\n", p->IP, p->port);
	sleep(1);
	// determino le entry che non ho a disposizione (in tmplist)
	sprintf(file, "registers/%d/%s.bin", MyPort, day);
	numEnd = discriminate(file, &tmplist);
	printf("Il peer richiede %d entry, delle quali ne ho %d\n", numBegin, numBegin-numEnd);
	sleep(1);
	if(numEnd == numBegin)
		printf("Non ho nessuna entry che serve al peer!\n");
	else if(numEnd < numBegin) 
		printf("Ho qualche entry richieste dal peer ma non tutte!\n");

	if(numEnd < numBegin){
		// ho qualche entry che serve al peer
		printf("Invio un messaggio a ritroso così che il peer possa sapere che ho qualche entry che gli serve!\n");
		sprintf(mex, "HAVE_ENTRIES %s %d","127.0.0.1",MyPort);
		len = strlen(mex);
		lmsg = htons(len);
		// invio un messaggio a ritroso
		ret = send(neighbors[index].sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return;
		}
		ret = send(neighbors[index].sd, (void*)mex, len, 0);
		if(ret < len){
			perror("Errore");
			return;
		}
		strcpy(mex, "REPLY_ENTRIES\n");
		// determino le entry che ho a disposizione
		printf("Le entry che ho a disposizione sono:\n");
		f = fopen(file, "rb");
		if(f == NULL){
			printf("Errore nell'apertura del file!\n");
			return;
		}
		for(p = peerList; p != 0; p = p->next){
			while(fread(&fileEntry, sizeof(struct fileEntry), 1, f)){
				if(fileEntry.port == p->port && strcmp(fileEntry.IP,p->IP) == 0){
					printf("%s %d %c %d\n", fileEntry.IP, fileEntry.port, fileEntry.type, fileEntry.quantity);
					sprintf(name, "%s %d %c %d\n", fileEntry.IP, fileEntry.port, fileEntry.type, fileEntry.quantity);
					strcat(mex, name);
				}
			}
			fseek(f, SEEK_SET, 0);
		}
		sleep(1);
		// attendo che il peer inizializzi una connessione
		printf("Attendo che il peer mi contatti..\n");
		addrlen = sizeof(src_addr);
		new_sd = accept(tcpsd, (struct sockaddr*)&src_addr, &addrlen);
		if(new_sd == -1){
			perror("Errore");
			return;
		}
		printf("Il peer mi ha contattato!\n");
		ret = recv(new_sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
				perror("Errore");
				return;
		}
		len = ntohs(lmsg);
		ret = recv(new_sd, (void*)buffer, len, 0);
		if(ret < len){
			perror("Errore");
			return;
		}
		buffer[len] = '\0';
		if(strncmp(buffer, "REQ_ENTRIES", 11) == 0){
			len = strlen(mex);
			lmsg = htons(len);
			// invio le entry al peer
			ret = send(new_sd, (void*)&lmsg, sizeof(uint16_t), 0);
			if(ret < sizeof(uint16_t)){
				perror("Errore");
				return;
			}
			ret = send(new_sd, (void*)mex, len, 0);
			if(ret < len){
				perror("Errore");
				return;
			}
		}
		printf("Ho inviato le entry al peer!\n");
		close(new_sd);
		sleep(1);
	}
	if(numEnd == 0){
		// se ho inviato tutte le entry al peer smetto di propagare il flooding
		printf("Ho inviato le entry che servono al peer!\nSmetto di propagare il flooding!\n");
	} else {
		//non ho entry o ne ho alcune quindi propago comunque la query
		contatore--;
		if(contatore == 0){
			//fine orizzonte limitato e quindi non propago la query 
			printf("Ho decrementato il contatore e si è azzerato!\nSiamo giunti alla fine dell'orizzonte limitato! Non propago più la richiesta!\n");
		} else {
			printf("Propago il flooding!\n");
			//ricompongo il messaggio togliendo le entry che ho io
			sprintf(mex, "FLOOD_FOR_ENTRIES\nday: %s\ncont: %d\ntype: %c\nentries:", day, contatore, type);
			//inserisco le entry che non ho a disposizione nel messaggio
			for(p = tmplist; p != 0; p = p->next){
				sprintf(name, " %s %d", p->IP, p->port);
				strcat(mex, name);
			}
			if(index == 0){ //se è il neighbor 0 propago verso il neighbor 1
				if(strcmp(neighbors[1].IP, "0.0.0.0") != 0 && neighbors[1].port != 0){ //se ho un neighbor 1
					printf("Invio il flooding al mio vicino %s:%d\n", neighbors[1].IP, neighbors[1].port);
					len = strlen(mex);
					lmsg = htons(len);
					ret = send(neighbors[1].sd, (void*)&lmsg, sizeof(uint16_t), 0);
					if(ret < sizeof(uint16_t)){
						perror("Errore");
						return;
					}
					ret = send(neighbors[1].sd, (void*)mex, len, 0);
					if(ret < len){
						perror("Errore");
						return;
					}
				}
			} else if(index == 1){//se è il neighbor 1 propago verso il neighbor 0
				if(strcmp(neighbors[0].IP, "0.0.0.0") != 0 && neighbors[0].port != 0){ //se ho un neighbor 0
					printf("Invio il flooding al mio vicino %s:%d\n", neighbors[0].IP, neighbors[0].port);
					len = strlen(mex);
					lmsg = htons(len);
					ret = send(neighbors[0].sd, (void*)&lmsg, sizeof(uint16_t), 0);
					if(ret < sizeof(uint16_t)){
						perror("Errore");
						return;
					}
					ret = send(neighbors[0].sd, (void*)mex, len, 0);
					if(ret < len){
						perror("Errore");
						return;
					}
				}
			}
		}
	}
}

/* advertiseGet: Funzione invocata quando si vuole avvertire il DS_server dell'inizio di un flooding
	o della fine di un flooding 
	Se voglio iniziare un flooding, così da bloccare le funzioni di boot, stop e altri flooding, prima 
	devo ricevere conferma dal DS_server (può infatti essere in corso un altro flooding)  */

int advertiseGet(int flag){
	int len, ret;
	uint16_t lmsg;
	char mex[50];
	sprintf(mex, "ADVGET %d", flag);
	len = strlen(mex);
	lmsg = htons(len);
	//Informo dell'inizio o della fine del flood in funzione del flag
	ret = send(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return -1;
	}
	ret = send(DSsd, (void*)mex, len, 0);
	if(ret < len){
		perror("Errore");
		return -1;
	}
	//Ricevo conferma
	ret = recv(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return -1;
	}
	len = ntohs(lmsg);
	ret = recv(DSsd, (void*)buffer, len, 0);;
	if(ret < len){
		perror("Errore");
		return -1;
	}
	sscanf(buffer, "ADVGET %d", &flag);
	if(flag == 0){
		//se posso iniziare il flooding
		advertisedGet = 1;
		return 0;
	} else if(flag == 1){ 
		//se vi è già un flooding in corso
		return -1;
	}
	return 0;
}

/* getList: Funzione invocata quando si vuole ottenere la lista dei peer connessi in un dato giorno
   Se nessun peer era connesso quel giorno restituisce 0,
   altrimenti riempe la lista peerList e restituisce il numero di peer connessi quel giorno */

int getList(char* dateName){
	int len, ret;
	uint16_t lmsg;
	char mex[50];
	int numPeer;
	char* s;
	struct peer* p;
	int flag;
	struct peer* r;
	strcpy(mex, "LIST\nday: ");
	strncat(mex, dateName, 8);
	len = strlen(mex);
	lmsg = htons(len);
	//richiedo la lista
	ret = send(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return -1;
	}
	ret = send(DSsd, (void*)mex, len, 0);
	if(ret < len){
		perror("Errore");
		return -1;
	}
	//Ricevo conferma
	ret = recv(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return -1;
	}
	len = ntohs(lmsg);
	ret = recv(DSsd, (void*)buffer, len, 0);;
	if(ret < len){
		perror("Errore");
		return -1;
	}
	buffer[len] = '\0';
	sscanf(buffer, "LIST %d\n", &flag);
	//se nessun peer era connesso quel giorno
	if(flag == 1)
		return 0;
	s = buffer;
	s = strtok(buffer, "\n");
	s = strtok(NULL,"\n");
	// ottenggo i peer connessi quel giorno e creo la lista
	for(numPeer = 0; s != NULL; numPeer++){
		r = malloc(sizeof(struct peer));
		sscanf(s, "%s %d\n", r->IP, &r->port);
		r->next = peerList;
		peerList = r;
		s = strtok(NULL,"\n");
  	}
	if(peerList != 0)
		printf("Il DS_Server mi ha inviato la lista relativa a quel giorno così composta:\n");
	for(p = peerList; p != 0; p = p->next)
		printf("%s:%d\n", p->IP, p->port);
	sleep(1);
	return numPeer; //restituisco il numero di peer connessi quel giorno
}

/* replyData: Funzione invocata alla ricezione di un REQ_DATA da un vicino
   Verifica se si ha a disposizione il dato aggregato e se sì, questo viene inviato,
   altrimenti si setta il codice di stato indicando della mancata presenza del dato aggregato */

void replyData(int index){
	char mex[200];
	char name[30];
	char operation;
	int len;
	uint16_t lmsg;
	char lower[9];
	char upper[9];
	char filename[50];
	char type;
	FILE* faggr;
	int available = 0;
	sscanf(buffer, "REQ_DATA\nlower: %s\nupper: %s\ntype: %c\noperation: %c", lower, upper, &type, &operation);
	printf("Ho ricevuto una richiesta REQ_DATA dal peer %s:%d per il dato aggregato: %s %s %c\n", neighbors[index].IP, neighbors[index].port, lower, upper, type);
	if(operation == 'T'){
		sprintf(filename, "registers/%d/sum.bin", MyPort);
		sprintf(mex, "REPLY_DATA");
		faggr = fopen(filename, "rb");
		if(faggr == NULL)
			goto fine;
		while(fread(&sumEntry, sizeof(sumEntry), 1, faggr)){
			if(sumEntry.type == type && strcmp(lower,sumEntry.lower) == 0 && strcmp(upper,sumEntry.upper) == 0){
				printf("Ho il totale già calcolato e vale %d\n",sumEntry.quantity);
				sleep(1);
				strcat(mex, " 0");
				sprintf(name, "\nvalue: %d", sumEntry.quantity);
				strcat(mex, name);
				available = 1;
				break;
			}
		}
		fclose(faggr);
	} else if(operation == 'V'){
		sprintf(filename, "registers/%d/diff.bin", MyPort);
		sprintf(mex, "REPLY_DATA");
		faggr = fopen(filename, "rb");
		if(faggr == NULL)
			goto fine;
		while(fread(&diffEntry, sizeof(diffEntry), 1, faggr)){
			if(diffEntry.type == type && strcmp(lower,diffEntry.lower) == 0 && strcmp(upper,diffEntry.upper) == 0){
				strcat(mex, " 0");
				printf("Ho il totale già calcolato e vale %s\n",diffEntry.quantity);
				sleep(1);
				sprintf(name, "\nvalue: %s", diffEntry.quantity);
				//printf("%s\n", name);
				strcat(mex, name);
				available = 1;
				break;
			}
		}
	fclose(faggr);
	}
fine: if(available == 0){
			strcat(mex, " 1");
			printf("Non ho a disposizione il dato!\n");
		}
	printf("Invio la risposta REPLY_DATA al mio vicino %s:%d\n", neighbors[index].IP, neighbors[index].port);
	len = strlen(mex);
	lmsg = htons(len);
	send(neighbors[index].sd, (void*)&lmsg, sizeof(uint16_t), 0);
	send(neighbors[index].sd, (void*)mex, len, 0);
	sleep(1);
}


/* reqData: Richiede il dato aggregato (totale o variazione) ai neighbors (se presenti)
	e restituisce il risultato qualora questi lo abbiano a disposizione */

int reqData(char* lower, char* upper, char type, char operation){
	char mex[200];
	int len, ret;
	uint16_t lmsg;
	char diff[50];
	int flag;
	sprintf(mex, "REQ_DATA\nlower: %s\nupper: %s\ntype: %c\noperation: %c", lower, upper, type, operation);
	//se non ho nessun vicino
	if(strcmp(neighbors[0].IP, "0.0.0.0") == 0 && neighbors[0].port == 0 &&
		strcmp(neighbors[1].IP, "0.0.0.0") == 0 && neighbors[1].port == 0){
		printf("Non ho nessun vicino!\n");
		return -1;
	}
	len = strlen(mex);
	lmsg = htons(len);
	//se ho un vicino
	if(strcmp(neighbors[0].IP, "0.0.0.0") != 0 && neighbors[0].port != 0){
		printf("Invio la richiesta al mio vicino %s:%d\n", neighbors[0].IP, neighbors[0].port);
		ret = send(neighbors[0].sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return -1;
		}
		ret = send(neighbors[0].sd, (void*)mex, len, 0);
		if(ret < len){
			perror("Errore");
			return -1;
		}
		ret = recv(neighbors[0].sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return -1;
		}
		len = ntohs(lmsg);
		//ricevo il messaggio
		ret = recv(neighbors[0].sd, (void*)buffer, len, 0);
		if(ret < len){
			perror("Errore");
			return -1;
		}
		buffer[len] = '\0';
		sleep(1);
		printf("Ho ricevuto risposta dal vicino!\n");
		if(strlen(buffer) > 5){
			if(operation == 'T'){
				sscanf(buffer, "REPLY_DATA %d",&flag);
				if(flag == 1){
					printf("Il vicino non ha il dato calcolato!\n");
					sleep(1);
				} else {
					sscanf(buffer, "REPLY_DATA %d\nvalue: %d", &flag, &ret);
					printf("Il vicino mi ha dato il dato già calcolato ed è %d\n", ret);
					sleep(1);
					return ret;
				}
			} else if(operation == 'V'){
				sscanf(buffer, "REPLY_DATA %d",&flag);
				if(flag == 1){
					printf("Il vicino non ha il dato calcolato!\n");
					sleep(1);
				} else {
					sscanf(buffer, "REPLY_DATA %d\nvalue: %s", &flag, diff);
					printf("Il vicino mi ha dato il dato già calcolato ed è %s\n", diff);
					strcpy(diffEntry.quantity, diff);
					return 0;
				}
			}
		}
	}
	if(strcmp(neighbors[1].IP, "0.0.0.0") != 0 && neighbors[1].port != 0){
		printf("Invio la richiesta al mio vicino %s:%d\n", neighbors[1].IP, neighbors[1].port);
		len = strlen(mex);
		lmsg = htons(len);
		ret = send(neighbors[1].sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return -1;
		}
		ret = send(neighbors[1].sd, (void*)mex, len, 0);
		if(ret < len){
			perror("Errore");
			return -1;
		}
		ret = recv(neighbors[1].sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return -1;
		}
		len = ntohs(lmsg);
		//ricevo il messaggio
		ret = recv(neighbors[1].sd, (void*)buffer, len, 0);
		if(ret < len){
			perror("Errore");
			return -1;
		}
		buffer[len] = '\0';
		printf("Ho ricevuto risposta dal vicino!\n");
		sscanf(buffer, "REPLY_DATA %d",&flag);
		if(operation == 'T'){
			if(flag == 1){
				printf("Il vicino non ha il dato calcolato!\n");
				sleep(1);
			} else {
				sscanf(buffer, "REPLY_DATA %d\nvalue: %d", &flag, &ret);
				printf("Il vicino mi ha dato il dato già calcolato ed è %d\n", ret);
				sleep(1);
				return ret;
			}
		} else if(operation == 'V'){
			sscanf(buffer, "REPLY_DATA %d",&flag);
			if(flag == 1){
				printf("Il vicino non ha il dato calcolato!\n");
				sleep(1);
			} else {
				sscanf(buffer, "REPLY_DATA %d\nvalue: %s", &flag, diff);
				printf("Il vicino mi ha dato il dato già calcolato ed è %s\n", diff);
				sleep(1);
				strcpy(diffEntry.quantity, diff);
				return 0;
			}
		}
		printf("Il vicino non ha il dato calcolato!\n");
	}
	return -1; // se sono qui i vicini non hanno il dato
}

/* totalDay: Effettua il calcolo sommando le entry di quel tipo presenti sul file specificato*/

int totalDay(char* filename, char type){
	int total = 0;
	FILE* f;
	f = fopen(filename, "rb");
	if(f == NULL){
		printf("Impossibile aprire il file di oggi!\n");
		return 0;
	}
	while(fread(&fileEntry, sizeof(fileEntry), 1, f))
		if(fileEntry.type == type)
			total += fileEntry.quantity;
	return total;
}

/* calculateTotal: funzione invocata quando si chiede il calcolo del totale di un dato giorno
   Ottiene la lista dei peer connessi quel giorno dal DS_server e se nessuno era connesso il totale sarà 0
   Se qualcuno era connesso verifica se ha a disposizione tutte le entry e se si calcola il totale
   Altrimenti se non ha a disposizione tutte le entry effettua un flooding e se riesce a ottenere tutte le entry
   per quel dato giorno calcola poi il totale, altrimenti l'aggregazione non viene calcolata  */

int calculateTotal(char* currentDay, char type){
	int day, ret, size, numEntries;
	char filename[50];
	int numPeer;
	struct stat info;
	peerList = 0;
	numPeer = getList(currentDay); //ottengo la lista dei peer connessi quel giorno
	if(numPeer != 0){ //se almeno un peer era connesso quel giorno
		sleep(2);
		sprintf(filename, "registers/%d/%s.bin", MyPort, currentDay);
		if(access(filename,F_OK)==0){ // anche questo peer era connesso quel giorno quindi ha qualche entry relativa ad esso (sicuramente almeno la propria)
			printf("Ho almeno un'entrata per quel giorno! Ero connesso anch'io!\n");
			sleep(2);
			ret = stat(filename, &info);
			size = info.st_size;
			numEntries = size / (2*sizeof(fileEntry));
			printf("Ho a disposizione %d peer entries e quel giorno erano collegati %d peers!\n", numEntries, numPeer);
			sleep(2);
			if(numEntries == numPeer){
				printf("Ho tutte le entry a disposizione, effettuo il calcolo!\n");
				sleep(2);
				day = totalDay(filename, type);
			} else {
				printf("Non ho tutte le entry a disposizione!\n");
				sleep(1);
				ret = discriminate(filename, &peerList);
				printf("Mi mancano %d entry per calcolare il dato...\n", ret);
				sleep(1);
				printf("Procedo col flooding!\n");
				ret = startFlooding(currentDay, type, numPeer);
				if(ret == -1){
					printf("Non sono riuscito ad ottenere tutte le entry relative a quel giorno, non posso calcolare il dato aggregato!\n");
					return -1;
				} 
				printf("Sono riuscito ad ottenere tutte le entry relative a quel giorno!\n");
				sleep(1);
				day = totalDay(filename, type);
			}
		} else {
			printf("Non ho nessuna entry per quel giorno, ho bisogno di tutte le entry per calcolare il dato...\n");
			sleep(1);
			printf("Procedo col flooding!\n");
			ret = startFlooding(currentDay, type, numPeer);
			if(ret == -1){
				printf("Non sono riuscito ad ottenere tutte le entry relative a quel giorno, non posso calcolare il totale!\n");
				return -1;
			} 
			printf("Sono riuscito ad ottenere tutte le entry relative a quel giorno!\n");
			day = totalDay(filename, type);
		}
	} else printf("Nessun peer era connesso quel giorno!\n"); //day rimarrà a 0
	return day;
}


/* total: funzione invocata quando si chiede il calcolo di un'aggregazione totale
   La verifica che l'aggregazione non sia già stata calcolata è stata fatta nella funzione get così da poter sfruttare questa funzione
   nel calcolo della variazione
   Se non ha a disposizione già il dato, informa il DS_server che sta per iniziare il calcolo di un'aggregazione e
   chiede ai vicini se hanno calcolato l'aggregazione, qualora sia connesso, e se la hanno disposizione salva il dato e termina
   Altrimenti calcola i totali relativi all'intervallo e calcola mano a mano la somma
   Informa il DS_Server che ha finito col calcolo dell'aggregazione
   Infine memorizza il risultato dell'aggregazione per soddisfare immediatamente successive richieste nella funzione get */

int total(char* lower, char* upper, char type, int variation){
	FILE* ftotal;
	int sum, day, ret;
	char currentDay[20];
	struct tm tm_time;
	time_t now;
	char filename[50];
	printf("L'intervallo selezionato per il calcolo del totale è %s - %s\n", lower, upper);

	//controllo se ho già calcolato il dato aggregato
	sprintf(filename, "registers/%d/total.bin", MyPort);
	writePermissions(1, filename);
	ftotal = fopen(filename, "ab+");
	if(ftotal == NULL){
		printf("Impossibile aprire il file dei totali!\n");
		writePermissions(0, filename);
		return -1;
	}
	now = time(NULL);
	tm_time = *localtime(&now);
	sum = 0;

	// Si avverte il DS_server del calcolo dell'aggregazione
	if(connected){
		if(advertisedGet == 0){
			printf("Avverto il DS_server che sto per iniziare il calcolo di un'aggregazione!\n");
			ret = advertiseGet(1);
			if(ret == -1){
				printf("Non posso iniziare con l'operazione, c'è già il calcolo di un'aggregazione in corso!\n");
				return -1;
			} 
		}
		sleep(1);
	}
	// se sono connesso chiedo ai vicini se hanno a disposizione il dato aggregato
	if(connected && !variation){ 
		printf("Chiedo ai vicini se hanno a disposizione il dato già calcolato!\n");
		ret = reqData(lower, upper, type, 'T');
		if(ret == -1)
			printf("Entrambi i vicini non hanno a disposizione il dato!\n");
		else {
			sum = ret;
			goto end;
		} 
		sleep(1);
	}
	strcpy(currentDay,lower);
	while (1){
		day = 0;
		printf("------------------------ %s ------------------------\n", currentDay);
		printf("Calcolo il totale del giorno %s\n", currentDay);
		sleep(1);
		//controllo se ho già calcolato il totale di quel giorno
		while(fread(&totalEntry, sizeof(totalEntry), 1, ftotal)){
			if(totalEntry.type == type && strcmp(currentDay,totalEntry.date) == 0){
				day = totalEntry.quantity;
				printf("Ho il totale già calcolato per questo giorno!\n");
				sleep(1);
				goto next;
			}
		} 
		if(!DSsd){ 
			//se non sono connesso mi baso solo sul file degli aggregati e su quello dei totali locali
			//non potrò infatti chiedere alcuna informazione al DS_server
			if(day == 0){
				printf("Non ho a disposizione i dati necessari per svolgere questa operazione offline! Prova a connetterti ad un DS_server!\n");
				sum = -1;
				goto end;
			}
		} else { //sono connesso
			if(day == 0) //non ho a disposizione questa informazione nel file dei totali
				day = calculateTotal(currentDay, type); // procedo al calcolo relativo al giorno currentDay
			if(day == -1){
				sum = -1;
				goto end;
			}
		}
		// memorizzo il risultato nel file dei totali (se siamo arrivati qui non è presente nel file dei totali)
		totalEntry.type = type;
		strcpy(totalEntry.date, currentDay);
		totalEntry.quantity = day;
		fwrite(&totalEntry, sizeof(totalEntry), 1, ftotal);
next:	fseek(ftotal, SEEK_SET, 0);
		printf("Il totale di questo giorno è %d\n", day);
		sleep(1);
		sum += day;
		if(strcmp(currentDay, upper) == 0)
			break;
		sscanf(currentDay,"%02d%02d%d",&tm_time.tm_mday,&tm_time.tm_mon,&tm_time.tm_year);
		addDay(&tm_time, +1);
		sprintf(currentDay,"%02d%02d%d", tm_time.tm_mday, tm_time.tm_mon, tm_time.tm_year);
	}
	// salvo il dato aggregato nel file nel caso dovesse riservirmi
end:
	fclose(ftotal);
	writePermissions(0, filename);
	// informo il DS_server che ho terminato il calcolo dell'agregazione così da fargli riabilitare le funzioni disattivate
	if(advertisedGet == 1){
		printf("Informo il DS_server che può adesso accettare calcoli di aggregazione!\n");
		advertiseGet(0);
		advertisedGet = 0;
	}
	sleep(1);
	return sum;
}

/* variation: funzione invocata quando si chiede il calcolo di un'aggregazione variazione
   Verifica se l'aggregazione non sia già stata calcolata, se sì la mostra a video direttamente e termina
   Altrimenti, verifica innanzitutto se può procedere col calcolo dell'aggregazione e come prima cosa
   chiede ai vicini se hanno calcolato l'aggregazione, qualora sia connesso, e se sì allora salva il dato e termina
   Altrimenti calcola i totali relativi all'intervallo selezionato e poi calcola le differenze
   Infine memorizza il risultato dell'aggregazione per soddisfare immediatamente successive richieste */

void variation(char* lower, char* upper, char type){
	FILE* faggr;
	FILE* ftotal;
	int diff, day1, day2, ret;
	char currentDay[30];
	char nextDay[30];
	struct tm tm_time;
	char filename[50];
	time_t now;
	char str[5];
	
	// caso differenza nulla implicita
	if(strcmp(lower, upper) == 0){
		printf("Mi stai chiedendo la differenza tra gli stessi numeri di un giorno, questa vale sempre 0!\n");
		return;
	}

	// controllo se ho già calcolato il dato aggregato
	sprintf(filename, "registers/%d/diff.bin", MyPort);
	writePermissions(1, filename);
	faggr = fopen(filename, "ab+");
	if(faggr == NULL){
		printf("Impossibile aprire il file degli aggregati!\n");
		writePermissions(0, filename);
		return;
	}
	while(fread(&diffEntry, sizeof(diffEntry), 1, faggr)){
		if(diffEntry.type == type && strcmp(lower,diffEntry.lower) == 0 && strcmp(upper,diffEntry.upper) == 0){
			printf("Ho le differenze già calcolate e valgono: %s\n",diffEntry.quantity);
			writePermissions(0, filename);
			return;
		}
	}
	printf("Non ho il dato aggregato già calcolato! Procedo con il calcolo..\n");
	sprintf(filename, "registers/%d/total.bin", MyPort);
	ftotal = fopen(filename, "rb");
	if(ftotal == NULL){
		printf("Impossibile aprire il file degli aggregati!\n");
		return;
	}
	sleep(1);
	now = time(NULL);
	tm_time = *localtime(&now);
	strcpy(currentDay,lower);
	if(connected){
		if(advertisedGet == 0){
			printf("Informo il DS server che sto per iniziare a calcolare un dato aggregato!\n");
			ret = advertiseGet(1);
			if(ret == -1){
				printf("Non posso iniziare con l'operazione, riprova più tardi!\n");
				return;
			} 
		}
	}
	sleep(1);
	if(connected){ 
		// se sono connesso chiedo ai vicini se hanno a disposizione il dato aggregato
		printf("Chiedo ai vicini se hanno a disposizione il dato già calcolato!\n");
		ret = reqData(lower, upper, type,'V');
		if(ret == -1)
			printf("I vicini non hanno a disposizione il dato!\n");
		else 
			goto save;
	}
	sleep(1);
	// calcolo i totali di questo intervallo così da poter calcolare le variazioni
	ret = total(lower, upper, type, 1);
	if(ret == -1){
		return;
	}
	memset(diffEntry.quantity, 0, sizeof(diffEntry.quantity));
	while (1){
		sscanf(currentDay,"%02d%02d%d",&tm_time.tm_mday,&tm_time.tm_mon,&tm_time.tm_year);
		addDay(&tm_time, 1);
		sprintf(nextDay,"%02d%02d%d", tm_time.tm_mday, tm_time.tm_mon, tm_time.tm_year);
		printf("------------------------- %s : %s -------------------------\n", currentDay, nextDay);
		// prendo i totali dei due giorni successivi e ne calcolo la differenza
		while(fread(&totalEntry, sizeof(totalEntry), 1, ftotal)){
			if(totalEntry.type == type && strcmp(currentDay,totalEntry.date) == 0){
				day1 = totalEntry.quantity;
			}
			else if(totalEntry.type == type && strcmp(nextDay,totalEntry.date) == 0){
				day2 = totalEntry.quantity;
			}
		}
		fseek(ftotal, SEEK_SET, 0);
		diff = day2 - day1;
	    printf("La differenza tra i due giorni vale %d\n", diff);	
		sleep(1);
		sprintf(str, "%d", diff); //vedi perche non va e prova differenza da 27 a 31
		strcat(diffEntry.quantity, str);
		// mi fermo quando arrivo al limite superiore
		if(strcmp(nextDay, upper) == 0)
			break;
		strcat(diffEntry.quantity, "/");
		sscanf(currentDay,"%02d%02d%d",&tm_time.tm_mday,&tm_time.tm_mon,&tm_time.tm_year);
		addDay(&tm_time, +1);
		sprintf(currentDay,"%02d%02d%d", tm_time.tm_mday, tm_time.tm_mon, tm_time.tm_year);
	}
save:
	if(advertisedGet == 1){
		printf("Informo il DS_server che può adesso accettare calcoli di aggregazione!\n");
		advertiseGet(0);
		advertisedGet = 0;
	}
	// memorizzo il dato aggregato per eventuali successive richieste
	printf("Memorizzo il dato calcolato nel caso potesse servirmi successivamente!\n");
	diffEntry.type = type;
	strncpy(diffEntry.lower, lower, 8);
	strncpy(diffEntry.upper, upper, 8);
	fwrite(&diffEntry, sizeof(diffEntry), 1, faggr);
	fclose(faggr);
	sprintf(filename, "registers/%d/diff.bin", MyPort);
	writePermissions(0, filename);
	sleep(1);
}

/* get: funzione invocata quando si digita il comando get
   Effettua i controlli sui parametri e in particolare verifica che le date riguardino solo registri chiusi se 
   specificate (includendo tra i registri chiusi quello di oggi se siamo dopo le 18)
   Invoca le funzioni per il calcolo di lower e upper bound qualora si usi * o qualora non si specifichi un periodo
   Invoca rispettivamente total o variation in funzione dell'aggregazione richiesta */

void get(char* aggr, char type, char* period){
	int sum;
	FILE* faggr;
	char lower[12];
	char upper[12];
	struct tm time_tm, time_now, time_x;
	struct tm time_lower, time_upper;
	time_t now;
	char* s;
	char filename[50];
	now = time(NULL);
	time_now = *localtime(&now);
	// controllo il formato dei parametri
	if(type != 'T' && type != 'N'){
		printf("Formato del tipo non valido, riprova!\n");
		return;
	}
	if(strcmp(aggr, "totale") != 0 && strcmp(aggr, "variazione") != 0){
		printf("Formato di aggr non valido, riprova!\n");
		return;
	}
	time_now.tm_mon += 1;
	time_now.tm_year += 1900;
	if(period != NULL){
		s = strtok(period, "-");
		if(s == NULL){
			printf("Data inserita non valida, riprova!\n");
			return;
		}
		strcpy(lower, s);
		s = strtok(NULL, "-");
		if(s == NULL){
			printf("Data inserita non valida, riprova!\n");
			return;
		}
		strcpy(upper,s);
	}
	// se si è specificato lower e upper bound devono essere consistenti in termine di ordine
	if(period != NULL && strcmp(lower, "*") != 0 && strcmp(upper, "*") != 0){
		sscanf(lower,"%02d:%02d:%d",&time_lower.tm_mday,&time_lower.tm_mon,&time_lower.tm_year);
		sscanf(upper,"%02d:%02d:%d",&time_upper.tm_mday,&time_upper.tm_mon,&time_upper.tm_year);
		time_x = getFirstDate(time_lower,time_upper);
		if(!(time_x.tm_mday == time_lower.tm_mday && time_x.tm_mon == time_lower.tm_mon && time_x.tm_year == time_lower.tm_year) && (time_lower.tm_mday != time_upper.tm_mday || time_lower.tm_mon != time_upper.tm_mon || time_lower.tm_year != time_upper.tm_year)){
			printf("Hai specificato una seconda data inferiore alla prima!\n");
			return;
		}
	}

	// se non è stato specificato il lower bound determino il valore del lower bound
	if(period == NULL || strcmp(lower, "*") == 0){
		time_tm = getMinRegister();
		if(time_tm.tm_mon == 0 && time_tm.tm_mday == 0 && time_tm.tm_year == 0){
			// qualora non ci siano registri chiusi
			printf("Non ho a disposizione nessun registro chiuso disponibile!\n");
			return;
		}
		sprintf(lower,"%02d%02d%d",time_tm.tm_mday,time_tm.tm_mon,time_tm.tm_year);
		printf("Ho preso come lower bound la più vecchia data per cui ho un registro chiuso: \n%02d:%02d:%d\n",time_tm.tm_mday,time_tm.tm_mon,time_tm.tm_year);
		sleep(1);
	} else {
		// se è stato specificato il lower bound verifico che non includa registri aperti o successivi a quello di oggi
		sscanf(lower,"%02d:%02d:%d",&time_tm.tm_mday,&time_tm.tm_mon,&time_tm.tm_year);
		sprintf(lower,"%02d%02d%d", time_tm.tm_mday, time_tm.tm_mon, time_tm.tm_year);
		// se siamo dopo le 18 può includere il registro di oggi
		if(time_now.tm_hour >= TIMEOUT_HOUR){
			time_x = getLastDate(time_tm,time_now);
			if(!(time_x.tm_mday == time_now.tm_mday && time_x.tm_mon == time_now.tm_mon && time_x.tm_year == time_now.tm_year)){
				printf("Hai selezionato una data che fa da lower bound non valida!\n");
				return;
			}
		} else {
			// prima delle 18 non può includere il registro di oggi
			addDay(&time_now, -1);
			time_x = getLastDate(time_tm,time_now);
			if(!(time_x.tm_mday == time_now.tm_mday && time_x.tm_mon == time_now.tm_mon && time_x.tm_year == time_now.tm_year)){
				printf("Hai selezionato una data che fa da lower bound non valida!\n");
				return;
			}
			addDay(&time_now, +1);
		}
	}

	// se non è stato specificato l'upper bound
	if(period == NULL || strcmp(upper, "*") == 0){
		time_tm = getMaxRegister();
		// qualora non ci siano registri chiusi
		if(time_tm.tm_mon == 0 && time_tm.tm_mday == 0 && time_tm.tm_year == 0){
			printf("Non ho a disposizione nessun registro chiuso disponibile!\n");
			return;
		}
		sprintf(upper,"%02d%02d%d",time_tm.tm_mday,time_tm.tm_mon,time_tm.tm_year);
		printf("Ho preso come upper bound la più recente data per cui ho un registro chiuso: \n%02d:%02d:%d\n",time_tm.tm_mday,time_tm.tm_mon,time_tm.tm_year);
		sleep(1);
	} else {
		// se è stato specificato il lower bound verifico che non includa registri aperti o successivi a quello di oggi
		sscanf(upper,"%02d:%02d:%d",&time_tm.tm_mday,&time_tm.tm_mon,&time_tm.tm_year);
		// se siamo dopo le 18 può includere il registro di oggi
		if(time_now.tm_hour >= TIMEOUT_HOUR){
			time_x = getLastDate(time_tm,time_now);
			if(!(time_x.tm_mday == time_now.tm_mday && time_x.tm_mon == time_now.tm_mon && time_x.tm_year == time_now.tm_year)){
				printf("Hai selezionato una data che fa da upper bound non valida!\n");
				return;
			}
		} else {
			// prima delle 18 non può includere il registro di oggi
			addDay(&time_now, -1);
			time_x = getLastDate(time_tm,time_now);
			if(!(time_x.tm_mday == time_now.tm_mday && time_x.tm_mon == time_now.tm_mon && time_x.tm_year == time_now.tm_year)){
				printf("Hai selezionato una data che fa da upper bound non valida!\n");
				return;
			}
			addDay(&time_now, +1);
		}
		sprintf(upper,"%02d%02d%d", time_tm.tm_mday, time_tm.tm_mon, time_tm.tm_year);
	}
	
	// qualora l'aggregazione richiesta sia quella del totale
	if(strcmp(aggr, "totale") == 0){
		sprintf(filename, "registers/%d/sum.bin", MyPort);
		// ripristino i diritti di scrittura necessari per memorizzare successivamente le entry
		writePermissions(1, filename);
		faggr = fopen(filename, "ab+");
		if(faggr == NULL){
			printf("Impossibile aprire il file degli aggregati!\n");
			writePermissions(0, filename);
			return;
		}
		// verifico se ho già calcolato il dato richiesto
		while(fread(&sumEntry, sizeof(sumEntry), 1, faggr)){
			if(sumEntry.type == type && strcmp(lower,sumEntry.lower) == 0 && strcmp(upper,sumEntry.upper) == 0){
				printf("Ho il totale complessivo già calcolato e vale %d\n",sumEntry.quantity);
				return;
			}
		}
		printf("Non ho il dato aggregato già calcolato! Procedo con il calcolo..\n");
		sleep(1);
		sum = total(lower, upper, type, 0);
		if(sum == -1)
			return;
		//se calcolato correttamente memorizzo il risultato nel file degli aggregati così da evitare elaborazione successive
		printf("Il totale complessivo è %d\n", sum);
		sumEntry.type = type;
		strncpy(sumEntry.lower, lower, 9);
		strncpy(sumEntry.upper, upper, 9);
		sumEntry.quantity = sum;
		fwrite(&sumEntry, sizeof(sumEntry), 1, faggr);
		fclose(faggr);
		printf("Ho memorizzato il dato aggregato nel caso possa servirmi successivamente!\n");
		writePermissions(0, filename);
	} else if(strcmp(aggr, "variazione") == 0){
		// qualora l'aggregazione richiesta sia quella della variazione
		variation(lower, upper, type);
	}
}


/* entryNeighbors: funzione invocata all'arresto del peer qualora questo sia connesso
   Invia le entry di oggi ai neighbors */

void entryNeighbors(){
	struct fileEntry entryT, entryN;
	FILE* f;
	int len, ret;
	uint16_t lmsg;
	char filename[50];
	// se non ho neighbors
	if(strcmp(neighbors[0].IP, "0.0.0.0") == 0 && neighbors[0].port == 0 &&
		strcmp(neighbors[1].IP, "0.0.0.0") == 0 && neighbors[1].port == 0)
		return;

	t = time(NULL);
	tm = *localtime(&t);
	tm.tm_mon += 1;
	tm.tm_year += 1900;
	if(tm.tm_hour >= TIMEOUT_HOUR)
		addDay(&tm, 1);
	sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
	f = fopen(filename, "rb");
	if(f == NULL){
		printf("Impossibile aprire il registro di oggi!\n");
		return;
	}
	// leggo le prime due entry che conterranno il numero di casi e tamponi registrati oggi da questo peer
	fread(&entryT, sizeof(fileEntry), 1, f);
	fread(&entryN, sizeof(fileEntry), 1, f);
	fclose(f);

	printf("Trasmetto i dati di oggi ai miei vicini...\n");
	sprintf(buffer, "STOP %s %d %c %d %s %d %c %d", entryT.IP, entryT.port, entryT.type, entryT.quantity, entryN.IP, entryN.port, entryN.type, entryN.quantity);
	len = strlen(buffer);
	lmsg = htons(len);
	// se ho un neighbor 0
	if(strcmp(neighbors[0].IP, "0.0.0.0") != 0 && neighbors[0].port != 0){
		printf("Trasmetto i dati di oggi al mio vicino %s:%d\n", neighbors[0].IP, neighbors[0].port);
		ret = send(neighbors[0].sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			printf("Errore nell'invio del messaggio!\n");
			return;
		}
		ret = send(neighbors[0].sd, (void*)buffer, len, 0);
		if(ret < len){
			printf("Errore nell'invio del messaggio!\n");
			return;
		}
	}
	// se ho un neighbor 1
	if(strcmp(neighbors[1].IP, "0.0.0.0") != 0 && neighbors[1].port != 0){
		printf("Trasmetto i dati di oggi al mio vicino %s:%d\n", neighbors[1].IP, neighbors[1].port);
		ret = send(neighbors[1].sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			printf("Errore nell'invio del messaggio!\n");
			return;
		}
		ret = send(neighbors[1].sd, (void*)buffer, len, 0);
		if(ret < len){
			printf("Errore nell'invio del messaggio!\n");
			return;
		}
	}
	sleep(3); // Così nel frattempo il DS_Server aggiorna i neighbors ai miei neighbors
}

/* terminate: funzione invocata quando il DS_server termina
   Memorizza le entry registrate oggi e chiude il registro */

void terminate(){
	char filename[50];
	t = time(NULL);
	tm = *localtime(&t);
	tm.tm_mon += 1;
	tm.tm_year += 1900;
	// chiudo il registro di oggi memorizzando le entry, scegliendo quello opportuno in funzione dell'orario
	sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
	if(tm.tm_hour < TIMEOUT_HOUR)
		closeRegister(filename);
	else {
		addDay(&tm, 1);
		sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
		closeRegister(filename);
	}
	close(DSsd);
	alarm(0); //disabilito il timer
	exit(0);
}

/* saveNeighbors: Funzione invocata quando i neighbors inviano delle entry 
   Memorizza le entry ricevute, eventualmente sovrascrivendo le entry precedenti se ha già ricevuto entry da quel peer */

void saveNeighbors(){
	struct fileEntry entryT, entryN;
	char tmpname[50];
	char filename[50];
	t = time(NULL);
	tm = *localtime(&t);
	tm.tm_mon += 1;
	tm.tm_year += 1900;
	if(tm.tm_hour >= TIMEOUT_HOUR)
		addDay(&tm, 1);
	sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
	FILE* f;
	FILE* ftmp;
	f = fopen(filename, "rb");
	if(f == NULL){
		printf("Impossibile aprire il registro di oggi!\n");
		return;
	}
	sscanf(buffer, "STOP %s %d %c %d %s %d %c %d", entryT.IP, &entryT.port, &entryT.type, &entryT.quantity, entryN.IP,&entryN.port, &entryN.type, &entryN.quantity);
	printf("\nUn peer ha lasciato il network e mi ha inviato le sue entry che memorizzo:\n%s %d %c %d\n%s %d %c %d\n", entryT.IP, entryT.port, entryT.type, entryT.quantity, entryN.IP, entryN.port, entryN.type, entryN.quantity);
	sprintf(tmpname, "registers/%d/temp.txt", MyPort);
	ftmp = fopen(tmpname, "wb"); 
	if(!ftmp) {
		printf("Problemi nel salvataggio!\n");
		return;
	}
	// memorizzo le entry ricevute dal peer sovrascrivendo eventuali entry già ricevute precedentemente nel corso della giornata dal peer
	while(fread(&fileEntry, sizeof(fileEntry), 1, f)){
		if(fileEntry.port == entryT.port && strcmp(fileEntry.IP, entryT.IP) == 0){
			fread(&fileEntry, sizeof(fileEntry), 1, f);
		} else fwrite(&fileEntry, sizeof(fileEntry), 1, ftmp);
	}
	fwrite(&entryT, sizeof(fileEntry), 1, ftmp);
	fwrite(&entryN, sizeof(fileEntry), 1, ftmp);
	fclose(f);
	fclose(ftmp);
	remove(filename);  
	rename(tmpname, filename);
}

/* stop: Funzione invocata quando si digita il comando stop da terminale o qualora si digiti CTRL+C */

void stop(){ 
	int len, ret;
	char filename[50];
	uint16_t lmsg;
	int status;
	if(connected){
		// se sono connesso informo il DS_Server della mia terminazione così che possa aggiornare i vicini dei miei vicini
		// se inoltre c'è il calcolo di un'aggregazione in corso mi indicherà che non posso terminare
		printf("Informo il DS_server della mia terminazione..\n");
		strcpy(buffer, "STOP");
		len = strlen(buffer);
		lmsg = htons(len);
		ret = send(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return;
		}
		ret = send(DSsd, (void*)buffer, len, 0);
		if(ret < len){
			perror("Errore");
			return;
		}
		//Ricevo conferma
		ret = recv(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return;
		}
		len = ntohs(lmsg);
		ret = recv(DSsd, (void*)buffer, len, 0);;
		if(ret < len){
			perror("Errore");
			return ;
		}
		buffer[len] = '\0';
		sscanf(buffer, "STOP %d\n", &status);
		printf("Ho ricevuto risposta dal server\n");
		if(status == 2){
			printf("Non posso terminare, c'è il calcolo di un'aggregazione in corso! Riprova più tardi!\n");
			return;
		}
	}
	t = time(NULL);
	tm = *localtime(&t);
	tm.tm_mon += 1;
	tm.tm_year += 1900;
	// chiudo il registro di oggi memorizzando le entry, scegliendo quello opportuno in funzione dell'orario
	sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
	if(tm.tm_hour < TIMEOUT_HOUR)
		closeRegister(filename);
	else {
		addDay(&tm, 1);
		sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
		closeRegister(filename);
		addDay(&tm, -1);
	}
	if(connected){
		// invio le entry ai miei neighbors
		entryNeighbors();
		close(DSsd); 
		close(neighbors[0].sd);
    	close(neighbors[1].sd);
	}
	// disabilito il timer
	alarm(0); 
	exit(0);
}

/* timeout: Quando si verifica l'evento di timeout chiudo il registro di oggi e apro quello di domani*/

void timeout(){
	char filename[50];
	printf("\nSono le %d, chiudo il registro di oggi e apro quello di domani!\n", TIMEOUT_HOUR);
	t = time(NULL);
	tm = *localtime(&t);
	tm.tm_mon += 1;
	tm.tm_year += 1900;
	sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
	closeRegister(filename);
	addDay(&tm, +1);
	sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
	openRegister(filename);
	addDay(&tm, -1);
	alarm(0);
}

/* startTimer: Calcolo il numero di secondi rimanenti per le 18 e imposto un allarme qualora le 18 non siano già passate */

void startTimer(){
	time_t now;
	time_t later;
	struct tm tm_later;
	int seconds;
	now = time(NULL);
	tm = *localtime(&now);
	tm_later = *localtime(&now);
	tm_later.tm_hour = TIMEOUT_HOUR;
	tm_later.tm_min = 00;
	tm_later.tm_sec = 00;
	later = mktime(&tm_later);
	// calcolo il numero di secondi rimanenti fino alle 18
	seconds = (int)difftime(later, now);
	if(seconds < 0){
		// se la differenza è negativa le 18 sono già passate e non è necessario impostare un allarme
		return;
	}
	alarm(seconds);
	signal(SIGALRM, timeout);
}

/* multiplexing: Funzione utilizzata per gestire i socket e lo stdin mediante I/O Multiplexing */

void multiplexing(){
	int i, ret, len;
	uint16_t lmsg;
	char IP0[16];
	int port0;
	char IP1[16];
	int port1;
	FD_ZERO(&readfds);
	updateMaster();
	while(1){
		//stampo il prompt dei comandi
		prompt(); 
		readfds = master;
		select(fdmax+1, &readfds, NULL, NULL, NULL);
		for(i = 0; i <= fdmax; ++i){
			if(FD_ISSET(i, &readfds)){
				if(i == fileno(stdin)){
					 //leggo il comando
					commandRead();
				 } else if(i == DSsd){
					//ricevo la lunghezza del messaggio
					ret = recv(DSsd, (void*)&lmsg, sizeof(uint16_t), 0);
					if(ret == 0){
						printf("\nIl DS_server ha abortito improvvisamente! Termino!!\n");
						terminate();
						break;
					}
					if(ret < sizeof(uint16_t)){
						perror("Errore");
						break;
					}
					len = ntohs(lmsg);
					//ricevo il messaggio
					ret = recv(DSsd, (void*)buffer, len, 0);
					if(ret == 0){
						printf("\nIl DS_server ha abortito improvvisamente! Termino!!\n");
						terminate();
						break;
					}
					if(ret < len){
						perror("Errore");
						break;
					}
					buffer[len] = '\0';
					if(strncmp(buffer, "UPDATE", 6) == 0){
							// Aggiornamento da parte del server dei neighbors
							sscanf(buffer, "UPDATE\npeer: %s %d\npeer: %s %d", IP0, &port0, IP1, &port1);
							if(strcmp(IP1, neighbors[1].IP) != 0 || port1 != neighbors[1].port){
								 	// qualora il peer 1 sia cambiato
                                    updateNeighbors(1, port1, IP1);
                                }
                        	if(strcmp(IP0, neighbors[0].IP) != 0 || port0 != neighbors[0].port){
                                    // qualora il peer 0 sia cambiato
                                    updateNeighbors(0, port0, IP0);
                            }
							printf("\nIl DS_server ha aggiornato la lista dei miei neighbors:\n%s:%d\n%s:%d\n", neighbors[0].IP, neighbors[0].port, neighbors[1].IP, neighbors[1].port);
							//aggiorno il set di descrittori opportunatamente
							updateMaster();
							break;
					} else if(strncmp(buffer, "TERMINATE", 9) == 0){
						printf("\nIl DS_server sta terminando e ha ordinato la mia terminazione, termino...\n");
						terminate();
					}
                } else if(i == neighbors[0].sd){
                    // socket TCP dedicato al neighbor di indice 0
					// ricevo lunghezza del messaggio
					ret = recv(neighbors[0].sd, (void*)&lmsg, sizeof(uint16_t), 0);
					if(ret == 0){
						printf("\nIl neighbor 0 si è disconnesso!\n");
						close(neighbors[0].sd);
						neighbors[0].sd = 0;
						updateMaster();
						break;
					}
					if(ret < sizeof(uint16_t)){
						perror("Errore");
						continue;
					}
					len = ntohs(lmsg);
					//ricevo il messaggio
					ret = recv(neighbors[0].sd, (void*)buffer, len, 0);
					if(ret == 0){
						printf("\nIl neighbor 0 si è disconnesso!\n");
						close(neighbors[0].sd);
						neighbors[0].sd = 0;
						updateMaster();
						break;
					}
					if(ret < len){
						perror("Errore");
						continue;
					}
					buffer[len] = '\0';
                    if(strncmp(buffer, "STOP", 4) == 0){
						// se il neighbor ha lanciato la stop
						saveNeighbors();
                    } else if(strncmp(buffer, "REQ_DATA", 8) == 0){
						// se il neighbor richiede un dato aggregato
						replyData(0);
					} else if(strncmp(buffer, "FLOOD_FOR_ENTRIES", 17) == 0){
						//se il neighbor richiede un flooding
						getFlood(0);
					}  else if(strncmp(buffer, "HAVE_ENTRIES", 12) == 0){
						// se il neighbor richiede di propagare il flooding
						propagateFlooding(1);
					}
				} else if(i == neighbors[1].sd){
                    //ricevo la lunghezza del messaggio
					ret = recv(neighbors[1].sd, (void*)&lmsg, sizeof(uint16_t), 0);
					if(ret == 0){
						printf("\nIl neighbor 1 si è disconnesso!\n");
						close(neighbors[1].sd);
						neighbors[1].sd = 0;
						updateMaster();
						break;
					}
					if(ret < sizeof(uint16_t)){
						perror("Errore1");
						break;
					}
					len = ntohs(lmsg);
					//ricevo il messaggio
					ret = recv(neighbors[1].sd, (void*)buffer, len, 0);
					if(ret == 0){
						printf("\nIl neighbor 1 si è disconnesso!\n");
						close(neighbors[1].sd);
						neighbors[1].sd = 0;
						updateMaster();
						break;
					}
					if(ret < len){
						perror("Errore1");
						break;
					}
					buffer[len] = '\0';
                   if(strncmp(buffer, "STOP", 4) == 0){
					   // se il neighbor ha lanciato la stop
						saveNeighbors();
                    } else if(strncmp(buffer, "REQ_DATA", 8) == 0){
						// se il neighbor richiede un dato aggregato
						replyData(1);
					} else if(strncmp(buffer, "FLOOD_FOR_ENTRIES", 17) == 0){
						//se il neighbor richiede un flooding
						getFlood(1);
					}  else if(strncmp(buffer, "HAVE_ENTRIES", 12) == 0){
						// se il neighbor richiede di propagare il flooding
						propagateFlooding(0);
					}
				}
			}
		}
	}
}


/* init: Operazioni iniziali */

void init(){
	int ret;
	char filename[50];
	// neighbors iniziali vuoti
	strcpy(neighbors[0].IP, "0.0.0.0");
	strcpy(neighbors[1].IP, "0.0.0.0");
	neighbors[0].port = 0;
	neighbors[1].port = 0;

	// inizializzazione delle variabili
	connected = 0;
	advertisedGet = 0;
	udpsd = 0;
	DSsd = 0;
	t = time(NULL);
	tm = *localtime(&t);
	tm.tm_mon += 1;
	tm.tm_year += 1900;

	// apro il registro di oggi scegliendo quello opportuno in funzione dell'orario
	sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
	if(tm.tm_hour < TIMEOUT_HOUR)
		openRegister(filename); //apro il registro di oggi
	else {
		addDay(&tm, 1);
		sprintf(filename,"registers/%d/%02d%02d%d.bin",MyPort,tm.tm_mday,tm.tm_mon,tm.tm_year);
		openRegister(filename);
	}
	// stampo la guida dei comandi all'avvio
	commandGuide(); 
	// imposto il timer per chiudere il registro e aprire quello del giorno dopo alle 18:00
	startTimer();
	ret = startTCPConnection();
	if(ret == -1){
		close(tcpsd);
		exit(0);
	}
	// associo il gestore alla digitazione di CTRL+C
	signal(SIGINT, stop);
}

int main(int argc, char** argv){
	//controllo il numero di parametri
	if(argc != 2 && argc != 4){
		printf("Utilizzo: %s <porta>\n", argv[0]);
		exit(-1);
	}
	//controllo che il numero di porta non sia tra quelli riservati
	if(checkPort(atoi(argv[1])) == -1){
		printf("Porta inserita non valida, scegliere un valore appartenente a [1024,65535]\n");
		exit(-1);
	}
	MyPort = atoi(argv[1]);
	init();
	if(argc == 4){
		// se specifica quattro parametri boot instantaneo con l'indirizzo IP e la porta del DS server
		prompt();
		printf("\n");
		start(argv[2], atoi(argv[3]));
	}
	multiplexing();
	return 0;
}