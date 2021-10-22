#include "utility/server/listUtility.h"
#include "utility/server/vars.h"
#include "utility/server/commandsUtility.h"

char commandLine[100];
char buf[100];
int myPort;

int udpsd;
int tcpsd;
int fdmax;
fd_set readfds, master;
struct sockaddr_in my_addr;

// variabile per la gestione del registro degli accessi
struct peer* peerRegister;

// variabili per la gestione della coda circolare
struct peer* head;
struct peer* tail;

// variabile di stato necessaria per non interferire al calcolo di un'aggregazione
int loadingGet;

/* checkPort: controlla che la porta sia valida */

int checkPort(int port){
	if(port > 65535 || port < 1024)
		return -1;
	return 0;
}

/* getPeer: dato il socket associato a un peer restituisce le sue informazioni*/

struct peer getPeer(int sd){
	struct peer* p;
	for(p = head; p != 0; p = p->next){
		if(p->sd == sd){
			return *p;
		}
	}
	return *head;
}

/* getsd: dati indirizzo IP e porta di un peer restituisce il socket associato */

int getsd(char* IP, int port){
	struct peer* p;
	for(p = head; p != 0; p = p->next)
		if(strcmp(p->IP, IP)==0 && p->port == port)
			return p->sd;
	return 0;
}

/* updateMaster: Aggiorna in maniera consistente il set di descrittori */

void updateMaster(){
	FD_ZERO(&master);
	FD_SET(fileno(stdin), &master);
	FD_SET(udpsd, &master);
	fdmax = udpsd;
	struct peer* p;
	for(p = head; p != 0; p = p->next){
		FD_SET(p->sd, &master);
		if(p->sd > fdmax) fdmax = p->sd;
	}
}

/* saveRegister: Memorizza il registro degli accessi giornaliero */

void saveRegister(){
	FILE* f;
	char filename[50];
	struct stat statRes;
	int fildes;
	time_t t;
	struct tm tm;
	struct peer* p;
	struct fileEntry entry;
	t = time(NULL);
	tm = *localtime(&t);
	char dirName[30];
	sprintf(dirName, "registers/%d", myPort);
	if(stat(dirName, &statRes) == -1){
		printf("Creo la mia directory contenente i registri, non esisteva..\n");
    	mkdir(dirName, 0700);
	}
	sprintf(filename,"registers/%d/%02d%02d%d.bin",myPort,tm.tm_mday,tm.tm_mon + 1,tm.tm_year + 1900);
	if(access(filename,F_OK)==0){
		f = fopen(filename, "rb");
		if(f == NULL){
			printf("Impossibile aprire il registro di oggi!\n");
			return;
		}
		// abilito il permesso di scrittura
		fildes = fileno(f);
		fchmod(fildes, S_IWUSR | S_IRUSR | S_IRGRP);
		fclose(f);
	}
	f = fopen(filename, "wb");
	if(f == NULL){
		printf("Impossibile aprire il registro di oggi!\n");
		return;
	}
	for(p = peerRegister; p != 0; p = p->next){
		entry.port = p->port;
		strcpy(entry.IP, p->IP);
		fwrite(&entry, sizeof(fileEntry), 1, f);
	}
	// rimuovo il permesso di scrittura dopo aver salvato le informazioni
	fildes = fileno(f);
	fchmod(fildes, S_IRUSR | S_IRGRP);
	fclose(f);
}

/* updateRegister: Aggiunge un peer al registro giornaliero degli accessi se non è già presente */

void updateRegister(char* IP, int port){
	struct peer* p;
	struct peer* r;
	r = malloc(sizeof(struct peer));
	strcpy(r->IP, IP);
	r->port = port;
	r->next = 0;
	if(peerRegister == 0){
		peerRegister = r;
		return;
	}
	// controlla se è già presente
	for(p = peerRegister; p->next != 0; p = p->next)
		if(p->port == port && (strcmp(p->IP, IP) == 0))
			return;
	// controlla l'ultimo elemento
	if(p->port == port && (strcmp(p->IP, IP) == 0))
			return;
	p->next = r;
}

/* loadRegister: Carico il registro degli accessi giornaliero, se è stato già creato */

