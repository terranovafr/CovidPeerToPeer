# make rule primaria
all: peer server

# make rule per i peer
peer: peer.o utility/peer/timeUtility.o utility/peer/registerUtility.o utility/peer/connectionUtility.o utility/peer/commandsUtility.o
	gcc -Wall peer.o utility/peer/connectionUtility.o utility/peer/registerUtility.o utility/peer/commandsUtility.o utility/peer/timeUtility.o -o peer

# make rule per il server
server: server.o utility/server/commandsUtility.o utility/server/listUtility.o utility/server/commandsUtility.h utility/server/listUtility.h utility/server/vars.h
	gcc -Wall server.o utility/server/listUtility.o utility/server/commandsUtility.o -o server

serverCommandsUtility: utility/server/commandsUtility.o utility/server/commandsUtility.h utility/server/vars.h
	gcc -Wall utility/server/commandsUtility.c -o utility/server/commandsUtility.o 

serverListUtility: utility/server/listUtility.o  utility/server/listUtility.h utility/server/vars.h
	gcc -Wall utility/server/listUtility.c -o utility/server/listUtility.o 

peerConnectionUtility: utility/peer/connectionUtility.o  utility/peer/connectionUtility.h utility/peer/vars.h
	gcc -Wall utility/peer/connectionUtility.c -o utility/peer/connectionUtility.o 

peerTimeUtility: utility/peer/timeUtility.o  utility/peer/timeUtility.h utility/peer/vars.h
	gcc -Wall utility/peer/timeUtility.c -o utility/peer/timeUtility.o 

peerCommandsUtility: utility/peer/commandsUtility.o utility/peer/commandsUtility.h utility/peer/vars.h
	gcc -Wall utility/peer/commandsUtility.c -o utility/peer/commandsUtility.o 

peerRegisterUtility: utility/peer/registerUtility.o utility/peer/registerUtility.h utility/peer/vars.h
	gcc -Wall utility/peer/registerUtility.c -o utility/peer/registerUtility.o 

# pulizia dei file della compilazione
clean:
	rm *o utility/peer/*o utility/server/*o peer server
