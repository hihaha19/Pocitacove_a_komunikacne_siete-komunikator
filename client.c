#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include "unistd.h"
#include <sys/types.h> 
#include <winsock2.h> 
#include <Windows.h>
#include <openssl/sha.h>
#include <sys/stat.h>


#pragma pack(1)

#define TYP_DATOVY_PAKET 100
#define TYP_PRVY_PAKET 101
#define TYP_ODPOVED_NA_ZNOVUVYZIADANIE 102
#define TYP_POTVRDZUJUCI_PAKET 103
#define TYP_KEEP_ALIVE_PAKET 104
#define GENERAL_ERROR	3
#define SUCCESS	0
#define VELKOST_SUBORU 1467
#define NO_RESPONSE 1
#define CHYBNY_FRAGMENT 2

typedef struct Datovy_paket {
    int poradie;
    int velkost;
    unsigned int kontrolny_sucet;
    char data[1456];
}DATOVY_PAKET;

typedef struct Prvy_paket {
    char typ_paketu[20];
    int pocet_fragmentov;
    char nazov_suboru[500];
    char miesto_ulozenia_suboru[200];
}PRVY_PAKET;

typedef struct Odpoved_na_znovuvyziadanie {
    char typ_paketu[20];
    char odpoved;
}ODPOVED_NA_ZNOVUVYZIADANIE;

typedef struct Potvrdzujuci_paket {
    char typ_paketu[22];
    char potvrdenie_spojenia;
    char vypln[20];
}POTVRDZUJUCI_PAKET;

typedef struct Keep_alive_paket {
    char typ_paketu[17];
    int casovy_usek;
}KEEP_ALIVE_PAKET;

typedef struct Klient_konfig {
    char cesta_k_suboru[200];
    char miesto_ulozenia_suboru[200];
    char nazov_suboru[100];
    int subor;
    char textova_sprava[1000];
    int velkost_fragmentu;
    int pocet_fragmentov;
    int chyba;
    unsigned int port_servera;
    unsigned int port_klienta;
    unsigned char ip_klienta[4];
    unsigned char ip_klienta_v_hexa[1];
}KLIENT_KONFIG;


#define VELKOST_DATOVY_PAKET sizeof(DATOVY_PAKET)
#define VELKOST_PRVY_PAKET sizeof(PRVY_PAKET)
#define VELKOST_ODPOVED_NA_ZNOVUVYZIADANIE sizeof(ODPOVED_NA_ZNOVUVYZIADANIE)
#define VELKOST_POTVRDZUJUCI_PAKET sizeof(POTVRDZUJUCI_PAKET)
#define VELKOST_KEEP_ALIVE_PAKET sizeof(KEEP_ALIVE_PAKET)

int inicializuj() {
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed. Error Code : %d", WSAGetLastError());
        return 1;
    }

    return 0;
}


