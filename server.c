//
// Created by danoz on 04/01/2022.
//
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

//kazdy klient bude mat svoju jedinecnu strukturu, akoby taka databaza klientov
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;                   //id pouzivatela, pre kazdeho dedinecne
    char name[32];                //meno pouzivatela
} client_t;

//pole klientov
client_t *clients[5];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int cli_count = 0;

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

//pridanie klienta do hry
void queue_add(client_t *cl){
    pthread_mutex_lock(&clients_mutex);

    for(int i=0; i < 5; ++i){
        if(!clients[i]){
            clients[i] = cl;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

//zmazanie klienta, odstranenia z hry
void queue_remove(int uid){
    pthread_mutex_lock(&clients_mutex);

    for(int i=0; i < 5; ++i){
        if(clients[i]){
            if(clients[i]->uid == uid){
                clients[i] = NULL;
                break;
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

//poslanie spravy vsetkym klientom, ked sa nieco stane, resp ked niekto nieco vykonal
void send_message(char *s, int uid){
    pthread_mutex_lock(&clients_mutex);

    //kazdemu klientovi okrem odosielatela posleme spravu
    for(int i=0; i<5; ++i){
        if(clients[i]){
            if(clients[i]->uid != uid){
                if(write(clients[i]->sockfd, s, strlen(s)) < 0){
                    perror("ERROR: write to descriptor failed");
                    break;
                }
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

//vlakno na manipulaciu s klientami, tu sa bude odohravat hlavna cast programu, pre kazdeho klienta sa vytvori vlakno
void * handle_client(void * data) {
    char buff_out[2048];
    char name[32];
    int leave_flag = 0; //tu zistujeme, ci je klient pripojeny

    cli_count++;
    client_t *cli = (client_t *)data;

    //pridanie klienta do hry
    if(recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) <  2 || strlen(name) >= 32-1){
        printf("Didn't enter the name.\n");
        leave_flag = 1;
    } else{
        strcpy(cli->name, name);
        sprintf(buff_out, "%s has joined\n", cli->name);      //ak zadal legitimne meno pridame ho do hry
        printf("%s", buff_out);
        send_message(buff_out, cli->uid);                           //posleme ostatnym spravu, ze mame dalsieho hraca
    }

    bzero(buff_out, 2048);

    while(1){
        //vsetko co nie je nula je true
        if (leave_flag) {
            break;
        }

        //cakame, kym klient nieco vykona
        int receive = recv(cli->sockfd, buff_out, 2048, 0);
        if (receive > 0){
            //ak nieco dostaneme
            if(strlen(buff_out) > 0){
                send_message(buff_out, cli->uid);                          //posleme vsetkym klientom spravu, normalka
                str_trim_lf(buff_out, strlen(buff_out));
                printf("%s -> %s\n", buff_out, cli->name);          //posleme spravu, ktoru poslal dany klient
            }
            //ak nic nedostaneme, konec
        } else if (receive == 0 || strcmp(buff_out, "exit") == 0){         //ak chce klient opustit server
            sprintf(buff_out, "%s has left\n", cli->name);
            printf("%s", buff_out);
            send_message(buff_out, cli->uid);                              //posleme spravu, ze klient odisiel
            leave_flag = 1;
        } else {
            //ak dostaneme nejaku blbost, konec
            printf("ERROR: -1\n");
            leave_flag = 1;
        }

        bzero(buff_out, 2048);
    }

    //klient odisiel, zmazeme ho
    close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main() {
    //defaultne nastavenia
    char* ip = "127.0.0.1";
    int port = 9002;                 //port, na ktory sa pripajame
    int uid = 10;

    int option = 1;
    int listenFD = 0, connfD = 0;    //hlavny socket, kontrola pripojenia
    struct sockaddr_in serv_addr;    //server
    struct sockaddr_in client_addr;  //klient
    pthread_t tid;

    //nastavenia socketu
    listenFD = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(port);

    //signaly / prerusenia
    signal(SIGPIPE, SIG_IGN);
    if(setsockopt(listenFD, SOL_SOCKET,(SO_REUSEPORT | SO_REUSEADDR),(char*)&option,sizeof(option)) < 0){
        perror("ERROR: setsockopt failed");
        return EXIT_FAILURE;
    }

    //bindovanie
    if(bind(listenFD, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR: Socket binding failed");
        return EXIT_FAILURE;
    }

    //cakanie na klienta
    if (listen(listenFD, 10) < 0) {
        perror("ERROR: Socket listening failed");
        return EXIT_FAILURE;
    }

    //zaciname
    printf("VITAJTE V HRE\n");

    //tato cast bude nastavovat noveho klienta
    while (1) {
        socklen_t clientLength = sizeof(client_addr);
        connfD = accept(listenFD, (struct sockaddr*)&client_addr, &clientLength);

        //zistime, ci sme neprekrosili pocet klientov, ktori mozu ist do hry (max je 5)
        if (cli_count + 1 == 5) {
            printf("Uz je pripojeny maximalny pocet hracov\n");
            close(connfD);
            continue;
        }

        //nastavenie klienta
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->address = client_addr;
        cli->sockfd = connfD;
        cli->uid = uid++;

        //pridame klienta do hry
        queue_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void*)cli);

        usleep(1000);
    }
}