void loadRegister(){
	FILE* f;
	char filename[50];
	time_t t;
	struct tm tm;
	struct fileEntry entry;
	t = time(NULL);
	tm = *localtime(&t);
	sprintf(filename,"registers/%d/%02d%02d%d.bin",myPort,tm.tm_mday,tm.tm_mon + 1,tm.tm_year + 1900);
	if(access(filename,F_OK)!=0) // se non è presente (primo accesso gionaliero del DS_server)
		return;
	f = fopen(filename, "rb");
	if(f == NULL){
		printf("Impossibile aprire il registro di oggi!\n");
		return;
	}
	while(fread(&entry, sizeof(fileEntry), 1, f))
		updateRegister(entry.IP, entry.port);
	fclose(f);
}


/* printRegister: Funzione di supporto utilizzata per stampare il contenuto del registro giornaliero degli accessi */

void printRegister(){
	struct peer* p;
	for(p = peerRegister; p != 0; p = p->next)
		printf("%s %d\n", p->IP, p->port);
}

/* readFile: Funzione di supporto utilizzata per leggere il contenuto dei file binari nella propria folder */

void readFile(char* day){
	char file[60];
	FILE* fday;
	struct fileEntry entry;
	sprintf(file, "registers/%d/%s.bin", myPort, day);
	fday = fopen(file, "rb");
	if(fday == NULL)
		return;
	while(fread(&entry, sizeof(struct fileEntry), 1, fday))
		printf("%s %d\n", entry.IP, entry.port);
	fclose(fday);
}

/* status: Mostra le informazioni sullo stato del network */

void status(){
	struct peer* p;
	if(head == 0){
		printf("La lista dei peers è vuota!\n");
		return;
	}
	for(p = head; p != 0; p = p->next)
		printf("Il peer %s:%d è connesso al network\n", p->IP, p->port);
}


/* showAllNeighbors: Mostra i neighbors di ogni peer del network */

void showAllNeighbors(){
	struct peer* q;
	struct peer* p;
	if(head == NULL){
		printf("La lista dei peers è vuota!\n");
		return;
	}
	if(tail == head){ // un solo elemento
		printf("Abbiamo solo il peer %s:%d senza neighbors\n", head->IP, head->port);
		return;
	} else if (head->next == tail){ // due elementi
		printf("Il peer %s:%d ha il neighbor %s:%d\n", head->IP, head->port, tail->IP, tail->port);
		printf("Il peer %s:%d ha il neighbor %s:%d\n", tail->IP, tail->port, head->IP, head->port);
	} else { //più di due elementi
		printf("Il peer %s:%d ha i neighbors %s:%d e %s:%d\n", head->IP, head->port, head->next->IP, head->next->port, tail->IP, tail->port);
		for(p = head, q = p->next; q->next != 0; p = q, q = q->next)
			printf("Il peer %s:%d ha i neighbors %s:%d e %s:%d\n", q->IP, q->port, p->IP, p->port, q->next->IP, q->next->port);
		printf("Il peer %s:%d ha i neighbors %s:%d e %s:%d\n", q->IP, q->port, p->IP, p->port, head->IP, head->port);
	}
}

/* showNeighbors: Mostra i neighbors di un peer dato il numero di porta */

void showNeighbors(int peer){
	struct peer* q;
	struct peer* p;
	if(head == NULL){
		printf("La lista dei peers è vuota, impossibile mostrare i vicini del peer %d!\n", peer);
		return;
	}
	if(head->port == peer){
		if(head->next == 0)
			printf("Il peer %d ancora non ha vicini!\n", peer);
		else if(tail == head->next)
			printf("Il peer %d ha un vicino ed è il vicino %s:%d \n", peer, tail->IP, tail->port);
		else
			printf("Il peer %d ha come vicini %s:%d e %s:%d\n", peer, head->next->IP, head->next->port, tail->IP, tail->port);
	} else if(tail->port == peer){
			if(head->next == tail)
				printf("Il peer %d ha un vicino ed è il vicino %s:%d \n", peer, head->IP, head->port);
	} else {
		for(p = head, q = p->next; q != 0 && q->port != peer; p = q, q = q->next);
		if(q == 0)
			printf("Il peer %d non è presente nella mia lista!\n", peer);
		else if(q->next == 0)
				printf("Il peer %d ha come vicini %s:%d e %s:%d\n", peer, p->IP, p->port, head->IP, head->port);
		else printf("Il peer %d ha come vicini %s:%d e %s:%d\n", peer, p->IP, p->port, q->next->IP, q->next->port);
	}
}