int posli_paket(SOCKET sockfd, void* paket, int size, int pocet_fragmentov, int* pocet_keep_alive_bez_odpovede, int* pocet_sekund, KLIENT_KONFIG konfig) {
    int rv = GENERAL_ERROR, pocet_bytov = 0, velkost_adresy_servera = 0, n = 0, odpoved_na_poslanie = 0;
    DATOVY_PAKET* datovy_paket = NULL;
    PRVY_PAKET* prvy_paket = NULL;
    KEEP_ALIVE_PAKET* keep_alive_paket = NULL;
    struct sockaddr_in adresa_servera, adresa_klienta;
    unsigned char vystup_hashu[20] = { 0 };
    unsigned int checksum = 0;
    char data[2000], odpoved_na_znovuzaslanie[2000] = { 0 }, odpoved_od_servera[VELKOST_POTVRDZUJUCI_PAKET] = { 0 };


    memset(data, 0, sizeof(data));
    memset(odpoved_na_znovuzaslanie, 0, sizeof(odpoved_na_znovuzaslanie));
    memset(&adresa_servera, 0, sizeof(adresa_servera));

    adresa_servera.sin_family = AF_INET;
    adresa_servera.sin_port = htons(konfig.port_servera);
    adresa_servera.sin_addr.s_addr = htonl(0x7f000001);

    adresa_klienta.sin_family = AF_INET;
    adresa_klienta.sin_addr.s_addr = INADDR_ANY;
    adresa_klienta.sin_port = htons(konfig.port_klienta);

    bind(sockfd, (struct sockaddr*)&adresa_klienta, sizeof(struct sockaddr_in));

    velkost_adresy_servera = sizeof(adresa_servera);


    if (size == VELKOST_DATOVY_PAKET)
    {
        datovy_paket = (DATOVY_PAKET*)paket;
        memcpy(data, datovy_paket, VELKOST_DATOVY_PAKET);
        sendto(sockfd, data, VELKOST_DATOVY_PAKET, 0, &(adresa_servera), sizeof(adresa_servera));

        pocet_bytov = recvfrom(sockfd, odpoved_od_servera, sizeof(odpoved_od_servera), 0, (struct sockaddr*)&adresa_servera, &velkost_adresy_servera);

        if (pocet_bytov < 0) {
            printf("Server neodpovedal na prijatie paketu, koncim spojenie\n");
            rv = NO_RESPONSE;
        }

        POTVRDZUJUCI_PAKET* odozva_od_servera = (POTVRDZUJUCI_PAKET*)malloc(1 * VELKOST_POTVRDZUJUCI_PAKET);
        memcpy(odozva_od_servera, odpoved_od_servera, VELKOST_POTVRDZUJUCI_PAKET);
        if (odozva_od_servera->potvrdenie_spojenia == 'p' && strcmp(odozva_od_servera->typ_paketu, "potvrdenie_spojenia", sizeof("potvrdenie_spojenia")) == 0) {
            printf("Odoslal som fragment s poradovym cislom %d a velkostou %d\n", datovy_paket->poradie, datovy_paket->velkost);
            rv = SUCCESS;
        }


        else if (odozva_od_servera->potvrdenie_spojenia == 'n' && strcmp(odozva_od_servera->typ_paketu, "potvrdenie_spojenia", sizeof("potvrdenie_spojenia")) == 0) {
            ODPOVED_NA_ZNOVUVYZIADANIE* odpoved = (ODPOVED_NA_ZNOVUVYZIADANIE*)malloc(1 * VELKOST_ODPOVED_NA_ZNOVUVYZIADANIE);

            memcpy(odpoved->typ_paketu, "odpoved_na_znovuvyziadanie", sizeof("odpoved_na_znovuvyziadanie"));

            printf("Mam znova poslat chybny fragment? Ano (1) alebo nie (0)\n");
            scanf("%d", &odpoved_na_poslanie);

            if (odpoved_na_poslanie == 1) {
                odpoved->odpoved = 'p';
                memcpy(odpoved_na_znovuzaslanie, odpoved, VELKOST_ODPOVED_NA_ZNOVUVYZIADANIE);
                sendto(sockfd, odpoved_na_znovuzaslanie, VELKOST_ODPOVED_NA_ZNOVUVYZIADANIE, 0, &(adresa_servera), sizeof(adresa_servera));
                //vypocitanie spravneho checksumu
                SHA1(datovy_paket->data, strlen(datovy_paket->data), &vystup_hashu);
                for (n = 0; n < 20; n++)
                    checksum += (unsigned int)vystup_hashu[n];

                datovy_paket->kontrolny_sucet = checksum;
                memcpy(data, datovy_paket, VELKOST_DATOVY_PAKET);
                sendto(sockfd, data, VELKOST_DATOVY_PAKET, 0, &(adresa_servera), sizeof(adresa_servera));

                pocet_bytov = recvfrom(sockfd, odpoved_od_servera, sizeof(odpoved_od_servera), 0, (struct sockaddr*)&adresa_servera, &velkost_adresy_servera);
                memcpy(odozva_od_servera, odpoved_od_servera, VELKOST_POTVRDZUJUCI_PAKET);

                if (strcmp(odozva_od_servera->typ_paketu, "potvrdenie_spojenia", sizeof("potvrdenie_spojenia")) == 0 && odozva_od_servera->potvrdenie_spojenia == 'p') {
                    printf("Odoslal som fragment s poradovym cislom %d a velkostou %d\n", datovy_paket->poradie, datovy_paket->velkost);
                    rv = SUCCESS;
                }

                else if (odozva_od_servera->potvrdenie_spojenia == 'n' && strcmp(odozva_od_servera->typ_paketu, "potvrdenie_spojenia", sizeof("potvrdenie_spojenia")) == 0) {
                    printf("Dva krat po sebe som odoslal chybny fragment, ukoncujem spojenie\n");
                    goto err;
                }
            }

            else if (odpoved_na_poslanie == 0) {
                odpoved->odpoved = 'n';
                memcpy(odpoved_na_znovuzaslanie, odpoved, VELKOST_ODPOVED_NA_ZNOVUVYZIADANIE);
                sendto(sockfd, odpoved_na_znovuzaslanie, VELKOST_ODPOVED_NA_ZNOVUVYZIADANIE, 0, &(adresa_servera), sizeof(adresa_servera));
                closesocket(sockfd);
                goto err;
            }
        }
    }

    else if (size == VELKOST_PRVY_PAKET)
    {
        prvy_paket = (PRVY_PAKET*)paket;

        memcpy(data, prvy_paket, VELKOST_PRVY_PAKET);
        sendto(sockfd, data, VELKOST_PRVY_PAKET, 0, &(adresa_servera), sizeof(adresa_servera));

        if (konfig.subor == 1) {
            printf("Prenasam subor %s\n", konfig.nazov_suboru);
            printf("Absolutna cesta %s\n", konfig.cesta_k_suboru);
        }


        pocet_bytov = recvfrom(sockfd, odpoved_od_servera, sizeof(odpoved_od_servera), 0, (struct sockaddr*)&adresa_servera, &velkost_adresy_servera);

        POTVRDZUJUCI_PAKET* odozva_od_servera = (POTVRDZUJUCI_PAKET*)malloc(1 * sizeof(POTVRDZUJUCI_PAKET));
        memcpy(odozva_od_servera, odpoved_od_servera, VELKOST_POTVRDZUJUCI_PAKET);

        if (strcmp(odozva_od_servera->typ_paketu, "potvrdenie_spojenia") == 0 && odozva_od_servera->potvrdenie_spojenia == 'p')
            rv = SUCCESS;

        if (pocet_bytov < 0) {
            printf("Server neodpoveda na spravu\n");
            rv = NO_RESPONSE;
        }
    }

    else if (size == VELKOST_KEEP_ALIVE_PAKET) {
        keep_alive_paket = (KEEP_ALIVE_PAKET*)paket;
        memcpy(data, keep_alive_paket, VELKOST_KEEP_ALIVE_PAKET);
        sendto(sockfd, data, VELKOST_PRVY_PAKET, 0, &(adresa_servera), sizeof(adresa_servera));
        if (strcmp(keep_alive_paket->typ_paketu, "posledny_paket") == 0) {
            closesocket(sockfd);
            return 0;
        }
            
        pocet_bytov = recvfrom(sockfd, odpoved_od_servera, sizeof(odpoved_od_servera), 0, (struct sockaddr*)&adresa_servera, &velkost_adresy_servera);

        if (pocet_bytov < 0) {
            printf("Keep alive bez odpovede\n");
            rv = NO_RESPONSE;
        }

        POTVRDZUJUCI_PAKET* odozva_od_servera = (POTVRDZUJUCI_PAKET*)malloc(1 * sizeof(POTVRDZUJUCI_PAKET));
        memcpy(odozva_od_servera, odpoved_od_servera, VELKOST_POTVRDZUJUCI_PAKET);

        if (strcmp(odozva_od_servera->typ_paketu, "potvrdenie_keep_alive") == 0 && odozva_od_servera->potvrdenie_spojenia == 'p')
            rv = SUCCESS;

        else {
            if (*pocet_keep_alive_bez_odpovede < 4) {
                (*pocet_keep_alive_bez_odpovede)++;
                rv = NO_RESPONSE;
            }


            else if (*pocet_keep_alive_bez_odpovede >= 4 && *pocet_keep_alive_bez_odpovede < 5) {
                (*pocet_sekund) = 20;
                (*pocet_keep_alive_bez_odpovede)++;
                rv = NO_RESPONSE;
            }


            else if ((*pocet_keep_alive_bez_odpovede > 4) && (*pocet_keep_alive_bez_odpovede < 9)) {
                (*pocet_keep_alive_bez_odpovede)++;
                rv = NO_RESPONSE;
            }


            else if ((*pocet_keep_alive_bez_odpovede) >= 9) {
                (*pocet_keep_alive_bez_odpovede)++;
                printf("Klient ukoncuje spojenie, server 9-krat neodpovedal na keep alive\n");
                closesocket(sockfd);
                rv = NO_RESPONSE;
            }
        }
    }

err:
    return rv;
}

