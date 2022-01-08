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
#include <string.h>

//kazdy klient bude mat svoju jedinecnu strukturu, akoby taka databaza klientov s ich kartami a zetonami, server k nim ma pristup
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;                      //id pouzivatela, pre kazdeho dedinecne
    char name[32];                //meno pouzivatela
    int prvaKarta;
    int druhaKarta;
    int prvaFarba;
    int druhaFarba;
    int pocetZetonov;
    int klientovaStavka;
    int* celkovaStavka;
    int* potrebnaStavka;
    int meniliSme;
    int najvyssia;
    int skoncil;
    //pthread_mutex_t* mutex;
    //pthread_cond_t *cakamenaKlienta;
} client_t;

//tato struktura bude ukladat informacie o celkovych veciach v ramci hry (spolocne karty, bindy a pod)
typedef struct {
    //int karty[5];
    int bigBlind;
    int smallBlind;
    int* celkovaStavka;
    int* potrebnaStavka;
    int prvaKarta;
    int druhaKarta;
    int tretiaKarta;
    int stvrtaKarta;
    int piataKarta;
    int prvaFarba;
    int druhaFarba;
    int tretiaFarba;
    int stvrtaFarba;
    int piataFarba;
    //pthread_mutex_t* mutex;
    //pthread_cond_t *cakamenaKlienta;
} dataHry;

//pole klientov
client_t *clients[5];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cakameNaKlienta = PTHREAD_COND_INITIALIZER;
int cli_count = 0;
int koniecHry = 0;
int karty[7];
int faraby[7] = {0};

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
    //pthread_mutex_lock(&clients_mutex);

    //kazdemu klientovi posleme spravu
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

    //pthread_mutex_unlock(&clients_mutex);
}

//poslanie spravy jednemu klientovi
void send_messageToConcrete(char *s, int uid){
    //pthread_mutex_lock(&clients_mutex);

    //posleme spravu nasmu klientovi
    for(int i=0; i<5; ++i){
        if(clients[i]){
            if(clients[i]->uid == uid){
                if(write(clients[i]->sockfd, s, strlen(s)) < 0){
                    perror("ERROR: write to descriptor failed");
                    break;
                }
            }
        }
    }

    //pthread_mutex_unlock(&clients_mutex);
}

//poslanie spravy vsetkym klientom
void send_messageToAll(char *s){
    //pthread_mutex_lock(&clients_mutex);

    //kazdemu klientovi okrem odosielatela posleme spravu
    for(int i=0; i<5; ++i){
        if(clients[i]){
            if(write(clients[i]->sockfd, s, strlen(s)) < 0){
                perror("ERROR: write to descriptor failed");
                break;
            }
        }
    }

    //pthread_mutex_unlock(&clients_mutex);
}

//zo spravy klienta zistim, ci checkol alebo navysil alebo zlozil
void klientovaAkcia(char buff_out[2048], client_t* cli) {
    int cislo = (int)strtol(buff_out, NULL, 10);
    printf("Cislo je %d\n", cislo);
    printf("Buffer je je %s\n", buff_out);
    //char str[2048] = strstr(buff_out, " ");
    if (cislo == 0) {
        if (*cli->potrebnaStavka - cli->klientovaStavka == 0) {
            sprintf(buff_out, "Hrac %s checkuje\n", cli->name);
            send_messageToAll(buff_out);
        } else {
            if (*cli->potrebnaStavka - cli->klientovaStavka < cli->pocetZetonov) {
                sprintf(buff_out, "Hrac %s dorovnava\n", cli->name);
                send_messageToAll(buff_out);
                cli->pocetZetonov -= *cli->potrebnaStavka - cli->klientovaStavka;
                *cli->celkovaStavka += *cli->potrebnaStavka - cli->klientovaStavka;
                cli->klientovaStavka = *cli->potrebnaStavka;
            } else {
                sprintf(buff_out, "Hrac %s dava all in\n", cli->name);
                send_messageToAll(buff_out);
                *cli->celkovaStavka += cli->pocetZetonov;
                cli->klientovaStavka += cli->pocetZetonov;
                cli->pocetZetonov = 0;
            }
        }
    } else {
        if (cislo > 0) {
            if (cislo + (*cli->potrebnaStavka - cli->klientovaStavka) < cli->pocetZetonov) {
                *cli->celkovaStavka += cislo + (*cli->potrebnaStavka - cli->klientovaStavka);
                cli->pocetZetonov -= cislo + (*cli->potrebnaStavka - cli->klientovaStavka);
                cli->klientovaStavka += cislo + (*cli->potrebnaStavka - cli->klientovaStavka);
                *cli->potrebnaStavka = cli->klientovaStavka;
                cli->meniliSme = 1;
                sprintf(buff_out, "Hrac %s navysil stavku o %d\n", cli->name, cislo);
                send_messageToAll(buff_out);
            } else {
                *cli->celkovaStavka += cli->pocetZetonov;
                cli->klientovaStavka += cli->pocetZetonov;
                if (*cli->potrebnaStavka < cli->klientovaStavka) {
                    *cli->potrebnaStavka = cli->klientovaStavka;
                }
                cli->pocetZetonov = 0;
                cli->meniliSme = 1;
                sprintf(buff_out, "Hrac %s dava all in\n", cli->name);
                send_messageToAll(buff_out);
            }
        } else {
            cli->meniliSme = 2;
            sprintf(buff_out, "Hrac %s zlozil karty o %d\n", cli->name, cislo);
            send_messageToAll(buff_out);
        }
    }
}

