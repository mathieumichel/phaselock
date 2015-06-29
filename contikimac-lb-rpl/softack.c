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


#include "../contikimac-lb-rpl/softack.h"

#include "contikimac.h"
#include "net/packetbuf.h"
#include "net/mac/frame802154.h"
#include "dev/leds.h"
#include <string.h>

#include "cc2420-softack.h"
#include "tools/cooja-debug.h"
#include "rtimer-arch.h"
#include "node-id.h"
#include "linkaddr.h"



#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTDEBUG(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#define PRINTDEBUG(...)
#endif
static void softack_acked_callback(const uint8_t *buf, uint8_t len);
static void softack_input_callback(const uint8_t *buf, uint8_t len, uint8_t **ackbufptr, uint8_t *acklen);

extern rtimer_clock_t phaselock_target;
extern uint32_t cycle_target;

extern uint32_t cycle_time;
extern volatile rtimer_clock_t current_cycle_start_time;
static unsigned char ackbuf[3 + 10 + 4] = {0x02, 0x00};
/* Seqno of the last acked frame */
static uint8_t last_acked_seqno = -1;


/* Called for every incoming frame from interrupt if concerned we ack and then add our phase*/
static void
softack_input_callback(const uint8_t *frame, uint8_t framelen, uint8_t **ackbufptr, uint8_t *acklen)
{
  rtimer_clock_t encounter_time = RTIMER_NOW();
  uint8_t fcf, is_data, is_ack, ack_required, seqno;
  uint8_t *dest_addr = NULL;
  int do_ack = 0;
  int do_vote=0;

  fcf = frame[0];
  is_data = (fcf & 7) == 1;
  is_ack = (fcf & 7) == 2;
  ack_required = (fcf >> 5) & 1;
  dest_addr = frame + 3 + 2;
  seqno = frame[2];
  *acklen=0;
  if(is_ack){
    linkaddr_t dest;
    memcpy(&dest, frame+3, 8);
    if(linkaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                      &dest)){
     memcpy(&cycle_target,frame+13,4);
     memcpy(&phaselock_target,frame+11,2);

    }
    do_ack=0;
  }


  else if(is_data) {
    if(ack_required) {
      uint8_t dest_addr_host_order[8];
      int i;
      /* Convert from 802.15.4 little endian to Contiki's big-endian addresses */
      for(i=0; i<8; i++) {
        dest_addr_host_order[i] = dest_addr[7-i];
      }
     // printf("got data1\n");
      if(linkaddr_cmp((linkaddr_t*)dest_addr_host_order, &linkaddr_node_addr)){
        /* Unicast, for us */
        //printf("got data2\n");
        do_ack = 1;

      }
    }
  }

  if(do_ack) { /* Prepare ack */
    //leds_on(LEDS_RED);
      uint16_t phase = 0;
      *ackbufptr = ackbuf;
      *acklen = sizeof(ackbuf);
//      ackbuf[0]=0x02;
//      ackbuf[1]=0x00;
      ackbuf[2] = seqno;
      /* Append our address to the standard 802.15.4 ack */
      linkaddr_copy((linkaddr_t*)(ackbuf+3), &linkaddr_node_addr);
      /* Append the phase info to the ack */
      if(node_id!= 1){ //not needed for the root
        if(RTIMER_CLOCK_LT(current_cycle_start_time,encounter_time)){
          phase=encounter_time-current_cycle_start_time;//WHAT IF nodes wake up planned during this reception?
        }
        else{
          PRINTF("mouahahaha\n");
        }
      }
      //memcpy(ackbuf+7,&cycle_time,4);
      //memcpy(ackbuf+11,&phase, 2);//two bytes needed to store the phase info
      ackbuf[3+8] = phase & 0xff;
      ackbuf[3+8+1] = (phase >> 8)& 0xff;
      ackbuf[13] = cycle_time & 0xff;
      ackbuf[14] = (cycle_time >> 8) & 0xff;
      ackbuf[15] = (cycle_time >> 16) & 0xff;
      ackbuf[16] = (cycle_time >> 24) & 0xff;
    } else if(!do_vote) {
      *acklen = 0;
    }
}

static void
softack_acked_callback(const uint8_t *frame, uint8_t framelen)
{
  last_acked_seqno = frame[2];
}

/*---------------------------------------------------------------------------*/

/* Anycast-specific inits */
void
softack_init()
{
  cc2420_softack_subscribe(softack_input_callback,softack_acked_callback);
}

