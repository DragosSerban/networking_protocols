#include <stdio.h>      /* printf, sprintf */
#include <stdlib.h>     /* exit, atoi, malloc, free */
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */
#include <arpa/inet.h>
#include "helpers.h"
#include "requests.h"
#include "parson.h"

#define HOST "34.254.242.81"
#define PORT 8080
#define MAX 256

int main(int argc, char *argv[])
{
    char *session_cookie = NULL; // pointer to the login cookie
    char *lib_auth_header = NULL; // pointer to the library access token
    char command[MAX]; // where we'll store the command

    while (fgets(command, MAX, stdin)) {
        // we make the connection to the server
        int sockfd = open_connection(HOST, PORT, AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            printf("Error! Can't start connection to server!\n");
            return 0;
        }

        // we eliminate extra '\n' from the command
        if (command[strlen(command) - 1] == '\n')
            command[strlen(command) - 1] = '\0';
        if (!strcmp(command, "exit")) {
            // "exit" command, we free the space used by
            // the library authentication token and end the program
            if (lib_auth_header)
                free(lib_auth_header);
            return 0;
        } else if (!strcmp(command, "register")) {
            // get new username
            printf("username=");
            char username[MAX];
            fgets (username, MAX, stdin);
            if (username[strlen(username) - 1] == '\n')
                username[strlen(username) - 1] = '\0';
            // get new password
            printf("password=");
            char password[MAX];
            fgets (password, MAX, stdin);
            if (password[strlen(password) - 1] == '\n')
                password[strlen(password) - 1] = '\0';

            // create JSON object with username and password
            JSON_Value *root_value = json_value_init_object();
            if (!root_value) {
                printf("Error! Failed initializing JSON object!\n");
                continue;
            }
            JSON_Object *root_object = json_value_get_object(root_value);
            if (!root_object) {
                printf("Error! Failed getting JSON object!\n");
                continue;
            }

            json_object_set_string(root_object, "username", username);
            json_object_set_string(root_object, "password", password);

            char *json_string = json_serialize_to_string_pretty(root_value);

            // send message to server
            char *message_to_send = compute_post_request(HOST, "/api/v1/tema/auth/register",
            "application/json", &json_string, 1, NULL, NULL, 0);
            send_to_server(sockfd, message_to_send);

            // receive message from server
            char *message_received = receive_from_server(sockfd);

            // verify if username is taken or not
            int username_taken = 0;
            char *token = strtok(message_received, "\n");
            while (token) {
                if (!strncmp(token, "{\"error\":\"The username ",
                    strlen("{\"error\":\"The username "))) {
                    printf("Error! User already exixts!\n");
                    username_taken = 1;
                }
                token = strtok(NULL, "\n");
            }

            // if username wasn't taken, the account was created
            if (!username_taken)
                printf("User created successfully!\n");

        } else if (!strcmp(command, "login")) {
            // get username
            printf("username=");
            char username[MAX];
            fgets (username, MAX, stdin);
            if (username[strlen(username) - 1] == '\n')
                username[strlen(username) - 1] = '\0';
            // get password
            printf("password=");
            char password[MAX];
            fgets (password, MAX, stdin);
            if (password[strlen(password) - 1] == '\n')
                password[strlen(password) - 1] = '\0';

            // create JSON object with username and password
            JSON_Value *root_value = json_value_init_object();
            if (!root_value) {
                printf("Error! Failed initializing JSON object!\n");
                continue;
            }
            JSON_Object *root_object = json_value_get_object(root_value);
            if (!root_object) {
                printf("Error! Failed getting JSON object!\n");
                continue;
            }

            json_object_set_string(root_object, "username", username);
            json_object_set_string(root_object, "password", password);

            char *json_string = json_serialize_to_string_pretty(root_value);

            // send message to server
            char *message_to_send = compute_post_request(HOST,
                "/api/v1/tema/auth/login", "application/json",
                &json_string, 1, NULL, NULL, 0);
            send_to_server(sockfd, message_to_send);

            // receive message from server
            char *message_received = receive_from_server(sockfd);

            // verify if there is an error (credentials not good,
            // username wasn't found)
            // and also get session_token if everything went alright
            int error = 0;
            char *token = strtok(message_received, "\n");
            while (token) {
                if (!strncmp(token, "{\"error\":\"Credentials are not good!\"}",
                    strlen("{\"error\":\"Credentials are not good!\"}"))) {
                    printf("Error! Credentials are not good!\n");
                    error = 1;
                    break;
                } else if (!strncmp(token,
                    "{\"error\":\"No account with this username!\"}",
                    strlen("{\"error\":\"No account with this username!\"}"))) {
                    printf("Error! No account with this username!\n");
                    error = 1;
                    break;
                } else if (!strncmp(token, "Set-Cookie: connect.sid=",
                    strlen("Set-Cookie: connect.sid="))) {
                    session_cookie = strtok(token + strlen("Set-Cookie: "), ";");
                    break;
                }
                token = strtok(NULL, "\n");
            }

            // if there isn't any error and we have the session cookie,
            // the user is connected
            if (!error && session_cookie)
                printf("User connected successfully!\n");

            // free space used by previous authentication header if it exists
            if (lib_auth_header) {
                free(lib_auth_header);
                lib_auth_header = NULL;
            }

        } else if (!strcmp(command, "enter_library")) {
            if (session_cookie == NULL) {
                // we aren't logged in, so we can't access the library
                printf ("Error! You are not authenticated!\n");
            } else {
                // send the message to server
                char *message_to_send = compute_get_request(HOST,
                "/api/v1/tema/library/access", NULL, NULL, &session_cookie, 1);
                send_to_server(sockfd, message_to_send);

                // receive the message from server
                char *message_received = receive_from_server(sockfd);

                // verify if we got any error messages (if we aren't logged in)
                // and if there aren't any errors,
                // get the library authentication token
                int connected = 1;
                char *token = strtok(message_received, "\n");
                while (token) {
                    if (!strncmp(token,
                        "{\"error\":\"You are not logged in!\"}",
                        strlen("{\"error\":\"You are not logged in!\"}"))) {
                        // error: we aren't connected to any account
                        printf("Error! You are not logged in!\n");
                        connected = 0;
                        break;
                    } else if (!strncmp(token, "{\"token\":\"",
                        strlen("{\"token\":\""))) {
                        // we found the token => we create the auth header
                        char *temp = strtok(token, "\"");
                        for (int i = 0; i < 3; i++)
                            temp = strtok(NULL, "\"");
                        lib_auth_header = temp;

                        char *auth_header = calloc(strlen(lib_auth_header) + MAX, sizeof(char));
                        strcat(auth_header, "Authorization: Bearer ");
                        strcat(auth_header, lib_auth_header);
                        lib_auth_header = auth_header;
                    }
                    token = strtok(NULL, "\n");
                }

                // we are connected to the library
                if (connected)
                    printf ("You are connected to the library now!\n");
            }

        } else if (!strcmp(command, "get_books")) {
            if (session_cookie == NULL || lib_auth_header == NULL) {
                // we aren't connected to an account or to the library
                printf ("Error! You don't have access to the library!\n");
            } else {
                // create URL
                char *url = "/api/v1/tema/library/books";

                // send message to server
                char *message_to_send = compute_get_request(HOST, url, NULL, lib_auth_header, &session_cookie, 1);
                send_to_server(sockfd, message_to_send);

                // receive message from server
                char *message_received = receive_from_server(sockfd);

                // print list of books in the library
                printf("List of books:\n");
                char *token = strtok(message_received, "[{}]");
                while (token) {
                    if (!strncmp(token, "\"error\":\"Authorization header is missing!\"",
                        strlen("\"error\":\"Authorization header is missing!\""))) {
                        printf("%s", "Error! You don't have access to the library!\n");
                        break;
                    } else if (token[0] == '\"') {
                        printf("%s\n", token);
                    }
                    token = strtok(NULL, "[{}]");
                }
            }

        } else if (!strcmp(command, "get_book")) {
            // get ID data
            // verify if ID is an integer before transmiting it
            printf("id=");
            unsigned int id_as_int;
            char temp;
            if (scanf("%d%c", &id_as_int, &temp) != 2 || temp != '\n') {
                printf("Error! ID is not an integer!\n");
                continue;
            }
            char id[MAX];
            sprintf(id, "%d", id_as_int);

            if (session_cookie == NULL || lib_auth_header == NULL) {
                printf ("Error! You don't have access to the library!\n");
            } else {
                // create URL
                char *url = calloc(strlen(id) + 64, sizeof(char));
                strcat(url, "/api/v1/tema/library/books/");
                strcat(url, id);

                // send message to server
                char *message_to_send = compute_get_request(HOST, url, NULL, lib_auth_header, &session_cookie, 1);
                send_to_server(sockfd, message_to_send);

                // receive message from server
                char *message_received = receive_from_server(sockfd);

                // search for the book / a known error
                char *token = strtok(message_received, "\n");
                while (token) {
                    if (!strncmp(token, "{\"id\":", strlen("{\"id\":"))) {
                        printf("Book details:\n");

                        JSON_Value* root = json_parse_string(token);
                        if (!root) {
                            printf("Error! Failed to parse JSON\n");
                            break;
                        }
                        JSON_Object* json_obj = json_value_get_object(root);
                        if (!json_obj) {
                            printf("Error! Failed to get JSON object from JSON value\n");
                            break;
                        }

                        // Extract elements from the JSON object
                        int id = json_object_get_number(json_obj, "id");
                        const char* title = json_object_get_string(json_obj, "title");
                        const char* author = json_object_get_string(json_obj, "author");
                        const char* publisher = json_object_get_string(json_obj, "publisher");
                        const char* genre = json_object_get_string(json_obj, "genre");
                        int page_count = json_object_get_number(json_obj, "page_count");

                        // print details about current book
                        printf("ID: %d\n", id);
                        printf("Title: %s\n", title);
                        printf("Author: %s\n", author);
                        printf("Publisher: %s\n", publisher);
                        printf("Genre: %s\n", genre);
                        printf("Page Count: %d\n", page_count);

                        // free the memory
                        json_value_free(root);

                        break;
                    } else if (!strncmp(token, "{\"error\":\"No book was found!\"}",
                        strlen("{\"error\":\"No book was found!\"}"))) {
                        printf("%s", "Error! No book was found!\n");
                        break;
                    } else if (!strncmp(token, "\"error\":\"Authorization header is missing!\"}",
                        strlen("\"error\":\"Authorization header is missing!\"}"))) {
                        printf("Error! You don't have access to the library!\n");
                        break;
                    } else if (!strncmp(token, "{\"error\":\"id is not int!\"}",
                        strlen("{\"error\":\"id is not int!\"}"))) {
                        printf("Error! ID is not an integer!\n");
                        break;
                    }
                    token = strtok(NULL, "\n");
                }

                free(url);
            }

        } else if (!strcmp(command, "add_book")) {
            char title[MAX];
            char author[MAX];
            char genre[MAX];
            char publisher[MAX];
            char page_count[MAX];

            printf("title=");
            fgets(title, MAX, stdin);
            title[strlen(title) - 1] = '\0';
            printf("author=");
            fgets(author, MAX, stdin);
            author[strlen(author) - 1] = '\0';
            printf("genre=");
            fgets(genre, MAX, stdin);
            genre[strlen(genre) - 1] = '\0';
            printf("publisher=");
            fgets(publisher, MAX, stdin);
            publisher[strlen(publisher) - 1] = '\0';
            printf("page_count=");

            // verify if page_count is an integer before transmiting it
            unsigned int page_count_as_int;
            char temp;
            if (scanf("%d%c", &page_count_as_int, &temp) != 2 || temp != '\n') {
                printf("Error! Page_Count is not an integer!\n");
                continue;
            }
            sprintf(page_count, "%d", page_count_as_int);

            if (!session_cookie || !lib_auth_header) {
                printf("Error! You don't have access to the library!\n");
            } else {
                // create JSON object with book details
                JSON_Value *root_value = json_value_init_object();
                if (!root_value) {
                    printf("Error! Failed initializing JSON object!\n");
                    continue;
                }
                JSON_Object *root_object = json_value_get_object(root_value);
                if (!root_object) {
                    printf("Error! Failed getting JSON object!\n");
                    continue;
                }

                json_object_set_string(root_object, "title", title);
                json_object_set_string(root_object, "author", author);
                json_object_set_string(root_object, "genre", genre);
                json_object_set_string(root_object, "page_count", page_count);
                json_object_set_string(root_object, "publisher", publisher);

                char *json_string = json_serialize_to_string_pretty(root_value);

                // send message to server
                char *message_to_send = compute_post_request(HOST, "/api/v1/tema/library/books",
                "application/json", &json_string, 1, lib_auth_header, &session_cookie, 1);
                send_to_server(sockfd, message_to_send);

                // receive message from server
                char *message_received = receive_from_server(sockfd);

                // verify if there is an error
                // (book info not correct / you don't have access to the library)
                int error = 0;
                char *token = strtok(message_received, "\n");
                while (token) {
                    if (!strncmp(token, "{\"error\":\"Something Bad Happened\"}",
                            strlen("{\"error\":\"Something Bad Happened\"}"))) {
                        printf("Error! Book informations are not correct!\n");
                        error = 1;
                        break;
                    } else if (!strncmp(token, "{\"error\":\"Authorization header is missing!\"}",
                            strlen("{\"error\":\"Authorization header is missing!\"}"))) {
                        printf("Error! You don't have access to the library!\n");
                        error = 1;
                        break;
                    } else if (!strncmp(token, "{\"error\":\"page_count is not int!\"}",
                        strlen("{\"error\":\"page_count is not int!\"}"))) {
                        printf("Error! Page_Count is not an integer!\n");
                        break;
                    }
                    token = strtok(NULL, "\n");
                }

                // there weren't any errors => book was added to the database
                if (!error && !strncmp(message_received, "HTTP/1.1 200 OK",
                    strlen("HTTP/1.1 200 OK")))
                    printf("Book added successfully!\n");
            }

        } else if (!strcmp(command, "delete_book")) {
            // read the book ID
            // verify if it is an integer
            printf("id=");
            unsigned int id_as_int;
            char temp;
            if (scanf("%d%c", &id_as_int, &temp) != 2 || temp != '\n') {
                printf("Error! ID is not an integer!\n");
                continue;
            }
            char id[MAX];
            memset(id, 0, MAX);
            sprintf(id, "%d", id_as_int);

            if (session_cookie == NULL || lib_auth_header == NULL) {
                printf ("Error! You don't have access to the library!\n");
            } else {
                // create the url
                char *url = calloc(MAX, sizeof(char));
                strcat(url, "/api/v1/tema/library/books/");
                strcat(url, id);

                // send message to server
                char *message_to_send = compute_delete_request(HOST, url,
                lib_auth_header, &session_cookie, 1);
                send_to_server(sockfd, message_to_send);

                // receive message from server
                char *message_received = receive_from_server(sockfd);

                // find if there were some errors while trying to delete the book
                int error = 0;
                char *token = strtok(message_received, "\n");
                while (token) {
                    if (!strncmp(token, "{\"error\":\"No book was deleted!\"}",
                            strlen("{\"error\":\"No book was deleted!\"}"))) {
                        printf("Error! Invalid book id!\n");
                        error = 1;
                    } else if (!strncmp(token, "{\"error\":\"Authorization header is missing!\"}",
                            strlen("{\"error\":\"Authorization header is missing!\"}"))) {
                        printf("Error! You don't have access to the library!\n");
                        error = 1;
                    }
                    token = strtok(NULL, "\n");
                }

                // we successfully deleted the book
                if (!error && !strncmp(message_received, "HTTP/1.1 200 OK",
                    strlen("HTTP/1.1 200 OK")))
                    printf("Book deleted successfully!\n");

                free(url);
            }

        } else if (!strcmp(command, "logout")) {
            if (!session_cookie) {
                // there isn't any session cookie
                printf("Error! You are not connected to an account!\n");
            } else {
                // compute the message and send it to server
                char *message_to_send = compute_get_request(HOST, "/api/v1/tema/auth/logout", NULL, NULL, &session_cookie, 1);
                send_to_server(sockfd, message_to_send);

                // get the response
                char *message_received = receive_from_server(sockfd);

                // verify if we logged out correctly
                int error = 0;
                char *token = strtok(message_received, "\n");
                while (token) {
                    if (!strncmp(token, "{\"error\":\"You are not logged in!\"}", strlen("{\"error\":\"You are not logged in!\"}"))) {
                        // error: we weren't logged in
                        printf("Error! You are not connected to an account!\n");
                        error = 1;
                    }
                    token = strtok(NULL, "\n");
                }

                // logout command was successfull
                if (!error)
                    printf("Successfully disconnected from account!\n");

                // we lose the session cookie and the library authentication token
                session_cookie = NULL;
                if (lib_auth_header) {
                    free(lib_auth_header);
                    lib_auth_header = NULL;
                }
            }

        } else {
            // we got an invalid command
            printf("Error! Invalid command!\n");
        }

        close_connection(sockfd);
    }

    return 0;
}
