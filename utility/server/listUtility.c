#include "listUtility.h"
#include "vars.h"

/* insert: Inserisce il peer in lista */

void insert(char* IP, int port, int peersd){
	struct peer* q;
	struct peer* p;
	struct peer* r = malloc(sizeof(struct peer));
	if(r == NULL){
		printf("Impossibile allocare memoria\n");
		exit(-1);
	}
	r->port = port; strcpy(r->IP, IP); r->sd = peersd;
	//se la lista è vuota
	if(head == NULL){
		head = tail = r;
		r->next = 0;
		return;
	}
	//inserimento in testa
	if(head->port >= port){
		r->next = head;
		head = r;
		return;
	}
	//inserimento in coda
	if(tail->port < port){
		r->next = 0;
		tail->next = r;
		tail = r;
		return;
	}
	//altrimenti trovo il punto intermedio in cui inserire
	for(p = head, q = p->next; q != 0 && q->port < port; p=q, q = q->next);
	p->next = r;
	r->next = q;
}

/* isRegistered: Controlla se il peer fa parte della lista */

int isRegistered(char* IP, int port){
	struct peer* p;
	for(p = head; p != 0; p = p->next)
		if(strcmp(p->IP, IP)==0 && p->port == port)
			return 1;
	return 0;
}

/* delete: Cancella un peer dalla lista */

void delete(char* IP, int port){
	struct peer* q;
	struct peer* p;
	// cancellazione dell'elemento in testa
	if(head->port == port && strcmp(head->IP, IP) == 0){
		p = head->next;
		free(head);
		head = p;
		return;
	}
	for(p = head, q = p->next; !(q->port == port && strcmp(q->IP, IP) == 0); p=q, q = q->next);
	if(tail == q)
		tail = p;
	p->next = q->next;
	free(q);
}
