/* net.h — hálózati alrendszer közös definíciók */
#ifndef KERINGO_NET_H
#define KERINGO_NET_H

#include "kernel.h"

#define ETH_ADDR_LEN 6
#define ETH_HDR_LEN  14

typedef struct netdev netdev_t;
struct netdev {
    int type;                    /* 1 = e1000, 2 = rtl8139 */
    u8 mac[6];
    u32 ip, mask, gw, dns;
    int ip_ok;
    u32 rx_pkts, tx_pkts;
    int (*send)(netdev_t *dev, const u8 *pkt, u32 len);
    void (*poll)(netdev_t *dev);
    void (*shutdown)(netdev_t *dev);
};

/* net.c */
extern netdev_t *net_dev;
int  net_up(void);
int  net_hw_ready(void);
void net_rx(u8 *pkt, u32 len);               /* driverek hívják */
int  net_send_raw(const u8 *pkt, u32 len);
int  net_poll(void);                         /* RX feldolgozás, vissza: feldolgozott db */
void net_poll_ms(int ms);                    /* poll+varakozas */
int  net_send_ip(u8 proto, u32 dip, const u8 *payload, u32 len);
int  net_send_udp(u32 dip, u16 sport, u16 dport, const u8 *data, u32 len);
int  net_send_tcp_raw(u32 dip, u16 sport, u16 dport, u32 seq, u32 ack,
                      u8 flags, const u8 *data, u32 len);
u32  net_checksum(const u8 *buf, u32 len);
void net_ip_str(u32 ip, char *out, int outsz);
int  net_ip_parse(const char *s, u32 *out);
int  net_arp_resolve(u32 ip, u8 *mac_out);   /* 1 ha megvan (cache+keres) */
int  net_dns_resolve(const char *name, u32 *ip_out);
const char *net_chipname(void);
u32  net_self_ip(void);

/* driverek */
int e1000_add_controller(pci_dev_t pcidev);
int rtl8139_add_controller(pci_dev_t pcidev);

/* tcp.c */
typedef struct {
    u32 rip; u16 lport, rport;
    u32 snd_nxt, snd_una, rcv_nxt, iss;
    int state;                  /* 0 zárt, 1 syn_sent, 2 established, 3 fin */
    u8 *rbuf; u32 rlen, rcap;
    u32 last_send, last_ack_time;
    int used;
} tcpconn_t;

int  tcp_connect(tcpconn_t *c, u32 rip, u16 rport);
int  tcp_listen(tcpconn_t *c, u16 port);     /* 0.5 Távvezérlő: figyelő socket */
int  tcp_send(tcpconn_t *c, const u8 *data, u32 len);
int  tcp_wait_data(tcpconn_t *c, u32 timeout_ms);
void tcp_close(tcpconn_t *c);
void tcp_shutdown_all(void);
tcpconn_t *tcp_alloc(void);
u32  tcp_read(tcpconn_t *c, u8 *buf, u32 max);  /* elérhető adat kimásolása */
int  tcp_connected(tcpconn_t *c);
void tcp_input(const u8 *pkt, u32 len, u32 srcip);

/* http.c */
typedef struct {
    int status;                 /* HTTP státuszkód */
    u8 *body; u32 body_len;
    char content_type[48];
} http_result_t;

int  http_fetch(const char *url, http_result_t *res);
void http_free_result(http_result_t *res);
/* MJPEG élő adás */
int  http_stream_open(const char *url, tcpconn_t **out);
int  http_stream_poll(tcpconn_t *c, u8 **frame_out, u32 *frame_len);

#endif
