#include "vars.h"
#include "commandsUtility.h"

/* commandRead: Funzione che si occupa della lettura del comando, dello split dei parametri e dell'invocazione della giusta funzione*/

void commandRead(){
	int numPar, ret;
	char* token;
	char* command;
	char* parameter[3];
	// utilizzo il valore speciale indicato come primo parametro altrimenti lo spazio viene visto come terminatore di stringa
	ret = scanf("%[^\n\r]%*c", buffer); 
	if(ret == 0){ 
		// altrimenti se digito solo il carattere \n rimane nel buffer
		// svuoto il buffer qualora sia stato inserito solo \n
		printf("Devi inserire un comando!\n");
		while(getchar() != '\n');
		return;
	}
	// Leggo il comando
	command = strtok(buffer, " ");
	token = strtok(NULL," ");
	// Leggo i parametri del comando
	for(numPar = 0; token != NULL; numPar++){
		parameter[numPar] = token;
    	token = strtok(NULL," ");
   	}
	// In funzione del nome del comando e del numero di parametri invoco la giusta funzione
	if(strcmp(command,"start") == 0 && numPar == 2)
		start(parameter[0], atoi(parameter[1]));
	else if(strcmp(command,"add") == 0 && numPar == 2)
		add(parameter[0], atoi(parameter[1]));
	else if(strcmp(command,"read") == 0 && numPar == 1) // funzione di supporto per leggere il contenuto di file
		readFile(parameter[0]);
	else if(strcmp(command,"get") == 0 && numPar == 3)
		get(parameter[0], parameter[1][0], parameter[2]);
	else if(strcmp(command,"get") == 0 && numPar == 2) // qualora non si specifichi il terzo parametro il periodo ricopre tutta la finestra temporale
		get(parameter[0], parameter[1][0], NULL);
	else if(strcmp(command, "stop") == 0 && numPar == 0)
		stop();
	else if(strcmp(command, "help") == 0 && numPar == 0)
		commandGuide();
	else
		printf("Il comando inserito non è valido, riprova!\n");
}

/* commandGuide: stampa la guida dei comandi */

void commandGuide(){
	printf("\n*************************************** PEER COVID STARTED %d ***********************************************\n\n", MyPort);
	printf("Digita un comando:\n\n");
	printf("1) start DS_addr DS_port : richiede al DS la connessione al network\n");
	printf("2) add type quantity : aggiunge al register odierno l'evento type con quantità quantity\n");
	printf("3) get aggr type period : effettua una richiesta di elaborazione sui dati\n");
	printf("4) stop : disconnessione dal network\n\n");
}

/* prompt: stampa il prompt dei comandi */

void prompt(){
	printf("peer@covid:~$ ");
	fflush(stdout);
}