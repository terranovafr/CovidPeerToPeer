#include "vars.h"
#include "connectionUtility.h"


/* startUDPConnection: crea il socket UDP */

int startUDPConnection(char* addr, int port){
	int ret;
	if(udpsd != 0) 
		return -1; 
	udpsd = socket(AF_INET, SOCK_DGRAM, 0);
	if(udpsd == -1){
        perror("Errore");
        return -1;
    }
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(MyPort);
	inet_pton(AF_INET, "127.0.0.1", &my_addr.sin_addr);

	//creo inoltre la struttura del DS server
	memset(&DS_addr, 0, sizeof(DS_addr));
	DS_addr.sin_family = AF_INET;
	DS_addr.sin_port = htons(port);
	inet_pton(AF_INET, addr, &DS_addr.sin_addr);

	ret = bind(udpsd, (struct sockaddr*)&my_addr, sizeof(my_addr));
	if(ret == -1){
		perror("Errore nella bind UDP");
		return -1;
    }
	return 0;
} 

/* startTCPConnection: crea il socket TCP */

int startTCPConnection(){
	int ret;
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(MyPort);
	inet_pton(AF_INET, "127.0.0.1", &my_addr.sin_addr);
   	tcpsd = socket(AF_INET, SOCK_STREAM, 0);
    if(tcpsd == -1){
        perror("Errore");
        return -1;
    }
	if(setsockopt(tcpsd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
    	printf("Errore");
    ret = bind(tcpsd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    if(ret == -1){
        perror("Errore");
        return -1;
    }
    ret = listen(tcpsd, 10);
	return 1;
}