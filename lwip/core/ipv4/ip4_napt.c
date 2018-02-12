#include "lwip/ip4_napt.h"

#include "lwip/icmp.h"
#include "lwip/tcp.h"
#include "lwip/ip.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/ip_frag.h"
#include "lwip/inet_chksum.h"
#include "lwip/netif.h"
#include "lwip/icmp.h"
#include "lwip/igmp.h"
#include "lwip/raw.h"
#include "lwip/udp.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"
#include "lwip/stats.h"
#include "lwip/sys.h"
#include <stdlib.h>
#include "esp_attr.h"

#if IP_FORWARD
#if IP_NAPT

#ifndef NAPT_DEBUG
#define NAPT_DEBUG      LWIP_DBG_ON
#endif

#define NO_IDX ((u16_t)-1)
#define NT(x) ((x) == NO_IDX ? NULL : &ip_napt_table[x])

u16_t napt_list = NO_IDX, napt_list_last = NO_IDX, napt_free = 0;

static struct napt_table *ip_napt_table;
struct portmap_table *ip_portmap_table;

int nr_active_napt_tcp = 0, nr_active_napt_udp = 0, nr_active_napt_icmp = 0;
uint16_t ip_napt_max = 0;
uint8_t ip_portmap_max = 0;

void ip_napt_init(uint16_t max_nat, uint8_t max_portmap) {
	u16_t i;

	ip_napt_max = max_nat;
	ip_portmap_max = max_portmap;

	ip_napt_table = (struct napt_table*) malloc(
			sizeof(struct napt_table[ip_napt_max]));
	ip_portmap_table = (struct portmap_table*) malloc(
			sizeof(struct portmap_table[ip_portmap_max]));

	for (i = 0; i < ip_napt_max - 1; i++)
		ip_napt_table[i].next = i + 1;
	ip_napt_table[i].next = NO_IDX;
}

void ip_napt_enable(u32_t addr, int enable) {
	struct netif *netif;
	for (netif = netif_list; netif; netif = netif->next) {
		if (netif_is_up(netif) && !ip_addr_isany(&netif->ip_addr)
				&& netif->ip_addr.u_addr.ip4.addr == addr) {
			netif->napt = !!enable;
			break;
		}
	}
}

void ip_napt_enable_no(u8_t number, int enable) {
	struct netif *netif;
	for (netif = netif_list; netif; netif = netif->next) {
		if (netif->num == number) {
			netif->napt = !!enable;
			break;
		}
	}
}

void checksumadjust(unsigned char *chksum, unsigned char *optr, int olen,
		unsigned char *nptr, int nlen)
/* assuming: unsigned char is 8 bits, long is 32 bits.
 - chksum points to the chksum in the packet
 - optr points to the old data in the packet
 - nptr points to the new data in the packet
 */
{
	long x, old, new;
	x = chksum[0] * 256 + chksum[1];
	x = ~x & 0xFFFF;
	while (olen) {
		old = optr[0] * 256 + optr[1];
		optr += 2;
		x -= old & 0xffff;
		if (x <= 0) {
			x--;
			x &= 0xffff;
		}
		olen -= 2;
	}
	while (nlen) {
		new = nptr[0] * 256 + nptr[1];
		nptr += 2;
		x += new & 0xffff;
		if (x & 0x10000) {
			x++;
			x &= 0xffff;
		}
		nlen -= 2;
	}
	x = ~x & 0xFFFF;
	chksum[0] = x / 256;
	chksum[1] = x & 0xff;
}

