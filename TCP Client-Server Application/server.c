#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/tcp.h>

#define MAX_CONNECTIONS 10000
#define MAX 256

typedef struct {
    char name[51];
    int sf;
} topic;

typedef struct {
    char id[100];
} connect_to_server;

typedef struct {
    int size;
} next_msg_size;

typedef struct {
    char type[16];
    char topic[51];
    int sf;
} msg_from_subscriber;

typedef struct {
    int data_type;
    char topic[51];
    char ip[16];
    int port;
    char payload[1501];
} tcp_message;

typedef struct {
    char topic[50];
    uint8_t data_type;
    char content[1500];
} udp_message;

typedef struct {
    int socket;
    char id[100];
    unsigned int ip;
    unsigned short port;
    topic *topics;
    int nb_of_topics;
    tcp_message **waiting_messages;
    int nb_of_waiting_messages;
} client;

void move_udp_msg_to_tcp_msg(udp_message *udp_msg, tcp_message *tcp_msg) {
    // move data type & topic from udp_message structure to tcp_message structure
    tcp_msg->data_type = udp_msg->data_type;
    // be sure that tcp_msg->topic ends with '\0'
    strncpy(tcp_msg->topic, udp_msg->topic, 50);

    if (udp_msg->data_type == 0) {
        // data type is INT
        char sign = *(udp_msg->content);
        uint32_t integer = ntohl(*(uint32_t *)(udp_msg->content + 1));
        long long payload;
        if (sign == 0)
            payload = (long long)integer;
        else
            payload = (-1) * (long long)integer;
        sprintf(tcp_msg->payload, "%lld", payload);

    } else if (udp_msg->data_type == 1) {
        // data type is SHORT_REAL
        float payload = ntohs(*(uint16_t *)(udp_msg->content)) / 100.0;
        if (payload == (int)payload)
            sprintf(tcp_msg->payload, "%d", (int)payload);
        else
            sprintf(tcp_msg->payload, "%.2f", payload);

    } else if (udp_msg->data_type == 2) {
        // data type is FLOAT
        char sign = *(char *)(udp_msg->content);
        uint32_t integer = ntohl(*(uint32_t *)(udp_msg->content + 1));
        uint8_t power = *(uint8_t *)(udp_msg->content + 5);
        float fractional_number = integer;
        for (int i = 0; i < power; i++)
            fractional_number /= 10;
        float payload;
        if (sign == 0)
            payload = fractional_number;
        else
            payload = (-1) * fractional_number;
        sprintf(tcp_msg->payload, "%.4f", payload);

    } else if (udp_msg->data_type == 3) {
        // data type is STRING
        // tcp_msg->payload[1500] must always be '\0'
        strncpy(tcp_msg->payload, udp_msg->content, 1500);
    }
}

