/**
 *@srijansareen_assignment1
 *@author  Srijan Sareen <srijansareen@buffalo.edu>
 *@version 1.1
 *
 *@section LICENSE
 *
 *This program is free software; you can redistribute it and/or
 *modify it under the terms of the GNU General Public License as
 *published by the Free Software Foundation; either version 2 of
 *the License, or (at your option) any later version.
 *
 *This program is distributed in the hope that it will be useful, but
 *WITHOUT ANY WARRANTY; without even the implied warranty of
 *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *General Public License for more details at
 *http://www.gnu.org/copyleft/gpl.html
 *
 *@section DESCRIPTION
 *Text Chat Application in C Using Socket Programming
 */
//There may be some overlap with beej code, specifically from 
//https://beej.us/guide/bgnet/examples/selectserver.c


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
#include "../include/global.h"
#include "../include/logger.h"

//Define constants and structures: Data Size and Buffer Size
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX_DATA_SIZE 500
#define MAX_BUFFER_SIZE 500 *200
#define STDIN 0

typedef struct Message{
    char content[MAX_BUFFER_SIZE];
    struct Client*sender;
    struct Message*next;
    bool broadcast;
}Message;

typedef struct Client{
    char hostname[MAX_DATA_SIZE];
    char ip[MAX_DATA_SIZE];
    char port[MAX_DATA_SIZE];
    int messages_sent;
    int messages_received;
    char status[MAX_DATA_SIZE];
    int socket;
    struct Client *blocked_clients;
    struct Client *next;
    bool logged_in;
    bool server;
    struct Message *queued_messages;
}Client;

Client*new_client=NULL;
Client*client_list=NULL;
Client*local_client=NULL;
Client*server_info=NULL;
//For setsockopt
int opt_val=1; 

//Function Declarations
void *get_ip_address(struct sockaddr *sa);
bool is_valid_ip(char ip[MAX_DATA_SIZE]);
void set_hostname_and_ip(Client *client);
void send_message(int fd, char msg[]);
void init_client_or_server(bool is_server, char *port);
void init_server();
void init_client();
int register_client_listener();
void process_command(char command[], int fd);
void process_host_command(char command[], int fd);
void process_server_command(char command[], int fd);
void process_client_command(char command[]);
void print_author();
void print_ip();
void print_port();
void print_client_list();
int connect_to_server(char server_ip[], char server_port[]);
void login_to_server(char server_ip[], char server_port[]);
void handle_server_login(char client_ip[], char client_port[], char client_hostname[], int fd);
void refresh_client_list(char clientListString[]);
void handle_refresh_request(int fd);
void handle_send_message(char client_ip[], char msg[], int fd);
void send_message_to_client(char command[]);
void handle_received_message(char client_ip[], char msg[]);
void handle_broadcast_message(char msg[], int fd);
void block_or_unblock_client(char command[], bool block);
void handle_block_or_unblock(char command[], bool block, int fd);
void print_blocked_clients(char blocker_ip_addr[]);
void handle_client_logout(int fd);
void exit_client();
void print_statistics();
void handle_file_transfer(char peer_ip[], char file_name[]);
void receive_file_from_peer(int peer_fd);

/**
 *@brief Getting the IP address (IPv4 or IPv6) from a sockaddr structure
 *@param sa A pointer to the sockaddr structure: this is in <sys/socket.h> and not user defined: check beej guide
 *@return A pointer to the IP address
 */