/* t must be indexed by napt_free */
static void ip_napt_insert(struct napt_table *t) {
	u16_t ti = t - ip_napt_table;
	if (ti != napt_free)
		*((int*) 1) = 1; //DEBUG
	napt_free = t->next;
	t->prev = NO_IDX;
	t->next = napt_list;
	if (napt_list != NO_IDX)
		NT(napt_list)->prev = ti;
	napt_list = ti;
	if (napt_list_last == NO_IDX)
		napt_list_last = ti;

#if LWIP_TCP
	if (t->proto == IP_PROTO_TCP)
		nr_active_napt_tcp++;
#endif
#if LWIP_UDP
	if (t->proto == IP_PROTO_UDP)
		nr_active_napt_udp++;
#endif
#if LWIP_ICMP
	if (t->proto == IP_PROTO_ICMP)
		nr_active_napt_icmp++;
#endif
//os_printf("T: %d, U: %d, I: %d\r\n", nr_active_napt_tcp, nr_active_napt_udp, nr_active_napt_icmp);
}

static void ip_napt_free(struct napt_table *t) {
	u16_t ti = t - ip_napt_table;
	if (ti == napt_list)
		napt_list = t->next;
	if (ti == napt_list_last)
		napt_list_last = t->prev;
	if (t->next != NO_IDX)
		NT(t->next)->prev = t->prev;
	if (t->prev != NO_IDX)
		NT(t->prev)->next = t->next;
	t->prev = NO_IDX;
	t->next = napt_free;
	napt_free = ti;

#if LWIP_TCP
	if (t->proto == IP_PROTO_TCP)
		nr_active_napt_tcp--;
#endif
#if LWIP_UDP
	if (t->proto == IP_PROTO_UDP)
		nr_active_napt_udp--;
#endif
#if LWIP_ICMP
	if (t->proto == IP_PROTO_ICMP)
		nr_active_napt_icmp--;
#endif
	LWIP_DEBUGF(NAPT_DEBUG, ("ip_napt_free\n"));
	napt_debug_print();
}


#if LWIP_TCP
static u8_t ip_napt_find_port(u8_t proto, u16_t port) {
	int i, next;
	for (i = napt_list; i != NO_IDX; i = next) {
		struct napt_table *t = &ip_napt_table[i];
		next = t->next;
		if (t->proto == proto && t->mport == port)
			return 1;
	}
	return 0;
}

static struct portmap_table *
ip_portmap_find(u8_t proto, u16_t mport);

static u8_t tcp_listening(u16_t port) {
	struct tcp_pcb_listen *t;
	for (t = tcp_listen_pcbs.listen_pcbs; t; t = t->next)
		if (t->local_port == port)
			return 1;
	if (ip_portmap_find(IP_PROTO_TCP, port))
		return 1;
	return 0;
}

#endif

#if LWIP_UDP
static u8_t udp_listening(u16_t port) {
	struct udp_pcb *pcb;
	for (pcb = udp_pcbs; pcb; pcb = pcb->next)
		if (pcb->local_port == port)
			return 1;
	if (ip_portmap_find(IP_PROTO_UDP, port))
		return 1;
	return 0;
}
#endif // LWIP_UDP

static u16_t ip_napt_new_port(u8_t proto, u16_t port) {
	if (PP_NTOHS(port) >= IP_NAPT_PORT_RANGE_START
			&& PP_NTOHS(port) <= IP_NAPT_PORT_RANGE_END)
		if (!ip_napt_find_port(proto, port) && !tcp_listening(port))
			return port;
	for (;;) {
		port =
				PP_HTONS(
						IP_NAPT_PORT_RANGE_START + esp_random() % (IP_NAPT_PORT_RANGE_END - IP_NAPT_PORT_RANGE_START + 1));
		if (ip_napt_find_port(proto, port))
			continue;
#if LWIP_TCP
		if (proto == IP_PROTO_TCP && tcp_listening(port))
			continue;
#endif // LWIP_TCP
#if LWIP_UDP
		if (proto == IP_PROTO_UDP && udp_listening(port))
			continue;
#endif // LWIP_UDP

		return port;
	}
}