/* esc: Funzione invocata quando da linea di comando si digita esc
	Verifica che non sia in corso il calcolo di un'aggregazione, se non è in corso nessun calcolo
	informa i peer della terminazione così che anch'essi possano terminare e termina */

void esc(){
	struct peer* p;
	int len, ret;
	uint16_t lmsg;
	strcpy(buf, "TERMINATE");
	len = strlen(buf);
	lmsg = htons(len);
	if(loadingGet == 1){
		printf("Non posso eseguire questa azione,  c'è il calcolo di un'aggregazione in corso!\n");
		return;
	}
	for(p = head; p != 0; p=p->next){
		printf("Comunico al peer %s:%d di dover terminare!\n", p->IP, p->port);
		ret = send(p->sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return;
		}
		ret = send(p->sd, (void*)buf, len, 0);
		if(ret < len){
			perror("Errore");
			return;
		}
		close(p->sd);
	}
	close(tcpsd);
	close(udpsd);
	// salva su file il registro degli accessi giornaliero
	saveRegister();
	printf("Termino...\n");
	exit(0);
}

/* getNeighbors: Restituisce le informazioni sui neighbors di un peer */

struct neighbors getNeighbors(char* IP, int port){
	struct peer* q;
	struct peer* p;
	struct neighbors n;
	strcpy(n.prevIP,"0.0.0.0");
	n.prevPort = 0;
	strcpy(n.nextIP,"0.0.0.0");
	n.nextPort = 0;
	if(head == NULL){
		return n;
	}
	// faccio gli opportuni controlli su testa e cosa poichè gestisco una coda circolare
	if(head->port == port && (strcmp(head->IP, IP) == 0)){
		if(head->next == 0){}
		else if(head->next == tail){
			strncpy(n.nextIP,tail->IP, strlen(tail->IP)+1);
			n.nextPort = tail->port;
		} else {
			strncpy(n.nextIP,head->next->IP, strlen(head->next->IP)+1);
			n.nextPort = head->next->port;
			strncpy(n.prevIP,tail->IP, strlen(tail->IP)+1);
			n.prevPort = tail->port;
		}
	} else if(tail->port == port && (strcmp(tail->IP, IP) == 0) && head->next == tail){
			strncpy(n.prevIP,head->IP, strlen(head->IP)+1);
			n.prevPort = head->port;
	} else {
		for(p = head, q = p->next; q->next != 0 && !(q->port == port && (strcmp(q->IP, IP) == 0)); p = q, q = q->next);
		if(q->next != 0){
			strncpy(n.prevIP,p->IP, strlen(p->IP)+1);
			n.prevPort = p->port;
			strncpy(n.nextIP,q->next->IP, strlen(q->next->IP)+1);
			n.nextPort = q->next->port;
		 } else {
			strncpy(n.prevIP,p->IP, strlen(p->IP)+1);
			n.prevPort = p->port;
			strncpy(n.nextIP,head->IP, strlen(head->IP)+1);
			n.nextPort = head->port;
		 }
	}
	return n;
}

/* updateNeighbors: Invia i neighbors ad un peer */

void updateNeighbors(char* IP, int port){
	uint16_t lmsg;
	int ret, len;
	struct neighbors n;
	int sd;

	n = getNeighbors(IP, port);
	sd = getsd(IP, port);
	sprintf(buf, "UPDATE\npeer: %s %d\n peer: %s %d", n.prevIP, n.prevPort, n.nextIP, n.nextPort);

	len = strlen(buf);
	lmsg = htons(len);
	ret = send(sd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return;
	}
	ret = send(sd, (void*)buf, len, 0);
	if(ret < len){
		perror("Errore");
		return;
	}
	printf("Ho aggiornato i neighbors del peer %s:%d con %s:%d e %s:%d\n", IP, port, n.prevIP, n.prevPort, n.nextIP, n.nextPort);
}

/* getNumPeer: Funzione invocata quando un peer vuole conoscere il numero di peer attualmente connessi
	Invia il messaggio di risposta al peer fornendo l'informazione */