int main(int argc, char *argv[]) {
    // verify if nb of args is correct
    if (argc != 2) {
        perror("Error! Incorrect number of args in server.\n");
        return -1;
    }

    // deactivate buffering
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // auxiliary buffer
    char buf[MAX];
    // current number of clients allowed; it will increase if more clients are connected
    int crt_max_conn = MAX_CONNECTIONS;

    int udp_sock, tcp_sock, client_sock;
    struct sockaddr_in server_addr_tcp, server_addr_udp, client_addr;

    // clean buffers and structures
    memset(&server_addr_udp, 0, sizeof(server_addr_udp));
    memset(&server_addr_tcp, 0, sizeof(server_addr_tcp));
    memset(&client_addr, 0, sizeof(client_addr));

    // create UDP socket
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(udp_sock < 0){
        perror("Error! Can't create UDP socket.\n");
        return -1;
    }

    // set port and IP on UDP connection
    server_addr_udp.sin_family = AF_INET;
    server_addr_udp.sin_port = htons(atoi(argv[1]));
    server_addr_udp.sin_addr.s_addr = INADDR_ANY;

    // bind the UDP socket
    if(bind(udp_sock, (struct sockaddr*)&server_addr_udp, sizeof(server_addr_udp)) < 0) {
        perror("Error! Can't bind UDP socket.\n");
        return -1;
    }

    // use setsockopt to free the UDP port faster after we end running the program
    int enable = 1;
    if (setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("Error! Function setsockopt failed in server.\n");
        return -1;
    }

    // Create TCP socket
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(tcp_sock < 0){
        perror("Error! Can't create TCP socket.\n");
        return -1;
    }

    // set port and ip on the TCP connection
    server_addr_tcp.sin_family = AF_INET;
    server_addr_tcp.sin_port = htons(atoi(argv[1]));
    server_addr_tcp.sin_addr.s_addr = INADDR_ANY;

    // bind the TCP socket
    if(bind(tcp_sock, (struct sockaddr*)&server_addr_tcp, sizeof(server_addr_tcp)) < 0) {
        perror("Error! Can't bind TCP socket.\n");
        return -1;
    }

    // use setsockopt to free the TCP port faster after we end running the program
    enable = 1;
    if (setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("Error! Function setsockopt failed in server.\n");
        return -1;
    }

    // use setsockopt to disable Nagle's algorithm
    enable = 1;
    if (setsockopt(tcp_sock, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0) {
        perror("Error! Function setsockopt failed in server.\n");
        return -1;
    }

    // listen on TCP socket
    if(listen(tcp_sock, 1) < 0) {
        perror("Error! Can't listen on TCP socket.\n");
        return -1;
    }

    // array of clients
    client *clients = calloc(MAX_CONNECTIONS, sizeof(client));
    if (!clients) {
        perror("Error! Can't allocate client array.\n");
        return -1;
    }

    // first 3 clients will be null, because they don't have an equivalent
    // subscriber in poll_fds (first 3 fds are stdin, udp_sock and tcp_sock)
    int nb_clients = 3;
    memset(clients, 0, 3 * sizeof(client));

    // create a poll structure array for all sockets
    struct pollfd poll_fds[MAX_CONNECTIONS];

    // configure base file descriptors in poll structure
    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = udp_sock;
    poll_fds[1].events = POLLIN;
    poll_fds[2].fd = tcp_sock;
    poll_fds[2].events = POLLIN;

    while (1) {
        // wait for a client to connect
        int ret = poll(poll_fds, nb_clients, -1);
        if (ret < 0) {
            perror("Error! Function poll failed.\n");
            return -1;
        }

        // find out which socket wanted to connect
        for (int i = 0; i < nb_clients; i++) {
            if (poll_fds[i].revents & POLLIN) {

                if (poll_fds[i].fd == STDIN_FILENO) {
                    // if input came from stdin, read it
                    memset(buf, 0, MAX);
                    if (read(STDIN_FILENO, buf, MAX * sizeof(char)) < 0) {
                        perror("Error! Can't read from standard input.\n");
                        return -1;
                    }

                    // if message is "exit", close all the clients
                    if (!strncmp(buf, "exit", 4)) {
                        for (int i = 3; i < nb_clients; i++) {
                            if (poll_fds[i].fd != -1) {
                                free(clients[i].topics);
                                free(clients[i].waiting_messages);

                                // send message about the size of "exit" message
                                next_msg_size size;
                                memset(&size, 0, sizeof(next_msg_size));
                                size.size = 4;
                                if (send(poll_fds[i].fd, &size, sizeof(next_msg_size), 0) < 0) {
                                    perror("Error! Can't send exit message to client.\n");
                                    return -1;
                                }

                                // send "exit" message to current client
                                tcp_message tcp_msg;
                                memset(&tcp_msg, 0, sizeof(tcp_message));
                                tcp_msg.data_type = -1;
                                if (send(poll_fds[i].fd, &tcp_msg, size.size, 0) < 0) {
                                    perror("Error! Can't send exit message to client.\n");
                                    return -1;
                                }

                                close(poll_fds[i].fd);
                            }
                        }
                        // close UDP and TCP sockets
                        close(tcp_sock);
                        close(udp_sock);
                        free(clients);
                        return 0;
                    } else {
                        perror("Error! Invalid input.\n");
                    }
                } else if (poll_fds[i].fd == udp_sock) {
                    // if message came from the UDP socket
                    char udp_buf[sizeof(udp_message)];
                    memset(udp_buf, 0, sizeof(udp_message));
                    struct sockaddr_in udp_sock_addr;
                    long unsigned int len_udp_sock_addr = sizeof(struct sockaddr_in);

                    // get message
                    if (recvfrom(udp_sock, udp_buf, 1551, 0,
                        (struct sockaddr*)&udp_sock_addr, (socklen_t * restrict)&len_udp_sock_addr) < 0) {
                            perror("Error! Can't receive message from UDP client.\n");
                            return -1;
                        }

                    udp_message *udp_msg = (udp_message *)udp_buf;
                    tcp_message *tcp_msg = calloc(1, sizeof(tcp_message));
                    if (!tcp_msg) {
                        perror("Error! Can't allocate memory.\n");
                        return -1;
                    }

                    tcp_msg->port = htons(udp_sock_addr.sin_port);
                    strcpy(tcp_msg->ip, inet_ntoa(udp_sock_addr.sin_addr));

                    // copy message to tcp_message structure
                    move_udp_msg_to_tcp_msg(udp_msg, tcp_msg);

                    // search through clients
                    for (int i = 3; i < nb_clients; i++) {
                        // search through topics they are interested in
                        for (int j = 0; j < clients[i].nb_of_topics; j++) {
                            // if we find current topic stop and verify whether the client is online
                            // or if they are offline and with sf == 1, to save the message
                            if (!strcmp(clients[i].topics[j].name, tcp_msg->topic)) {
                                if (poll_fds[i].fd == -1 && clients[i].topics[j].sf) {
                                    // if there isn't enough space allocated in clients[i].waiting_messages,
                                    // reallocate
                                    if ((clients[i].nb_of_waiting_messages % MAX == 0)
                                        && (clients[i].nb_of_waiting_messages != 0)) {
                                        clients[i].waiting_messages = realloc(clients[i].waiting_messages,
                                            (MAX + clients[i].nb_of_waiting_messages));
                                        if (!clients[i].waiting_messages) {
                                            perror("Error! Can't allocate memory.\n");
                                            return -1;
                                        }
                                    }
                                    clients[i].waiting_messages[clients[i].nb_of_waiting_messages] = tcp_msg;
                                    clients[i].nb_of_waiting_messages++;
                                } else if (poll_fds[i].fd != -1) {
                                    int len = sizeof(tcp_message) - (1501
                                        - strlen(tcp_msg->payload) - 1);

                                    // send client info about the length of the message
                                    next_msg_size size;
                                    size.size = len;
                                    if (send(poll_fds[i].fd, &size, sizeof(next_msg_size), 0) < 0) {
                                        perror("Error! Can't send message to client.\n");
                                        return -1;
                                    }

                                    // send message to client
                                    if (send(poll_fds[i].fd, tcp_msg, size.size, 0) < 0) {
                                        perror("Error! Can't send message to client.\n");
                                        return -1;
                                    }
                                }
                            }
                        }
                    }
                    free(tcp_msg);
                } else if (poll_fds[i].fd == tcp_sock) {
                    // if message came from the TCP socket
                    unsigned int client_length = sizeof(client_addr);
                    // accept the new client
                    client_sock = accept(tcp_sock, (struct sockaddr*)&client_addr, &client_length);
                    if (client_sock < 0) {    
                        perror("Error! Can't accept client on TCP socket.\n");
                        return -1;
                    }

                    // deactivate Nagle algorithm
                    int enable = 1;
                    if (setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0) {
                        perror("Error! Function setsockopt failed in server.\n");
                        return -1;
                    }

                    // receive data from the new client socket
                    connect_to_server conn;
                    memset(&conn, 0, sizeof(connect_to_server));
                    if (recv(client_sock, &conn, MAX, 0) < 0) {
                        perror("Error! Can't receive from client.\n");
                        return -1;
                    }

                    memset(buf, 0, MAX);
                    strcpy(buf, conn.id);

                    // verify if client is already saved in the array of clients
                    int client_exists = 0;
                    for (int j = 3; j < nb_clients; j++) {
                        if (!strcmp(clients[j].id, buf)) {
                            // if it is, verify if it's turned on or not
                            if (clients[j].socket == -1 && poll_fds[j].fd == -1) {
                                // if it's turned off, reconnect it
                                printf("New client %s connected from %s:%d.\n",
                                    buf, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                                // send all messages on topics which had sf == 1 while the client was offline
                                for (int k = 0; k < clients[j].nb_of_waiting_messages; k++) {
                                    int len = sizeof(tcp_message) - (1501
                                        - strlen(clients[j].waiting_messages[k]->payload) - 1);

                                    next_msg_size size;
                                    size.size = len;
                                    // send info about current waiting message size
                                    if (send(client_sock, &size, sizeof(next_msg_size), 0) < 0) {
                                        perror("Error! Can't send to client.\n");
                                        return -1;
                                    }

                                    // send current waiting message
                                    if (send(client_sock, clients[j].waiting_messages[k], size.size, 0) < 0) {
                                        perror("Error! Can't send to client.\n");
                                        return -1;
                                    }
                                }
                                clients[j].nb_of_waiting_messages = 0;

                                // update the socket info
                                clients[j].socket = client_sock;
                                poll_fds[j].fd = client_sock;
                                poll_fds[j].events = POLLIN;
                            } else {
                                // if it was already on, then print this info in server
                                // and close the socket that wanted to connect
                                printf("Client %s already connected.\n", buf);

                                // send message about size
                                next_msg_size size;
                                memset(&size, 0, sizeof(next_msg_size));
                                size.size = 4;
                                if (send(client_sock, &size, sizeof(next_msg_size), 0) < 0) {
                                    perror("Error! Can't send to client.\n");
                                    return -1;
                                }

                                // send exit message to client trying to connect with the same ID
                                tcp_message tcp_msg;
                                memset(&tcp_msg, 0, sizeof(tcp_message));
                                tcp_msg.data_type = -1;
                                if (send(client_sock, &tcp_msg, size.size, 0) < 0) {
                                    perror("Error! Can't send to client.\n");
                                    return -1;
                                }
                            }
                            client_exists = 1;
                        }
                    }

                    if (!client_exists) {
                        // if the client doesn't exist, then create it
                        // if there isn't enough space in clients array,
                        // allocate more memory
                        if (nb_clients == crt_max_conn) {
                            crt_max_conn += MAX_CONNECTIONS;
                            clients = realloc(clients, crt_max_conn);
                            if (!clients) {
                                perror("Error! Can't allocate memory.\n");
                                return -1;
                            }
                        }
                        // create new entry in clients array
                        memcpy(clients[nb_clients].id, buf, MAX);
                        clients[nb_clients].socket = client_sock;
                        clients[nb_clients].ip = (unsigned int)client_addr.sin_addr.s_addr;
                        clients[nb_clients].port = (unsigned short)client_addr.sin_port;
                        clients[nb_clients].nb_of_topics = 0;
                        clients[nb_clients].nb_of_waiting_messages = 0;
                        clients[nb_clients].topics = calloc(MAX, sizeof(topic));
                        if (!clients[nb_clients].topics) {
                            perror("Can't allocate memory.\n");
                            return -1;
                        }
                        clients[nb_clients].waiting_messages = calloc(MAX, sizeof(tcp_message *));
                        if (!clients[nb_clients].waiting_messages) {
                            perror("Can't allocate memory.\n");
                            return -1;
                        }
                        printf("New client %s connected from %s:%d.\n",
                            buf, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                        // update info about sockets
                        poll_fds[nb_clients].fd = client_sock;
                        poll_fds[nb_clients].events = POLLIN;
                        nb_clients += 1;
                    }
                } else {
                    // get message sent by subscriber to server
                    msg_from_subscriber msg_from_sub;
                    if (recv(poll_fds[i].fd, &msg_from_sub, sizeof(msg_from_subscriber), 0) < 0) {
                        perror("Error! Can't receive from client.\n");
                        return -1;
                    }

                    if (!strcmp(msg_from_sub.type, "exit")) {
                        // message is exit => disconnect client
                        printf("Client %s disconnected.\n", clients[i].id);
                        clients[i].socket = -1;
                        close(poll_fds[i].fd);
                        poll_fds[i].fd = -1;
                    } else if (!strcmp(msg_from_sub.type, "subscribe")) {
                        // message is subscribe => add new topic in which client "i" is interested
                        if (clients[i].socket != -1) {
                            // if there isn't enough space allocated in clients[i].topics, reallocate
                            if ((clients[i].nb_of_topics % MAX == 0) && (clients[i].nb_of_topics != 0)) {
                                clients[i].topics = realloc(clients[i].topics, (MAX + clients[i].nb_of_topics));
                                if (!clients[i].topics) {
                                    perror("Error! Can't allocate memory.\n");
                                    return -1;
                                }
                            }
                            strcpy(clients[i].topics[clients[i].nb_of_topics].name, msg_from_sub.topic);
                            clients[i].topics[clients[i].nb_of_topics].sf = msg_from_sub.sf;
                            clients[i].nb_of_topics++;
                        }
                    } else if (!strcmp(msg_from_sub.type, "unsubscribe")) {
                        // message is unsubscribe => remove topic from list of topics in which
                        // client "i" is interested
                        if (clients[i].socket != -1)
                            for (int j = 0; j < clients[i].nb_of_topics; j++)
                                if (!strcmp(clients[i].topics[j].name, msg_from_sub.topic)) {
                                    for (int k = j; k < clients[i].nb_of_topics-1; k++)
                                        clients[i].topics[k] = clients[i].topics[k+1];
                                    clients[i].nb_of_topics -= 1;
                                    break;
                                }
                    } else {
                        perror("Error! Incorrect message.\n");
                    }
                }
            }
        }
    }

    // se elibereaza vectorul de clienti
    free(clients);
    return 0;
}