int posli(KLIENT_KONFIG konfiguracia_klienta) {
    int rv = GENERAL_ERROR, i = 0, j = 0, velkost_dat_na_odoslanie = 0, n = 0;
    char znak_v_subore = 0, status = 0;
    FILE* subor_na_poslanie = NULL;
    DATOVY_PAKET paket_s_fragmentom = { 0 };
    PRVY_PAKET otvorenie_spojenia = { 0 };
    KEEP_ALIVE_PAKET keep_alive = { 0 };
    unsigned char vystup_hashu[20] = { 0 };
    unsigned int checksum = 0;
    int koniec_suboru = 0, pocet_sekund = 10, keep_alive_bez_odpovede = 0, chyba = 0, poslany_fragment_s_chybou = 0;
    char data[2000] = { 0 };

    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket < 0) {
        printf("Vytvorenie socketu sa nepodarilo\n");
        return 0;
    }

    char* fragment = (char*)malloc(konfiguracia_klienta.velkost_fragmentu * 1);
    if (NULL == fragment) {
        printf("Nepodarilo sa alokovat fragment\n");
        goto err;
    }

    DWORD timeout = 8 * 1000;   
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    memset(fragment, 0, konfiguracia_klienta.velkost_fragmentu);
    chyba = konfiguracia_klienta.chyba;

    if (konfiguracia_klienta.subor == 1) {
        subor_na_poslanie = fopen(konfiguracia_klienta.cesta_k_suboru, "rb");

        if (NULL == subor_na_poslanie) {
            printf("Neotvoreny subor na poslanie\n");
            goto err;
        }

        fseek(subor_na_poslanie, 0, 0);

        while (koniec_suboru != EOF) {
            koniec_suboru = fgetc(subor_na_poslanie);
            velkost_dat_na_odoslanie++;
        }

        fseek(subor_na_poslanie, 0, 0);

        konfiguracia_klienta.pocet_fragmentov = velkost_dat_na_odoslanie / konfiguracia_klienta.velkost_fragmentu;
        if (velkost_dat_na_odoslanie % konfiguracia_klienta.velkost_fragmentu != 0)
            konfiguracia_klienta.pocet_fragmentov++;

        printf("Pocet fragmentov na odoslanie: %d s velkostou: %d\n", konfiguracia_klienta.pocet_fragmentov, konfiguracia_klienta.velkost_fragmentu);
        memcpy(otvorenie_spojenia.nazov_suboru, konfiguracia_klienta.nazov_suboru, strlen(konfiguracia_klienta.nazov_suboru));
        memcpy(otvorenie_spojenia.miesto_ulozenia_suboru, konfiguracia_klienta.miesto_ulozenia_suboru, strlen(konfiguracia_klienta.miesto_ulozenia_suboru));
        otvorenie_spojenia.pocet_fragmentov = konfiguracia_klienta.pocet_fragmentov;
        memcpy(otvorenie_spojenia.typ_paketu, "prvy_paket", sizeof("prvy_paket"));

        status = posli_paket(sockfd, &otvorenie_spojenia, VELKOST_PRVY_PAKET, konfiguracia_klienta.pocet_fragmentov, 0, 0, konfiguracia_klienta);

        if (status == GENERAL_ERROR)
            goto err;

        for (j = 0; j < konfiguracia_klienta.pocet_fragmentov; j++) {
            memset(fragment, 0, konfiguracia_klienta.velkost_fragmentu);
            for (i = 0; i < konfiguracia_klienta.velkost_fragmentu; i++) {
                koniec_suboru = fgetc(subor_na_poslanie);
                if (koniec_suboru == EOF) {
                    fragment[i] = (char)EOF;
                    break;
                }
                fragment[i] = (char)koniec_suboru;
            }

            memcpy(paket_s_fragmentom.data, fragment, i);

            paket_s_fragmentom.poradie = j;
            paket_s_fragmentom.velkost = i;
            checksum = 0;

            SHA1(fragment, strlen(paket_s_fragmentom.data), &vystup_hashu);


            for (n = 0; n < 20; n++)
                checksum += (unsigned int)vystup_hashu[n];

            if (chyba == 0 || (chyba == 1 && j != 0))
                paket_s_fragmentom.kontrolny_sucet = checksum;

            //poslanie prveho fragmentu s chybou
            else if (1 == chyba && 0 == j && 0 == poslany_fragment_s_chybou) {
                checksum = checksum - 500;
                poslany_fragment_s_chybou = 1;
                paket_s_fragmentom.kontrolny_sucet = checksum;
            }


            status = posli_paket(sockfd, &paket_s_fragmentom, VELKOST_DATOVY_PAKET, konfiguracia_klienta.pocet_fragmentov, 0, 0, konfiguracia_klienta);
            if (status == GENERAL_ERROR)
                goto err;
            else if (status == NO_RESPONSE)
                goto err;

            for (n = 0; n < konfiguracia_klienta.velkost_fragmentu; n++)
                paket_s_fragmentom.data[n] = 0;
        }
    }

    else {
        velkost_dat_na_odoslanie = strlen(konfiguracia_klienta.textova_sprava);
        konfiguracia_klienta.pocet_fragmentov = velkost_dat_na_odoslanie / konfiguracia_klienta.velkost_fragmentu;
        if (velkost_dat_na_odoslanie % konfiguracia_klienta.velkost_fragmentu != 0)
            konfiguracia_klienta.pocet_fragmentov++;

        printf("Pocet fragmentov na odoslanie: %d s velkostou: %d\n", konfiguracia_klienta.pocet_fragmentov, konfiguracia_klienta.velkost_fragmentu);
        memcpy(otvorenie_spojenia.nazov_suboru, "TEXTOVA SPRAVA", strlen("TEXTOVA SPRAVA"));
        otvorenie_spojenia.pocet_fragmentov = konfiguracia_klienta.pocet_fragmentov;
        memcpy(otvorenie_spojenia.typ_paketu, "prvy_paket", sizeof("prvy_paket"));
        status = posli_paket(sockfd, &otvorenie_spojenia, VELKOST_PRVY_PAKET, konfiguracia_klienta.pocet_fragmentov, 0, 0, konfiguracia_klienta);

        if (status == GENERAL_ERROR)
            goto err;

        for (j = 0; j < konfiguracia_klienta.pocet_fragmentov; j++) {
            memset(fragment, 0, konfiguracia_klienta.velkost_fragmentu);
            for (i = 0; i < konfiguracia_klienta.velkost_fragmentu; i++) {
                znak_v_subore = konfiguracia_klienta.textova_sprava[j * konfiguracia_klienta.velkost_fragmentu + i];
                if (znak_v_subore == '\n') {
                    fragment[i] = '\n';
                    break;
                }
                fragment[i] = znak_v_subore;
            }

            memset(paket_s_fragmentom.data, 0, konfiguracia_klienta.velkost_fragmentu);
            memcpy(paket_s_fragmentom.data, fragment, i);
            paket_s_fragmentom.poradie = j;
            paket_s_fragmentom.velkost = i;

            checksum = 0;

            SHA1(fragment, strlen(paket_s_fragmentom.data), &vystup_hashu);

            for (n = 0; n < 20; n++) 
                checksum += (unsigned int)vystup_hashu[n];
            
            if (chyba == 0 || (chyba == 1 && j != 0))
                paket_s_fragmentom.kontrolny_sucet = checksum;

            //poslanie prveho fragmentu s chybou
            else if (1 == chyba && 0 == j && 0 == poslany_fragment_s_chybou) {
                checksum = checksum - 500;
                poslany_fragment_s_chybou = 1;
                paket_s_fragmentom.kontrolny_sucet = checksum;
            }

            status = posli_paket(sockfd, &paket_s_fragmentom, VELKOST_DATOVY_PAKET, konfiguracia_klienta.pocet_fragmentov, 0, 0, konfiguracia_klienta);
            if (GENERAL_ERROR == status) {
                printf("Ukoncujem spojenie\n");
                goto err;
            }

            else if (NO_RESPONSE == status) {
                printf("Server neodpoveda na odoslany fragment, ukoncujem spojenie\n");
                goto err;
            }   
        }
    }

    //keep alive

    memcpy(keep_alive.typ_paketu, "keep_alive_paket", sizeof("keep_alive_paket"));
    keep_alive.casovy_usek = 10;

    int pocet_keep_alive = 0;
    int pokracuj = 1;

    while ((keep_alive_bez_odpovede) < 10 && pokracuj == 1) {
        pocet_keep_alive++;
        if (pocet_keep_alive > 6) {
            printf("Pokracovat v posielani keep alive sprav? Ano (1) alebo nie (0)\n");
            scanf("%d", &pokracuj);
            if (pokracuj == 1) {
                printf("Pokracujem v posielani keep alive sprav\n");
                pocet_keep_alive = 0;
            }

            else if (pokracuj == 0) {
                //ukoncovaci keep alive
                memcpy(keep_alive.typ_paketu, "posledny_paket", sizeof("posledny_paket"));
                status = posli_paket(sockfd, &keep_alive, VELKOST_KEEP_ALIVE_PAKET, konfiguracia_klienta.pocet_fragmentov, &keep_alive_bez_odpovede, &pocet_sekund, konfiguracia_klienta);
                printf("Ukoncujem posielanie keep alive sprav\n");
                closesocket(sockfd);
                return 0;
            }
        }

        printf("Posielam keep alive spravu\n");
        status = posli_paket(sockfd, &keep_alive, VELKOST_KEEP_ALIVE_PAKET, konfiguracia_klienta.pocet_fragmentov, &keep_alive_bez_odpovede, &pocet_sekund, konfiguracia_klienta);
        keep_alive.casovy_usek = pocet_sekund;
        if (keep_alive_bez_odpovede < 10)
            Sleep(pocet_sekund * 1000);
    }

    rv = SUCCESS;

