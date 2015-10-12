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
#include "contikimac.h"
#include "net/packetbuf.h"
#include "cc2420-softack.h"
#include "net/mac/frame802154.h"
#include "dev/leds.h"
#include <string.h>
#include "deployment.h"
#include "cooja-debug.h"
#include "rtimer-arch.h"
#include "node-id.h"

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
static void softack_input_callback(const uint8_t *buf, uint8_t len, uint8_t **ackbufptr, uint8_t *acklen, uint8_t *code);

rtimer_clock_t phaselock_target;
extern volatile rtimer_clock_t current_cycle_start_time;
static unsigned char ackbuf[3 + 10] = {0x02, 0x00};
/* Seqno of the last acked frame */
static uint8_t last_acked_seqno = -1;


/* Called for every incoming frame from interrupt if concerned we ack and then add our phase*/
static void
softack_input_callback(const uint8_t *frame, uint8_t framelen, uint8_t **ackbufptr, uint8_t *acklen, uint8_t *code)
{
  rtimer_clock_t encounter_time = RTIMER_NOW();
  uint8_t fcf, is_data, is_ack, ack_required, is_probe, is_signal, seqno;
  uint8_t *dest_addr = NULL;
  int do_ack = 0;
  int do_vote=0;

  fcf = frame[0];
  is_data = (fcf & 7) == 1;
  is_ack = (fcf & 7) == 2;
  is_signal = (fcf & 7) ==3;
  is_probe = (fcf & 7) == 7;
  ack_required = (fcf >> 5) & 1;
  dest_addr = frame + 3 + 2;
  seqno = frame[2];
  *code=SOFTACK_NULL;//by default nothing has to be done


  if(is_ack && contikimac_sending() && !contikimac_broadcasting()){
    rimeaddr_t dest;

    memcpy(&dest, frame+3, 8);
    //COOJA_DEBUG_PRINTF("softack : ack from %u %u\n",node_id_from_rimeaddr(&dest),fcf);
    //COOJA_DEBUG_PRINTF("softack : ack from %u %u\n",frame[10],fcf);//id for cooja
    if(rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                      &dest)){
     //got_ack=1;
     memcpy(&phaselock_target,frame+11,2);
     //COOJA_DEBUG_PRINTF("ack\n");
     //COOJA_DEBUG_PRINTF("softack: ack from %u-%lu\n",node_id_from_rimeaddr(&dest),(unsigned long)((unsigned long)phaselock_target* 100*1000/RTIMER_ARCH_SECOND));
     //printf("softack : ack from %u phase %lu\n",node_id_from_rimeaddr(&dest),\n(,unsigned long)((unsigned long)phase* 1000/RTIMER_ARCH_SECOND));
    }
    else{//we don't have sent the packet data we don't have to handle the phase
      //COOJA_DEBUG_PRINTF("softack : ack from %u \n",node_id_from_rimeaddr(&dest));
    }
    do_ack=0;
  }


  else if(is_data) {
	  printf("data here\n");
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
    }
    else{
      //COOJA_DEBUG_PRINTF("softack : bcast %u\n",fcf);
    }
  }

  if(do_ack) { /* Prepare ack */
    //leds_on(LEDS_RED);
      uint16_t phase = 0;
      *ackbufptr = ackbuf;
      *acklen = sizeof(ackbuf);
      ackbuf[0]=0x02;
      ackbuf[1]=0x00;
      ackbuf[2] = seqno;
      /* Append our address to the standard 802.15.4 ack */
      rimeaddr_copy((rimeaddr_t*)(ackbuf+3), &rimeaddr_node_addr);
      /* Append our rank to the ack */
      if(node_id!= 1){ //not needed for the root
        if(RTIMER_CLOCK_LT(current_cycle_start_time,encounter_time)){
          phase=encounter_time-current_cycle_start_time;//WHAT IF nodes wake up planned during this reception?
        }
        else{
          COOJA_DEBUG_PRINTF("mouahahaha\n");
        }
      }
      //memcpy(ackbuf+11,&phase, 2);//two bytes needed to store the phase info
      ackbuf[3+8] = phase & 0xff;
      ackbuf[3+8+1] = (phase >> 8)& 0xff;
      //COOJA_DEBUG_PRINTF("phase %lu\n",(unsigned long)((unsigned long)phase* 100*1000/RTIMER_ARCH_SECOND));
      *code=SOFTACK_ACK;
      //leds_off(LEDS_RED);
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

