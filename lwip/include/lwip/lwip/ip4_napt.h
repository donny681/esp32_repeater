#ifndef __LWIP_NAPT_H__
#define __LWIP_NAPT_H__
//#include "lwip/lwipopts.h"
#include "lwip/opt.h"
#include <stdio.h>
#include <stdint.h>
#include "lwip/err.h"
#include "lwip/ip4.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "spiffs_config.h"

//#include "common.h"

//#define LWIP_TCP 1
#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

/* Get one byte from the 4-byte address */
#define napt_ip4_addr1(ipaddr) (((u8_t*)(ipaddr))[0])
#define napt_ip4_addr2(ipaddr) (((u8_t*)(ipaddr))[1])
#define napt_ip4_addr3(ipaddr) (((u8_t*)(ipaddr))[2])
#define napt_ip4_addr4(ipaddr) (((u8_t*)(ipaddr))[3])
/* These are cast to u16_t, with the intent that they are often arguments
 * to printf using the U16_F format from cc.h. */
#define napt_ip4_addr1_16(ipaddr) ((u16_t)napt_ip4_addr1(ipaddr))
#define napt_ip4_addr2_16(ipaddr) ((u16_t)napt_ip4_addr2(ipaddr))
#define napt_ip4_addr3_16(ipaddr) ((u16_t)napt_ip4_addr3(ipaddr))
#define napt_ip4_addr4_16(ipaddr) ((u16_t)napt_ip4_addr4(ipaddr))

#define nat_ip_addr_cmp(addr1, addr2) ((addr1)->addr == (addr2)->addr)
#if IP_FORWARD
#if IP_NAPT

/* Default size of the tables used for NAPT */
#define IP_NAPT_MAX 512
#define IP_PORTMAP_MAX 32

/* Timeouts in sec for the various protocol types */
#define IP_NAPT_TIMEOUT_MS_TCP (30*60*1000)
#define IP_NAPT_TIMEOUT_MS_TCP_DISCON (20*1000)
#define IP_NAPT_TIMEOUT_MS_UDP (2*1000)
#define IP_NAPT_TIMEOUT_MS_ICMP (2*1000)

#define IP_NAPT_PORT_RANGE_START 49152
#define IP_NAPT_PORT_RANGE_END   61439

struct napt_table {
  u32_t last;
  u32_t src;
  u32_t dest;
  u16_t sport;
  u16_t dport;
  u16_t mport;
  u8_t proto;
  u8_t fin1 : 1;
  u8_t fin2 : 1;
  u8_t finack1 : 1;
  u8_t finack2 : 1;
  u8_t synack : 1;
  u8_t rst : 1;
  u16_t next, prev;
};

struct portmap_table {
  u32_t maddr;
  u32_t daddr;
  u16_t mport;
  u16_t dport;
  u8_t proto;
  u8 valid;
};

extern struct portmap_table *ip_portmap_table;

/**
 * Allocates and initializes the NAPT tables.
 *
 * @param max_nat max number of enties in the NAPT table (use IP_NAPT_MAX if in doubt)
 * @param max_portmap max number of enties in the NAPT table (use IP_PORTMAP_MAX if in doubt)
 */
void ESP_IRAM_ATTR
ip_napt_init(uint16_t max_nat, uint8_t max_portmap);


/**
 * Enable/Disable NAPT for a specified interface.
 *
 * @param addr ip address of the interface
 * @param enable non-zero to enable NAPT, or 0 to disable.
 */
void ESP_IRAM_ATTR
ip_napt_enable(u32_t addr, int enable);


/**
 * Enable/Disable NAPT for a specified interface.
 *
 * @param netif number of the interface
 * @param enable non-zero to enable NAPT, or 0 to disable.
 */
void ESP_IRAM_ATTR
ip_napt_enable_no(u8_t number, int enable);


/**
 * Register port mapping on the external interface to internal interface.
 * When the same port mapping is registered again, the old mapping is overwritten.
 * In this implementation, only 1 unique port mapping can be defined for each target address/port.
 *
 * @param proto target protocol
 * @param maddr ip address of the external interface
 * @param mport mapped port on the external interface, in host byte order.
 * @param daddr destination ip address
 * @param dport destination port, in host byte order.
 */
u8_t ESP_IRAM_ATTR
ip_portmap_add(u8_t proto, u32_t maddr, u16_t mport, u32_t daddr, u16_t dport);


/**
 * Unregister port mapping on the external interface to internal interface.
 *
 * @param proto target protocol
 * @param maddr ip address of the external interface
 */
u8_t ESP_IRAM_ATTR
ip_portmap_remove(u8_t proto, u16_t mport);

/**
 * NAPT for a forwarded packet. It checks weather we need NAPT and modify
 * the packet source address and port if needed.
 *
 * @param p the packet to forward (p->payload points to IP header)
 * @param iphdr the IP header of the input packet
 * @param inp the netif on which this packet was received
 * @param outp the netif on which this packet will be sent
 * @return ERR_OK if packet should be sent, or ERR_RTE if it should be dropped
 */
err_t ESP_IRAM_ATTR
ip_napt_forward(struct pbuf *p, struct ip_hdr *iphdr, struct netif *inp, struct netif *outp);


/**
 * NAPT for an input packet. It checks weather the destination is on NAPT
 * table and modifythe packet destination address and port if needed.
 *
 * @param p the packet to forward (p->payload points to IP header)
 * @param iphdr the IP header of the input packet
 * @param inp the netif on which this packet was received
 */
void ESP_IRAM_ATTR
ip_napt_recv(struct pbuf *p, struct ip_hdr *iphdr, struct netif *inp);

void ESP_IRAM_ATTR
napt_debug_print();

#endif /* IP_NAPT */
#endif /* IP_FORWARD */

#ifdef __cplusplus
}
#endif

#endif /* __LWIP_NAPT_H__ */
