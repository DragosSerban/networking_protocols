PCom - Tema 2 - Serban Dragos-Andrei, 322CB

Au fost folosite urmatoarele structuri de date:
- client (contine id, ip, port, socket, 2 vectori -> unul de topics, ce contine topicurile la care
este abonat clientul curent, unul de waiting_messages, care contine mesajele ce asteapta sa fie
trimise - sf == 1 si nu clientul a fost deconectat de la server)
- udp_message (structura cu topic + tip_date + continut, primite de server de la un client udp)
- tcp_message (structura ce contine aceleasi campuri ca udp_message + port + ip -> date trimise
de la server la un client tcp, ce contine datele preluate de la un client udp)
- msg_from_subscriber (structura ce contine datele trimise de un client la server - exit, subscribe,
unsubscribe - topic si sf sunt optionale)
- next_msg_size (structura utilizata pt a trimite lungimea unui mesaj de la server la subscriber -
pt a fi eficient, sa nu se trimita toata dimensiunea unui tcp_message)
- connect_to_server (structura ce contine un string - trimite id-ul clientului catre server atunci
cand se conecteaza)
- topic (contine campurile id si sf)

Server:
- cand serverul se deschide, verifica numarul argumentelor sa fie corect, dezactiveaza bufferingul,
zeroizeaza structurile si bufferele necesare, creeaza socketul udp, ii da bind, foloseste sockopt
pe el pentru a il face sa se elibereze rapid, creeaza socketul tcp, ii da bind, foloseste sockopt
pt a-l face sa se elibereze rapid si pt a dezactiva algoritmul lui Nagle, asteapta conexiuni pe
portul tcp
- creeaza un vector de clienti alocat dinamic, caruia ii poate creste de fiecare data cand este
necesar dimensiunea, se creeaza o structura poll si initial se introduc fd-urile pt stdin, udp si
tcp
- se asteapta sa se primeasca date pe o intrare din structura, atunci cand se primesc date, se
verifica:
1. daca au fost preluate de la stdin, se va citi (serverul accepta de la stdin doar comanda "exit");
se dezaloca memoria utilizata, se trimite "exit" la toti clientii, se inchid toti socketii
2. daca se primesc date de la udp_sock, se citesc date intr-o structura udp_message, se obtin ip-ul
si portul clientului udp, se creeaza o structura tcp_message in care se vor parsa datele primite de
la udp, prin intermediul functiei move_udp_msg_to_tcp_msg (aceasta copiaza topicul, verifica tipul
de date primit si parseaza conform cerintei - daca este INT, se obtine byte-ul de semn, numarul
initial si se formeaza nr rezultat prin inmultirea cu 1 sau cu -1; daca este SHORT_REAL, se
calculeaza impartirea la 100 si se parseaza numarul in functie de ce tip rezultat este, cu virgula
sau fara; daca este FLOAT, se obtine byte-ul de semn, numarul uint32_t si puterea, se imparte
numarul la (10 la puterea data), se calculeaza numarul cu semn si se formateaza, daca este STRING,
se copiaza 1500 de octeti din udp_msg->content in tcp_msg->payload, avand grija ca
tcp_msg->payload[1500] sa fie neaparat '\0', pt a marca sfarsitul stringului); apoi se cauta
in vectorul de clienti care clienti sunt abonati la topicul curent; daca clientul curent e
interesat si e conectat mai intai se trimite o structura initiala next_msg_size cu dimensiunea
structurii principale pt eficienta (stringul ce ocupa in structura principala 1500 de caractere
nu are de obicei atat de multe litere), iar apoi se trimite structura de tip tcp_message creata
anterior; altfel, daca clientul curent nu este conectat si are setat sf == 1 pentru topicul curent,
atunci se salveaza intr-un vector alocat dinamic, daca vectorul este plin, atunci se realoca
3. daca se primesc date de la tcp_sock, atunci se accepta noul client si se dezactiveaza algoritmul
Nagle pt socketul noului client; se primeste data cu noua conexiune (structura connect_to_server) si
se citeste id-ul; se verifica mai intai daca clientul a fost salvat anterior in vector, daca a fost
gasit atunci se verifica daca este pornit sau nu; daca este oprit atunci se porneste si se trimit
toate mesajele care asteptau in waiting_messages, se actualizeaza informatiile despre conexiune;
daca clientul era deja pornit atunci se trimite un mesaj de exit la clientul care a incercat sa se
conecteze cu acelasi id; daca clientul nu exista de la bun inceput, atunci se adauga - daca vectorul
de clienti nu este suficient de mare, atunci se realoca spatiu, apoi se creeaza o intrare noua in
vector si se completeaza campurile cu informatiile despre noul client, se introduce noul socket in
structura poll
4.daca se primeste un mesaj de la un subscriber, se verifica ce tip de mesaj e (exit, subscribe sau
unsubscribe) - daca mesajul este "exit", se deconecteaza clientul, clients[i].socket si poll_fds[i].fd
devin -1; daca mesajul este "subscribe", se verifica daca mai exista spatiu in vectorul de topicuri
al clientului curent (daca nu mai este, atunci se realoca), se introduce in vector topicul; daca
mesajul este "unsubscribe", atunci topicul va fi sters din vectorul de topicuri al clientului curent

Subscriber:
- se verifica mai intai ca numarul de argumente sa fie corect, se dezactiveaza bufferingul, se
creeaza socketul tcp si o structura sockaddr_in; se foloseste setsockopt pt a ne asigura ca portul
va fi eliberat cat de repede cu putinta dupa inchiderea sa si se mai foloseste inca odata pt a
dezactiva algoritmul Nagle
- clientul se va conecta la server prin conexiunea tcp realizata anterior
- se foloseste o structura de tipul connect_to_server pt a trimite id-ul clientului catre server
- se va folosi o structura poll pentru a tine cont de toti socketii / fd-urile; la inceput este
adaugat fd-ul pt stdin, iar apoi pt conexiunea tcp
- se asteapta sa se primeasca un mesaj de pe unul din socketi:
1. daca s-a primit mesaj de la stdin, atunci acesta se citeste si se va parsa cu strtok comanda de
la tastatura; se va crea o structura de tip msg_from_subscriber pt a trimite comanda mai departe la
server - daca comanda este "exit", se va inchide clientul si acesta va indorma serverul cu privire la
inchidere; daca comanda este "subscribe", se va trimite un mesaj la server pt ca acesta sa aboneze
clientul la topic; daca comanda este "unsubscribe", se va trimite un mesaj la server pt ca acesta sa
dezaboneze clientul de la topic
2. daca mesajul a venit de la server, acesta este preluat (mai intai mesajul scurt cu dimensiunea
mesajului principal, iar apoi mesajul principal, pentru a se citi eficient); se verifica tipul
mesajului: daca este -1 => exit, se inchide conexiunea tcp si clientul, altfel se vor printa cu
ajutorul functiei print_subscription_notification urmatoarele:
- daca tipul este 0 => se printeaza mesaj pentru INT
- daca tipul este 1 => se printeaza mesaj pentru SHORT_REAL
- daca tipul este 2 => se printeaza mesaj pentru FLOAT
- daca tipul este 3 => se printeaza mesaj pentru STRING
- altfel => se printeaza eroare