void *get_ip_address(struct sockaddr *sa){
    if (sa->sa_family==AF_INET){
        return &(((struct sockaddr_in*) sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

/**
 *@brief Check if an IP address is a valid IPv4 address
 *@param ip A string containing the IP address to check
 *@return returning True if the IP address is valid, else false
 */
bool is_valid_ip(char ip[MAX_DATA_SIZE]){
    struct sockaddr_in sa;
    int result=inet_pton(AF_INET, ip, &(sa.sin_addr));
    return result !=0;
}

/**
 *@brief Setting the hostname and IP address for a client structure
 *@param client A pointer to the client structure
 */
void set_hostname_and_ip(Client *client){
    char hostbuffer[MAX_DATA_SIZE];
    char *IPbuffer;
    struct hostent *host_entry;
    int hostname;
    //retrieve hostname
    hostname=gethostname(hostbuffer, sizeof(hostbuffer));
    //retrieve host information
    host_entry=gethostbyname(hostbuffer);
    memcpy(client->ip, inet_ntoa(*((struct in_addr *) host_entry->h_addr_list[0])), sizeof(client->ip));
    memcpy(client->hostname, hostbuffer, sizeof(client->hostname));
}

/**
 *@brief Initializing the host structure and start server or client initialization
 *@param is_server True if the host is a server, else false
 *@param port The port number to listen on
 */
void init_client_or_server(bool is_server, char *port){
    local_client=malloc(sizeof(Client));
    memcpy(local_client->port, port, sizeof(local_client->port));
    local_client->server=is_server;
    set_hostname_and_ip(local_client);
    if (is_server){
        init_server();
    }else{
        init_client();
    }
}

/**
 *@brief Send a command message to a remote host
 *@param fd The file descriptor of the remote host
 *@param msg The message to send
 */
void send_message(int fd, char msg[]){
    if (send(fd, msg, strlen(msg)+1,0)==-1){
        perror("send");
    }
}

/**
 *@brief Initializing the server,setimg up the listener, and handling incoming connections and data
 */
void init_server(){
    int listener=0, status;
    struct addrinfo hints, *ai, *p;
    //getting a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags=AI_PASSIVE;
    if ((status=getaddrinfo(NULL, local_client->port, &hints,&ai)) !=0){
        fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    for (p=ai; p!=NULL; p=p->ai_next){
        if ((listener=socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0){
            continue;
        }
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(int));
        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0){
            close(listener);
            continue;
        }
        break;
    }

    if (p==NULL){
        fprintf(stderr,"server: failed to bind\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(ai);

    if (listen(listener, 10)==-1){
        perror("listen");
        exit(EXIT_FAILURE);
    }

    local_client->socket=listener;
    fd_set master;
    fd_set read_fds;
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(listener, &master);
    FD_SET(STDIN, &master);
    int fdmax=(listener> STDIN) ? listener : STDIN;

    while (1){
        read_fds=master;
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL)==-1){
            perror("select");
            exit(EXIT_FAILURE);
        }

        for (int fd=0; fd <=fdmax; fd++){
            if (FD_ISSET(fd, &read_fds)){
                if (fd==listener){
                    struct sockaddr_storage remoteaddr;
                    socklen_t addrlen=sizeof remoteaddr;
                    int newfd=accept(listener, (struct sockaddr *) &remoteaddr, &addrlen);

                    if (newfd==-1){
                        perror("accept");
                    }else{
                        FD_SET(newfd, &master);
                        if (newfd>fdmax){
                            fdmax=newfd;
                        }
                        new_client=malloc(sizeof(Client));
                        inet_ntop(remoteaddr.ss_family, get_ip_address((struct sockaddr *) &remoteaddr), new_client->ip, INET6_ADDRSTRLEN);
                        new_client->socket=newfd;
                        new_client->logged_in=true;
                    }
                }else if (fd==STDIN){
                    char command[MAX_BUFFER_SIZE];
                    memset(command, '\0', MAX_BUFFER_SIZE);
                    if (fgets(command, MAX_BUFFER_SIZE - 1, stdin) !=NULL){
                        process_command(command, fd);
                    }
                }else{
                    char buf[MAX_BUFFER_SIZE];
                    int nbytes=recv(fd, buf, sizeof buf, 0);
                    if (nbytes<=0){
                        if (nbytes==0){
                            printf("selectserver: socket %d hung up\n", fd);
                        }else{
                            perror("recv");
                        }
                        close(fd);
                        FD_CLR(fd, &master);
                    }else{
                        process_command(buf, fd);
                    }
                }
            }
        }
    }
}

/**
 *@brief Initialize the client, set up the listener, and handle user input and server communication
 */
void init_client(){
    register_client_listener();
    while (1){
        char command[MAX_BUFFER_SIZE];
        memset(command, '\0', MAX_BUFFER_SIZE);
        if (fgets(command, MAX_BUFFER_SIZE, stdin) !=NULL){
            process_command(command, STDIN);
        }
    }
}

/**
 *@brief Register a listener for the client to handle peer-to-peer file transfer
 *
 *@return The listener socket file descriptor
 */
int register_client_listener(){
    int listener=0, status;
    struct addrinfo hints, *ai, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags=AI_PASSIVE;

    if ((status=getaddrinfo(NULL, local_client->port, &hints, &ai)) !=0){
        fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    for (p=ai; p !=NULL; p=p->ai_next){
        if ((listener=socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0){
            continue;
        }
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(int));
        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0){
            close(listener);
            continue;
        }
        break;
    }

    if (p==NULL){
        fprintf(stderr,"client: failed to bind\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(ai);

    if (listen(listener, 10)==-1){
        perror("listen");
        exit(EXIT_FAILURE);
    }

    local_client->socket=listener;
    return listener;
}

/**
 *@brief Process a command from the user or a remote client
 *@param command The command to process
 *@param fd The file descriptor of the sender
 */
void process_command(char command[], int fd){
    process_host_command(command, fd);
    if (local_client->server){
        process_server_command(command, fd);
    }else{
        process_client_command(command);
    }
}

/**
 *@brief Process host-specific commands
 *@param command The command to process
 *@param fd The file descriptor of the sender
 */
void process_host_command(char command[], int fd){
    if (strstr(command,"AUTHOR") !=NULL){
        print_author();
    }else if (strstr(command,"IP") !=NULL){
        print_ip();
    }else if (strstr(command,"PORT") !=NULL){
        print_port();
    }
}

/**
 *@brief Process server-specific commands
 *@param command The command to process
 *@param fd The file descriptor of the sender
 */
void process_server_command(char command[], int fd){
    if (strstr(command,"LIST") !=NULL){
        print_client_list();
    }else if (strstr(command,"STATISTICS ") !=NULL){
        print_statistics();
    }else if (strstr(command,"BLOCKED") !=NULL){
        char client_ip[MAX_DATA_SIZE];
        sscanf(command,"BLOCKED %s", client_ip);
        print_blocked_clients(client_ip);
    }else if (strstr(command,"LOGIN") !=NULL){
        char client_hostname[MAX_DATA_SIZE], client_port[MAX_DATA_SIZE], client_ip[MAX_DATA_SIZE];
        sscanf(command,"LOGIN %s %s %s", client_ip, client_port, client_hostname);
        handle_server_login(client_ip, client_port, client_hostname, fd);
    }else if (strstr(command,"BROADCAST") !=NULL){
        char message[MAX_BUFFER_SIZE];
        int cmdi=10;
        int msgi=0;
        while (command[cmdi] !='\0'){
            message[msgi]=command[cmdi];
            cmdi +=1;
            msgi +=1;
        }
        message[msgi - 1]='\0';
        handle_broadcast_message(message, fd);
    }else if (strstr(command,"REFRESH") !=NULL){
        handle_refresh_request(fd);
    }else if (strstr(command,"SEND") !=NULL){
        char client_ip[MAX_DATA_SIZE], message[MAX_DATA_SIZE];
        int cmdi=5;
        int ipi=0;
        while (command[cmdi] !=' '){
            client_ip[ipi]=command[cmdi];
            cmdi +=1;
            ipi +=1;
        }
        client_ip[ipi]='\0';
        cmdi++;
        int msgi=0;
        while (command[cmdi] !='\0'){
            message[msgi]=command[cmdi];
            cmdi +=1;
            msgi +=1;
        }
        message[msgi - 1]='\0'; //Removing the new line
        handle_send_message(client_ip, message, fd);
    }else if (strstr(command,"UNBLOCK") !=NULL){
        handle_block_or_unblock(command, false, fd);
    }else if (strstr(command,"BLOCK") !=NULL){
        handle_block_or_unblock(command, true, fd);
    }else if (strstr(command,"LOGOUT") !=NULL){
        handle_client_logout(fd);
    }else if (strstr(command,"EXIT") !=NULL){
        handle_exit(fd);
    }
}

/**
 *@brief Process client-specific commands
 *@param command The command to process
 */
void process_client_command(char command[]){
    if (strstr(command,"LIST") !=NULL){
        if (local_client->logged_in){
            print_client_list();
        }else{
            cse4589_print_and_log("[LIST:ERROR]\n");
            cse4589_print_and_log("[LIST:END]\n");
        }
    }else if (strstr(command,"SUCCESSLOGIN") !=NULL){
        cse4589_print_and_log("[LOGIN:SUCCESS]\n");
        cse4589_print_and_log("[LOGIN:END]\n");
    }else if (strstr(command,"ERRORLOGIN") !=NULL){
        cse4589_print_and_log("[LOGIN:ERROR]\n");
        cse4589_print_and_log("[LOGIN:END]\n");
    }else if (strstr(command,"SUCCESSLOGOUT") !=NULL){
        local_client->logged_in=false;
        cse4589_print_and_log("[LOGOUT:SUCCESS]\n");
        cse4589_print_and_log("[LOGOUT:END]\n");
    }else if (strstr(command,"ERRORLOGOUT") !=NULL){
        cse4589_print_and_log("[LOGOUT:ERROR]\n");
        cse4589_print_and_log("[LOGOUT:END]\n");
    }else if (strstr(command,"SUCCESSBROADCAST") !=NULL){
        cse4589_print_and_log("[BROADCAST:SUCCESS]\n");
        cse4589_print_and_log("[BROADCAST:END]\n");
    }else if (strstr(command,"SUCCESSUNBLOCK") !=NULL){
        cse4589_print_and_log("[UNBLOCK:SUCCESS]\n");
        cse4589_print_and_log("[UNBLOCK:END]\n");
    }else if (strstr(command,"SUCCESSBLOCK") !=NULL){
        cse4589_print_and_log("[BLOCK:SUCCESS]\n");
        cse4589_print_and_log("[BLOCK:END]\n");
    }else if (strstr(command,"ERRORUNBLOCK") !=NULL){
        cse4589_print_and_log("[UNBLOCK:ERROR]\n");
        cse4589_print_and_log("[UNBLOCK:END]\n");
    }else if (strstr(command,"ERRORBLOCK") !=NULL){
        cse4589_print_and_log("[BLOCK:ERROR]\n");
        cse4589_print_and_log("[BLOCK:END]\n");
    }else if (strstr(command,"SUCCESSSEND") !=NULL){
        cse4589_print_and_log("[SEND:SUCCESS]\n");
        cse4589_print_and_log("[SEND:END]\n");
    }else if (strstr(command,"LOGIN") !=NULL){ //takes two arguments server ip and server port
        char server_ip[MAX_DATA_SIZE], server_port[MAX_DATA_SIZE];
        int cmdi=6;
        int ipi=0;
        while (command[cmdi] !=' ' && ipi < 256){
            server_ip[ipi]=command[cmdi];
            cmdi +=1;
            ipi +=1;
        }
        server_ip[ipi]='\0';

        cmdi +=1;
        int pi=0;
        while (command[cmdi] !='\0'){
            server_port[pi]=command[cmdi];
            cmdi +=1;
            pi +=1;
        }
        server_port[pi- 1]='\0'; //REMOVE THE NEW LINE
        login_to_server(server_ip, server_port);
    }else if (strstr(command,"REFRESHRESPONSE") !=NULL){
        refresh_client_list(command);
    }else if (strstr(command,"REFRESH") !=NULL){
        if (local_client->logged_in){
            send_message(server_info->socket,"REFRESH\n");
        }else{
            cse4589_print_and_log("[REFRESH:ERROR]\n");
            cse4589_print_and_log("[REFRESH:END]\n");
        }
    }else if (strstr(command,"SENDFILE") !=NULL){
        if (local_client->logged_in){
            char peer_ip[MAX_DATA_SIZE], file_name[MAX_DATA_SIZE];
            sscanf(command,"SENDFILE %s %s\n", peer_ip, file_name);
            handle_file_transfer(peer_ip, file_name);
        }else{
            cse4589_print_and_log("[SENDFILE:ERROR]\n");
            cse4589_print_and_log("[SENDFILE:END]\n");
        }
    }else if (strstr(command,"SEND") !=NULL){
        if (local_client->logged_in){
            send_message_to_client(command);
        }else{
            cse4589_print_and_log("[SEND:ERROR]\n");
            cse4589_print_and_log("[SEND:END]\n");
        }
    }else if (strstr(command,"RECEIVE") !=NULL){
        char client_ip[MAX_DATA_SIZE], message[MAX_BUFFER_SIZE];
        int cmdi=8;
        int ipi=0;
        while (command[cmdi] !=' ' && ipi < 256){
            client_ip[ipi]=command[cmdi];
            cmdi +=1;
            ipi +=1;
        }
        client_ip[ipi]='\0';

        cmdi +=1;
        int msgi=0;
        while (command[cmdi] !='\0'){
            message[msgi]=command[cmdi];
            cmdi +=1;
            msgi +=1;
        }
        message[msgi - 1]='\0'; //REMOVE THE NEW LINE
        handle_received_message(client_ip, message);
    }else if (strstr(command,"BROADCAST") !=NULL){
        if (local_client->logged_in){
            send_message(server_info->socket, command);
        }else{
            cse4589_print_and_log("[BROADCAST:ERROR]\n");
            cse4589_print_and_log("[BROADCAST:END]\n");
        }
    }else if (strstr(command,"UNBLOCK") !=NULL){
        if (local_client->logged_in){
            block_or_unblock_client(command, false);
        }else{
            cse4589_print_and_log("[UNBLOCK:ERROR]\n");
            cse4589_print_and_log("[UNBLOCK:END]\n");
        }
    }else if (strstr(command,"BLOCK") !=NULL){
        if (local_client->logged_in){
            block_or_unblock_client(command, true);
        }else{
            cse4589_print_and_log("[BLOCK:ERROR]\n");
            cse4589_print_and_log("[BLOCK:END]\n");
        }
    }else if (strstr(command,"LOGOUT") !=NULL){
        if (local_client->logged_in){
            send_message(server_info->socket, command);
        }else{
            cse4589_print_and_log("[LOGOUT:ERROR]\n");
            cse4589_print_and_log("[LOGOUT:END]\n");
        }
    }else if (strstr(command,"EXIT") !=NULL){
        exit_client();
    }
}

/**
 *@brief Print the author information
 */
void print_author(){
    cse4589_print_and_log("[AUTHOR:SUCCESS]\n");
    cse4589_print_and_log("I, srijansareen, have read and understood the course academic integrity policy.\n");
    cse4589_print_and_log("[AUTHOR:END]\n");
}

/**
 *@brief Print the IP address of the local client
 */
void print_ip(){
    cse4589_print_and_log("[IP:SUCCESS]\n");
    cse4589_print_and_log("IP:%s\n", local_client->ip);
    cse4589_print_and_log("[IP:END]\n");
}

/**
 *@brief Print the port number of the local client
 */
void print_port(){
    cse4589_print_and_log("[PORT:SUCCESS]\n");
    cse4589_print_and_log("PORT:%s\n", local_client->port);
    cse4589_print_and_log("[PORT:END]\n");
}

/**
 *@brief Print the list of currently logged-in clients
 */
void print_client_list(){
    cse4589_print_and_log("[LIST:SUCCESS]\n");

    Client *temp=client_list;
    int id=1;
    while (temp !=NULL){
        if (temp->logged_in){
            cse4589_print_and_log("%-5d%-35s%-20s%-8s\n", id, temp->hostname, temp->ip, (temp->port));
            id++;
        }
        temp=temp->next;
    }

    cse4589_print_and_log("[LIST:END]\n");
}

/**
 *@brief Print statistics of all clients that have ever logged in
 */
void print_statistics(){
    cse4589_print_and_log("[STATISTICS:SUCCESS]\n");

    Client *temp=client_list;
    int id=1;
    while (temp !=NULL){
        cse4589_print_and_log("%-5d%-35s%-8d%-8d%-8s\n", id, temp->hostname, temp->messages_sent, temp->messages_received, temp->logged_in ? "logged-in" : "logged-out");
        id++;
        temp=temp->next;
    }

    cse4589_print_and_log("[STATISTICS:END]\n");
}

/**
 *@brief Connect to the server from the client side
 *@param server_ip The IP address of the server
 *@param server_port The port number of the server
 *@return 1 if successful, 0 otherwise
 */
int connect_to_server(char server_ip[], char server_port[]){
    server_info=malloc(sizeof(Client));
    memcpy(server_info->ip, server_ip, sizeof(server_info->ip));
    memcpy(server_info->port, server_port, sizeof(server_info->port));
    int server_fd=0, status;
    struct addrinfo hints, *ai, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags=AI_PASSIVE;
    if ((status=getaddrinfo(server_info->ip, server_info->port, &hints, &ai)) !=0){
        return 0;
    }

    for (p=ai; p !=NULL; p=p->ai_next){
        server_fd=socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_fd < 0){
            continue;
        }
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(int));
        if (connect(server_fd, p->ai_addr, p->ai_addrlen) < 0){
            close(server_fd);
            continue;
        }
        break;
    }

    if (p==NULL){
        return 0;
    }

    server_info->socket=server_fd;
    freeaddrinfo(ai);

    int listener=register_client_listener();
    fd_set master;
    fd_set read_fds;
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(server_info->socket, &master);
    FD_SET(STDIN, &master);
    FD_SET(listener, &master);
    int fdmax=(server_info->socket> STDIN) ? server_info->socket : STDIN;
    fdmax=(fdmax> listener) ? fdmax : listener;

    while (local_client->logged_in){
        read_fds=master;
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL)==-1){
            perror("select");
            exit(EXIT_FAILURE);
        }

        for (int fd=0; fd <=fdmax; fd++){
            if (FD_ISSET(fd, &read_fds)){
                if (fd==server_info->socket){
                    char buf[MAX_BUFFER_SIZE];
                    int nbytes=recv(fd, buf, sizeof buf, 0);
                    if (nbytes <=0){
                        if (nbytes==0){
                            printf("selectclient: socket %d hung up\n", fd);
                        }else{
                            perror("recv");
                        }
                        close(fd);
                        FD_CLR(fd, &master);
                    }else{
                        process_command(buf, fd);
                    }
                }else if (fd==STDIN){
                    char command[MAX_BUFFER_SIZE];
                    memset(command, '\0', MAX_BUFFER_SIZE);
                    if (fgets(command, MAX_BUFFER_SIZE - 1, stdin) !=NULL){
                        process_command(command, STDIN);
                    }
                }else if (fd==listener){
                    struct sockaddr_storage remoteaddr;
                    socklen_t addrlen=sizeof remoteaddr;
                    int new_peer_fd=accept(fd, (struct sockaddr *) &remoteaddr, &addrlen);
                    if (new_peer_fd !=-1){
                        receive_file_from_peer(new_peer_fd);
                    }
                }
            }
        }
    }

    return 1;
}

/**
 *@brief Login the client to the server
 *@param server_ip The IP address of the server
 *@param server_port The port number of the server
 */
void login_to_server(char server_ip[], char server_port[]){
    if (server_ip==NULL || server_port==NULL){
        cse4589_print_and_log("[LOGIN:ERROR]\n");
        cse4589_print_and_log("[LOGIN:END]\n");
        return;
    }
    if (server_info==NULL){
        if (!is_valid_ip(server_ip) || connect_to_server(server_ip, server_port)==0){
            cse4589_print_and_log("[LOGIN:ERROR]\n");
            cse4589_print_and_log("[LOGIN:END]\n");
            return;
        }
    }else{
        if (strstr(server_info->ip, server_ip)==NULL || strstr(server_info->port, server_port)==NULL){
            cse4589_print_and_log("[LOGIN:ERROR]\n");
            cse4589_print_and_log("[LOGIN:END]\n");
            return;
        }
    }

    local_client->logged_in=true;

    char msg[MAX_DATA_SIZE *4];
    sprintf(msg,"LOGIN %s %s %s\n", local_client->ip, local_client->port, local_client->hostname);
    send_message(server_info->socket, msg);

    fd_set master;
    fd_set read_fds;
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(server_info->socket, &master);
    FD_SET(STDIN, &master);
    FD_SET(local_client->socket, &master);
    int fdmax=(server_info->socket> STDIN) ? server_info->socket : STDIN;
    fdmax=(fdmax> local_client->socket) ? fdmax : local_client->socket;

    while (local_client->logged_in){
        read_fds=master;
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL)==-1){
            perror("select");
            exit(EXIT_FAILURE);
        }

        for (int fd=0; fd <=fdmax; fd++){
            if (FD_ISSET(fd, &read_fds)){
                if (fd==server_info->socket){
                    char buf[MAX_BUFFER_SIZE];
                    int nbytes=recv(fd, buf, sizeof buf, 0);
                    if (nbytes <=0){
                        if (nbytes==0){
                            printf("selectclient: socket %d hung up\n", fd);
                        }else{
                            perror("recv");
                        }
                        close(fd);
                        FD_CLR(fd, &master);
                    }else{
                        process_command(buf, fd);
                    }
                }else if (fd==STDIN){
                    char command[MAX_BUFFER_SIZE];
                    memset(command, '\0', MAX_BUFFER_SIZE);
                    if (fgets(command, MAX_BUFFER_SIZE - 1, stdin) !=NULL){
                        process_command(command, STDIN);
                    }
                }else if (fd==local_client->socket){
                    struct sockaddr_storage remoteaddr;
                    socklen_t addrlen=sizeof remoteaddr;
                    int new_peer_fd=accept(fd, (struct sockaddr *) &remoteaddr, &addrlen);
                    if (new_peer_fd !=-1){
                        receive_file_from_peer(new_peer_fd);
                    }
                }
            }
        }
    }
}