void getNumPeer(int sd){
	struct peer* p;
	int numPeer;
	int ret, len;
	uint16_t lmsg;
	char mex[30];
	numPeer = 0;
	struct peer peerSrc;
	peerSrc = getPeer(sd);
	for(p = head; p != 0; p = p->next)
		numPeer++;
	printf("\nIl peer %s:%d vuole conoscere il numero di peer attualmente connessi al network..\n", peerSrc.IP, peerSrc.port);
	sprintf(mex, "GETNUM 0\nnum: %d", numPeer);
	printf("Invio il numero dei peer al peer %s:%d!\n", peerSrc.IP, peerSrc.port);
	len = strlen(mex);
	lmsg = htons(len);
	ret = send(sd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return;
	}
	ret = send(sd, (void*)mex, len, 0);
	if(ret < len){
		perror("Errore");
		return;
	}
}

/* startGet: Funzione invocata quando un peer vuole iniziare/terminare il calcolo di un'aggregazione
	Nel caso volesse iniziare il calcolo verifica che non vi sia già il calcolo di un'aggregazione in corso
	Se vi è già un'aggregazione in corso invita a riprovare più tardi */

void startGet(int sd){
	int ret, len;
	uint16_t lmsg;
	char mex[30];
	int flag;
	struct peer peerSrc;
	peerSrc = getPeer(sd);
	sscanf(buf, "ADVGET %d", &flag);
	if(flag == 1)
		printf("\nIl peer %s:%d vuole iniziare il calcolo di un'aggregazione..\n", peerSrc.IP, peerSrc.port);
	if((flag == 1 && loadingGet == 1)){
			strcpy(mex, "ADVGET 1");
			printf("Il peer non può iniziare il calcolo dell'aggregazione, c'è n'è già uno in corso!\n");
	} else {
		strcpy(mex, "ADVGET 0");
		if(flag == 1){
			printf("Il peer può iniziare il calcolo dell'aggregazione! Disabilito le funzioni start, stop ed esc!\n");
			loadingGet = 1;
		} else {
			printf("\nIl peer %s:%d ha terminato il calcolo dell'aggregazione, ripristino le funzioni start, stop ed esc!\n", peerSrc.IP, peerSrc.port);
			loadingGet = 0;	
		}
	}
	len = strlen(mex);
	lmsg = htons(len);
	ret = send(sd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return;
	}
	ret = send(sd, (void*)mex, len, 0);
	if(ret < len){
		perror("Errore");
		return;
	}
}

/* stop: Funzione invocata quando un peer invoca la stop 
	La funzione verifica che non sia in corso il calcolo di un'aggregazione e quindi 
	se possibile elimina il peer dal network, aggiornando successivamente i neighbors
	ai suoi vicini */

void stop(int sd){
	int ret, len;
	uint16_t lmsg;
	struct neighbors n;
	struct peer peerSrc;
	peerSrc = getPeer(sd);
	printf("\nHo ricevuto uno stop da %s:%d\n", peerSrc.IP, peerSrc.port);
	// se vi è il calcolo di un'aggregazione in corso non posso cancellarlo dal network
	if(loadingGet == 1){
		printf("Non posso eseguire questa azione, c'è il calcolo di un'aggregazione in corso!\n");
		sprintf(buf, "STOP 2\n");
		len = strlen(buf);
		lmsg = htons(len);
		ret = send(sd, (void*)&lmsg, sizeof(uint16_t), 0);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return;
		}
		ret = send(sd, (void*)buf, len, 0);
		if(ret < len){
			perror("Errore");
			return;
		}
		return;
	}
	sprintf(buf, "STOP 0\n");
	len = strlen(buf);
	lmsg = htons(len);
	ret = send(sd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return;
	}
	ret = send(sd, (void*)buf, len, 0);
	if(ret < len){
		perror("Errore");
		return;
	}
	// cancello il peer dalla lista
	n = getNeighbors(peerSrc.IP, peerSrc.port);
	delete(peerSrc.IP,peerSrc.port);
	printf("Ho rimosso il peer %s:%d nel network!\n", peerSrc.IP, peerSrc.port);
	sleep(2);
	// aggiorno i neighbors ai suoi vicini
	if(strcmp(n.prevIP, "0.0.0.0") != 0 && n.prevPort != 0)
		updateNeighbors(n.prevIP, n.prevPort);
	if(strcmp(n.nextIP, "0.0.0.0") != 0 && n.nextPort != 0)
		updateNeighbors(n.nextIP, n.nextPort);
	// aggiorno il set di descrittori
	updateMaster();
}

