#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h> 
#include <signal.h>

#define NUM_NEIGHBORS 2

extern char buffer[1024];
extern int MyPort;
extern int DSsd;
extern int udpsd;
extern int tcpsd;
extern struct sockaddr_in my_addr, DS_addr;
extern fd_set readfds, master;
extern int fdmax;
extern int advertisedGet;
extern int connected;

extern struct peer* peerList;

extern time_t t;
extern struct tm tm;

struct neighbors{
	int port;
	char IP[16];
	int sd;
	struct sockaddr_in neighbor_addr;
}; 

extern struct neighbors neighbors[NUM_NEIGHBORS];

struct entry {
	char type;
	int quantity;
	struct entry* next;
};

extern struct entry* todayRegister;

struct fileEntry {
	char IP[16];
	int port;
	char type;
	int quantity;
};

extern struct fileEntry fileEntry;

struct sumEntry {
	char lower[9];
	char upper[9];
	char type;
	int quantity;
}; 

extern struct sumEntry sumEntry;

struct diffEntry{
	char lower[9];
	char upper[9];
	char type;
	char quantity[50];
};

extern struct diffEntry diffEntry;

struct totalEntry {
	char date[9];
	char type;
	int quantity;
};

extern struct totalEntry totalEntry;

struct peer{
	char IP[16];
	int port;
	struct peer* next;
};