#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h> 
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>

extern char commandLine[100];
extern char buf[100];
extern int myPort;

extern int udpsd;
extern int tcpsd;
extern int fdmax;
extern fd_set readfds, master;
extern struct sockaddr_in my_addr;

struct peer{
	char IP[16];
	int port;
    int sd;
	struct peer* next;
};

struct fileEntry {
	char IP[16];
	int port;
};

extern struct fileEntry fileEntry;

extern struct peer* peerRegister;

// variabili di stato della coda circolare
extern struct peer* head;
extern struct peer* tail;

struct neighbors{
	char prevIP[16];
	int prevPort;
	char nextIP[16];
	int nextPort;
};

// variabile di stato necessaria per non interferire al calcolo di un'aggregazione
extern int loadingGet;