static struct napt_table*
ip_napt_find(u8_t proto, u32_t addr, u16_t port, u16_t mport, u8_t dest) {
	u16_t i, next;
	struct napt_table *t;

	LWIP_DEBUGF(NAPT_DEBUG, ("ip_napt_find\n"));
	LWIP_DEBUGF(NAPT_DEBUG,
			("looking up in table %s: %"U16_F".%"U16_F".%"U16_F".%"U16_F", port: %u, mport: %u\n", (dest ? "dest" : "src"), napt_ip4_addr1_16(&addr), napt_ip4_addr2_16(&addr), napt_ip4_addr3_16(&addr), napt_ip4_addr4_16(&addr), PP_HTONS(port), PP_HTONS(mport)));
	napt_debug_print();

	u32_t now = sys_now();
	for (i = napt_list; i != NO_IDX; i = next) {
		t = NT(i);
		next = t->next;
#if LWIP_TCP
		if (t->proto == IP_PROTO_TCP
				&& ((((t->finack1 && t->finack2)|| !t->synack)
						&& now - t->last > IP_NAPT_TIMEOUT_MS_TCP_DISCON)
						|| now - t->last > IP_NAPT_TIMEOUT_MS_TCP)) {
			ip_napt_free(t);
			continue;
		}
#endif
#if LWIP_UDP
		if (t->proto == IP_PROTO_UDP && now - t->last > IP_NAPT_TIMEOUT_MS_UDP) {
			ip_napt_free(t);
			continue;
		}
#endif
#if LWIP_ICMP
		if (t->proto == IP_PROTO_ICMP && now - t->last > IP_NAPT_TIMEOUT_MS_ICMP) {
			ip_napt_free(t);
			continue;
		}
#endif
		if (dest == 0 && t->proto == proto && t->src == addr
				&& t->sport == port) {
			t->last = now;
			LWIP_DEBUGF(NAPT_DEBUG, ("found\n"));
			return t;
		}
		if (dest == 1 && t->proto == proto && t->dest == addr
				&& t->dport == port && t->mport == mport) {
			t->last = now;
			LWIP_DEBUGF(NAPT_DEBUG, ("found\n"));
			return t;
		}
	}

	LWIP_DEBUGF(NAPT_DEBUG, ("not found\n"));
	return NULL;
}

static u16_t
ip_napt_add(u8_t proto, u32_t src, u16_t sport, u32_t dest, u16_t dport)
{
  struct napt_table *t = ip_napt_find(proto, src, sport, 0, 0);
  if (t) {
    t->last = sys_now();
    t->dest = dest;
    t->dport = dport;
    /* move this entry to the top of napt_list */
    ip_napt_free(t);
    ip_napt_insert(t);

    LWIP_DEBUGF(NAPT_DEBUG, ("ip_napt_add\n"));
    napt_debug_print();

    return t->mport;
  }
  t = NT(napt_free);
  if (t) {
    u16_t mport = sport;
#if LWIP_TCP
    if (proto == IP_PROTO_TCP)
      mport = ip_napt_new_port(IP_PROTO_TCP, sport);
#endif
#if LWIP_TCP
    if (proto == IP_PROTO_UDP)
      mport = ip_napt_new_port(IP_PROTO_UDP, sport);
#endif
    t->last = sys_now();
    t->src = src;
    t->dest = dest;
    t->sport = sport;
    t->dport = dport;
    t->mport = mport;
    t->proto = proto;
    t->fin1 = t->fin2 = t->finack1 = t->finack2 = t->synack = t->rst = 0;
    ip_napt_insert(t);

    LWIP_DEBUGF(NAPT_DEBUG, ("ip_napt_add\n"));
    napt_debug_print();

    return mport;
  }
  printf("NAT table full\n");
  return 0;
}

