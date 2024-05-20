#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include "mydhcp.h"
#include "list.h"

#define TIMER_WAIT_SEC 1
#define TIMER_WAIT_USEC 0
#define TIMER_WAIT_REQ 10

enum State
{
    STATE_INIT,
    STATE_WAIT_REQ,
    STATE_IN_USE,
    STATE_TERMINATE,
};

struct client
{
    struct client *fp;
    struct client *bp;
    short status;
    short before_status;

    // below: network byte order
    struct in_addr id; // ip addr = client id
    in_port_t port;
    struct in_addr ip_addr; // allocate IP addr
    struct in_addr netmask;
    uint16_t ttl;
    int ttlcounter;
    int timeout_counter; // decide terminate or resend

    // for resend
    struct sockaddr_in client_addr;
    int code;
};

unsigned int deadline;
struct client chead;
struct freelist fhead;
struct freelist f[100];
struct client c[100];
int alrmflag;
struct itimerval ovalue;
int sock;

void release_client(struct client *c)
{
    char client_id_buf[NSIZE] = {0};
    char client_addr_buf[NSIZE] = {0};
    strncpy(client_id_buf, inet_ntoa(c->id), sizeof(client_id_buf));
    strncpy(client_addr_buf, inet_ntoa(c->ip_addr), sizeof(client_addr_buf));
    printf("release the client: id(IP addr): %s, allocated IP addr: %s\n", client_id_buf, client_addr_buf);
    printf("\n");

    struct freelist *p;
    int pair_num = 0;

    for (p = fhead.free_fp; p != &fhead; p = p->free_fp)
    {
        if (p != NULL)
        {
            pair_num++;
        }
    }

    f[pair_num].netmask.s_addr = c->netmask.s_addr;
    f[pair_num].ip.s_addr = c->ip_addr.s_addr;

    insert_free_tail(&f[pair_num]);

    c->bp->fp = c->fp;
    c->fp->bp = c->bp;
    c->bp = c->fp = NULL;

    // free c
    c->status = STATE_INIT;
    c->id.s_addr = 0;
    c->ip_addr.s_addr = 0;
    c->netmask.s_addr = 0;
    c->port = 0;
    c->ttlcounter = 0;
    c->ttl = 0;

    /* debug: checked whther the client is released by looking at the freelist */
    // int i = 0;
    // for (p = fhead.free_fp, i = 0; p != &fhead && i < pair_num; p = p->free_fp, i++)
    // {
    // if (p != NULL)
    // {
    // char ip_buf[NSIZE] = {0};
    // char netmask_buf[NSIZE] = {0};
    // strncpy(ip_buf, inet_ntoa(p->ip), sizeof(ip_buf));
    // strncpy(netmask_buf, inet_ntoa(p->netmask), sizeof(netmask_buf));
    //
    // printf("IP addr: %s, netmask: %s\n", ip_buf, netmask_buf);
    // }
    // }
}

void read_config(char *file)
{
    int i = 0;
    int j = 0;
    int first_line = 0;
    int pair_num = 0;
    FILE *fp;
    char line[NSIZE] = {0};
    char deadline_array[NSIZE] = {0};
    char ip_addr[NSIZE] = {0};
    char net_mask[NSIZE] = {0};

    fhead.free_bp = fhead.free_fp = &fhead;
    chead.bp = chead.fp = &chead;

    if ((fp = fopen(file, "r")) == NULL)
    {
        fprintf(stderr, "cannot open the file\n");
        exit(1);
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        i = 0;
        j = 0;

        if (first_line == 0)
        {
            while (!(isspace(line[i])) && line[i] != '\n')
            {
                deadline_array[j] = line[i];
                i++;
                j++;
            }
            i++;
            deadline_array[j] = '\0';
            int deadline_int = 0;

            if ((deadline_int = atoi(deadline_array)) <= 0)
            {
                fprintf(stderr, "the time must be a positive number\n");
                exit(1);
            }
            deadline = (unsigned int)deadline_int;
            memset(deadline_array, 0, sizeof(deadline_array));

            printf("deadline:%d\n", deadline);
            first_line++;
            i = 0;
            continue;
        }

        j = 0;
        while (!(isspace(line[i])) && line[i] != '\n' && line[i] != '\0')
        {
            ip_addr[j] = line[i];
            i++;
            j++;
        }
        ip_addr[j] = '\0';

        j = 0;
        i++;
        while (!(isspace(line[i])) && line[i] != '\n' && line[i] != '\0')
        {
            net_mask[j] = line[i];
            i++;
            j++;
        }
        net_mask[j] = '\0';
        j = 0;
        i = 0;

        printf("IP addr:%s, netmask:%s\n", ip_addr, net_mask);
        inet_pton(AF_INET, ip_addr, &f[pair_num].ip.s_addr);
        inet_pton(AF_INET, net_mask, &f[pair_num].netmask.s_addr);

        insert_free_tail(&f[pair_num]);
        memset(ip_addr, 0, sizeof(ip_addr));
        memset(net_mask, 0, sizeof(net_mask));
        pair_num++;
    }

    // clsoe config_file
    if (fclose(fp) == EOF)
    {
        fprintf(stderr, "Error: cannot close file %s\n", file);
        exit(1);
    }
    printf("\n");
}