err:
    return rv;
}

int konfiguracia_odosielania(KLIENT_KONFIG* konfiguracia_klienta) {
    int velkost_fr = 0, pocet_fr = 0, velkost_suboru = 0, rv = GENERAL_ERROR, ip1 = 0, ip2 = 0, ip3 = 0, ip4 = 0, port_servera = 0, port_klienta = 0;
    char chyba[10], cesta_k_suboru[200], cesta_k_ulozenemu_suboru[200], subor[10], nazov_suboru[100];
    FILE* konfiguracny_subor = NULL, * subor_na_poslanie = NULL;


    memset(cesta_k_suboru, 0, 200);
    memset(cesta_k_ulozenemu_suboru, 0, 200);
    memset(nazov_suboru, 0, 100);
    memset(konfiguracia_klienta->textova_sprava, 0, 1000);

    konfiguracny_subor = fopen("klient.conf", "r");

    if (NULL == konfiguracny_subor) {
        printf("Neotvoreny konfiguracny subor\n");
        goto err;
    }

    if (0 == fscanf(konfiguracny_subor, "Subor:%s\n", subor))
        goto err;

    fgets(konfiguracia_klienta->textova_sprava, 17, konfiguracny_subor);

    if (strcmp(subor, "False") == 0) {
        if (fgets(konfiguracia_klienta->textova_sprava, 1000, konfiguracny_subor) == NULL)
            goto err;
    }

    if (strcmp(subor, "True") == 0) {
        if (0 == fscanf(konfiguracny_subor, "Nazov suboru:%s\n", nazov_suboru))
            goto err;

        if (0 == fscanf(konfiguracny_subor, "Cesta k odosielanemu suboru:%s\n", cesta_k_suboru))
            goto err;

        if (0 == fscanf(konfiguracny_subor, "Cesta k ulozenemu suboru:%s\n", cesta_k_ulozenemu_suboru))
            goto err;
    }

    else {
        if (0 != fscanf(konfiguracny_subor, "%*s %*s\n%*s %*s %*s %*s\n%*s %*s %*s %*s\n"))
            goto err;
    }

    if (0 == fscanf(konfiguracny_subor, "Velkost fragmentu:%d\n", &velkost_fr))
        goto err;

    if (0 == fscanf(konfiguracny_subor, "Chyba:%s\n", chyba))
        goto err;

    if (0 == fscanf(konfiguracny_subor, "IP odosielatela:%d.%d.%d.%d\n", &ip1, &ip2, &ip3, &ip4))
        goto err;

    if (0 == fscanf(konfiguracny_subor, "Port servera:%d\n", &port_servera))
        goto err;

    if (0 == fscanf(konfiguracny_subor, "Port klienta:%d", &port_klienta))
        goto err;

    if (!(ip1 >= 0 && ip1 <= 255) || !(ip2 >= 0 && ip2 <= 255) || !(ip3 >= 0 && ip3 <= 255) || !(ip4 >= 0 && ip4 <= 255)) {
        printf("Chyba v IP\n");
        goto err;
    }

    (*konfiguracia_klienta).ip_klienta[0] = (unsigned char)ip1;
    (*konfiguracia_klienta).ip_klienta[1] = (unsigned char)ip2;
    (*konfiguracia_klienta).ip_klienta[2] = (unsigned char)ip3;
    (*konfiguracia_klienta).ip_klienta[3] = (unsigned char)ip4;

    if (strcmp(subor, "True") == 0) {
        subor_na_poslanie = fopen(cesta_k_suboru, "rb");

        if (NULL == subor_na_poslanie) {
            printf("Zle zadana cesta k suboru alebo subor neexistuje\n");
            goto err;
        }
    }
      

    if (!(velkost_fr > 0 && velkost_fr < 1457)) {
        printf("Zle zadana velkost fragmentu\n");
        goto err;
    }

    if (strcmp(subor, "True") == 0)
        (*konfiguracia_klienta).subor = 1;

    else if (strcmp(subor, "False") == 0)
        (*konfiguracia_klienta).subor = 0;

    else {
        printf("Zle zadany subor v konfiguracnom subore\n");
        goto err;
    }

    if (strcmp(chyba, "True") == 0)
        (*konfiguracia_klienta).chyba = 1;

    else if (strcmp(chyba, "False") == 0)
        (*konfiguracia_klienta).chyba = 0;

    else {
        printf("Zle zadana chyba v subore\n");
        goto err;
    }

    (*konfiguracia_klienta).port_servera = port_servera;
    (*konfiguracia_klienta).port_klienta = port_klienta;

    memcpy((*konfiguracia_klienta).cesta_k_suboru, cesta_k_suboru, sizeof(cesta_k_suboru));
    memcpy((*konfiguracia_klienta).nazov_suboru, nazov_suboru, sizeof(nazov_suboru));
    memcpy((*konfiguracia_klienta).miesto_ulozenia_suboru, cesta_k_ulozenemu_suboru, sizeof(cesta_k_ulozenemu_suboru));

    (*konfiguracia_klienta).velkost_fragmentu = velkost_fr;


    rv = SUCCESS;

err:
    if (0 != konfiguracny_subor)
        fclose(konfiguracny_subor);
    if (0 != subor_na_poslanie)
        fclose(subor_na_poslanie);
    return rv;
}