/**
 *@brief Handle login requests from clients on the server side
 *
 *@param client_ip The IP address of the client
 *@param client_port The port number of the client
 *@param client_hostname The hostname of the client
 *@param fd The file descriptor of the client
 */
void handle_server_login(char client_ip[], char client_port[], char client_hostname[], int fd){
    char client_return_msg[MAX_BUFFER_SIZE]="REFRESHRESPONSE FIRST\n";
    Client *temp=client_list;
    bool is_new=true;
    Client *requesting_client=malloc(sizeof(Client));

    while (temp !=NULL){
        if (temp->socket==fd){
            requesting_client=temp;
            is_new=false;
            break;
        }
        temp=temp->next;
    }

    if (is_new){
        memcpy(new_client->hostname, client_hostname, sizeof(new_client->hostname));
        memcpy(new_client->port, client_port, sizeof(new_client->port));
        requesting_client=new_client;
        int client_port_value=atoi(client_port);
        if (client_list==NULL){
            client_list=malloc(sizeof(Client));
            client_list=new_client;
        }else if (client_port_value < atoi(client_list->port)){
            new_client->next=client_list;
            client_list=new_client;
        }else{
            Client *temp=client_list;
            while (temp->next!=NULL && atoi(temp->next->port)<client_port_value){
                temp=temp->next;
            }
            new_client->next=temp->next;
            temp->next=new_client;
        }

    }else{
        requesting_client->logged_in=true;
    }

    temp=client_list;
    while (temp !=NULL){
        if (temp->logged_in){
            char clientString[MAX_DATA_SIZE *4];
            sprintf(clientString,"%s %s %s\n", temp->ip, temp->port, temp->hostname);
            strcat(client_return_msg, clientString);
        }
        temp=temp->next;
    }

    strcat(client_return_msg,"ENDREFRESH\n");
    Message *temp_message=requesting_client->queued_messages;
    char receive[MAX_BUFFER_SIZE *3];

    while (temp_message !=NULL){
        requesting_client->messages_received++;
        sprintf(receive,"RECEIVE %s %s    ", temp_message->sender->ip, temp_message->content);
        strcat(client_return_msg, receive);

        if (!temp_message->broadcast){
            cse4589_print_and_log("[RELAYED:SUCCESS]\n");
            cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", temp_message->sender->ip, requesting_client->ip, temp_message->content);
            cse4589_print_and_log("[RELAYED:END]\n");
        }
        temp_message=temp_message->next;
    }
    send_message(fd, client_return_msg);
    requesting_client->queued_messages=temp_message;
}

