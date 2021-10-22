#include "vars.h"
#include "commandsUtility.h"

/* commandRead: Funzione che si occupa della lettura del comando, dello split dei parametri e dell'invocazione della giusta funzione*/

void commandRead(){
	int ret;
	char c;
	char* command;
	char* parameter;
	ret = scanf("%[^\n\r]%*c", commandLine);
	if(ret== 0){
		// altrimenti se digito solo il carattere \n rimane nel buffer
		// svuoto il buffer qualora sia stato inserito solo \n
		printf("Devi inserire un comando!\n");
		while(getchar() != '\n');
		return;
	}
	command = strtok(commandLine, " ");
	parameter = strtok(NULL," "); // si ha al più un solo parametro
	if(strcmp(command,"help") == 0 && parameter == NULL)
		commandGuide();
	else if(strcmp(command,"status") == 0 && parameter == NULL)
		status();
	else if(strcmp(command,"showneighbor") == 0 && parameter == NULL)
		showAllNeighbors();
	else if(strcmp(command, "showneighbor") == 0)
		showNeighbors(atoi(parameter));
	else if(strcmp(command, "esc") == 0 && parameter == NULL)
		esc();
	else if(strcmp(command, "exit") == 0 && parameter == NULL)
		exit(0);
	else if(strcmp(command, "print") == 0 && parameter == NULL)
		printRegister();
	else if(strcmp(command, "read") == 0 && parameter != NULL)
		readFile(parameter);
	else
		printf("Il comando inserito non è valido, riprova!\n");
}

/* commandGuide: Stampa la guida dei comandi */

void commandGuide(){
	printf("\n*************************************** DS COVID STARTED ***********************************************\n\n");
	printf("Digita un comando:\n\n");
	printf("1) help : mostra i dettagli dei comandi\n");
	printf("2) status : mostra un elenco dei peer connessi\n");
	printf("3) showneighbor <peer> :mostra i neighbor di un peer\n");
	printf("4) esc : chiude il DS\n\n");
}

/* prompt: stampa il prompt dei comandi */

void prompt(){
	printf("DS@covid:/# ");
	fflush(stdout);
}
