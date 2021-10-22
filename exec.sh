make
read -p "Compilazione eseguita. Premi invio per eseguire..."
# COMMENTARE LE TRE SUCCESSIVE RIGHE DI CODICE SE NON SI VUOLE RICARICARE I REGISTRI CON I VALORI DI DEFAULT ALL'AVVIO
chmod +x createRegisters.sh
./createRegisters.sh
read -p "Ho ricaricato i registri con i valori di prova (Commentare le righe di codice evidenziate nel file exec.sh per evitare il ricaricamento dei registri ad ogni avvio). Premi invio per eseguire..."
chmod +x server
chmod +x peer
gnome-terminal -x sh -c "./server 8000; exec bash"

for port in {8002,8004,8006,8008,8010}
do
    gnome-terminal -x sh -c "./peer $port 127.0.0.1 8000; exec bash"
    
done