/**
 *@brief Refresh the client list with the updated list from the server
 *
 *@param clientListString The string containing the updated client list
 */
void refresh_client_list(char clientListString[]){
    char *received=strstr(clientListString,"RECEIVE");
    int rcvi=received - clientListString, cmdi=0;
    char command[MAX_DATA_SIZE];
    int blank_count=0;
    while (received !=NULL && rcvi < strlen(clientListString)){
        if (clientListString[rcvi]==' ')
            blank_count++;
        else
            blank_count=0;
        command[cmdi]=clientListString[rcvi];
        if (blank_count==4){
            command[cmdi - 3]='\0';
            strcat(command,"\n");
            process_command(command, STDIN);
            cmdi=-1;
        }
        cmdi++;
        rcvi++;
    }
    bool is_refresh=false;
    client_list=malloc(sizeof(Client));
    Client *head=client_list;
    const char delimiter[2]="\n";
    char *token=strtok(clientListString, delimiter);
    if (strstr(token,"NOTFIRST")){
        is_refresh=true;
    }
    if (token !=NULL){
        token=strtok(NULL, delimiter);
        char client_ip[MAX_DATA_SIZE], client_port[MAX_DATA_SIZE], client_hostname[MAX_DATA_SIZE];
        while (token !=NULL){
            if (strstr(token,"ENDREFRESH") !=NULL){
                break;
            }
            Client *new_client=malloc(sizeof(Client));
            sscanf(token,"%s %s %s\n", client_ip, client_port, client_hostname);
            token=strtok(NULL, delimiter);
            memcpy(new_client->port, client_port, sizeof(new_client->port));
            memcpy(new_client->ip, client_ip, sizeof(new_client->ip));
            memcpy(new_client->hostname, client_hostname, sizeof(new_client->hostname));
            new_client->logged_in=true;
            client_list->next=new_client;
            client_list=client_list->next;
        }
        client_list=head->next;
    }
    if (is_refresh){
        cse4589_print_and_log("[REFRESH:SUCCESS]\n");
        cse4589_print_and_log("[REFRESH:END]\n");
    }else{
        process_command("SUCCESSLOGIN", STDIN);
    }
}

