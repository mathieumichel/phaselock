#ifndef RPL_TOOLS_H
#define RPL_TOOLS_H

/* Used to identify packets carrying ORPL log */
#define ORPL_LOG_MAGIC 0xcafebabe

struct app_data {
  uint32_t magic;
  uint32_t seqno;
  uint16_t src;
  uint16_t dest;
  uint8_t hop;
  uint8_t ping;
  uint8_t fpcount;
  uint16_t dc_metric;
};

/* Copy an appdata to another with no assumption that the addresses are aligned */
void appdata_copy(struct app_data *dst, struct app_data *src);
/* Get dataptr from the packet currently in uIP buffer */
struct app_data *appdataptr_from_uip();
/* Get dataptr from the current packetbuf */
struct app_data *appdataptr_from_packetbuf();
/* Log information about a data packet along with ORPL routing information */
void log_appdataptr(struct app_data *dataptr);
/* Return node id from its rime address */
uint16_t log_node_id_from_rimeaddr(const void *rimeaddr);
/* Return node id from its IP address */
uint16_t log_node_id_from_ipaddr(const void *ipaddr);
/* Prints out the content of the active routing set */
void rpl_log_print_routing_set();
/* Starts logging process */
void rpl_log_start();

#define LOG_FROM_APPDATAPTR(appdataptr, ...) { printf(__VA_ARGS__); log_appdataptr(appdataptr); }
#define LOG_NULL(...) LOG_FROM_APPDATAPTR(NULL, __VA_ARGS__)
#define LOG_FROM_UIP(...) LOG_FROM_APPDATAPTR(appdataptr_from_uip(), __VA_ARGS__)
#define LOG_FROM_PACKETBUF(...) LOG_FROM_APPDATAPTR(appdataptr_from_packetbuf(), __VA_ARGS__)
#define LOG_INC_HOPCOUNT_FROM_PACKETBUF() { appdataptr_from_packetbuf()->hop++; }
#define LOG_INC_FPCOUNT_FROM_PACKETBUF() { appdataptr_from_packetbuf()->fpcount++; }
#define LOG_IPADDR(addr) uip_debug_ipaddr_print(addr)


#define LOG_NODEID_FROM_RIMEADDR log_node_id_from_rimeaddr
#define LOG_NODEID_FROM_IPADDR log_node_id_from_ipaddr


#endif

