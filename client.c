//
// Created by danoz on 04/01/2022.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

volatile sig_atomic_t flag = 0;
int sockFD = 0;
char name[32];

void str_overwrite_stdout() {
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf (char* arr, int length) {
    int i;
    for (i = 0; i < length; i++) { // trim \n
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

void exit_program(int sig) {
    flag = 1;
}

//toto vlakno manazuje posielanie sprav na server
void send_msg_handler() {
    char message[2048] = {};
    char buffer[2048 + 32] = {};

    while(1) {
        str_overwrite_stdout();
        //caka, kym pouzivatel nenapise spravu
        fgets(message, 2048, stdin);
        str_trim_lf(message, 2048);

        //ak napise exit, koncime, inak posleme dalej
        if (strcmp(message, "exit") == 0) {
            break;
        } else {
            sprintf(buffer, "%s\n", message);
            send(sockFD, buffer, strlen(buffer), 0);
        }

        bzero(message, 2048);
        bzero(buffer, 2048 + 32);
    }
    exit_program(2);
}

//toto vlakno manazuje prijimanie sprav od servera
void recv_msg_handler() {
    char message[2048] = {};
    while (1) {
        //caka na spravu od servera
        int receive = recv(sockFD, message, 2048, 0);
        if ((char)message[0] == 'e') {
            break;
        }
        if (receive > 0) {
            printf("%c", (char)message[0]);
            printf("%s", message);
            str_overwrite_stdout();
        } else if (receive == 0) {
            break;
        } else {
            break;
        }
        memset(message, 0, sizeof(message));
    }
    exit_program(2);
}

int main() {
    //defaultne nastavenia
    char* ip = "127.0.0.1";
    int port = 9004;                 //port, na ktory sa pripajame

    signal(SIGINT, exit_program);


    //nastavime pouzivatelovi meno, resp sam si ho nastavi
    printf("Zadajte svoje meno\n");
    fgets(name, 32, stdin);
    str_trim_lf(name, strlen(name));

    if (strlen(name) > 32 || strlen(name) < 2){
        printf("Dlzka mena nie je validna.\n");
        return EXIT_FAILURE;
    }

    //nastavenia socketu
    struct sockaddr_in server_addr; //adresa servera
    sockFD = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    //pripojenie na server
    int pripojenie = connect(sockFD, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (pripojenie == -1) {
        printf("Nepodarilo sa pripojit na server\n");
        return EXIT_FAILURE;
    }

    //posleme meno na server
    send(sockFD, name, 32, 0);
    printf("VITAJTE V HRE\n");

    //dve vlakna pre poslanie spravy a obdrzanie spravy
    pthread_t send_msg_thread;
    if(pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0){
        printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }
    pthread_t recv_msg_thread;
    if(pthread_create(&recv_msg_thread, NULL, (void *) recv_msg_handler, NULL) != 0){
        printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }

    //cyklus, ktory bude drzat klienta pri zivote, ked zmenime flag, skonci
    while (1){
        //vsetko co nie je nula je true
        if(flag){
            printf("\nKoncim\n");
            break;
        }
    }

    close(sockFD);

    return EXIT_SUCCESS;
}