/**
 *@brief Handle refresh requests from clients on the server side
 *@param fd The file descriptor of the client
 */
void handle_refresh_request(int fd){
    char clientListString[MAX_BUFFER_SIZE]="REFRESHRESPONSE NOTFIRST\n";
    Client *temp=client_list;
    while (temp !=NULL){
        if (temp->logged_in){
            char clientString[MAX_DATA_SIZE *4];
            sprintf(clientString,"%s %s %s\n", temp->ip, temp->port, temp->hostname);
            strcat(clientListString, clientString);
        }
        temp=temp->next;
    }
    strcat(clientListString,"ENDREFRESH\n");
    send_message(fd, clientListString);
}

/**
 *@brief Handle send message requests from clients on the server side
 *
 *@param client_ip The IP address of the recipient client
 *@param msg The message to send
 *@param fd The file descriptor of the sender client
 */
void handle_send_message(char client_ip[], char msg[], int fd){
    char receive[MAX_DATA_SIZE *4];
    Client *temp=client_list;
    Client *from_client=malloc(sizeof(Client)), *to_client=malloc(sizeof(Client));;
    while (temp !=NULL){
        if (strstr(client_ip, temp->ip) !=NULL){
            to_client=temp;
        }
        if (fd==temp->socket){
            from_client=temp;
        }
        temp=temp->next;
    }
    if (to_client==NULL || from_client==NULL){
        cse4589_print_and_log("[RELAYED:ERROR]\n");
        cse4589_print_and_log("[RELAYED:END]\n");
        return;
    }

    from_client->messages_sent++;
    bool is_blocked=false;

    temp=to_client->blocked_clients;
    while (temp !=NULL){
        if (strstr(from_client->ip, temp->ip) !=NULL){
            is_blocked=true;
            break;
        }
        temp=temp->next;
    }
    send_message(from_client->socket,"SUCCESSSEND\n");
    if (is_blocked){
        cse4589_print_and_log("[RELAYED:SUCCESS]\n");
        cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", from_client->ip, to_client->ip, msg);
        cse4589_print_and_log("[RELAYED:END]\n");
        return;
    }

    if (to_client->logged_in){
        to_client->messages_received++;
        sprintf(receive,"RECEIVE %s %s\n", from_client->ip, msg);
        send_message(to_client->socket, receive);
        cse4589_print_and_log("[RELAYED:SUCCESS]\n");
        cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", from_client->ip, to_client->ip, msg);
        cse4589_print_and_log("[RELAYED:END]\n");
    }else{
        Message *new_message=malloc(sizeof(Message));
        memcpy(new_message->content, msg, sizeof(new_message->content));
        new_message->sender=from_client;
        new_message->broadcast=false;
        if (to_client->queued_messages==NULL){
            to_client->queued_messages=new_message;
        }else{
            Message *temp_message=to_client->queued_messages;
            while (temp_message->next !=NULL){
                temp_message=temp_message->next;
            }
            temp_message->next=new_message;
        }
    }
}

