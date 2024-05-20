#include <inttypes.h>
#include <netinet/in.h>

typedef uint16_t be16_t; // 16bit value in network endian
typedef uint32_t be32_t; // 32bit value in network endian

#define MYDHCP_MSGTYPE_DISCOVER ((uint8_t)1)
#define MYDHCP_MSGTYPE_OFFER ((uint8_t)2)
#define MYDHCP_MSGTYPE_REQUEST ((uint8_t)3)
#define MYDHCP_MSGTYPE_ACK ((uint8_t)4)
#define MYDHCP_MSGTYPE_RELEASE ((uint8_t)5)

#define MYDHCP_MSGCODE_OFFER_OK ((uint8_t)0)
#define MYDHCP_MSGCODE_OFFER_NG ((uint8_t)1)
#define MYDHCP_MSGCODE_ALLOC ((uint8_t)2)
#define MYDHCP_MSGCODE_EXT ((uint8_t)3)
#define MYDHCP_MSGCODE_ACK_OK ((uint8_t)0)
#define MYDHCP_MSGCODE_ACK_NG ((uint8_t)4)

#define NSIZE 32

struct __attribute__((__packed__)) mydhcp_msg
{
    uint8_t type;
    uint8_t code;
    be16_t ttl;
    struct in_addr ip_addr;
    struct in_addr netmask;
};
