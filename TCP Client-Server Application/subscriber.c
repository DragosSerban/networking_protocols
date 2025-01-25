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

void print_subscription_notification(tcp_message *tcp_msg) {
    // prints message from server (from subscribed topics)
    if (tcp_msg->data_type == 0) {
        // TYPE INT
        printf("%s:%d - %s - %s - %s\n", tcp_msg->ip, tcp_msg->port,
            tcp_msg->topic, "INT", tcp_msg->payload);
    } else if (tcp_msg->data_type == 1) {
        // TYPE SHORT_REAL
        printf("%s:%d - %s - %s - %s\n", tcp_msg->ip, tcp_msg->port,
            tcp_msg->topic, "SHORT_REAL", tcp_msg->payload);
    } else if (tcp_msg->data_type == 2) {
        // TYPE FLOAT
        printf("%s:%d - %s - %s - %s\n", tcp_msg->ip, tcp_msg->port,
            tcp_msg->topic, "FLOAT", tcp_msg->payload);
    } else if (tcp_msg->data_type == 3) {
        // TYPE STRING
        printf("%s:%d - %s - %s - %s\n", tcp_msg->ip, tcp_msg->port,
            tcp_msg->topic, "STRING", tcp_msg->payload);
    } else {
        perror("Error! Message isn't correct.\n");
    }
}

int main(int argc, char *argv[]) {
    // verify if nb of args is correct
    if (argc != 4) {
        perror("Error! Incorrect number of args in subscriber.\n");
        return -1;
    }

    // deactivate buffering
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // auxiliary buffers in which we store data before sending it
    char buf[MAX];

    // create TCP socket
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0){
        perror("Error! Subscriber socket can't be created.\n");
        return -1;
    }

    // set port and IP on TCP connection
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[3]));
    server_addr.sin_addr.s_addr = inet_addr(argv[2]);

    // use setsockopt to free the port faster after we end running the program
    int enable = 1;
    if (setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("Error! Function setsockopt failed in subscriber.\n");
        return -1;
    }
    // use setsockopt to deactivate Nagle algorithm
    enable = 1;
    if (setsockopt(tcp_fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0) {
        perror("Error! Function setsockopt failed in subscriber.\n");
        return -1;
    }

    // connect to server via TCP socket
    if (connect(tcp_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Error! Can't connect to server.\n");
        return -1;
    }

    // use this structure to connect client to server
    connect_to_server *conn = calloc(1, sizeof(connect_to_server));
    if (!conn) {
        perror("Error! Can't allocate memory.\n");
        return -1;
    }
    strcpy(conn->id, argv[1]);

    // send client IP to server
    if (send(tcp_fd, conn, MAX, 0) < 0) {
        perror("Error! Client ID can't be sent to server!\n");
        return -1;
    }

    free(conn);

    // create a poll structure array for all sockets
    struct pollfd poll_fds[2];
    int nb_clients = 2;

    // save poll structure's file descriptors
    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = tcp_fd;
    poll_fds[1].events = POLLIN;

    while(1) {
        // wait to receive a message from a socket
        int ret = poll(poll_fds, nb_clients, -1);
        if (ret < 0) {
            perror("Error! Function poll failed.\n");
            return -1;
        }

        // find out who wanted to send message
        for (int i = 0; i < nb_clients; i++) {
            if (poll_fds[i].revents & POLLIN) {

                if (poll_fds[i].fd == STDIN_FILENO) {
                    // if input came from stdin, read it
                    memset(buf, 0, MAX);
                    if (read(STDIN_FILENO, buf, MAX * sizeof(char)) < 0) {
                        perror("Error! Can't read from standard input.\n");
                        return -1;
                    }

                    // get command
                    char *cmd = strtok(buf, " \n");

                    // we will use this structure in order to send a command to the server
                    msg_from_subscriber msg;
                    memset(&msg, 0, sizeof(msg_from_subscriber));

                    if (!strcmp(cmd, "exit")) {
                        // if it's exit, close the TCP socket and tell server about the action
                        strcpy(msg.type, "exit");
                        if (send(tcp_fd, &msg, sizeof(msg_from_subscriber), 0) < 0) {
                            perror("Error! Can't send command to server.\n");
                            return -1;
                        }
                        poll_fds[1].fd = -1;
                        close(tcp_fd);
                        return 0;
                    } else if (!strcmp(cmd, "subscribe")) {
                        // if it's subscribe, tell the server to subscribe client to topic
                        strcpy(msg.type, "subscribe");
                        strcpy(msg.topic, strtok(NULL, " \n"));
                        char *store_forward = strtok(NULL, " \n");
                        if (store_forward == NULL) {
                            perror("Error! Invalid input.\n");
                            continue;
                        }
                        msg.sf = atoi(store_forward);
                        if (msg.sf != 0 && msg.sf != 1) {
                            perror("Error! Invalid input.\n");
                            continue;
                        }
                        if (send(tcp_fd, &msg, sizeof(msg_from_subscriber), 0) < 0) {
                            perror("Error! Can't send command to server.\n");
                            return -1;
                        }
                        printf("Subscribed to topic.\n");
                    } else if (!strcmp(cmd, "unsubscribe")) {
                        // if it's unsubscribe, tell server to unsubscribe client from topic
                        strcpy(msg.type, "subscribe");
                        strcpy(msg.topic, strtok(NULL, " \n"));
                        if (send(tcp_fd, &msg, sizeof(msg_from_subscriber), 0) < 0) {
                            perror("Error! Can't send command to server.\n");
                            return -1;
                        }
                        printf("Unsubscribed from topic.\n");
                    } else {
                        perror("Error! Invalid input.\n");
                    }
                } else if (poll_fds[i].fd == tcp_fd) {
                    // if the message came from the server, receive it

                    // first of all we'll receive a small message with the size of the main message
                    next_msg_size size;
                    if (recv(tcp_fd, &size, sizeof(next_msg_size), 0) < 0) {
                        perror("Error! Can't receive message from server.\n");
                        return -1;
                    }

                    // read the message efficiently from the server
                    tcp_message tcp_msg;
                    if (recv(tcp_fd, &tcp_msg, size.size, 0) < 0) {
                        perror("Error! Can't receive message from server.\n");
                        return -1;
                    }

                    // verify message type (-1 -> exit; 0 -> INT; 1 -> SHORT_REAL; 2 -> FLOAT; 3 -> STRING)
                    if (tcp_msg.data_type == -1) {
                        poll_fds[1].fd = -1;
                        close(tcp_fd);
                        return 0;
                    } else {
                        print_subscription_notification(&tcp_msg);
                    }
                }
            }
        }
    }

    return 0;
}
