#include "registerUtility.h"
#include "vars.h"

/* writePermissions: Aggiorna i permessi di scrittura su un file */

void writePermissions(int enabled, char* filename){
	FILE* f;
	int fildes;
	f = fopen(filename, "rb");
	if(f == NULL){
		return;
	}
	fildes = fileno(f);
	if(enabled == 1)
		fchmod(fildes, S_IRUSR | S_IRGRP | S_IWUSR);
	else 
		fchmod(fildes, S_IRUSR | S_IRGRP);
	fclose(f);
}

/* openRegister: funzione invocata all'avvio del peer, e alle 18 per aprire il registro relativo al giorno dopo
   Crea eventuali cartelle assenti necessarie per la memorizzazione e i file per memorizzare i dati aggregati:
   total.bin, sum.bin, diff.bin
   Se il registro di oggi già esiste si ripristinano solo i diritti di scrittura altrimenti si crea il file */

void openRegister(char* name){
	FILE* f;
	struct stat statRes;
	char dirName[50];
	char specialName[50];
	sprintf(dirName, "registers");
	if(stat(dirName, &statRes) == -1){
		printf("Creo la directory contenente i registri, non esisteva..\n");
    	mkdir(dirName, 0700);
	}
	sprintf(dirName, "registers/%d", MyPort);
	if(stat(dirName, &statRes) == -1){
		printf("Creo la mia directory contenente i registri, non esisteva..\n");
    	mkdir(dirName, 0700);
	}
	sprintf(specialName, "registers/%d/total.bin", MyPort);
	if(access(specialName,F_OK)!=0){
		f = fopen(specialName, "wb");
		if(f == NULL){
			printf("Impossibile aprire il file dei totali!\n");
			return;
		}
	}
	sprintf(specialName, "registers/%d/sum.bin", MyPort);
	if(access(specialName,F_OK)!=0){
		f = fopen(specialName, "wb");
		if(f == NULL){
			printf("Impossibile aprire il file degli aggregati!\n");
			return;
		}
	}
	sprintf(specialName, "registers/%d/diff.bin", MyPort);
	if(access(specialName,F_OK)!=0){
		f = fopen(specialName, "wb");
		if(f == NULL){
			printf("Impossibile aprire il file degli aggregati delle differenze!\n");
			return;
		}
	}
	if(access(name,F_OK) != 0){
		f = fopen(name, "wb");
		if(f == NULL){
			printf("Impossibile aprire il registro di oggi!\n");
			return;
		}
		// inserisco due entry con valore nulllo
		fileEntry.port = MyPort;
		fileEntry.type = 'T';
		fileEntry.quantity = 0;
		strcpy(fileEntry.IP, "127.0.0.1");
		fwrite(&fileEntry, sizeof(fileEntry), 1, f);
		fileEntry.port = MyPort;
		fileEntry.type = 'N';
		fileEntry.quantity = 0;
		strcpy(fileEntry.IP, "127.0.0.1");
		fwrite(&fileEntry, sizeof(fileEntry), 1, f);
		fclose(f);
	} else {
		// se il registro è stato già creato nella giornata di oggi ripristino semplicemente i diritti di scrittura
		writePermissions(1, name);
	}
}

/* closeRegister: funzione invocata all'arresto del peer e alle 18 per aprire il registro del giorno successivo
   Memorizza le entry quotidiane, aggiornando eventuali entry già presenti, e toglie i permessi di scrittura al file quotidiano */

void closeRegister(char* filename){
	struct entry* e;
	struct fileEntry entryT, entryN;
	FILE* f;
	FILE* ftmp;
	// se non ho inserito alcuna entry oggi modifico solo i permessi del file rimuovendo il permesso di scrittura	
	if(todayRegister == 0){
		writePermissions(0, filename);
		return;
	}
	f = fopen(filename, "rb"); 
	if(f == NULL){
		printf("Problemi nel salvataggio!\n");
		return;
	}
	printf("Salvo prima il contenuto del mio registro giornaliero..\n");
	entryT.type = 'T';
	entryN.type = 'N';
	entryT.quantity = entryN.quantity = 0;
	// sommo il contenuto delle entry memorizzate oggi realizzando delle entry cumulative
	for(e = todayRegister; e != 0; e = e->next){
		if(e->type == 'T')
			entryT.quantity += e->quantity;
		else if(e->type == 'N')
			entryN.quantity += e->quantity;
	}
	entryT.port = MyPort;
	entryN.port = MyPort;
	strcpy(entryN.IP, "127.0.0.1");
	strcpy(entryT.IP, "127.0.0.1");
	// uso un file temporaneo per poter sovrascrivere le due entry già registrate
	ftmp = fopen("temp.txt", "ab"); 
	if(!ftmp) {
		printf("Problemi nel salvataggio!\n");
		return;
	}
	// leggo le prime due entry che conterranno il numero di casi e tamponi registrati oggi da questo peer in delle sessioni giornaliere precedenti
	fread(&fileEntry, sizeof(fileEntry), 1, f);
	entryT.quantity += fileEntry.quantity;
	fread(&fileEntry, sizeof(fileEntry), 1, f);
	entryN.quantity += fileEntry.quantity;
	// aggiorno il numero di casi e tamponi
	fwrite(&entryT, sizeof(fileEntry), 1, ftmp);
	fwrite(&entryN, sizeof(fileEntry), 1, ftmp);
	// copio le righe dalla terza all'ultima, se presenti, quelle di altri neighbors quindi
	while(fread(&fileEntry, sizeof(fileEntry), 1, f))
		fwrite(&fileEntry, sizeof(fileEntry), 1, ftmp);
	fclose(f);
	fclose(ftmp);
	// elimino il file originale
	remove(filename);  
	// rinomino il file temporaneo
	rename("temp.txt", filename);
	printf("Ho inserito i seguenti totali nel file odierno:\nT: %d \nN: %d\n",entryT.quantity, entryN.quantity);
	// disabilito i permessi di scrittura sul file di oggi
	writePermissions(0, filename);
	todayRegister = 0;
}