u8_t
ip_portmap_add(u8_t proto, u32_t maddr, u16_t mport, u32_t daddr, u16_t dport)
{
  mport = PP_HTONS(mport);
  dport = PP_HTONS(dport);
  int i;

  for (i = 0; i < ip_portmap_max; i++) {
    struct portmap_table *p = &ip_portmap_table[i];
    if (p->valid && p->proto == proto && p->mport == mport) {
      p->dport = dport;
      p->daddr = daddr;
    } else if (!p->valid) {
      p->maddr = maddr;
      p->daddr = daddr;
      p->mport = mport;
      p->dport = dport;
      p->proto = proto;
      p->valid = 1;
      return 1;
    }
  }
  return 0;
}

static struct portmap_table *
ip_portmap_find(u8_t proto, u16_t mport)
{
  int i;
  for (i = 0; i < ip_portmap_max; i++) {
    struct portmap_table *p = &ip_portmap_table[i];
    if (!p->valid)
      return 0;
    if (p->proto == proto && p->mport == mport)
      return p;
  }
  return NULL;
}

static struct portmap_table *
ip_portmap_find_dest(u8_t proto, u16_t dport, u32_t daddr)
{
  int i;
  for (i = 0; i < ip_portmap_max; i++) {
    struct portmap_table *p = &ip_portmap_table[i];
    if (!p->valid)
      return 0;
    if (p->proto == proto && p->dport == dport && p->daddr == daddr)
      return p;
  }
  return NULL;
}

u8_t
ip_portmap_remove(u8_t proto, u16_t mport)
{
  mport = PP_HTONS(mport);
  struct portmap_table *last = &ip_portmap_table[ip_portmap_max - 1];
  struct portmap_table *m = ip_portmap_find(proto, mport);
  if (!m)
    return 0;
  for (; m != last; m++)
    memcpy(m, m + 1, sizeof(*m));
  last->valid = 0;
  return 1;
}

#if LWIP_TCP
void
ip_napt_modify_port_tcp(struct tcp_hdr *tcphdr, u8_t dest, u16_t newval)
{
  if (dest) {
    checksumadjust((unsigned char *)&tcphdr->chksum, (unsigned char *)&tcphdr->dest, 2, (unsigned char *)&newval, 2);
    tcphdr->dest = newval;
  } else {
    checksumadjust((unsigned char *)&tcphdr->chksum, (unsigned char *)&tcphdr->src, 2, (unsigned char *)&newval, 2);
    tcphdr->src = newval;
  }
}


void
ip_napt_modify_addr_tcp(struct tcp_hdr *tcphdr, ip4_addr_p_t *oldval, u32_t newval)
{
  checksumadjust((unsigned char *)&tcphdr->chksum, (unsigned char *)&oldval->addr, 4, (unsigned char *)&newval, 4);
}
#endif // LWIP_TCP


#if LWIP_UDP
void
ip_napt_modify_port_udp(struct udp_hdr *udphdr, u8_t dest, u16_t newval)
{
  if (dest) {
    checksumadjust((unsigned char *)&udphdr->chksum, (unsigned char *)&udphdr->dest, 2, (unsigned char *)&newval, 2);
    udphdr->dest = newval;
  } else {
    checksumadjust((unsigned char *)&udphdr->chksum, (unsigned char *)&udphdr->src, 2, (unsigned char *)&newval, 2);
    udphdr->src = newval;
  }
}

void
ip_napt_modify_addr_udp(struct udp_hdr *udphdr, ip4_addr_p_t *oldval, u32_t newval)
{
  checksumadjust((unsigned char *)&udphdr->chksum, (unsigned char *)&oldval->addr, 4, (unsigned char *)&newval, 4);
}
#endif // LWIP_UDP


void
ip_napt_modify_addr(struct ip_hdr *iphdr, ip4_addr_p_t *field, u32_t newval)
{
  checksumadjust((unsigned char *)&IPH_CHKSUM(iphdr), (unsigned char *)&field->addr, 4, (unsigned char *)&newval, 4);
  field->addr = newval;
}