//vlakno na manipulaciu s klientami, pre kazdeho klienta sa vytvori vlakno
void * handle_client(void * data) {
    char buff_out[2048];
    char name[32];
    int leave_flag = 0; //tu zistujeme, ci je klient pripojeny
    char pocetHracov[2048];

    cli_count++;
    client_t *cli = (client_t *)data;

    //pridanie klienta do hry
    if(recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) <  2 || strlen(name) >= 32-1){
        printf("Zadali ste zle meno.\n");
        leave_flag = 1;
    } else{
        strcpy(cli->name, name);
        sprintf(buff_out, "%s has joined\n", cli->name);      //ak zadal legitimne meno pridame ho do hry
        printf("%s", buff_out);
        printf("Celkovo je v hre %d hracov\n", cli_count);
        send_message(buff_out, cli->uid);                           //posleme ostatnym spravu, ze mame dalsieho hraca
        sprintf(pocetHracov, "Pocet hracov v hre: %d\n", cli_count);
        send_messageToAll(pocetHracov);
    }

    bzero(buff_out, 2048);

    while(1){
        //vsetko co nie je nula je true
        if (leave_flag) {
            break;
        }

        //ak uz nema zetony, skoncil
        /*if (cli->skoncil > 0) {
            sprintf(buff_out, "");
            send_messageToConcrete(buff_out, cli->uid);
            //write(cli->sockfd, "exit", 0);
            leave_flag = 1;
            //receive = 0;
            //break;
        }*/

        //cakame, kym klient nieco vykona
        int receive = recv(cli->sockfd, buff_out, 2048, 0);

        if (cli_count <= 1) {
            sprintf(buff_out, "HURA VYHRALI STE\n");
            send_messageToConcrete(buff_out, cli->uid);
            sprintf(buff_out, "exit\n");
            send_messageToConcrete(buff_out, cli->uid);
            //write(cli->sockfd, "exit", 0);
            leave_flag = 1;
            receive = 0;
            koniecHry = 1;
            //break;
        }

        if (receive > 0){
            //ak nieco dostaneme
            if(strlen(buff_out) > 0){
                send_message(buff_out, cli->uid);                          //posleme vsetkym klientom spravu, normalka
                //str_trim_lf(buff_out, strlen(buff_out));
                printf("%s -> %s\n", buff_out, cli->name);          //posleme spravu, ktoru poslal dany klient
                klientovaAkcia(buff_out, cli);
                pthread_cond_signal(&cakameNaKlienta);
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

int kontrola(dataHry* data, int cislo, int farba) {
    int pocetRovnakych = 0;
    for (int i = 0; i < cli_count; ++i) {
        if (clients[i]) {
            if (cislo == clients[i]->prvaKarta) {
                if (farba == clients[i]->prvaFarba) {
                    pocetRovnakych++;
                }
            }
        }
        if (cislo == clients[i]->druhaKarta) {
            if (farba == clients[i]->druhaFarba) {
                pocetRovnakych++;
            }
        }
    }
    if (cislo == data->prvaKarta) {
        if (farba == data->prvaFarba) {
            pocetRovnakych++;
        }
    }
    if (cislo == data->druhaKarta) {
        if (farba == data->druhaFarba) {
            pocetRovnakych++;
        }
    }
    if (cislo == data->tretiaKarta) {
        if (farba == data->tretiaFarba) {
            pocetRovnakych++;
        }
    }
    if (cislo == data->stvrtaKarta) {
        if (farba == data->stvrtaFarba) {
            pocetRovnakych++;
        }
    }
    if (cislo == data->piataKarta) {
        if (farba == data->prvaFarba) {
            pocetRovnakych++;
        }
    }
    if (pocetRovnakych > 1) {
        return 1;
    } else {
        return 0;
    }
}

int straightFlash(dataHry* data, int cislo) {
    printf("Dosli sme sem");

    printf("Dosli sme sem");
    karty[0] = clients[cislo]->prvaKarta;
    printf("Dosli sme sem");
    karty[1] = clients[cislo]->druhaKarta;
    karty[2] = data->prvaKarta;
    karty[3] = data->druhaKarta;
    karty[4] = data->tretiaKarta;
    karty[5] = data->stvrtaKarta;
    karty[6] = data->piataKarta;
    printf("Dosli sme sem");
    faraby[0] = clients[cislo]->prvaFarba;
    printf("Dosli sme sem");
    faraby[1] = clients[cislo]->druhaFarba;
    faraby[2] = data->prvaFarba;
    faraby[3] = data->druhaFarba;
    faraby[4] = data->tretiaFarba;
    faraby[5] = data->stvrtaFarba;
    faraby[6] = data->piataFarba;
    printf("Dosli sme sem");
    int zmena = 1;
    while (zmena > 0) {
        zmena = 0;
        for (int i = 0; i < 6; ++i) {
            if (karty[i] > karty[i+1]) {
                int a = karty[i];
                karty[i] = karty[i+1];
                karty[i+1] = a;
                int b = faraby[i];
                faraby[i] = faraby[i+1];
                faraby[i+1] = b;
                zmena++;
            }
        }
    }
    printf("Dosli sme sem2");
    int postupka = 0;
    int posledne = -1;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (faraby[i] == j) {
                if (posledne + 1 == karty[i]) {
                    postupka++;
                } else {
                    postupka = 0;
                }
                if (postupka == 4) {
                    //clients[i]->najvyssia = karty[i];
                    return 1;
                }
                posledne = karty[i];
            }
        }
    }
    printf("Dosli sme sem3");
    return 0;
}

int fourofAKind(dataHry* data, int cislo) {
    //int karty[7];
    karty[0] = clients[cislo]->prvaKarta;
    karty[1] = clients[cislo]->druhaKarta;
    karty[2] = data->prvaKarta;
    karty[3] = data->druhaKarta;
    karty[4] = data->tretiaKarta;
    karty[5] = data->stvrtaKarta;
    karty[6] = data->piataKarta;
    int zmena = 1;
    while (zmena > 0) {
        zmena = 0;
        for (int i = 0; i < 6; ++i) {
            if (karty[i] > karty[i+1]) {
                int a = karty[i];
                karty[i] = karty[i+1];
                karty[i+1] = a;
                zmena++;
            }
        }
    }
    int pocetRovnakych = 0;
    for (int i = 0; i < 6; ++i) {
        if (karty[i] == karty[i+1]) {
            pocetRovnakych++;
        } else {
            pocetRovnakych = 0;
        }
        if (pocetRovnakych == 3) {
            //clients[i]->najvyssia = karty[i];
            return 1;
        }
    }
    return 0;
}

int fullHouse(dataHry* data, int cislo) {
    //int karty[7];
    karty[0] = clients[cislo]->prvaKarta;
    karty[1] = clients[cislo]->druhaKarta;
    karty[2] = data->prvaKarta;
    karty[3] = data->druhaKarta;
    karty[4] = data->tretiaKarta;
    karty[5] = data->stvrtaKarta;
    karty[6] = data->piataKarta;
    int zmena = 1;
    while (zmena > 0) {
        zmena = 0;
        for (int i = 0; i < 6; ++i) {
            if (karty[i] > karty[i+1]) {
                int a = karty[i];
                karty[i] = karty[i+1];
                karty[i+1] = a;
                zmena++;
            }
        }
    }
    int pocetRovnakych = 0;
    int cislo1 = 0;
    for (int i = 0; i < 6; ++i) {
        if (karty[i] == karty[i+1]) {
            pocetRovnakych++;
            cislo1 = karty[i];
        } else {
            pocetRovnakych = 0;
        }
        if (pocetRovnakych == 2) {
            //clients[i]->najvyssia = karty[i];
            for (int j = 0; j < 6; ++j) {
                if (karty[j] == karty[j+1] && cislo1 != karty[j]) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

int flush(dataHry* data, int cislo) {
    //int faraby[7];
    //int karty[7];
    karty[0] = clients[cislo]->prvaKarta;
    karty[1] = clients[cislo]->druhaKarta;
    karty[2] = data->prvaKarta;
    karty[3] = data->druhaKarta;
    karty[4] = data->tretiaKarta;
    karty[5] = data->stvrtaKarta;
    karty[6] = data->piataKarta;
    faraby[0] = clients[cislo]->prvaFarba;
    faraby[1] = clients[cislo]->druhaFarba;
    faraby[2] = data->prvaFarba;
    faraby[3] = data->druhaFarba;
    faraby[4] = data->tretiaFarba;
    faraby[5] = data->stvrtaFarba;
    faraby[6] = data->piataFarba;
    int zmena = 1;
    while (zmena > 0) {
        zmena = 0;
        for (int i = 0; i < 6; ++i) {
            if (faraby[i] > faraby[i+1]) {
                int a = faraby[i];
                faraby[i] = faraby[i+1];
                faraby[i+1] = a;
                zmena++;
            }
        }
    }
    int pocetRovnakych = 0;
    for (int i = 0; i < 6; ++i) {
        if (faraby[i] == faraby[i+1]) {
            pocetRovnakych++;
        } else {
            pocetRovnakych = 0;
        }
        if (pocetRovnakych == 4) {
            //clients[i]->najvyssia = karty[i];
            return 1;
        }
    }
    return 0;
}

int straight(dataHry* data, int cislo) {
    //int karty[7];
    karty[0] = clients[cislo]->prvaKarta;
    karty[1] = clients[cislo]->druhaKarta;
    karty[2] = data->prvaKarta;
    karty[3] = data->druhaKarta;
    karty[4] = data->tretiaKarta;
    karty[5] = data->stvrtaKarta;
    karty[6] = data->piataKarta;
    int zmena = 1;
    while (zmena > 0) {
        zmena = 0;
        for (int i = 0; i < 6; ++i) {
            if (karty[i] > karty[i+1]) {
                int a = karty[i];
                karty[i] = karty[i+1];
                karty[i+1] = a;
                zmena++;
            }
        }
    }
    int postupka = 0;
    for (int i = 0; i < 6; ++i) {
        if (karty[i] == karty[i+1] + 1) {
            postupka++;
        } else {
            if (karty[i] != karty[i+1]) {
                postupka = 0;
            }
        }
        if (postupka == 4) {
            //clients[i]->najvyssia = karty[i];
            return 1;
        }
    }
    return 0;
}

int threeofAKind(dataHry* data, int cislo) {
    //int karty[7];
    karty[0] = clients[cislo]->prvaKarta;
    karty[1] = clients[cislo]->druhaKarta;
    karty[2] = data->prvaKarta;
    karty[3] = data->druhaKarta;
    karty[4] = data->tretiaKarta;
    karty[5] = data->stvrtaKarta;
    karty[6] = data->piataKarta;
    int zmena = 1;
    while (zmena > 0) {
        zmena = 0;
        for (int i = 0; i < 6; ++i) {
            if (karty[i] > karty[i+1]) {
                int a = karty[i];
                karty[i] = karty[i+1];
                karty[i+1] = a;
                zmena++;
            }
        }
    }
    int pocetRovnakych = 0;
    for (int i = 0; i < 6; ++i) {
        if (karty[i] == karty[i+1]) {
            pocetRovnakych++;
        } else {
            pocetRovnakych = 0;
        }
        if (pocetRovnakych == 2) {
            //clients[i]->najvyssia = karty[i];
            return 1;
        }
    }
    return 0;
}

int twoPairs(dataHry* data, int cislo) {
    //int karty[7];
    karty[0] = clients[cislo]->prvaKarta;
    karty[1] = clients[cislo]->druhaKarta;
    karty[2] = data->prvaKarta;
    karty[3] = data->druhaKarta;
    karty[4] = data->tretiaKarta;
    karty[5] = data->stvrtaKarta;
    karty[6] = data->piataKarta;
    int zmena = 1;
    while (zmena > 0) {
        zmena = 0;
        for (int i = 0; i < 6; ++i) {
            if (karty[i] > karty[i+1]) {
                int a = karty[i];
                karty[i] = karty[i+1];
                karty[i+1] = a;
                zmena++;
            }
        }
    }
    int cislo1 = 0;
    for (int i = 0; i < 6; ++i) {
        if (karty[i] == karty[i+1]) {
            //clients[i]->najvyssia = karty[i];
            cislo1 = karty[i];
            for (int j = 0; j < 6; ++j) {
                if (karty[j] == karty[j + 1] && cislo1 != karty[j]) {
                    //if (clients[i]->najvyssia < karty[i]) {
                        //clients[i]->najvyssia = karty[i];
                    //}
                    return 1;
                }
            }
        }
    }
    return 0;
}

int onePair(dataHry* data, int cislo) {
    karty[0] = clients[cislo]->prvaKarta;
    karty[1] = clients[cislo]->druhaKarta;
    karty[2] = data->prvaKarta;
    karty[3] = data->druhaKarta;
    karty[4] = data->tretiaKarta;
    karty[5] = data->stvrtaKarta;
    karty[6] = data->piataKarta;
    int zmena = 1;
    while (zmena > 0) {
        zmena = 0;
        for (int i = 0; i < 6; ++i) {
            if (karty[i] > karty[i+1]) {
                int a = karty[i];
                karty[i] = karty[i+1];
                karty[i+1] = a;
                zmena++;
            }
        }
    }
    for (int i = 0; i < 6; ++i) {
        if (karty[i] == karty[i+1]) {
            //clients[i]->najvyssia = karty[i];
            return 1;
        }
    }
    return 0;
}

void vyhodnotVysledky(dataHry* data) {
    char buff_out[2048];
    int body[cli_count];
    int vitazi[cli_count];
    //dataHry *dataH = (dataHry *)data;
    int pocetVitazov = 0;
    for (int i = 0; i < cli_count; ++i) {
        if (clients[i]) {
            if (clients[i]->meniliSme == 2) {
                sprintf(buff_out, "Hrac %s zlozil karty\n", clients[i]->name);
                send_messageToAll(buff_out);
                body[i] = -1;
            } else {
                sprintf(buff_out, "Hrac %s ma karty %d,f%d a %d, %d\n", clients[i]->name, clients[i]->prvaKarta,
                        clients[i]->prvaFarba, clients[i]->druhaKarta, clients[i]->druhaFarba);
                send_messageToAll(buff_out);
                printf("Dosli sme sem3\n");
                //number = straightFlash(dataH, i);
                printf("Dosli sme sem4\n");
                dataHry *dataH = (dataHry *) data;
                int f = i;
                if ((int) straightFlash(dataH, f) == 1) {
                    //if (2 == 1) {
                    printf("Dosli sme sem5\n");
                    sprintf(buff_out, "Hrac %s ma cistu postupku\n", clients[i]->name);
                    send_messageToAll(buff_out);
                    body[i] = 8;
                } else {
                    printf("Disli sme sem5\n");
                    if ((int) fourofAKind(dataH, f) == 1) {
                        //if (2 == 1) {
                        printf("Disli sme sem61\n");
                        sprintf(buff_out, "Hrac %s ma styri rovnake karty\n", clients[i]->name);
                        send_messageToAll(buff_out);
                        body[i] = 7;
                    } else {
                        printf("Disli sme sem62\n");
                        if ((int) fullHouse(dataH, f) == 1) {
                            //if (2 == 1) {
                            printf("Disli sme sem71\n");
                            sprintf(buff_out, "Hrac %s ma full house\n", clients[i]->name);
                            send_messageToAll(buff_out);
                            body[i] = 6;
                        } else {
                            printf("Disli sme sem72\n");
                            if ((int) flush(dataH, f) == 1) {
                                //if (2 == 1) {
                                printf("Disli sme sem81\n");
                                sprintf(buff_out, "Hrac %s ma flush (5 s rovnakou farbou)\n", clients[i]->name);
                                send_messageToAll(buff_out);
                                body[i] = 5;
                            } else {
                                printf("Disli sme sem82\n");
                                if ((int) straight(dataH, f) == 1) {
                                    //if (2 == 1) {
                                    printf("Disli sme sem91\n");
                                    sprintf(buff_out, "Hrac %s ma straight (postupku bez farby)\n", clients[i]->name);
                                    send_messageToAll(buff_out);
                                    body[i] = 4;
                                } else {
                                    printf("Disli sme sem92\n");
                                    if ((int) threeofAKind(dataH, f) == 1) {
                                        //if (2 == 1) {
                                        printf("Disli sme sem101\n");
                                        sprintf(buff_out, "Hrac %s ma tri rovnake\n", clients[i]->name);
                                        send_messageToAll(buff_out);
                                        body[i] = 3;
                                    } else {
                                        printf("Disli sme sem102\n");
                                        if ((int) twoPairs(dataH, f) == 1) {
                                            //if (2 == 1) {
                                            printf("Disli sme sem111\n");
                                            sprintf(buff_out, "Hrac %s ma dva pary\n", clients[i]->name);
                                            send_messageToAll(buff_out);
                                            body[i] = 2;
                                        } else {
                                            printf("Disli sme sem112\n");
                                            if ((int) onePair(dataH, f) == 1) {
                                                //if (2 == 1) {
                                                printf("Disli sme sem121\n");
                                                sprintf(buff_out, "Hrac %s ma par\n", clients[i]->name);
                                                send_messageToAll(buff_out);
                                                body[i] = 1;
                                            } else {
                                                printf("Disli sme sem122\n");
                                                body[i] = 0;
                                                if (clients[i]->prvaKarta > clients[i]->druhaKarta) {
                                                    clients[i]->najvyssia = clients[i]->prvaKarta;
                                                } else {
                                                    clients[i]->najvyssia = clients[i]->druhaKarta;
                                                }
                                                sprintf(buff_out, "Hrac %s ma najvyssiu kartu %d\n", clients[i]->name,
                                                        clients[i]->najvyssia);
                                                send_messageToAll(buff_out);
                                            }

                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    printf("Dosli sme na koniec\n");
    int najvyssie = -1;
    for (int i = 0; i < cli_count; ++i) {
        if (clients[i]) {
            if (body[i] > najvyssie) {
                najvyssie = body[i];
                vitazi[0] = i;
                pocetVitazov = 0;
            }
            if (body[i] == najvyssie) {
                vitazi[pocetVitazov] = i;
                pocetVitazov += 1;
            }
        }
    }
    printf("Dosli sme na koniec2\n");
    for (int i = 0; i < pocetVitazov; ++i) {
        if (clients[i]) {
            clients[vitazi[i]]->pocetZetonov += (*data->celkovaStavka / pocetVitazov);
            sprintf(buff_out, "Vyhral hrac %s\n", clients[vitazi[i]]->name);
            send_messageToAll(buff_out);
        }
    }
    //ak uz nema zetony, skoncil
    for (int i = 0; i < cli_count; ++i) {
        if (clients[i]) {
            if (clients[i]->pocetZetonov <= 0) {
                sprintf(buff_out, "Klient %s ma nula zetonov, skoncil\n", clients[i]->name);
                send_messageToAll(buff_out);
                sprintf(buff_out, "Napiste rozluckovu vetu\n");
                send_messageToConcrete(buff_out, clients[i]->uid);
                pthread_cond_wait(&cakameNaKlienta, &clients_mutex);
                sprintf(buff_out, "exit");
                send_messageToConcrete(buff_out, clients[i]->uid);
                clients[i]->skoncil = 1;
            }
        }
    }
    printf("Dosli sme na koniec3\n");
}

void * hlavny_program(void * data) {
    //budeme cakat pol minuty, kym sa vsetci pripoja
    printf("Cakame na hracov\n");
    char buff_out[2048];
    dataHry *dataH = (dataHry *)data;
    usleep(30000000);
    int kontrilaOriginality = 1;
    int pocetHrajucich = 0;
    int zlozilPosledny = 0;
    int vitaz = 0;
    int pocetBodov = 0;
    int druha = 0;

    //ak je ich malo koncime, ak je ich dost, ideme hrat
    if (cli_count <= 1) {
        printf("Nedostatok hracov na zahajenie hry\n");
        sprintf(buff_out, "Nedostatok hracov na zahajenie hry\n");
        send_messageToAll(buff_out);
    } else {
        printf("ZACINAME HRAT S POCTOM HRACOV %d\n", cli_count);
        sprintf(buff_out, "ZACINAME HRAT S POCTOM HRACOV %d\n", cli_count);
        send_messageToAll(buff_out);
        while (cli_count > 1) {
            int mameStavene = 0;
            //blindy
            //pthread_mutex_lock(&clients_mutex);
            printf("ZACINAME NOVE KOLO\n");
            sprintf(buff_out, "ZACINAME NOVE KOLO\n");
            send_messageToAll(buff_out);
            pocetHrajucich = cli_count;
            for (int i = 0; i < cli_count; ++i) {
                if (clients[i]) {
                    if (dataH->bigBlind == i) {
                        clients[i]->pocetZetonov -= 40;
                        clients[i]->klientovaStavka += 40;
                        *dataH->celkovaStavka += 40;
                        sprintf(buff_out, "Vkladate big blind 40, vas pocet zetonov je %d\n", clients[i]->pocetZetonov);
                        send_messageToConcrete(buff_out, clients[i]->uid);
                        sprintf(buff_out, "Hrac %s vklada big blind 40\n", clients[i]->name);
                        send_message(buff_out, clients[i]->uid);
                    }
                    if (dataH->smallBlind == i) {
                        clients[i]->pocetZetonov -= 20;
                        clients[i]->klientovaStavka += 20;
                        *dataH->celkovaStavka += 20;
                        sprintf(buff_out, "Vkladate small blind 20, vas pocet zetonov je %d\n", clients[i]->pocetZetonov);
                        send_messageToConcrete(buff_out, clients[i]->uid);
                        sprintf(buff_out, "Hrac %s vklada small blind 20\n", clients[i]->name);
                        send_message(buff_out, clients[i]->uid);
                    }
                } else {
                    printf("Klient neexustuje CHYBA");
                }
            }
            *dataH->potrebnaStavka = 40;
            //prve stavky, zacnem uz rovno s kartami
            printf("PRIJIMAM PRVE STAVKY\n");
            sprintf(buff_out, "PRIJIMAM PRVE STAVKY\n");
            send_messageToAll(buff_out);
            printf("ROZDAVAM PRVE DVE KARTY\n");
            sprintf(buff_out, "ROZDAVAM PRVE DVE KARTY\n");
            send_messageToAll(buff_out);
            for (int i = 0; i < cli_count; ++i) {
                kontrilaOriginality = 1;
                while (kontrilaOriginality > 0) {
                    kontrilaOriginality = 0;
                    if (clients[i]) {
                        clients[i]->prvaKarta = (rand() % 14) + 1;
                        clients[i]->prvaFarba = (rand() % 4) + 1;
                        clients[i]->druhaKarta = (rand() % 14) + 1;
                        clients[i]->druhaFarba = (rand() % 4) + 1;
                    } else {
                        printf("Klient neexustuje CHYBA");
                    }
                    kontrilaOriginality += kontrola(dataH, clients[i]->prvaKarta, clients[i]->prvaFarba);
                    kontrilaOriginality += kontrola(dataH, clients[i]->druhaKarta, clients[i]->druhaFarba);
                }
                sprintf(buff_out, "Vase karty su %d,f%d a %d,f%d\n", clients[i]->prvaKarta, clients[i]->prvaFarba, clients[i]->druhaKarta, clients[i]->druhaFarba);
                send_messageToConcrete(buff_out, clients[i]->uid);
            }
            //prve dve karty
            if (pocetHrajucich < 2) {
                mameStavene = 1;
            }
            while (mameStavene == 0) {
                mameStavene = 1;
                for (int i = 0; i < cli_count; ++i) {
                    if (clients[i]) {
                        if (clients[i]->meniliSme == 0) {
                            sprintf(buff_out, "Vas pocet zetonov je %d\n Celkovo je v hre %d\n Potrebna stavka je %d\n Vasa stavka je %d\n Vase karty su %d,f%d a %d,f%d\n Stlac 0 pre check/dorovnanie, kladne cislo pre zvysenie stavky, zaporne pre zlozenie\n", clients[i]->pocetZetonov, *clients[i]->celkovaStavka, *clients[i]->potrebnaStavka, clients[i]->klientovaStavka, clients[i]->prvaKarta, clients[i]->prvaFarba, clients[i]->druhaKarta, clients[i]->druhaFarba);
                            send_messageToConcrete(buff_out, clients[i]->uid);
                            pthread_cond_wait(&cakameNaKlienta, &clients_mutex);
                            if (clients[i]->meniliSme == 1) {
                                mameStavene = 0;
                                clients[i]->meniliSme = 0;
                            }
                            if (clients[i]->meniliSme == 2) {
                                pocetHrajucich--;
                                zlozilPosledny = clients[i]->uid;
                            }
                        }
                    } else {
                        printf("Klient neexustuje CHYBA");
                    }
                }
            }
            //pridavam dalsie tri karty
            printf("ROZDAVAM TRI SPOLOCNE KARTY\n");
            sprintf(buff_out, "ROZDAVAM TRI SPOLOCNE KARTY\n");
            send_messageToAll(buff_out);
            kontrilaOriginality = 1;
            if (pocetHrajucich < 2) {
                kontrilaOriginality = 0;
            }
            while (kontrilaOriginality > 0) {
                kontrilaOriginality = 0;
                dataH->prvaKarta = (rand() % 14) + 1;
                dataH->druhaKarta = (rand() % 14) + 1;
                dataH->tretiaKarta = (rand() % 14) + 1;
                dataH->prvaFarba = (rand() % 4) + 1;
                dataH->druhaFarba = (rand() % 4) + 1;
                dataH->tretiaFarba = (rand() % 4) + 1;
                kontrilaOriginality += kontrola(dataH, dataH->prvaKarta, dataH->prvaFarba);
                kontrilaOriginality += kontrola(dataH, dataH->druhaKarta, dataH->druhaFarba);
                kontrilaOriginality += kontrola(dataH, dataH->tretiaKarta, dataH->tretiaFarba);
            }
            sprintf(buff_out, "Spolocne karty su %d,f%d a %d,f%d a %d,f%d\n", dataH->prvaKarta, dataH->prvaFarba, dataH->druhaKarta, dataH->druhaFarba, dataH->tretiaKarta, dataH->tretiaFarba);
            send_messageToAll(buff_out);

            mameStavene = 0;
            if (pocetHrajucich < 2) {
                mameStavene = 1;
            }
            while (mameStavene == 0) {
                mameStavene = 1;
                for (int i = 0; i < cli_count; ++i) {
                    if (clients[i]) {
                        if (clients[i]->meniliSme == 0) {
                            sprintf(buff_out, "Vas pocet zetonov je %d\n Celkovo je v hre %d\n Potrebna stavka je %d\n Vasa stavka je %d\n Vase karty su %d,f%d a %d,f%d\n Spolocne karty su %d,f%d a %d,f%d a %d,f%d\n Stlac 0 pre check/dorovnanie, kladne cislo pre zvysenie stavky, zaporne pre zlozenie\n", clients[i]->pocetZetonov, *clients[i]->celkovaStavka, *clients[i]->potrebnaStavka, clients[i]->klientovaStavka, clients[i]->prvaKarta, clients[i]->prvaFarba, clients[i]->druhaKarta, clients[i]->druhaFarba, dataH->prvaKarta, dataH->prvaFarba, dataH->druhaKarta, dataH->druhaFarba, dataH->tretiaKarta, dataH->tretiaFarba);
                            send_messageToConcrete(buff_out, clients[i]->uid);
                            pthread_cond_wait(&cakameNaKlienta, &clients_mutex);
                            if (clients[i]->meniliSme == 1) {
                                mameStavene = 0;
                                clients[i]->meniliSme = 0;
                            }
                            if (clients[i]->meniliSme == 2) {
                                pocetHrajucich--;
                                zlozilPosledny = clients[i]->uid;
                            }
                        }
                    } else {
                        printf("Klient neexustuje CHYBA");
                    }
                }
            }
            //pridavam stvrtu kartu
            printf("PRIDAVAM STVRTU KARTU\n");
            sprintf(buff_out, "PRIDAVAM STVRTU KARTU\n");
            send_messageToAll(buff_out);
            kontrilaOriginality = 1;
            if (pocetHrajucich < 2) {
                kontrilaOriginality = 0;
            }
            while (kontrilaOriginality > 0) {
                kontrilaOriginality = 0;
                dataH->stvrtaKarta = (rand() % 14) + 1;
                dataH->stvrtaFarba = (rand() % 4) + 1;
                kontrilaOriginality += kontrola(dataH, dataH->stvrtaKarta, dataH->stvrtaFarba);
            }
            sprintf(buff_out, "Stvrta karta je %d,f%d\n", dataH->stvrtaKarta, dataH->stvrtaFarba);
            send_messageToAll(buff_out);

            mameStavene = 0;
            if (pocetHrajucich < 2) {
                mameStavene = 1;
            }
            while (mameStavene == 0) {
                mameStavene = 1;
                for (int i = 0; i < cli_count; ++i) {
                    if (clients[i]) {
                        if (clients[i]->meniliSme == 0) {
                            sprintf(buff_out, "Vas pocet zetonov je %d\n Celkovo je v hre %d\n Potrebna stavka je %d\n Vasa stavka je %d\n Vase karty su %d,f%d a %d,f%d\n Spolocne karty su %d,f%d a %d,f%d a %d,f%d a %d,f%d\n Stlac 0 pre check/dorovnanie, kladne cislo pre zvysenie stavky, zaporne pre zlozenie\n", clients[i]->pocetZetonov, *clients[i]->celkovaStavka, *clients[i]->potrebnaStavka, clients[i]->klientovaStavka, clients[i]->prvaKarta, clients[i]->prvaFarba, clients[i]->druhaKarta, clients[i]->druhaFarba, dataH->prvaKarta, dataH->prvaFarba, dataH->druhaKarta, dataH->druhaFarba, dataH->tretiaKarta, dataH->tretiaFarba, dataH->stvrtaKarta, dataH->stvrtaFarba);
                            send_messageToConcrete(buff_out, clients[i]->uid);
                            pthread_cond_wait(&cakameNaKlienta, &clients_mutex);
                            if (clients[i]->meniliSme == 1) {
                                mameStavene = 0;
                                clients[i]->meniliSme = 0;
                            }
                            if (clients[i]->meniliSme == 2) {
                                pocetHrajucich--;
                                zlozilPosledny = clients[i]->uid;
                            }
                        }
                    } else {
                        printf("Klient neexustuje CHYBA");
                    }
                }
            }
            //pridavam piatu kartu
            printf("PRIDAVAM PIATU KARTU\n");
            sprintf(buff_out, "PRIDAVAM PIATU KARTU\n");
            send_messageToAll(buff_out);
            kontrilaOriginality = 1;
            if (pocetHrajucich < 2) {
                kontrilaOriginality = 0;
            }
            while (kontrilaOriginality > 0) {
                kontrilaOriginality = 0;
                dataH->piataKarta = (rand() % 14) + 1;
                dataH->piataFarba = (rand() % 4) + 1;
                kontrilaOriginality += kontrola(dataH, dataH->piataKarta, dataH->piataFarba);
            }
            sprintf(buff_out, "Piata karta je %d,f%d\n", dataH->piataKarta, dataH->piataFarba);
            send_messageToAll(buff_out);

            mameStavene = 0;
            if (pocetHrajucich < 2) {
                mameStavene = 1;
            }
            while (mameStavene == 0) {
                mameStavene = 1;
                for (int i = 0; i < cli_count; ++i) {
                    if (clients[i]) {
                        if (clients[i]->meniliSme == 0) {
                            sprintf(buff_out, "Vas pocet zetonov je %d\n Celkovo je v hre %d\n Potrebna stavka je %d\n Vasa stavka je %d\n Vase karty su %d,f%d a %d,f%d\n Spolocne karty su %d,f%d a %d,f%d a %d,f%d a %d,f%d a %d,f%d\n Stlac 0 pre check/dorovnanie, kladne cislo pre zvysenie stavky, zaporne pre zlozenie\n", clients[i]->pocetZetonov, *clients[i]->celkovaStavka, *clients[i]->potrebnaStavka, clients[i]->klientovaStavka, clients[i]->prvaKarta, clients[i]->prvaFarba, clients[i]->druhaKarta, clients[i]->druhaFarba, dataH->prvaKarta, dataH->prvaFarba, dataH->druhaKarta, dataH->druhaFarba, dataH->tretiaKarta, dataH->tretiaFarba, dataH->stvrtaKarta, dataH->stvrtaFarba, dataH->piataKarta, dataH->piataFarba);
                            send_messageToConcrete(buff_out, clients[i]->uid);
                            pthread_cond_wait(&cakameNaKlienta, &clients_mutex);
                            if (clients[i]->meniliSme == 1) {
                                mameStavene = 0;
                                clients[i]->meniliSme = 0;
                            }
                            if (clients[i]->meniliSme == 2) {
                                pocetHrajucich--;
                                zlozilPosledny = clients[i]->uid;
                            }
                        }
                    } else {
                        printf("Klient neexustuje CHYBA");
                    }
                }
            }
            //spocitame body a urcime vitaza kola
            sprintf(buff_out, " \n");
            send_messageToAll(buff_out);
            sprintf(buff_out, "VYHODNOTENIE VYSLEDKOV\n");
            send_messageToAll(buff_out);
            if (pocetHrajucich == 1) {
                for (int i = 0; i < cli_count; ++i) {
                    if (clients[i]) {
                        if (clients[i]->meniliSme == 0) {
                            sprintf(buff_out, "VITAZOM KOLA JE %s\n", clients[i]->name);
                            send_messageToAll(buff_out);
                            clients[i]->pocetZetonov += *dataH->celkovaStavka;
                            break;
                        }
                    }
                }
            }
            if (pocetHrajucich == 0) {
                for (int i = 0; i < cli_count; ++i) {
                    if (clients[i]) {
                        if (clients[i]->uid == zlozilPosledny) {
                            sprintf(buff_out, "VITAZOM KOLA JE %s\n", clients[i]->name);
                            send_messageToAll(buff_out);
                            clients[i]->pocetZetonov += *dataH->celkovaStavka;
                            break;
                        }
                    }
                }
            }
            if (pocetHrajucich > 1) {
                vyhodnotVysledky(dataH);
            }

            printf("Dosli sme na koniec4\n");
            dataH->bigBlind += 1;
            dataH->smallBlind += 1;
            if (dataH->bigBlind >= cli_count) {
                dataH->bigBlind = 0;
            }
            if (dataH->smallBlind >= cli_count) {
                dataH->smallBlind = 0;
            }
            *dataH->celkovaStavka = 0;
            *dataH->potrebnaStavka = 0;
            printf("Dosli sme na koniec5\n");
            for (int i = 0; i < cli_count; ++i) {
                if (clients[i]) {
                    clients[i]->klientovaStavka = 0;
                    clients[i]->meniliSme = 0;
                    clients[i]->prvaKarta = 0;
                    clients[i]->druhaKarta = 0;
                }
            }
            printf("Dosli sme na koniec6\n");
            dataH->prvaKarta = 0;
            dataH->druhaKarta = 0;
            dataH->tretiaKarta = 0;
            dataH->stvrtaKarta = 0;
            dataH->piataKarta = 0;
            printf("Dosli sme na koniec7\n");
            zlozilPosledny = 0;
            vitaz = 0;
            druha = 0;
            pocetBodov = 0;
            //pthread_mutex_unlock(&clients_mutex);
        }
        //printf("Dosli sme sem 3\n");
    }
    koniecHry = 1;
    return NULL;
}

int main() {
    //defaultne nastavenia
    srand(time(NULL));
    char* ip = "127.0.0.1";
    int port = 9004;                 //port, na ktory sa pripajame
    int uid = 10;

    int option = 1;
    int listenFD = 0, connfD = 0;    //hlavny socket, kontrola pripojenia
    struct sockaddr_in serv_addr;    //server
    struct sockaddr_in client_addr;  //klient
    pthread_t tid;
    pthread_t hid;

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

    //pthread_cond_t cakameNaKlienta;
    //pthread_cond_init(&cakameNaKlienta, NULL);

    //vytvorime si hlavnu sekciu hry

    //dataHry *d = (dataHry *)malloc(sizeof(dataHry));
    //int karty[5];
    int smallBlind = 1;
    int bigBlind = 0;
    int celkovaStavka = 0;
    int potrebnaStavka = 0;
    dataHry d = {bigBlind, smallBlind, &celkovaStavka, &potrebnaStavka, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    //d->mutex = &clients_mutex;
    //d->cakamenaKlienta = &cakameNaKlienta;
    pthread_create(&hid, NULL, &hlavny_program, &d);

    //tato cast bude nastavovat noveho klienta
    while (koniecHry == 0) {
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
        cli->pocetZetonov = 1000;
        cli->klientovaStavka = 0;
        cli->celkovaStavka = &celkovaStavka;
        cli->potrebnaStavka = &potrebnaStavka;
        cli->prvaKarta = 0;
        cli->druhaKarta = 0;
        cli->prvaFarba = 0;
        cli->druhaFarba = 0;
        cli->meniliSme = 0;
        cli->najvyssia = 0;
        cli->skoncil = 0;

        //pridame klienta do hry
        queue_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void*)cli);

        usleep(1000);
    }
    pthread_join(hid, NULL);
    pthread_mutex_destroy(&clients_mutex);
    pthread_cond_destroy(&cakameNaKlienta);
    printf("Skoncili sme\n");
}