/* startConnection: Funzione che crea il socket UDP e il socket TCP sulla porta specificata */

int startConnection(){
	int ret;
	udpsd = socket(AF_INET, SOCK_DGRAM, 0);
	if(udpsd == -1){
		perror("Errore");
		return -1;
	}
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(myPort);
	my_addr.sin_addr.s_addr = INADDR_ANY;
	ret = bind(udpsd, (struct sockaddr*)&my_addr, sizeof(my_addr));
	if(ret == -1){
		perror("Errore");
		return -1;
	}
   	tcpsd = socket(AF_INET, SOCK_STREAM, 0);
    if(tcpsd == -1){
        perror("Errore");
        return -1;
    }
	// così da evitare l'errore Address Already in Use
	if(setsockopt(tcpsd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0){
    	printf("Errore sulla setsockopt\n");
		return -1;
	}
    ret = bind(tcpsd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    if(ret == -1){
        perror("Errore");
        return -1;
    }
    ret = listen(tcpsd, 10);
	if(ret == -1){
        perror("Errore");
        return -1;
    }
    return 1;
}

/* validSender: Funzione che confronta due strutture sockaddr_in */

int validSender(struct sockaddr_in src_addr, struct sockaddr_in valid_addr){
	if(src_addr.sin_port == valid_addr.sin_port && src_addr.sin_addr.s_addr == valid_addr.sin_addr.s_addr) 
		return 1;
	else return 0; 
}

/* boot: Funzione invocata quando un peer chiede il boot mediante socket UDP
   Aggiunge il peer nel network qualora questo non sia già registrato e qualora non sia in corso il calcolo
   di un'aggregazione
   Apre una connessione TCP col peer e invia i vicini al peer
   Aggiorna successivamente i vicini agli altri peer */

void boot(struct sockaddr_in peer_addr){
	int ret, len;
	uint16_t lmsg;
	struct neighbors n;
	socklen_t addrlen;
	struct timeval timeout;
	struct sockaddr_in src_addr;
	fd_set sockfds;
	char IP[16];
	int port;
    int peersd;
	int flag;
	inet_ntop(AF_INET, (void*)&peer_addr.sin_addr, IP, INET_ADDRSTRLEN);
	port = ntohs(peer_addr.sin_port);
	printf("\nHo ricevuto un boot da %s:%d\n", IP, port);
	while(1){
		// se quell'IP e quella porta sono già utilizzati restituisco un errore
		if(isRegistered(IP, port)){
			printf("Il peer %s:%d non è stato inserito, è già presente nel network!\n", IP, port);
			flag = 1;
		}
		// se c'è il calcolo di un'aggregazione in corso invito a riprovare più tardi 
		if(loadingGet == 1){
			printf("Non posso eseguire questa azione, c'è il calcolo di un'aggregazione in corso!\n");
			flag = 2;
		} else flag = 0;
		addrlen = sizeof(peer_addr);
		sprintf(buf, "BOOT %d\n", flag);
		len = strlen(buf);
		lmsg = htons(len);
		ret = sendto(udpsd, (void*)&lmsg, sizeof(uint16_t), 0, (struct sockaddr*)&peer_addr, addrlen);
		if(ret < sizeof(uint16_t)){
			perror("Errore");
			return;
		}
		ret = sendto(udpsd, (void*)buf, len, 0, (struct sockaddr*)&peer_addr, addrlen);
		if(ret < len){
			perror("Errore");
			return;
		}
		// se ho restituito un errore non vado avanti
		if(flag == 1 || flag == 2)
			return;
		timeout.tv_sec = 3; 
		timeout.tv_usec = 0;
wait:   FD_ZERO(&sockfds);
		FD_SET(udpsd, &sockfds);
		// aspetto nel caso il peer non ricevesse correttamente la risposta
		ret = select(udpsd+1, &sockfds, NULL, NULL, &timeout);
		if(ret == 0){
			// non ho ricevuto un'altra richiesta quindi posso proseguire
			break;
		} else {
			ret = recvfrom(udpsd, (void*)&lmsg, sizeof(uint16_t), 0,(struct sockaddr*)&src_addr, &addrlen);
			if(ret < sizeof(uint16_t)){
				perror("Errore");
				return;
			}			
			len = ntohs(lmsg);
			ret = recvfrom(udpsd, (void*)buf, len, 0,(struct sockaddr*)&src_addr, &addrlen);
			if(ret < len){
				perror("Errore");
				return;
			}
			buf[len] = '\0';
			if(strncmp(buf, "BOOT", 4)==0){
				if(validSender(src_addr, peer_addr)){
					// se il peer ha ritrasmesso non ha ricevuto correttamente la risposta e ritrasmetto di conseguenza
					printf("Il peer non ha ricevuto correttamente la risposta, ritrasmetto!\n");
				} else {
					// se si tratta di un altro peer gli invio un messaggio di BOOT con codice di stato 1
					// per invitarlo a riprovare successivamente visto che sto già servendo un'altra richiesta
					strcpy(buf, "BOOT 1\n");
					len = strlen(buf);
					lmsg = htons(len);
					ret = sendto(udpsd, (void*)&lmsg, sizeof(uint16_t), 0, (struct sockaddr*)&src_addr, addrlen);
					if(ret < sizeof(uint16_t)){
						perror("Errore");
						return;
					}
					ret = sendto(udpsd, (void*)buf, len, 0, (struct sockaddr*)&src_addr, addrlen);
					if(ret < len){
						perror("Errore");
						return;
					}
					goto wait;
				}
			}
		}
	}
    peersd = accept(tcpsd, (struct sockaddr*)&peer_addr, &addrlen);
    if(peersd == -1){
        perror("Errore");
        return;
    }
	// inserisco il peer nella lista
    insert(IP,port, peersd);
	// aggiorno correttamente il set di descrittori
	updateMaster();
	// aggiorno il registro degli accessi di oggi
	updateRegister(IP,port);
	// comunico al peer i suoi vicini
	n = getNeighbors(IP, port);
	sprintf(buf, "UPDATE\npeer: %s %d\npeer: %s %d", n.prevIP, n.prevPort, n.nextIP, n.nextPort);
	len = strlen(buf);
	lmsg = htons(len);
    ret = send(peersd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return;
	}
	ret = send(peersd, (void*)buf, len, 0);
	if(ret < len){
		perror("Errore");
		return;
	}
	printf("Ho inserito il peer %s:%d nel network!\n", IP, port);
	// se il peer appena inserito ha dei vicini aggiorno ai suoi vicini i loro vicini, che con l'inserimento del peer saranno sicuramente cambiati
	if(strcmp(n.prevIP, "0.0.0.0") != 0 && n.prevPort != 0)
		updateNeighbors(n.prevIP, n.prevPort);
	if(strcmp(n.nextIP, "0.0.0.0") != 0 && n.nextPort != 0)
		updateNeighbors(n.nextIP, n.nextPort);
}

/* list: Funzione invocata quando un peer richiede la lista dei peer che sono stati connessi un determinato giorno
	La funzione fornisce la lista dei peer se quel giorno almeno un peer era connesso, altrimenti setta il codice di stato
	indicando che quel giorno nessun peer era connesso */

void list(int sd){
	int ret, len;
	uint16_t lmsg;
	char mex[300];
	char peerEntry[30];
	FILE* f;
	char filename[50];
	char date[10];
	struct fileEntry entry;
	struct peer peerSrc;
	struct stat info;
	int size;
	peerSrc = getPeer(sd);
	// prendo il giorno per il quale si vuole ottenere la lista
	sscanf(buf, "LIST\nday: %s", date);
	printf("\nHo ricevuto una richiesta della lista da %s:%d per la data %s\n", peerSrc.IP, peerSrc.port, date);
	strcpy(mex, "LIST ");
	sprintf(filename,"registers/%d/%s.bin", myPort, date); 
	if(access(filename,F_OK)==0){
		//se ho il file relativo a quel giorno
		f = fopen(filename, "rb");
		if(f == NULL){
			printf("Impossibile aprire il registro di oggi!\n");
			return;
		}
		ret = stat(filename, &info);
		size = info.st_size;
		if(size == 0){
			strcat(mex, "1\n"); // file vuoto, quindi 0 peer connessi quel giorno
		} else {
			printf("Invio la lista così composta:\n");
			// qualche peer era connesso quel giorno, creo la lista
			strcat(mex, "0\n");
			while(fread(&entry, sizeof(fileEntry), 1, f)){
				printf("%s %d\n", entry.IP, entry.port);
				sprintf(peerEntry, "%s %d\n", entry.IP, entry.port);
				strcat(mex, peerEntry);
			}
		}
	} else strcat(mex, "1\n"); // se non ho quel file allora c'erano 0 peer connessi quel giorno
	printf("Invio la lista dei peer al peer %s:%d!\n", peerSrc.IP, peerSrc.port);
	len = strlen(mex);
	lmsg = htons(len);
	ret = send(sd, (void*)&lmsg, sizeof(uint16_t), 0);
	if(ret < sizeof(uint16_t)){
		perror("Errore");
		return;
	}
	ret = send(sd, (void*)mex, len, 0);
	if(ret < len){
		perror("Errore");
		return;
	}
}

/* init: Operazioni iniziali */

void init(){
	int ret;
	loadingGet = 0;
	// creo socket TCP e UDP
	ret = startConnection();
	if(ret == -1){
		printf("Errore di connessione, non riesco a connettermi! Abortisco!\n");
		exit(-1);
	}
	commandGuide();
	//carico il registro degli accessi giornaliero
	loadRegister();
}

/* multiplexing: Funzione utilizzata per gestire i socket e lo stdin mediante I/O Multiplexing */

void multiplexing(){
	int i, ret, len;
	struct sockaddr_in src_addr;
	socklen_t addrlen;
	uint16_t lmsg;
	struct peer p;
	FD_ZERO(&readfds);
	updateMaster();
	while(1){
		prompt();
		readfds = master;
		select(fdmax+1, &readfds, NULL, NULL, NULL);
		for(i = 0; i <= fdmax; ++i){
			if(FD_ISSET(i, &readfds)){
				if(i == fileno(stdin)){
					commandRead();
					break;
				} else if(i == udpsd){ //socket UDP
					addrlen = sizeof(src_addr);
					ret = recvfrom(udpsd, (void*)&lmsg, sizeof(uint16_t), 0,(struct sockaddr*)&src_addr, &addrlen);
					if(ret < sizeof(uint16_t)){
						perror("Errore");
						continue;
					}
					len = ntohs(lmsg);
					ret = recvfrom(udpsd, (void*)buf, len, 0,(struct sockaddr*)&src_addr, &addrlen);
					buf[len] = '\0';
					if(ret < len){
						perror("Errore");
						continue;
					}
					if(strncmp(buf, "BOOT", 4)==0)
						boot(src_addr);
					else
						printf("Messaggio non riconosciuto!\n");
				} else {
					ret = recv(i, (void*)&lmsg, sizeof(uint16_t), 0);
					if(ret == 0){
						printf("\nUn peer si è disconnesso!\n");
						p = getPeer(i);
						delete(p.IP, p.port);
						updateMaster();
						break;
					}
					if(ret < sizeof(uint16_t)){
						perror("Errore");
						continue;
					}
					len = ntohs(lmsg);
					ret = recv(i, (void*)buf, len, 0);
					if(ret < len){
						perror("Errore");
						continue;
					}
					buf[len] = '\0';
					if(strncmp(buf, "STOP", 4)==0){
						stop(i);
						break;
					} else if(strncmp(buf, "LIST", 4)==0)
						list(i);
					else if(strncmp(buf, "GETNUM", 6)==0)
						getNumPeer(i);
					else if(strncmp(buf, "ADVGET", 6)==0)
						startGet(i);
				}
			}
		}
	}
}


int main(int argc, char** argv){
	if(argc != 2){
		printf("Utilizzo: %s <porta>\n", argv[0]);
		exit(-1);
	}
	if(checkPort(atoi(argv[1])) == -1){
		printf("Porta inserita non valida, scegliere un valore appartenente a [1024,65535]\n");
		exit(-1);
	}
	myPort = atoi(argv[1]);
	//Invoco la esc quando viene digitato CTRL+C
	signal(SIGINT, esc); 
	init();
	multiplexing();
	return 0;
}
