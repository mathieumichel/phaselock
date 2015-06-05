#include "deployment.h"
#include "contiki.h"
#include "lib/random.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/uip-debug.h"
#include "net/packetbuf.h"
#include "net/rpl/rpl.h"
#include "net/netstack.h"
#include "net/rpl/rpl-private.h"
#include "node-id.h"
#include "simple-energest.h"
#include "simple-udp.h"
#include "tools/rpl-log.h"

#if IN_UMONS
#include "dev/button-sensor.h"
#include "dev/light-sensor.h"
#endif

#include <stdio.h>
#include <string.h>

#define SEND_INTERVAL   (2 * 60 * CLOCK_SECOND)
#define UDP_PORT 1234

static struct simple_udp_connection unicast_connection;

/*---------------------------------------------------------------------------*/
PROCESS(unicast_sender_process, "ORPL -- Collect-only Application");
AUTOSTART_PROCESSES(&unicast_sender_process);
/*---------------------------------------------------------------------------*/
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  LOG_FROM_APPDATAPTR((struct app_data *)data,"App: received");//ORPL_LOG((struct app_data*)data);

}
/*---------------------------------------------------------------------------*/
void app_send_to(uint16_t id) {
  static unsigned int cpt;
  struct app_data data;
  uip_ipaddr_t dest_ipaddr;

  data.magic = ORPL_LOG_MAGIC;
  data.seqno = ((uint32_t)node_id << 16) + cpt;
  data.src = node_id;
  data.dest = id;
  data.hop = 0;
  data.fpcount = 0;

  //node_ip6addr(&dest_ipaddr, id);
  set_ipaddr_from_id(&dest_ipaddr, id);

  LOG_FROM_APPDATAPTR(&data,"App: sending");//ORPL_LOG(&data);
  simple_udp_sendto(&unicast_connection, &data, sizeof(data), &dest_ipaddr);
//  printf("to ");
//  LOG_IPADDR(&dest_ipaddr);
//  printf("\n");
  cpt++;
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_sender_process, ev, data)
{
  static struct etimer periodic_timer;
  static struct etimer send_timer;
  uip_ipaddr_t global_ipaddr;

  PROCESS_BEGIN();


  random_rand();
  rpl_log_start();
  if(node_id ==0) {
    NETSTACK_RDC.off(0);
    uint16_t mymac = rimeaddr_node_addr.u8[7] << 8 | rimeaddr_node_addr.u8[6];
    printf("Node id unset, my mac is 0x%04x\n", mymac);
    PROCESS_EXIT();
  }

  cc2420_set_txpower(RF_POWER);
  cc2420_set_cca_threshold(RSSI_THR);
  printf("App: %u starting\n", node_id);
  deployment_init(&global_ipaddr);
  //rpl_setup(node_id == ROOT_ID, node_id);
  simple_udp_register(&unicast_connection, UDP_PORT,
                      NULL, UDP_PORT, receiver);

  if(node_id == ROOT_ID) {
    uip_ipaddr_t my_ipaddr;
    set_ipaddr_from_id(&my_ipaddr, node_id);
    NETSTACK_RDC.off(1);
  }
  else {
    etimer_set(&periodic_timer,10 * 60 * CLOCK_SECOND);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    etimer_set(&periodic_timer, SEND_INTERVAL);


    while(1) {

      etimer_set(&send_timer, random_rand() % (SEND_INTERVAL));

      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&send_timer));

     // if(rpl_get_any_dag()!=NULL){
      if(default_instance != NULL) {
      app_send_to(ROOT_ID);
      }
      else{
        printf("App: not in DODAG\n");
      }

      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      etimer_reset(&periodic_timer);

    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