/**
 *@brief Send a message from the client to the server
 *@param command The command containing the message
 */
void send_message_to_client(char command[]){
    char client_ip[MAX_DATA_SIZE];
    int cmdi=5;
    int ipi=0;
    while (command[cmdi] !=' '){
        client_ip[ipi]=command[cmdi];
        cmdi +=1;
        ipi +=1;
    }
    client_ip[ipi]='\0';
    if (!is_valid_ip(client_ip)){
        cse4589_print_and_log("[SEND:ERROR]\n");
        cse4589_print_and_log("[SEND:END]\n");
        return;
    }
    Client *temp=client_list;
    while (temp !=NULL){
        if (strstr(temp->ip, client_ip) !=NULL){
            send_message(server_info->socket, command);
            break;
        }
        temp=temp->next;
    }
    if (temp==NULL){
        cse4589_print_and_log("[SEND:ERROR]\n");
        cse4589_print_and_log("[SEND:END]\n");
    }
}

/**
 *@brief Handle received messages on the client side
 *@param client_ip The IP address of the sender client
 *@param msg The received message
 */
void handle_received_message(char client_ip[], char msg[]){
    cse4589_print_and_log("[RECEIVED:SUCCESS]\n");
    cse4589_print_and_log("msg from:%s\n[msg]:%s\n", client_ip, msg);
    cse4589_print_and_log("[RECEIVED:END]\n");
}

/**
 *@brief Handle broadcast message requests from clients on the server side
 *@param msg The broadcast message
 *@param fd The file descriptor of the sender client
 */
void handle_broadcast_message(char msg[], int fd){
    Client *temp=client_list;
    Client *from_client=malloc(sizeof(Client));
    while (temp !=NULL){
        if (fd==temp->socket){
            from_client=temp;
        }
        temp=temp->next;
    }
    Client *to_client=client_list;
    int id=1;
    from_client->messages_sent++;
    while (to_client !=NULL){
        if (to_client->socket==fd){
            to_client=to_client->next;
            continue;
        }

        bool is_blocked=false;

        Client *temp_blocked=to_client->blocked_clients;
        while (temp_blocked !=NULL){
            if (temp_blocked->socket==fd){
                is_blocked=true;
                break;
            }
            temp_blocked=temp_blocked->next;
        }

        if (is_blocked){
            to_client=to_client->next;
            continue;
        }

        char receive[MAX_DATA_SIZE *4];

        if (to_client->logged_in){
            to_client->messages_received++;
            sprintf(receive,"RECEIVE %s %s\n", from_client->ip, msg);
            send_message(to_client->socket, receive);
        }else{
            Message *new_message=malloc(sizeof(Message));
            memcpy(new_message->content, msg, sizeof(new_message->content));
            new_message->sender=from_client;
            new_message->broadcast=true;
            if (to_client->queued_messages==NULL){
                to_client->queued_messages=new_message;
            }else{
                Message *temp_message=to_client->queued_messages;
                while (temp_message->next !=NULL){
                    temp_message=temp_message->next;
                }
                temp_message->next=new_message;
            }
        }
        to_client=to_client->next;
    }

    cse4589_print_and_log("[RELAYED:SUCCESS]\n");
    cse4589_print_and_log("msg from:%s, to:255.255.255.255\n[msg]:%s\n", from_client->ip, msg);
    cse4589_print_and_log("[RELAYED:END]\n");
    send_message(from_client->socket,"SUCCESSBROADCAST\n");
}

