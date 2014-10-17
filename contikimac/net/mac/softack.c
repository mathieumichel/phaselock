/*
 * Copyright (c) 2014, University Of Mons.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         softack callback functions,
 * \author
 *         Mathieu MICHEL <mathieu.michel@umons.ac.be>
 */


#include "softack.h"
#include "net/packetbuf.h"
#include "cc2420-softack.h"
#include "net/mac/frame802154.h"
#include "dev/leds.h"
#include <string.h>
#include "deployment.h"
#include "cooja-debug.h"
#include "rtimer-arch.h"


rtimer_clock_t phaselock_target;
extern volatile rtimer_clock_t current_cycle_start_time;
/* A buffer where extended 802.15.4 are prepared */
static unsigned char ackbuf[3 + EXTRA_ACK_LEN] = {0x02, 0x00};
/* Seqno of the last acked frame */
static uint8_t last_acked_seqno = -1;
uint8_t got_ack=0;
/* Called for every incoming frame from interrupt if concerned we ack and then add our phase*/
static void
orpl_softack_input_callback(const uint8_t *frame, uint8_t framelen, uint8_t **ackbufptr, uint8_t *acklen)
{
  uint8_t fcf, is_data, is_ack, ack_required, seqno;
  uint8_t *dest_addr = NULL;
  int do_ack = 0;

  fcf = frame[0];
  is_data = (fcf & 7) == 1;
  is_ack = fcf == 2;
  ack_required = (fcf >> 5) & 1;
  dest_addr = frame + 3 + 2;
  seqno = frame[2];
  if(is_data) {
    if(ack_required) {
      uint8_t dest_addr_host_order[8];
      int i;
      /* Convert from 802.15.4 little endian to Contiki's big-endian addresses */
      for(i=0; i<8; i++) {
        dest_addr_host_order[i] = dest_addr[7-i];
      }

      if(rimeaddr_cmp((rimeaddr_t*)dest_addr_host_order, &rimeaddr_node_addr)){
        /* Unicast, for us */
        //COOJA_DEBUG_PRINTF("softack : data %u\n",fcf);
        do_ack = 1;
      }
      else if(!rimeaddr_cmp((rimeaddr_t*)dest_addr_host_order,
                      &rimeaddr_null)) {
        //COOJA_DEBUG_PRINTF("softack : data not for us %u\n",fcf);
        // Not anycast --> sleep to avoid overhearing
      }
    }
    else{
      //COOJA_DEBUG_PRINTF("softack : bcast %u\n",fcf);
    }
  }
  else if(is_ack){
    rimeaddr_t dest;
    memcpy(&dest, frame+3, 8);
    //COOJA_DEBUG_PRINTF("softack : ack from %u %u\n",node_id_from_rimeaddr(&dest),fcf);
    //COOJA_DEBUG_PRINTF("softack : ack from %u %u\n",frame[10],fcf);//id for cooja
    if(rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                      &dest)){
     got_ack=1;
     uint16_t phase;
     memcpy(&phase,frame+11,2);
     phaselock_target=phase;
     //printf("softack : ack from %u phase %lu\n",node_id_from_rimeaddr(&dest),\n(,unsigned long)((unsigned long)phase* 1000/RTIMER_ARCH_SECOND));
    }
    else{//we don't have sent the packet data we don't have to handle the phase
      //COOJA_DEBUG_PRINTF("softack : ack from %u \n",node_id_from_rimeaddr(&dest));
    }
    do_ack=0;
  }

  if(do_ack) { /* Prepare ack */
    leds_on(LEDS_RED);
      uint16_t phase = 0;
      *ackbufptr = ackbuf;
      *acklen = sizeof(ackbuf);
      ackbuf[2] = seqno;
      /* Append our address to the standard 802.15.4 ack */
      rimeaddr_copy((rimeaddr_t*)(ackbuf+3), &rimeaddr_node_addr);
      /* Append our rank to the ack */
      if(node_id!= 1){ //not needed for the root
      phase=RTIMER_NOW()-current_cycle_start_time;
      }
      memcpy(ackbuf+11,&phase, 2);//two bytes needed to store the phase info
      //ackbuf[3+8] = phase & 0xff;
      //ackbuf[3+8+1] = (curr_edc >> 8)& 0xff;
      COOJA_DEBUG_PRINTF("phase %lu\n",(unsigned long)((unsigned long)phase* 1000/RTIMER_ARCH_SECOND));
      leds_off(LEDS_RED);
    } else {
      *acklen = 0;
    }
}



/* Anycast-specific inits */
void
softack_init()
{
  /* Subscribe to 802.15.4 softack driver */
  cc2420_softack_subscribe(orpl_softack_input_callback);
}

