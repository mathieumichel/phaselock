#include <stdio.h>

#include "contiki.h"
#include "net/netstack.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "dev/button-sensor.h"
#include "dev/cc2420/cc2420.h"
#include "dev/leds.h"
#include "powertrace.h"

#include "cooja-debug.h"

#define MIN_WAIT CLOCK_SECOND * 1
#define MAX_WAIT CLOCK_SECOND * 3



#if USING_COOJA
#define PRINTF cooja_debug
#else
#define PRINTF printf
#endif

/*---------------------------------------------------------------------------*/
PROCESS(net_test_process, "Net test");
AUTOSTART_PROCESSES(&net_test_process);
/*---------------------------------------------------------------------------*/
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{
  char *msg;

  msg = (char *)packetbuf_dataptr();
  msg[packetbuf_datalen()] = '\0';

  PRINTF("RX %d.%d %s\n", from->u8[0], from->u8[1], msg);
}
static const struct unicast_callbacks unicast_callbacks = {recv_uc};
static struct unicast_conn uc;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(net_test_process, ev, data)
{
  static struct etimer et;
  linkaddr_t addr;
  char buf[128];
  int len;
  clock_time_t interval;
  static unsigned msg_counter;

  PROCESS_EXITHANDLER(unicast_close(&uc);)

  PROCESS_BEGIN();


#if !USING_COOJA
 //cc2420_set_cca_threshold(-25);

 cc2420_set_txpower(31);
 cc2420_set_cca_threshold(-32-14);
#endif
  unicast_open(&uc, 115, &unicast_callbacks);

  etimer_set(&et, MAX_WAIT * 2);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

  while(1) {
    interval = MIN_WAIT + (random_rand() % (MAX_WAIT - MIN_WAIT));
    etimer_set(&et, interval);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

#if USING_COOJA
    addr.u8[0] = 1;
    addr.u8[1] = 0;
#else
    addr.u8[0] = 1;
    addr.u8[1] = 0;
#endif /* USING_COOJA */

    if(!linkaddr_cmp(&addr, &linkaddr_node_addr)) {
      /* Add padding to make the link-layer packet 67 bytes. */
      len = snprintf(buf, sizeof(buf),
                     "MSG %u 12345678901234567890123456789012345678901",
                     msg_counter++);
      PRINTF("TX %s (len  = %d) \n", buf, len);
      packetbuf_copyfrom(buf, len);

      unicast_send(&uc, &addr);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
