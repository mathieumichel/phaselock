#include "contiki.h"
#include "deployment.h"
#include "net/rpl/rpl.h"
#include "net/rpl/rpl-private.h"
#include "net/packetbuf.h"
#include "tools/rpl-log.h"
#include <stdio.h>
#include <string.h>

static int neighbor_set_size = 0;


/*---------------------------------------------------------------------------*/
/* Copy an appdata to another with no assumption that the addresses are aligned */
void
appdata_copy(struct app_data *dst, struct app_data *src)
{
  int i;
  for(i=0; i<sizeof(struct app_data); i++) {
    ((char*)dst)[i] = (((char*)src)[i]);
  }
}

/*---------------------------------------------------------------------------*/

/* Get dataptr from the packet currently in uIP buffer */
struct app_data *
appdataptr_from_uip()
{
  struct app_data *ptr;
  struct app_data data;
  if(uip_len < sizeof(struct app_data)) return NULL;
  ptr = (struct app_data *)((char*)uip_buf + ((uip_len - sizeof(struct app_data))));
  appdata_copy(&data, ptr);
  if(data.magic == ORPL_LOG_MAGIC) {
    return ptr;
  } else {
    return NULL;
  }
}

/*---------------------------------------------------------------------------*/

/* Get dataptr from the current packetbuf */
struct app_data *
appdataptr_from_packetbuf()
{
  struct app_data *ptr;
  struct app_data data;
  if(packetbuf_datalen() < sizeof(struct app_data)){

    return NULL;
  }
  ptr = (struct app_data *)((char*)packetbuf_dataptr() + ((packetbuf_datalen() - sizeof(struct app_data))));
  appdata_copy(&data, ptr);

  if(data.magic == ORPL_LOG_MAGIC) {
    return ptr;
  } else {
    return NULL;
  }
}

/*---------------------------------------------------------------------------*/
void log_appdataptr(struct app_data *dataptr) {

  struct app_data data;
    int curr_dio_interval = default_instance != NULL ? default_instance->dio_intcurrent : 0;
  int curr_rank = default_instance != NULL ? default_instance->current_dag->rank : 0xffff;

  if(dataptr) {
    appdata_copy(&data, dataptr);

    printf(" [%lx %u_%u %u->%u]",
        data.seqno,
        data.hop,
        data.fpcount,
        data.src,
        data.dest
          );

  }

  printf(" {%u/%u %u %u} \n",
 				1,
        neighbor_set_size,
        curr_rank,
        curr_dio_interval
        );

}

uint16_t
log_node_id_from_rimeaddr(const void *rimeaddr)
{
  return node_id_from_rimeaddr((const linkaddr_t *)rimeaddr);
}

/* Return node id from its IP address */
uint16_t
log_node_id_from_ipaddr(const void *ipaddr)
{
  return node_id_from_ipaddr((const uip_ipaddr_t *)ipaddr);
}


PROCESS(rpl_log_process, "RPL Log");
/* Starts logging process */
void
rpl_log_start() {
  process_start(&rpl_log_process, NULL);
}

/* The logging process */
PROCESS_THREAD(rpl_log_process, ev, data)
{
  static struct etimer periodic;
  PROCESS_BEGIN();
  //etimer_set(&periodic, 60 * CLOCK_SECOND);
  etimer_set(&periodic, 2 * 60 * CLOCK_SECOND);//put to 120 second (send_interval) to have same check interval than orpl
  //simple_energest_start();

  while(1) {
    static int cnt = 0;
    neighbor_set_size = uip_ds6_nbr_num();

    PROCESS_WAIT_UNTIL(etimer_expired(&periodic));
    etimer_reset(&periodic);
    simple_energest_step();


  }

  PROCESS_END();
}