/**
 *@brief Block or unblock a client
 *@param command The command to block or unblock
 *@param block True if blocking, false if unblocking
 */
void block_or_unblock_client(char command[], bool block){
    char client_ip[MAX_DATA_SIZE];
    if (block){
        sscanf(command,"BLOCK %s\n", client_ip);
    }else{
        sscanf(command,"UNBLOCK %s\n", client_ip);
    }

    Client *temp=client_list;
    while (temp !=NULL){
        if (strstr(client_ip, temp->ip) !=NULL){
            break;
        }
        temp=temp->next;
    }
    Client *blocked_client=temp;

    temp=local_client->blocked_clients;
    while (temp !=NULL){
        if (strstr(client_ip, temp->ip) !=NULL){
            break;
        }
        temp=temp->next;
    }
    Client *blocked_client_2=temp;

    if (blocked_client !=NULL && blocked_client_2==NULL && block){
        Client *new_blocked_client=malloc(sizeof(Client));
        memcpy(new_blocked_client->ip, blocked_client->ip, sizeof(new_blocked_client->ip));
        memcpy(new_blocked_client->port, blocked_client->port, sizeof(new_blocked_client->port));
        memcpy(new_blocked_client->hostname, blocked_client->hostname, sizeof(new_blocked_client->hostname));
        new_blocked_client->socket=blocked_client->socket;
        new_blocked_client->next=NULL;
        if (local_client->blocked_clients !=NULL){
            Client *temp_blocked=local_client->blocked_clients;
            while (temp_blocked->next !=NULL){
                temp_blocked=temp_blocked->next;
            }
            temp_blocked->next=new_blocked_client;
        }else{
            local_client->blocked_clients=new_blocked_client;
        }
        send_message(server_info->socket, command);
    }else if (blocked_client !=NULL && blocked_client_2 !=NULL && !block){
        Client *temp_blocked=local_client->blocked_clients;
        if (strstr(blocked_client->ip, temp_blocked->ip) !=NULL){
            local_client->blocked_clients=local_client->blocked_clients->next;
        }else{
            Client *previous=temp_blocked;
            while (temp_blocked !=NULL){
                if (strstr(temp_blocked->ip, blocked_client->ip) !=NULL){
                    previous->next=temp_blocked->next;
                    break;
                }
                temp_blocked=temp_blocked->next;
            }
        }
        send_message(server_info->socket, command);
    }else{
        if (block){
            cse4589_print_and_log("[BLOCK:ERROR]\n");
            cse4589_print_and_log("[BLOCK:END]\n");
        }else{
            cse4589_print_and_log("[UNBLOCK:ERROR]\n");
            cse4589_print_and_log("[UNBLOCK:END]\n");
        }
    }
}

/**
 *@brief Handling block or unblock requests from clients on the server side
 *@param command The command to block or unblock
 *@param block True if blocking, false if unblocking
 *@param fd The file descriptor of the sender client
 */
void handle_block_or_unblock(char command[], bool block, int fd){
    char client_ip[MAX_DATA_SIZE], client_port[MAX_DATA_SIZE];
    if (block){
        sscanf(command,"BLOCK %s %s\n", client_ip, client_port);
    }else{
        sscanf(command,"UNBLOCK %s %s\n", client_ip, client_port);
    }
    Client *temp=client_list;
    Client *requesting_client=malloc(sizeof(Client));
    Client *blocked_client=malloc(sizeof(Client));

    while (temp !=NULL){
        if (temp->socket==fd){
            requesting_client=temp;
        }
        if (strstr(client_ip, temp->ip) !=NULL){
            blocked_client=temp;
        }
        temp=temp->next;
    }

    if (blocked_client !=NULL){
        if (block){
            Client *new_blocked_client=malloc(sizeof(Client));
            memcpy(new_blocked_client->ip, blocked_client->ip, sizeof(new_blocked_client->ip));
            memcpy(new_blocked_client->port, blocked_client->port, sizeof(new_blocked_client->port));
            memcpy(new_blocked_client->hostname, blocked_client->hostname, sizeof(new_blocked_client->hostname));
            new_blocked_client->socket=blocked_client->socket;
            new_blocked_client->next=NULL;
            int new_blocked_client_port_value=atoi(new_blocked_client->port);
            if (requesting_client->blocked_clients==NULL){
                requesting_client->blocked_clients=malloc(sizeof(Client));
                requesting_client->blocked_clients=new_blocked_client;
            }else if (new_blocked_client_port_value < atoi(requesting_client->blocked_clients->port)){
                new_blocked_client->next=requesting_client->blocked_clients;
                requesting_client->blocked_clients=new_blocked_client;
            }else{
                Client *temp=requesting_client->blocked_clients;
                while (temp->next !=NULL && atoi(temp->next->port) < new_blocked_client_port_value){
                    temp=temp->next;
                }
                new_blocked_client->next=temp->next;
                temp->next=new_blocked_client;
            }

            send_message(fd,"SUCCESSBLOCK\n");
        }else{
            Client *temp_blocked=requesting_client->blocked_clients;
            if (strstr(temp_blocked->ip, blocked_client->ip) !=NULL){
                requesting_client->blocked_clients=requesting_client->blocked_clients->next;
            }else{
                Client *previous=temp_blocked;
                while (temp_blocked !=NULL){
                    if (strstr(temp_blocked->ip, blocked_client->ip) !=NULL){
                        previous->next=temp_blocked->next;
                        break;
                    }
                    temp_blocked=temp_blocked->next;
                }
            }
            send_message(fd,"SUCCESSUNBLOCK\n");
        }
    }else{
        if (block){
            send_message(fd,"ERRORBLOCK\n");
        }else{
            send_message(fd,"ERRORUNBLOCK\n");
        }
    }
}