int klient() {
    int velkost_fragmentu = 0, pocet_fragmentov = 0, velkost_suboru = 0, chyba = 0, spojenie_pokracuje = 1;
    KLIENT_KONFIG konfiguracia_klienta = { 0 };

    konfiguracia_odosielania(&konfiguracia_klienta);
    posli(konfiguracia_klienta);

    return 0;
}

int server() {
    int sockfd, pocet_bytov, navratova_hodnota_odoslania = 0, pocet_prichadzajucich_paketov = 0, i = 0, n = 0, poradie_paketu = 0, o = 0;
    struct sockaddr_in adresa_servera, adresa_klienta;
    char prijata_sprava[1500], text_prijatej_spravy[1460];
    unsigned char vystup_hashu[20] = { 0 };
    unsigned int checksum = 0, prijaty_checksum = 0;
    POTVRDZUJUCI_PAKET* paket_s_odpovedou = NULL;
    DATOVY_PAKET* datovy_paket = (DATOVY_PAKET*)malloc(1 * sizeof(DATOVY_PAKET));
    KLIENT_KONFIG konfiguracia = { 0 };
    FILE* miesto_ulozenia_suboru = NULL;

    konfiguracia_odosielania(&konfiguracia);

    paket_s_odpovedou = (POTVRDZUJUCI_PAKET*)malloc(1 * sizeof(POTVRDZUJUCI_PAKET));
    char odoslana_sprava[VELKOST_POTVRDZUJUCI_PAKET];
    int velkost_klienta = sizeof(adresa_klienta);

    memset(odoslana_sprava, 0, sizeof(odoslana_sprava));

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&adresa_servera, 0, sizeof(adresa_servera));

    adresa_servera.sin_family = AF_INET;
    adresa_servera.sin_port = htons(konfiguracia.port_servera);
    adresa_servera.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr*)&adresa_servera, sizeof(adresa_servera)) != 0)
        printf("Chyba v bind\n");

    DWORD timeout = 8 * 1000;  
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    pocet_bytov = recvfrom(sockfd, prijata_sprava, sizeof(prijata_sprava), 0, (struct sockaddr*)&adresa_klienta, &velkost_klienta);
    PRVY_PAKET* otvorenie_spojenia = (PRVY_PAKET*)malloc(1 * sizeof(PRVY_PAKET));
    memcpy(otvorenie_spojenia, prijata_sprava, VELKOST_PRVY_PAKET);

    if (strcmp(otvorenie_spojenia->nazov_suboru, "TEXTOVA SPRAVA") != 0) {
        miesto_ulozenia_suboru = fopen(otvorenie_spojenia->miesto_ulozenia_suboru, "wb");
        if (miesto_ulozenia_suboru == NULL) {
            printf("Nepodarilo sa otvorit subor\n");
            return 0;
        }
    }


    int vysledok = memcmp(otvorenie_spojenia->typ_paketu, "prvy_paket", sizeof("prvy_paket"));

    if (vysledok == 0) {
        memcpy((*paket_s_odpovedou).typ_paketu, "potvrdenie_spojenia", sizeof("potvrdenie_spojenia"));
        (*paket_s_odpovedou).potvrdenie_spojenia = 'p';
        memcpy(odoslana_sprava, paket_s_odpovedou, VELKOST_POTVRDZUJUCI_PAKET);
        navratova_hodnota_odoslania = sendto(sockfd, odoslana_sprava, VELKOST_POTVRDZUJUCI_PAKET, 0, (struct sockaddr*)&(adresa_klienta), sizeof(adresa_klienta)); //pointer na sockaddr strukturu

        if (navratova_hodnota_odoslania < 0) {
            printf("Server neodpovedal na otvorenie spojenia kvoli %d\n", WSAGetLastError());
            return 1;
        }

        pocet_prichadzajucich_paketov = otvorenie_spojenia->pocet_fragmentov;

        while (i < pocet_prichadzajucich_paketov) {
            pocet_bytov = recvfrom(sockfd, prijata_sprava, sizeof(prijata_sprava), 0, (struct sockaddr*)&adresa_klienta, &velkost_klienta);
            if (pocet_bytov < 0) {
                printf("Klient prestal posielat fragmenty, ukoncujem spojenie\n");
                closesocket(sockfd);
                return 1;
            }
            checksum = 0;

            memcpy(datovy_paket, prijata_sprava, VELKOST_DATOVY_PAKET);
            poradie_paketu = datovy_paket->poradie;

            if (poradie_paketu == i) {
                prijaty_checksum = datovy_paket->kontrolny_sucet;
                SHA1(datovy_paket->data, strlen(datovy_paket->data), &vystup_hashu);

                for (n = 0; n < 20; n++)
                    checksum += (unsigned int)vystup_hashu[n];

                POTVRDZUJUCI_PAKET* potvrdenie_uspesneho_prijatia = (POTVRDZUJUCI_PAKET*)malloc(1 * sizeof(POTVRDZUJUCI_PAKET));

                if (checksum == prijaty_checksum) {
                    memcpy((*potvrdenie_uspesneho_prijatia).typ_paketu, "potvrdenie_spojenia", sizeof("potvrdenie_spojenia"));
                    potvrdenie_uspesneho_prijatia->potvrdenie_spojenia = 'p';
                    printf("Prijal som fragment s poradovym cislom %d bez chyb s velkostou %d\n", poradie_paketu, datovy_paket->velkost);
                    memcpy(odoslana_sprava, potvrdenie_uspesneho_prijatia, VELKOST_POTVRDZUJUCI_PAKET);
                    navratova_hodnota_odoslania = sendto(sockfd, odoslana_sprava, VELKOST_POTVRDZUJUCI_PAKET, 0, (struct sockaddr*)&(adresa_klienta), sizeof(adresa_klienta)); //pointer na sockaddr strukturu
                    
                    //zapisujem do suboru
                    if(konfiguracia.subor == 1)
                        fwrite(datovy_paket->data, sizeof(char), datovy_paket->velkost, miesto_ulozenia_suboru);

                    //zapisujem textovu spravu do pola
                    else {
                        for(o = 0; o < konfiguracia.velkost_fragmentu; o++) 
                            text_prijatej_spravy[i * konfiguracia.velkost_fragmentu + o] = datovy_paket->data[o];
                    }
                    i++;    
                }

                else {
                    memcpy((*potvrdenie_uspesneho_prijatia).typ_paketu, "potvrdenie_spojenia", sizeof("potvrdenie_spojenia"));
                    potvrdenie_uspesneho_prijatia->potvrdenie_spojenia = 'n';
                    memcpy(odoslana_sprava, potvrdenie_uspesneho_prijatia, VELKOST_POTVRDZUJUCI_PAKET);
                    printf("Prijal som fragment s poradovym cislom %d s chybnym checksumom\n", datovy_paket->poradie);
                    navratova_hodnota_odoslania = sendto(sockfd, odoslana_sprava, VELKOST_POTVRDZUJUCI_PAKET, 0, (struct sockaddr*)&(adresa_klienta), sizeof(adresa_klienta));
                    //znovuvyziadanie

                    pocet_bytov = recvfrom(sockfd, prijata_sprava, sizeof(prijata_sprava), 0, (struct sockaddr*)&adresa_klienta, &velkost_klienta);
                    ODPOVED_NA_ZNOVUVYZIADANIE* odpoved_od_klienta = (ODPOVED_NA_ZNOVUVYZIADANIE*)malloc(1 * sizeof(ODPOVED_NA_ZNOVUVYZIADANIE));
                    memcpy(odpoved_od_klienta, prijata_sprava, VELKOST_ODPOVED_NA_ZNOVUVYZIADANIE);

                    //klient nechce znovu odoslat chybny fragment, koncim
                    if (odpoved_od_klienta->odpoved == 'n') {
                        //ukonci spojenie
                        printf("Klient nechce druhy krat poslat chybny fragment, ukoncujem spojenie\n");
                        closesocket(sockfd);
                        return 0;
                    }

                    //ak nam klient znovu posle fragment, ktory mal chybu
                    else {
                        pocet_bytov = recvfrom(sockfd, prijata_sprava, sizeof(prijata_sprava), 0, (struct sockaddr*)&adresa_klienta, &velkost_klienta);
                        if (poradie_paketu == i) {
                            checksum = 0;
                            memcpy(datovy_paket, prijata_sprava, VELKOST_DATOVY_PAKET);
                            prijaty_checksum = datovy_paket->kontrolny_sucet;
                            SHA1(datovy_paket->data, strlen(datovy_paket->data), &vystup_hashu);
                            for (n = 0; n < 20; n++)
                                checksum += (unsigned int)vystup_hashu[n];

                            //tento krat je fragment v poriadku
                            if (checksum == prijaty_checksum) {
                                printf("Prijal som fragment s poradovym cislom %d bez chyb\n", poradie_paketu);
                                memcpy((*potvrdenie_uspesneho_prijatia).typ_paketu, "potvrdenie_spojenia", sizeof("potvrdenie_spojenia"));
                                potvrdenie_uspesneho_prijatia->potvrdenie_spojenia = 'p';
                                memcpy(odoslana_sprava, potvrdenie_uspesneho_prijatia, VELKOST_POTVRDZUJUCI_PAKET);
                                navratova_hodnota_odoslania = sendto(sockfd, odoslana_sprava, VELKOST_POTVRDZUJUCI_PAKET, 0, (struct sockaddr*)&(adresa_klienta), sizeof(adresa_klienta)); //pointer na sockaddr strukturu
                               
                                if(konfiguracia.subor == 1)
                                     fwrite(datovy_paket->data, sizeof(char), datovy_paket->velkost, miesto_ulozenia_suboru);

                                else {
                                    for (o = 0; o < konfiguracia.velkost_fragmentu; o++)
                                        text_prijatej_spravy[i * konfiguracia.velkost_fragmentu + o] = datovy_paket->data[o];
                                }
                                i++;
                            }

                            //dva krat zly fragment
                            else {
                                printf("Prijal som fragment s poradovym cislom %d s chybnym checksumom\n", datovy_paket->poradie);
                                printf("Dva krat po sebe mi prisiel chybny fragment, ukoncujem spojenie\n");
                                memcpy((*potvrdenie_uspesneho_prijatia).typ_paketu, "potvrdenie_spojenia", sizeof("potvrdenie_spojenia"));
                                potvrdenie_uspesneho_prijatia->potvrdenie_spojenia = 'u';
                                memcpy(odoslana_sprava, potvrdenie_uspesneho_prijatia, VELKOST_POTVRDZUJUCI_PAKET);
                                navratova_hodnota_odoslania = sendto(sockfd, odoslana_sprava, VELKOST_POTVRDZUJUCI_PAKET, 0, (struct sockaddr*)&(adresa_klienta), sizeof(adresa_klienta)); //pointer na sockaddr strukturu
                                closesocket(sockfd);
                                return 0;
                            }
                        }
                        //prisiel mi fragment so zlym poradovym cislom
                        else {
                            printf("Prisiel fragment so zlym poradovym cislom\n");
                        }
                    }

                }
            }
            //ak i sa nerovna poradie paketu
            else {
                printf("Prisiel fragment so zlym poradovym cislom\n");

            }

        }

            //vypisanie nazvu suboru a absolutna cesta
        if (konfiguracia.subor == 1) {
            printf("Prijal sa subor s nazvom %s\n", otvorenie_spojenia->nazov_suboru);
            printf("Absolutna adresa: %s\n\n", otvorenie_spojenia->miesto_ulozenia_suboru);
        }

        else
            printf("Prijata sprava: %s\n", text_prijatej_spravy);
    }

    //keep alive
    KEEP_ALIVE_PAKET* prijaty_keep_alive = (KEEP_ALIVE_PAKET*)malloc(1 * VELKOST_KEEP_ALIVE_PAKET);
    int a = 1, pocet_neprijatych_keep_alive = 0, pocet_sekund = 4;

    while (a == 1) {
        pocet_bytov = recvfrom(sockfd, prijata_sprava, sizeof(prijata_sprava), 0, (struct sockaddr*)&adresa_klienta, &velkost_klienta);

        if (pocet_bytov < 0) 
            pocet_neprijatych_keep_alive++;
        

        if (pocet_neprijatych_keep_alive > 5) {
            a = 0;
            printf("Koncim, klient prestal posielat keep alive\n");
            closesocket(sockfd);
            continue;
        }

        if (pocet_bytov > 0) {
            memcpy(prijaty_keep_alive, prijata_sprava, VELKOST_KEEP_ALIVE_PAKET);
            pocet_neprijatych_keep_alive = 0;
            if (strcmp(prijaty_keep_alive->typ_paketu, "keep_alive_paket") == 0) {
                POTVRDZUJUCI_PAKET* potvrdenie_prijatia_keep_alive = (POTVRDZUJUCI_PAKET*)malloc(1 * VELKOST_POTVRDZUJUCI_PAKET);
                potvrdenie_prijatia_keep_alive->potvrdenie_spojenia = 'p';
                printf("Prijaty keep alive\n");
                memcpy(potvrdenie_prijatia_keep_alive->typ_paketu, "potvrdenie_keep_alive", sizeof("potvrdenie_keep_alive"));
                memcpy(odoslana_sprava, potvrdenie_prijatia_keep_alive, VELKOST_POTVRDZUJUCI_PAKET);
                navratova_hodnota_odoslania = sendto(sockfd, odoslana_sprava, VELKOST_POTVRDZUJUCI_PAKET, 0, (struct sockaddr*)&(adresa_klienta), sizeof(adresa_klienta));
            }
            else if (strcmp(prijaty_keep_alive->typ_paketu, "posledny_paket") == 0) {
                closesocket(sockfd);
                a = 0;
            }
        }   
    }

    if(miesto_ulozenia_suboru != NULL)
        fclose(miesto_ulozenia_suboru);
    return 1;
}

int main() {
    char strana, cesta_k_suboru[500] = { 0 }, IP_klienta[4] = { 0 };
    int velkost_fragmentu = 0, port_servera = 0, port_klienta = 0;

    inicializuj();


    while (1) {
        printf("Som server 's' alebo klient 'k' alebo skoncim 'e' ?\n");
        scanf(" %c", &strana);

        switch (strana) {
        case 'k':
            klient();
            break;
        case 's':
            server();
            break;
        case 'e':
            goto err;
        default:
            continue;
        }

    }

err:
    return 0;
}