void insert_tail(struct client *c)
{
    c->bp = chead.bp;
    c->fp = &chead;
    chead.bp->fp = c;
    chead.bp = c;
}

struct client *create_client(struct sockaddr_in client_addr)
{
    // alloc IP
    struct freelist *p;
    if ((p = free_search()) != NULL)
    {
        remove_from_free(p);

        int client_num = 0;
        struct client *cp;
        for (cp = chead.fp; cp != &chead; cp = cp->fp)
        {
            if (cp != NULL)
            {
                client_num++;
            }
        }

        c[client_num].netmask.s_addr = p->netmask.s_addr;
        c[client_num].ip_addr.s_addr = p->ip.s_addr;
        c[client_num].ttl = htons(deadline);
        c[client_num].ttlcounter = TIMER_WAIT_REQ;
        c[client_num].id.s_addr = client_addr.sin_addr.s_addr;
        c[client_num].port = client_addr.sin_port;

        // insert client list
        insert_tail(&c[client_num]);

        // for debug: ckeck client_list
        //  client_num++;
        //  printf("client_ num:%d\n", client_num);
        //  int i = 0;
        //  for (cp = chead.fp, i = 0; cp != &chead && i < client_num; cp = cp->fp, i++)
        //{
        //      if (cp != NULL)
        //      {
        //          char ip_buf[NSIZE] = {0};
        //          char netmask_buf[NSIZE] = {0};
        //          strncpy(ip_buf, inet_ntoa(cp->ip_addr), sizeof(ip_buf));
        //          strncpy(netmask_buf, inet_ntoa(cp->netmask), sizeof(netmask_buf));
        //
        //         printf("IP addr: %s, netmask: %s\n", ip_buf, netmask_buf);
        //     }
        // }
        //  printf("\n");

        return &c[client_num];
    }
    else
        return NULL;
}