/**
 * NAPT for an input packet. It checks weather the destination is on NAPT
 * table and modifythe packet destination address and port if needed.
 *
 * @param p the packet to forward (p->payload points to IP header)
 * @param iphdr the IP header of the input packet
 * @param inp the netif on which this packet was received
 */
void ip_napt_recv(struct pbuf *p, struct ip_hdr *iphdr, struct netif *inp)
{
  struct portmap_table *m;
  struct napt_table *t;

#if LWIP_ICMP
  /* NAPT for ICMP Echo Request using identifier */
  if (IPH_PROTO(iphdr) == IP_PROTO_ICMP) {
    struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + IPH_HL(iphdr) * 4);
    if (iecho->type == ICMP_ER) {
      t = ip_napt_find(IP_PROTO_ICMP, iphdr->src.addr, iecho->id, iecho->id, 1);
      if (!t)
        return;
      ip_napt_modify_addr(iphdr, &iphdr->dest, t->src);
      return;
    }

    return;
  }
#endif // LWIP_ICMP

#if LWIP_TCP
  if (IPH_PROTO(iphdr) == IP_PROTO_TCP) {
    struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)p->payload + IPH_HL(iphdr) * 4);

    LWIP_DEBUGF(NAPT_DEBUG, ("ip_napt_recv\n"));
    LWIP_DEBUGF(NAPT_DEBUG, ("src: %"U16_F".%"U16_F".%"U16_F".%"U16_F", dest: %"U16_F".%"U16_F".%"U16_F".%"U16_F", ",
      ip4_addr1_16(&iphdr->src), ip4_addr2_16(&iphdr->src),
      ip4_addr3_16(&iphdr->src), ip4_addr4_16(&iphdr->src),
      ip4_addr1_16(&iphdr->dest), ip4_addr2_16(&iphdr->dest),
      ip4_addr3_16(&iphdr->dest), ip4_addr4_16(&iphdr->dest)));

      LWIP_DEBUGF(NAPT_DEBUG, ("sport %u, dport: %u\n",
                        PP_HTONS(tcphdr->src),
                        PP_HTONS(tcphdr->dest)));

    m = ip_portmap_find(IP_PROTO_TCP, tcphdr->dest);
    if (m) {
      /* packet to mapped port: rewrite destination */
      if (m->dport != tcphdr->dest)
        ip_napt_modify_port_tcp(tcphdr, 1, m->dport);
      ip_napt_modify_addr_tcp(tcphdr, &iphdr->dest, m->daddr);
      ip_napt_modify_addr(iphdr, &iphdr->dest, m->daddr);
      return;
    }
    t = ip_napt_find(IP_PROTO_TCP, iphdr->src.addr, tcphdr->src, tcphdr->dest, 1);
      if (!t)
        return; /* Unknown TCP session; do nothing */

      if (t->sport != tcphdr->dest)
        ip_napt_modify_port_tcp(tcphdr, 1, t->sport);
      ip_napt_modify_addr_tcp(tcphdr, &iphdr->dest, t->src);
      ip_napt_modify_addr(iphdr, &iphdr->dest, t->src);

      if ((TCPH_FLAGS(tcphdr) & (TCP_SYN|TCP_ACK)) == (TCP_SYN|TCP_ACK))
        t->synack = 1;
      if ((TCPH_FLAGS(tcphdr) & TCP_FIN))
        t->fin1 = 1;
      if (t->fin2 && (TCPH_FLAGS(tcphdr) & TCP_ACK))
        t->finack2 = 1; /* FIXME: Currently ignoring ACK seq... */
      if (TCPH_FLAGS(tcphdr) & TCP_RST)
        t->rst = 1;
      return;
  }
#endif // LWIP_TCP

