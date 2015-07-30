#include "contiki.h"
#include "node-id.h"
#include "simple-energest.h"
#include <stdio.h>
#include "deployment.h"
#include "tools/rpl-log.h"

static uint32_t last_tx, last_rx, last_time;
static uint32_t delta_tx, delta_rx, delta_time;
static uint32_t curr_tx, curr_rx, curr_time;


/*---------------------------------------------------------------------------*/
void simple_energest_start() {
  energest_flush();
  last_tx = energest_type_time(ENERGEST_TYPE_TRANSMIT);
  last_rx = energest_type_time(ENERGEST_TYPE_LISTEN);
  last_time = energest_type_time(ENERGEST_TYPE_CPU) + energest_type_time(ENERGEST_TYPE_LPM);
}

/*---------------------------------------------------------------------------*/
void simple_energest_step() {
    static uint16_t cnt;
    energest_flush();

    curr_tx = energest_type_time(ENERGEST_TYPE_TRANSMIT);
    curr_rx = energest_type_time(ENERGEST_TYPE_LISTEN);
    curr_time = energest_type_time(ENERGEST_TYPE_CPU) + energest_type_time(ENERGEST_TYPE_LPM);

    delta_tx = curr_tx - last_tx;
    delta_rx = curr_rx - last_rx;
    delta_time = curr_time - last_time;

    last_tx = curr_tx;
    last_rx = curr_rx;
    last_time = curr_time;


    uint32_t fraction = (100ul*(delta_tx+delta_rx))/delta_time;
    LOG_NULL("Duty Cycle: [%u %u] %8lu +%8lu /%8lu (%lu %%)",
                  node_id,
                  cnt++,
                  delta_tx, delta_rx, delta_time,
                  fraction
    );
}