/**
 *@brief Print the list of blocked clients for a given client
 *@param blocker_ip_addr The IP address of the client who blocked others
 */
void print_blocked_clients(char blocker_ip_addr[]){
    Client *temp=client_list;
    while (temp !=NULL){
        if (strstr(blocker_ip_addr, temp->ip) !=NULL){
            break;
        }
        temp=temp->next;
    }
    if (is_valid_ip(blocker_ip_addr) && temp){
        cse4589_print_and_log("[BLOCKED:SUCCESS]\n");
        Client *temp_blocked=client_list;
        temp_blocked=temp->blocked_clients;
        int id=1;
        while (temp_blocked !=NULL){
            cse4589_print_and_log("%-5d%-35s%-20s%-8d\n", id, temp_blocked->hostname, temp_blocked->ip, atoi(temp_blocked->port));
            id=id + 1;
            temp_blocked=temp_blocked->next;
        }
    }else{
        cse4589_print_and_log("[BLOCKED:ERROR]\n");
    }

    cse4589_print_and_log("[BLOCKED:END]\n");
}

/**
 *@brief Handle logout requests from clients on the server side
 *@param fd The file descriptor of the client
 */
void handle_client_logout(int fd){
    Client *temp=client_list;
    while (temp !=NULL){
        if (temp->socket==fd){
            send_message(fd,"SUCCESSLOGOUT\n");
            temp->logged_in=false;
            break;
        }
        temp=temp->next;
    }
    if (temp==NULL){
        send_message(fd,"ERRORLOGOUT\n");
    }
}

/**
 *@brief Exit the client application
 */
void exit_client(){
    send_message(server_info->socket,"EXIT");
    cse4589_print_and_log("[EXIT:SUCCESS]\n");
    cse4589_print_and_log("[EXIT:END]\n");
    exit(0);
}

/**
 *@brief Handle exit requests from clients on the server side
 *@param fd The file descriptor of the client
 */
void handle_exit(int fd){
    Client *temp=client_list;
    if (temp->socket==fd){
        client_list=client_list->next;
    }else{
        Client *previous=temp;
        while (temp !=NULL){
            if (temp->socket==fd){
                previous->next=temp->next;
                temp=temp->next;
                break;
            }
            temp=temp->next;
        }
    }
}

/**
 *@brief Handle file transfer requests from the client side
 *@param peer_ip The IP address of the peer client
 *@param file_name The name of the file to transfer
 */
void handle_file_transfer(char peer_ip[], char file_name[]){
    Client *to_client=client_list;
    while (to_client !=NULL){
        if (strstr(to_client->ip, peer_ip) !=NULL){
            break;
        }
        to_client=to_client->next;
    }

    int to_client_fd=0, status;
    struct addrinfo hints, *ai, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags=AI_PASSIVE;
    if ((status=getaddrinfo(to_client->ip, to_client->port, &hints, &ai)) !=0){
        exit(EXIT_FAILURE);
    }

    for (p=ai; p !=NULL; p=p->ai_next){
        to_client_fd=socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (to_client_fd < 0){
            continue;
        }
        setsockopt(to_client_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(int));
        if (connect(to_client_fd, p->ai_addr, p->ai_addrlen) < 0){
            close(to_client_fd);
            continue;
        }
        break;
    }

    if (p==NULL){
        exit(EXIT_FAILURE);
    }

    to_client->socket=to_client_fd;
    freeaddrinfo(ai);

    char buffer[MAX_DATA_SIZE]={0};
    struct stat s;
    stat(file_name, &s);
    FILE *file=fopen(file_name,"rb");
    long size=s.st_size;
    sprintf(buffer,"FILENAME %s\n", file_name);
    send(to_client->socket, buffer, sizeof(buffer), 0);
    sprintf(buffer,"FILESIZE %ld\n", size);
    send(to_client->socket, buffer, sizeof(buffer), 0);
    while (size> 0){
        int len=fread(buffer, 1, MIN(sizeof(buffer), size), file);
        send(to_client->socket, buffer, len, 0);
        size -=len;
    }
    fclose(file);
    cse4589_print_and_log("[SENDFILE:SUCCESS]\n");
    cse4589_print_and_log("[SENDFILE:END]\n");
}

/**
 *@brief Handle file reception from a peer client
 *@param peer_fd The file descriptor of the peer client
 */
void receive_file_from_peer(int peer_fd){
    char buffer[MAX_DATA_SIZE]={0};
    char temp[MAX_DATA_SIZE]={0};
    char received_file_name[MAX_DATA_SIZE];
    recv(peer_fd, buffer, MAX_DATA_SIZE, 0);
    sscanf(buffer,"FILENAME %s\n", received_file_name);
    recv(peer_fd, buffer, MAX_DATA_SIZE, 0);
    sscanf(buffer,"FILESIZE %s\n", temp);
    long received_file_size=atoi(temp);
    FILE *file=fopen(received_file_name,"wb");
    while (received_file_size> 0){
        int len=recv(peer_fd, buffer, MIN(sizeof(buffer), received_file_size), 0);
        fwrite(buffer, 1, len, file);
        received_file_size=received_file_size - len;
    }
    fclose(file);
    cse4589_print_and_log("[RECIEVE:SUCCESS]\n");
    cse4589_print_and_log("[RECIEVE:END]\n");
    fflush(stdout);
    close(peer_fd);
}

/**
 *@brief Main function to start the program
 *@param argc Number of arguments
 *@param argv The argument list
 *@return 0 EXIT_SUCCESS
 */
int main(int argc, char **argv){
    /*Init. Logger*/
    cse4589_init_log(argv[2]);
    /*Clear LOGFILE*/
    fclose(fopen(LOGFILE,"w"));
    if (argc !=3){
        exit(EXIT_FAILURE);
    }
    //initializing the host
    init_client_or_server(strcmp(argv[1],"s")==0, argv[2]);

    return 0;
}