#if LWIP_UDP
  if (IPH_PROTO(iphdr) == IP_PROTO_UDP) {
    struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t *)p->payload + IPH_HL(iphdr) * 4);
    m = ip_portmap_find(IP_PROTO_UDP, udphdr->dest);
    if (m) {
      /* packet to mapped port: rewrite destination */
      if (m->dport != udphdr->dest)
        ip_napt_modify_port_udp(udphdr, 1, m->dport);
      ip_napt_modify_addr_udp(udphdr, &iphdr->dest, m->daddr);
      ip_napt_modify_addr(iphdr, &iphdr->dest, m->daddr);
      return;
    }
    t = ip_napt_find(IP_PROTO_UDP, iphdr->src.addr, udphdr->src, udphdr->dest, 1);
      if (!t)
        return; /* Unknown session; do nothing */

      if (t->sport != udphdr->dest)
        ip_napt_modify_port_udp(udphdr, 1, t->sport);
      ip_napt_modify_addr_udp(udphdr, &iphdr->dest, t->src);
      ip_napt_modify_addr(iphdr, &iphdr->dest, t->src);
      return;
  }
#endif // LWIP_UDP
}

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
 err_t
ip_napt_forward(struct pbuf *p, struct ip_hdr *iphdr, struct netif *inp, struct netif *outp)
{
  if (!inp->napt)
    return ERR_OK;

#if LWIP_ICMP
  /* NAPT for ICMP Echo Request using identifier */
  if (IPH_PROTO(iphdr) == IP_PROTO_ICMP) {
    struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + IPH_HL(iphdr) * 4);
    if (iecho->type == ICMP_ECHO) {
      /* register src addr and iecho->id and dest info */
      ip_napt_add(IP_PROTO_ICMP, iphdr->src.addr, iecho->id, iphdr->dest.addr, iecho->id);

      ip_napt_modify_addr(iphdr, &iphdr->src, outp->ip_addr.u_addr.ip4.addr);
    }
    return ERR_OK;
  }
#endif

#if LWIP_TCP
  if (IPH_PROTO(iphdr) == IP_PROTO_TCP) {
    struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)p->payload + IPH_HL(iphdr) * 4);
    u16_t mport;

    struct portmap_table *m = ip_portmap_find_dest(IP_PROTO_TCP, tcphdr->src, iphdr->src.addr);
    if (m) {
      /* packet from port-mapped dest addr/port: rewrite source to this node */
      if (m->mport != tcphdr->src)
        ip_napt_modify_port_tcp(tcphdr, 0, m->mport);
      ip_napt_modify_addr_tcp(tcphdr, &iphdr->src, m->maddr);
      ip_napt_modify_addr(iphdr, &iphdr->src, m->maddr);
      return ERR_OK;
    }
    if ((TCPH_FLAGS(tcphdr) & (TCP_SYN|TCP_ACK)) == TCP_SYN &&
        PP_NTOHS(tcphdr->src) >= 1024) {
      /* Register new TCP session to NAPT */
      mport = ip_napt_add(IP_PROTO_TCP, iphdr->src.addr, tcphdr->src,
                          iphdr->dest.addr, tcphdr->dest);
    } else {
      struct napt_table *t = ip_napt_find(IP_PROTO_TCP, iphdr->src.addr, tcphdr->src, 0, 0);
      if (!t || t->dest != iphdr->dest.addr || t->dport != tcphdr->dest) {
#if LWIP_ICMP
        icmp_dest_unreach(p, ICMP_DUR_PORT);
#endif
        return ERR_RTE; /* Drop unknown TCP session */
      }
      mport = t->mport;
      if ((TCPH_FLAGS(tcphdr) & TCP_FIN))
        t->fin2 = 1;
      if (t->fin1 && (TCPH_FLAGS(tcphdr) & TCP_ACK))
        t->finack1 = 1; /* FIXME: Currently ignoring ACK seq... */
      if (TCPH_FLAGS(tcphdr) & TCP_RST)
        t->rst = 1;
    }

    if (mport != tcphdr->src)
      ip_napt_modify_port_tcp(tcphdr, 0, mport);
    ip_napt_modify_addr_tcp(tcphdr, &iphdr->src, outp->ip_addr.u_addr.ip4.addr);
    ip_napt_modify_addr(iphdr, &iphdr->src, outp->ip_addr.u_addr.ip4.addr);
    return ERR_OK;
  }
