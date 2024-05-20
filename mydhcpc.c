#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include "mydhcp.h"
#define ALRM_MESSAGE "Please type y if you want to continue to use of the IP addr:\n"

uint16_t alloc_req_ttl;
struct in_addr alloc_req_ip_addr;
struct in_addr alloc_req_netmask;

enum State
{
    STATE_INIT,
    STATE_WAIT_OFFER,
    STATE_WAIT_ACK,
    STATE_IN_USE,
    STATE_WAIT_EXT_ACK,
    STATE_EXIT,
};
int sighup_flag;
int alrmflag;

void sighup_handler(int signal)
{
    printf("SIG_HUP\n");
    sighup_flag++;
}

void sigalrm_handler(int signal)
{
    write(1, ALRM_MESSAGE, sizeof(ALRM_MESSAGE));
    alrmflag++;
}

int send_discover(int sock, struct sockaddr_in server_addr)
{
    struct mydhcp_msg msg = {
        .type = MYDHCP_MSGTYPE_DISCOVER,
        .code = 0,
        .ttl = 0,
        .ip_addr = {0},
        .netmask = {0},
    };

    if (sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("send_discover: sendto");
        return 1;
    }
    printf("Send a message: DHCPDISCOVER\n");
    char ipaddr_buf[NSIZE] = {0};
    strncpy(ipaddr_buf, inet_ntoa(server_addr.sin_addr), sizeof(ipaddr_buf));
    printf("(to the server: IP addr: %s)\n", ipaddr_buf);
    printf("\n");

    return 0;
}

enum State init(int sock, struct sockaddr_in server_addr)
{
    send_discover(sock, server_addr);
    return STATE_WAIT_OFFER;
}

int send_request(int sock, struct sockaddr_in server_addr, uint16_t ttl, struct in_addr ip_addr, struct in_addr netmask, int code)
{
    struct mydhcp_msg msg = {
        .type = MYDHCP_MSGTYPE_REQUEST,
        .code = code,
        .ttl = htons(ttl),
        .ip_addr = {.s_addr = htonl(ip_addr.s_addr)},
        .netmask = {.s_addr = htonl(netmask.s_addr)},
    };

    if (sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("send_request: sendto");
        return 1;
    }

    printf("Send a message: DHCPREQUEST\n");
    char ipaddr_buf[NSIZE] = {0};
    strncpy(ipaddr_buf, inet_ntoa(server_addr.sin_addr), sizeof(ipaddr_buf));
    printf("(to the server: IP addr: %s)\n", ipaddr_buf);
    printf("\n");
    return 0;
}

void send_release(int sock, struct sockaddr_in server_addr)
{
    struct mydhcp_msg msg = {
        .type = MYDHCP_MSGTYPE_RELEASE,
        .code = 0,
        .ttl = 0,
        .ip_addr = {0},
        .netmask = {0},
    };

    if (sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("send_release: sendto");
        exit(1);
    }

    printf("Send a message: DHCPRELEASE\n");
    char ipaddr_buf[NSIZE] = {0};
    strncpy(ipaddr_buf, inet_ntoa(server_addr.sin_addr), sizeof(ipaddr_buf));
    printf("(to the server: IP addr: %s)\n", ipaddr_buf);
    printf("\n");

    return;
}

enum State wait_offer(int sock, struct sockaddr_in server_addr)
{
    for (int i = 0; i < 2; i++)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval timeout = {
            .tv_sec = (time_t)10,
            .tv_usec = (suseconds_t)0,
        };

        if (select(sock + 1, &readfds, NULL, NULL, &timeout) < 0)
        {
            perror("wait_offer: select");
            return STATE_EXIT;
        }

        // if offer can successfully received, do main job after loop
        if (FD_ISSET(sock, &readfds))
        {
            break;
        }

        // first timeout
        if (i == 0)
        {
            printf("timeout occured.\n");

            int error = 0;
            error = send_discover(sock, server_addr);
            if (error == 1)
                return STATE_EXIT;
            continue;
        }