void send_offer(int sock, struct sockaddr_in client_addr, int allocatable, struct client *p)
{
    c->client_addr = client_addr;
    c->ttlcounter = 10;

    if (allocatable == 0)
    {
        struct mydhcp_msg msg = {
            .type = MYDHCP_MSGTYPE_OFFER,
            .code = MYDHCP_MSGCODE_OFFER_OK,
            .ttl = htons(deadline),
            .ip_addr = {.s_addr = htonl(p->ip_addr.s_addr)},
            .netmask = {.s_addr = htonl(p->netmask.s_addr)},
        };

        if (sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
        {
            perror("send_offer: sendto");
            exit(1);
        }

        char client_id_buf[NSIZE] = {0};
        char client_addr_buf[NSIZE] = {0};
        strncpy(client_id_buf, inet_ntoa(p->id), sizeof(client_id_buf));
        strncpy(client_addr_buf, inet_ntoa(p->ip_addr), sizeof(client_addr_buf));
        printf("Send a message: DHCPOFFER\n");
        printf("(to the client: id(IP addr): %s, allocated IP addr: %s)\n", client_id_buf, client_addr_buf);
        printf("\n");
    }
    else
    {
        struct mydhcp_msg msg = {
            .type = MYDHCP_MSGTYPE_OFFER,
            .code = MYDHCP_MSGCODE_OFFER_NG,
            .ttl = 0,
            .ip_addr = {0},
            .netmask = {0},
        };

        if (sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
        {
            perror("send_offer: sendto");
            exit(1);
        }

        char client_id_buf[NSIZE] = {0};
        strncpy(client_id_buf, inet_ntoa(client_addr.sin_addr), sizeof(client_id_buf));

        printf("Send a message: DHCPOFFER\n");
        printf("(to the client: id(IP addr): %s)\n", client_id_buf);
        printf("\n");
    }

    return;
}

void send_ack(int sock, struct sockaddr_in client_addr, int code, struct client *p)
{
    c->client_addr = client_addr;
    c->code = code;
    c->ttlcounter = deadline;

    if (code == 0)
    {
        struct mydhcp_msg msg = {
            .type = MYDHCP_MSGTYPE_ACK,
            .code = MYDHCP_MSGCODE_ACK_OK,
            .ttl = htons(deadline),
            .ip_addr = {.s_addr = htonl(p->ip_addr.s_addr)},
            .netmask = {.s_addr = htonl(p->netmask.s_addr)},
        };

        if (sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
        {
            perror("send_ack: sendto");
            exit(1);
        }

        char client_id_buf[NSIZE] = {0};
        char client_addr_buf[NSIZE] = {0};
        strncpy(client_id_buf, inet_ntoa(p->id), sizeof(client_id_buf));
        strncpy(client_addr_buf, inet_ntoa(p->ip_addr), sizeof(client_addr_buf));

        printf("Send a message: DHCPACK\n");
        printf("(to the client: id(IP addr): %s, allocated IP addr: %s)\n", client_id_buf, client_addr_buf);
        printf("\n");
    }
    else
    {
        struct mydhcp_msg msg = {
            .type = MYDHCP_MSGTYPE_ACK,
            .code = MYDHCP_MSGCODE_ACK_NG,
            .ttl = 0,
            .ip_addr = {0},
            .netmask = {0},
        };

        if (sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
        {
            perror("send_ack: sendto");
            exit(1);
        }

        char client_id_buf[NSIZE] = {0};
        char client_addr_buf[NSIZE] = {0};
        strncpy(client_id_buf, inet_ntoa(p->id), sizeof(client_id_buf));
        strncpy(client_addr_buf, inet_ntoa(p->ip_addr), sizeof(client_addr_buf));

        printf("Send a message: DHCPACK\n");
        printf("(to the client: id(IP addr): %s, allocated IP addr: %s)\n", client_id_buf, client_addr_buf);
        printf("\n");
    }
    return;
}

struct client *client_search(struct sockaddr_in client_addr)
{
    struct client *c;
    char addr_buf[NSIZE] = {0};
    char list_buf[NSIZE] = {0};
    u_int16_t addr_port = 0;
    uint16_t list_port = 0;
    int client_num = 0;

    for (c = chead.fp; c != &chead; c = c->fp)
    {
        if (c != NULL)
        {
            client_num++;
        }
    }

    int i = 0;
    for (c = chead.fp, i = 0; c != &chead && i < client_num; c = c->fp, i++)
    {
        if (c != NULL)
        {
            strncpy(addr_buf, inet_ntoa(client_addr.sin_addr), sizeof(addr_buf));
            strncpy(list_buf, inet_ntoa(c->id), sizeof(list_buf));
            addr_port = ntohs(client_addr.sin_port);
            list_port = ntohs(c->port);

            // if IP addr and port are the same, ok
            if (!strcmp(addr_buf, list_buf))
            {
                if (addr_port == list_port)
                {
                    return c;
                }
            }
        }
    }
    return NULL;
}

/* to decide ack_code */
int check_msg_client_addr(struct mydhcp_msg msg, struct client *c)
{
    char msg_addr_buf[NSIZE] = {0};
    char client_addr_buf[NSIZE] = {0};
    char msg_netmask_buf[NSIZE] = {0};
    char client_netmask_buf[NSIZE] = {0};

    struct in_addr ip_addr = {.s_addr = ntohl(msg.ip_addr.s_addr)};
    struct in_addr netmask = {.s_addr = ntohl(msg.netmask.s_addr)};

    strncpy(msg_addr_buf, inet_ntoa(ip_addr), sizeof(msg_addr_buf));
    strncpy(client_addr_buf, inet_ntoa(c->ip_addr), sizeof(client_addr_buf));
    strncpy(msg_netmask_buf, inet_ntoa(netmask), sizeof(msg_netmask_buf));
    strncpy(client_netmask_buf, inet_ntoa(c->netmask), sizeof(client_netmask_buf));

    // if IP addr and port are the same, ok
    if ((!strcmp(msg_addr_buf, client_addr_buf)) && (!strcmp(msg_netmask_buf, client_netmask_buf)) && (deadline >= ntohs(msg.ttl)))
        return MYDHCP_MSGCODE_ACK_OK;

    return MYDHCP_MSGCODE_ACK_NG;
}

void print_before_state(enum State before_state)
{
    switch (before_state)
    {
    case STATE_INIT:
        printf("(State changed from STATE_INIT)\n");
        break;
    case STATE_WAIT_REQ:
        printf("(State changed from STATE_WAIT_REQ)\n");
        break;
    case STATE_IN_USE:
        printf("(State changed from STATE_IN_USE)\n");
        break;
    default:
        break;
    }
}

void resend_msg_to_client(struct client *c)
{
    printf("resend a message\n");

    switch (c->status)
    {
    case STATE_WAIT_REQ:
        send_offer(sock, c->client_addr, 0, c);
        break;
    case STATE_IN_USE:
        send_ack(sock, c->client_addr, c->code, c);
        break;
    default:
        break;
    }
}

struct client *dc_ttlcounter()
{
    struct client *c;
    int client_num = 0;

    for (c = chead.fp; c != &chead; c = c->fp)
    {
        if (c != NULL)
        {
            client_num++;
        }
    }

    int i = 0;
    for (c = chead.fp, i = 0; c != &chead && i < client_num; c = c->fp, i++)
    {
        if (c != NULL)
        {
            if (c->status == STATE_IN_USE || c->status == STATE_WAIT_REQ)
            {
                c->ttlcounter--;
                if (c->ttlcounter == 0)
                {
                    c->timeout_counter++;
                    if (c->timeout_counter > 1)
                    {
                        printf("timeouut repeatedly occured.\n");
                        return c;
                    }
                    else if (c->timeout_counter == 1)
                    {
                        printf("timeouut occured. This is the first time.\n");
                        resend_msg_to_client(c);
                        if (c->timeout_counter > 1)
                        {
                            printf("timeouut repeatedly occured.\n");
                            return c;
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

void sigalrm_handler(int signal)
{
    alrmflag++;
    struct client *timeout_client = dc_ttlcounter();

    if (timeout_client != NULL)
    {
        printf("signal handler: timeout\n");

        release_client(c);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: ./mydhcpc <config_file>\n");
        exit(1);
    }
    signal(SIGALRM, sigalrm_handler);

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(51230),
        .sin_addr = {.s_addr = htonl(INADDR_ANY)},
    };

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        exit(1);
    }

    read_config(argv[1]);

    // for setitimer
    struct itimerval value;
    value.it_value.tv_usec = TIMER_WAIT_USEC;
    value.it_value.tv_sec = TIMER_WAIT_SEC;
    value.it_interval.tv_usec = TIMER_WAIT_USEC;
    value.it_interval.tv_sec = TIMER_WAIT_SEC;

    for (;;)
    {
        // recieve a message from clients
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        if (select(sock + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            if (alrmflag)
            {
                alrmflag = 0;
                setitimer(ITIMER_REAL, &value, &ovalue);
            }
            else
            {
                perror("main: select");
                exit(1);
            }
        }

        struct sockaddr_in client_addr;
        // if offer can successfully received, do main job after loop
        if (FD_ISSET(sock, &readfds))
        {
            int len = 0;
            struct mydhcp_msg msg = {0};
            int addrlen = sizeof(client_addr);
            if ((len = recvfrom(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&client_addr, (socklen_t *)(&addrlen))) < 0)
            {
                perror("main: recvfrom");
                exit(1);
            }

            struct client *c;
            c = client_search(client_addr);

            char client_id_buf[NSIZE] = {0};
            char client_addr_buf[NSIZE] = {0};
            if (msg.type == MYDHCP_MSGTYPE_RELEASE)
            {
                printf("Receive a message: DHCPRELEASE\n");

                memset(client_id_buf, 0, sizeof(client_id_buf));
                memset(client_addr_buf, 0, sizeof(client_addr_buf));
                strncpy(client_id_buf, inet_ntoa(c->id), sizeof(client_id_buf));
                strncpy(client_addr_buf, inet_ntoa(c->ip_addr), sizeof(client_addr_buf));
                printf("(from the client: id(IP addr): %s, allocated IP addr: %s)\n", client_id_buf, client_addr_buf);
                printf("\n");
                c->status = STATE_TERMINATE;
            }

            if (c != NULL)
            {
                switch (c->status)
                {
                case STATE_WAIT_REQ:
                    printf("State: STATE_WAIT_REQ\n");
                    print_before_state(c->before_status);

                    memset(client_id_buf, 0, sizeof(client_id_buf));
                    memset(client_addr_buf, 0, sizeof(client_addr_buf));
                    strncpy(client_id_buf, inet_ntoa(c->id), sizeof(client_id_buf));
                    strncpy(client_addr_buf, inet_ntoa(c->ip_addr), sizeof(client_addr_buf));
                    printf("(the client: id(IP addr): %s, allocated IP addr: %s)\n", client_id_buf, client_addr_buf);
                    printf("\n");

                    if (msg.type != MYDHCP_MSGTYPE_REQUEST)
                    {
                        fprintf(stderr, "main: expected REQUEST message, got something else.\n");
                        exit(1);
                    }
                    printf("Receive a message: DHCPREQUEST\n");
                    printf("(from the client: id(IP addr): %s, allocated IP addr: %s)\n", client_id_buf, client_addr_buf);
                    printf("\n");

                    int code = 0;
                    code = check_msg_client_addr(msg, c);
                    send_ack(sock, client_addr, code, c);
                    c->client_addr = client_addr;

                    c->status = STATE_IN_USE;
                    c->before_status = STATE_WAIT_REQ;
                    break;

                case STATE_IN_USE:
                    printf("State: STATE_IN_USE\n");
                    print_before_state(c->before_status);

                    memset(client_id_buf, 0, sizeof(client_id_buf));
                    memset(client_addr_buf, 0, sizeof(client_addr_buf));
                    strncpy(client_id_buf, inet_ntoa(c->id), sizeof(client_id_buf));
                    strncpy(client_addr_buf, inet_ntoa(c->ip_addr), sizeof(client_addr_buf));
                    printf("(the client: id(IP addr): %s, allocated IP addr: %s)\n", client_id_buf, client_addr_buf);
                    printf("\n");

                    if (msg.type != MYDHCP_MSGTYPE_REQUEST)
                    {
                        fprintf(stderr, "main: expected REQUEST message, got something else.\n");
                        exit(1);
                    }
                    printf("Receive a message: DHCPREQUEST\n");
                    printf("(from the client: id(IP addr): %s, allocated IP addr: %s)\n", client_id_buf, client_addr_buf);
                    printf("\n");

                    int decide_code = 0;
                    decide_code = check_msg_client_addr(msg, c);
                    send_ack(sock, client_addr, decide_code, c);
                    c->client_addr = client_addr;

                    c->before_status = STATE_IN_USE;

                    alrmflag = 0;
                    setitimer(ITIMER_REAL, &value, &ovalue);
                    break;

                case STATE_TERMINATE:
                    printf("State: STATE_TERMINATE \n");
                    print_before_state(c->before_status);

                    memset(client_id_buf, 0, sizeof(client_id_buf));
                    memset(client_addr_buf, 0, sizeof(client_addr_buf));
                    strncpy(client_id_buf, inet_ntoa(c->id), sizeof(client_id_buf));
                    strncpy(client_addr_buf, inet_ntoa(c->ip_addr), sizeof(client_addr_buf));
                    printf("(the client: id(IP addr): %s, allocated IP addr: %s)\n", client_id_buf, client_addr_buf);
                    printf("\n");

                    release_client(c);
                    break;
                }
            }
            else
            {
                if (msg.type != MYDHCP_MSGTYPE_DISCOVER)
                {
                    fprintf(stderr, "wait_discover: expected DISCOVER message, got something else.\n");
                    exit(1);
                }

                // if message is DISCOVER, create client
                int allocatable = 0;
                struct client *c;
                if ((c = create_client(client_addr)) == NULL)
                    allocatable = 1;

                printf("Receive a message: DHCPDISCOVER\n");
                char client_id_buf[NSIZE] = {0};
                strncpy(client_id_buf, inet_ntoa(client_addr.sin_addr), sizeof(client_id_buf));
                printf("(from the client: id(IP addr): %s\n", client_id_buf);
                printf("\n");

                // if allocatable
                if (allocatable == 0)
                {
                    char ip_buf[NSIZE] = {0};
                    char netmask_buf[NSIZE] = {0};
                    strncpy(ip_buf, inet_ntoa(c->id), sizeof(ip_buf));
                    strncpy(netmask_buf, inet_ntoa(c->netmask), sizeof(netmask_buf));

                    printf("allocated IP addr and netmask to the client (IP: %s)\n", inet_ntoa(client_addr.sin_addr));
                    printf("IP addr: %s, netmask: %s\n", ip_buf, netmask_buf);
                    printf("time limmit: %d\n", deadline);
                    printf("\n");

                    c->status = STATE_INIT;
                    c->before_status = STATE_INIT;
                    printf("State: STATE_INIT\n");

                    printf("(the client: id(IP addr): %s\n", client_id_buf);
                    printf("\n");

                    send_offer(sock, client_addr, allocatable, c);
                    c->client_addr = client_addr;
                    c->status = STATE_WAIT_REQ;
                    alrmflag = 0;
                    setitimer(ITIMER_REAL, &value, &ovalue);
                }
                else
                {
                    printf("cannot allocate IP addr and netmask to the client (IP: %s)\n", inet_ntoa(client_addr.sin_addr));
                    send_offer(sock, client_addr, allocatable, c);
                }
            }
        }
    }
}

// Problem 2.
// server: timeout 確認をした後にメッセージの再送ができない
// → client c, recvfrom の情報などが取得できないから