#endif

#if LWIP_UDP
  if (IPH_PROTO(iphdr) == IP_PROTO_UDP) {
    struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t *)p->payload + IPH_HL(iphdr) * 4);
    u16_t mport;

    struct portmap_table *m = ip_portmap_find_dest(IP_PROTO_UDP, udphdr->src, iphdr->src.addr);
    if (m) {
      /* packet from port-mapped dest addr/port: rewrite source to this node */
      if (m->mport != udphdr->src)
        ip_napt_modify_port_udp(udphdr, 0, m->mport);
      ip_napt_modify_addr_udp(udphdr, &iphdr->src, m->maddr);
      ip_napt_modify_addr(iphdr, &iphdr->src, m->maddr);
      return ERR_OK;
    }
    if (PP_NTOHS(udphdr->src) >= 1024) {
      /* Register new UDP session */
      mport = ip_napt_add(IP_PROTO_UDP, iphdr->src.addr, udphdr->src,
                          iphdr->dest.addr, udphdr->dest);
    } else {
      struct napt_table *t = ip_napt_find(IP_PROTO_UDP, iphdr->src.addr, udphdr->src, 0, 0);
      if (!t || t->dest != iphdr->dest.addr || t->dport != udphdr->dest) {
#if LWIP_ICMP
        icmp_dest_unreach(p, ICMP_DUR_PORT);
#endif
        return ERR_RTE; /* Drop unknown UDP session */
      }
      mport = t->mport;
    }

    if (mport != udphdr->src)
      ip_napt_modify_port_udp(udphdr, 0, mport);
    ip_napt_modify_addr_udp(udphdr, &iphdr->src, outp->ip_addr.u_addr.ip4.addr);
    ip_napt_modify_addr(iphdr, &iphdr->src, outp->ip_addr.u_addr.ip4.addr);
    return ERR_OK;
  }
#endif

  return ERR_OK;
}
#endif // IP_NAPT
#endif
//#if NAPT_DEBUG
/* Print NAPT table using LWIP_DEBUGF
 */
void napt_debug_print() {
	int i, next;
	LWIP_DEBUGF(NAPT_DEBUG, ("NAPT table:\n"));
	LWIP_DEBUGF(NAPT_DEBUG,
			(" src                     dest                    sport   dport   mport   \n"));
	LWIP_DEBUGF(NAPT_DEBUG,
			("+-----------------------+-----------------------+-------+-------+-------+\n"));
	for (i = napt_list; i != NO_IDX; i = next) {
		struct napt_table *t = &ip_napt_table[i];
		next = t->next;

		LWIP_DEBUGF(NAPT_DEBUG,
				("| %3"U16_F" | %3"U16_F" | %3"U16_F" | %3"U16_F" |", napt_ip4_addr1_16(&t->src), napt_ip4_addr2_16(&t->src), napt_ip4_addr3_16(&t->src), napt_ip4_addr4_16(&t->src)));

		LWIP_DEBUGF(NAPT_DEBUG,
				(" %3"U16_F" | %3"U16_F" | %3"U16_F" | %3"U16_F" |", napt_ip4_addr1_16(&t->dest), napt_ip4_addr2_16(&t->dest), napt_ip4_addr3_16(&t->dest), napt_ip4_addr4_16(&t->dest)));

		LWIP_DEBUGF(NAPT_DEBUG,
				(" %5"U16_F" | %5"U16_F" | %5"U16_F" |\n", PP_HTONS(t->sport), PP_HTONS(t->dport), PP_HTONS(t->mport)));

	}
}
//#endif /* NAPT_DEBUG */