        // second timeout
        fprintf(stderr, "wait_offer: timeout repeatedly occured.\n");
        return STATE_EXIT;
    }

    struct mydhcp_msg msg = {0};
    if (recv(sock, &msg, sizeof(msg), 0) < (ssize_t)sizeof(msg))
    {
        perror("wait_offer: recv");
        return STATE_EXIT;
    }

    if (msg.type != MYDHCP_MSGTYPE_OFFER)
    {
        fprintf(stderr, "wait_offer: expected OFFER message, got something else.\n");
        return STATE_EXIT;
    }
    printf("Receive a message: DHCPOFFER\n");
    char ipaddr_buf[NSIZE] = {0};
    strncpy(ipaddr_buf, inet_ntoa(server_addr.sin_addr), sizeof(ipaddr_buf));
    printf("(from the server: IP addr: %s)\n", ipaddr_buf);
    printf("\n");

    // if server refused to assign ip address
    if (msg.code == MYDHCP_MSGCODE_OFFER_NG)
    {
        printf("wait_offer: got OFFER_NG message.\n");
        printf("There might be no allocatable IP addr\n");
        printf("exit\n");
        exit(0);
    }

    struct in_addr ip_addr = {.s_addr = ntohl(msg.ip_addr.s_addr)};
    struct in_addr netmask = {.s_addr = ntohl(msg.netmask.s_addr)};

    char ip_buf[NSIZE] = {0};
    char netmask_buf[NSIZE] = {0};
    strncpy(ip_buf, inet_ntoa(ip_addr), sizeof(ip_buf));
    strncpy(netmask_buf, inet_ntoa(netmask), sizeof(netmask_buf));

    printf("Got IP addr and netmask.\n");
    printf("IP addr: %s, netmask: %s\n", ip_buf, netmask_buf);
    printf("time limmit: %u\n", ntohs(msg.ttl));
    printf("\n");

    // for resend message
    alloc_req_ip_addr = ip_addr;
    alloc_req_netmask = netmask;
    alloc_req_ttl = ntohs(msg.ttl);

    int error = 0;
    error = send_request(sock, server_addr, ntohs(msg.ttl), ip_addr, netmask, MYDHCP_MSGCODE_ALLOC);
    if (error == 1)
        return STATE_EXIT;

    return STATE_WAIT_ACK;
}

enum State wait_ack(int sock, struct sockaddr_in server_addr, uint16_t ttl, struct in_addr ip_addr, struct in_addr netmask, int code)
{
    for (int i = 0; i < 2; i++)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval timeout = {
            .tv_sec = (time_t)10,
            .tv_usec = (suseconds_t)0,
        };

        if (select(sock + 1, &readfds, NULL, NULL, &timeout) < 0)
        {
            perror("wait_ack: select");
            return STATE_EXIT;
        }

        // if ack can successfully received, do main job after loop
        if (FD_ISSET(sock, &readfds))
        {
            break;
        }

        // first timeout
        if (i == 0)
        {
            printf("timeout occured.\n");

            int error = 0;
            error = send_request(sock, server_addr, ttl, ip_addr, netmask, code);
            if (error == 1)
                return STATE_EXIT;

            continue;
        }

        // second timeout
        fprintf(stderr, "wait_ack: timeout repeatedly occured.\n");
        return STATE_EXIT;
    }

    struct mydhcp_msg msg = {0};
    if (recv(sock, &msg, sizeof(msg), 0) < (ssize_t)sizeof(msg))
    {
        perror("wait_ack: recv");
        return STATE_EXIT;
    }

    if (msg.type != MYDHCP_MSGTYPE_ACK)
    {
        fprintf(stderr, "wait_ack: expected ACK message, got something else.\n");
        return STATE_EXIT;
    }
    printf("Receive a message: DHCPACK\n");
    char ipaddr_buf[NSIZE] = {0};
    strncpy(ipaddr_buf, inet_ntoa(server_addr.sin_addr), sizeof(ipaddr_buf));
    printf("(from the server: IP addr: %s)\n", ipaddr_buf);
    printf("\n");

    // if server refused to assign ip address
    if (msg.code == MYDHCP_MSGCODE_ACK_NG)
    {
        fprintf(stderr, "wait_ack: got ACK_NG message.\n");
        return STATE_EXIT;
    }

    return STATE_IN_USE;
}

struct itimerval value;
struct itimerval ovalue;
enum State in_use(int sock, struct sockaddr_in server_addr, uint16_t ttl, struct in_addr ip_addr, struct in_addr netmask)
{
    int ttl_half = ttl / 2;

    value.it_value.tv_usec = 0;
    value.it_value.tv_sec = ttl_half;
    value.it_interval.tv_usec = 0;
    value.it_interval.tv_sec = ttl_half;

    if (setitimer(ITIMER_REAL, &value, &ovalue) == -1)
    {
        fprintf(stderr, "in_use: setitimer");
        return STATE_EXIT;
    }

    for (;;)
    {
        if (alrmflag > 0)
        {
            // if timeout passed, wait stdin
            alrmflag = 0;
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(0, &readfds);
            uint16_t waiit_stdin = ttl_half / 2;

            struct timeval stdin_timeout = {
                .tv_sec = (time_t)waiit_stdin,
                .tv_usec = (suseconds_t)0,
            };

            if (select(sock + 1, &readfds, NULL, NULL, &stdin_timeout) < 0)
            {
                perror("in_use: select");
                return STATE_EXIT;
            }
            if (FD_ISSET(0, &readfds))
            {
                int len = 0;
                char buf[5] = {0};
                if ((len = read(0, buf, sizeof(buf))) > 0)
                {
                    if (buf[0] == 'y' || buf[0] == 'Y')
                    {
                        int error = 0;
                        error = send_request(sock, server_addr, ttl, ip_addr, netmask, MYDHCP_MSGCODE_EXT);
                        if (!error)
                            return STATE_WAIT_EXT_ACK;
                    }
                }
            }

            printf("in_use: release IP addr\n");
            return STATE_EXIT;
        }
    }
}

void print_before_state(enum State before_state)
{
    switch (before_state)
    {
    case STATE_INIT:
        printf("(State changed from STATE_INIT)\n\n");
        break;
    case STATE_WAIT_OFFER:
        printf("(State changed from STATE_WAIT_OFFER)\n\n");
        break;
    case STATE_WAIT_ACK:
        printf("(State changed from STATE_WAIT_ACK)\n\n");
        break;
    case STATE_IN_USE:
        printf("(State changed from STATE_IN_USE)\n\n");
        break;
    case STATE_WAIT_EXT_ACK:
        printf("(State changed from STATE_EXT_ACK)\n\n");
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: ./mydhcpc <server-address>\n");
        exit(1);
    }

    signal(SIGHUP, sighup_handler);
    signal(SIGALRM, sigalrm_handler);

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(51230),
        .sin_addr = {.s_addr = htonl(INADDR_ANY)},
    };

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr.s_addr) < 1)
    {
        fprintf(stderr, "inet_pton: %s is not a valid server address.\n", argv[1]);
        exit(1);
    }

    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    enum State state = STATE_INIT;
    enum State before_state = STATE_INIT;

    if (sighup_flag)
    {
        state = STATE_EXIT;
    }

    for (;;)
    {
        switch (state)
        {
        case STATE_INIT:
            printf("State: STATE_INIT\n");
            print_before_state(before_state);
            state = init(sock, server_addr);
            before_state = STATE_INIT;
            break;
        case STATE_WAIT_OFFER:
            printf("State: STATE_WAIT_OFFER\n");
            print_before_state(before_state);
            state = wait_offer(sock, server_addr);
            before_state = STATE_WAIT_OFFER;
            break;
        case STATE_WAIT_ACK:
            printf("State: STATE_WAIT_ACK\n");
            print_before_state(before_state);
            state = wait_ack(sock, server_addr, alloc_req_ttl, alloc_req_ip_addr, alloc_req_netmask, MYDHCP_MSGCODE_ALLOC);
            before_state = STATE_WAIT_ACK;
            break;
        case STATE_IN_USE:
            printf("State: STATE_IN_USE\n");
            print_before_state(before_state);
            state = in_use(sock, server_addr, alloc_req_ttl, alloc_req_ip_addr, alloc_req_netmask);
            before_state = STATE_IN_USE;
            break;
        case STATE_WAIT_EXT_ACK:
            printf("State: STATE_WAIT_EXT_ACK\n");
            print_before_state(before_state);
            state = wait_ack(sock, server_addr, alloc_req_ttl, alloc_req_ip_addr, alloc_req_netmask, MYDHCP_MSGCODE_EXT);
            before_state = STATE_WAIT_EXT_ACK;
            break;
        case STATE_EXIT:
            printf("State: STATE_EXIT\n");
            print_before_state(before_state);

            send_release(sock, server_addr);
            exit(1);
            break;
        }
    }

    return 0;
}
