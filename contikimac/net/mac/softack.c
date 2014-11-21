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


#if WITH_STRAWMAN
#define AUX_LEN (0 + 2) //(CHECKSUM_LEN + FOOTER_LEN)
static volatile int straw_code_competing=0;

static int last_vote_len = 0;
//static uint8_t *probebuf, probelen = 0;
struct strawman_hdr {
  uint8_t type; /* packet type */
  uint8_t seq; /* data sequence number, also used to ack data */ /* XXX Fairness? */
  rimeaddr_t sender; /* packet sender/source */
  rimeaddr_t ack; /* optional: data ack address, used in TYPE_VOTE_PROBE */
  rimeaddr_t receiver; /* packet receiver/destination (TYPE_DATA) OR size estimation*/
};

unsigned char is_competing(void){
  return straw_code_competing;
}

//int get_vote_len(void){
//  return last_vote_len;
//}

void reset_competition(){
    straw_code_competing=0;
}
#endif /* WITH_STRAWMAN */




rtimer_clock_t phaselock_target;
extern volatile rtimer_clock_t current_cycle_start_time;
/* A buffer where extended 802.15.4 are prepared */
static unsigned char ackbuf[3 + 10] = {0x02, 0x00};
static unsigned char votebuf[3 + 8] = {0x00, 0x00};
static unsigned char probebuf[3 + 8] = {0x07, 0x00};
static unsigned char signalbuf[3 + 9]= {0x04, 0x00};

//static unsigned char probebuf[2 + 8] = {0x07, 0x00};
/* Seqno of the last acked frame */
static uint8_t last_acked_seqno = -1;
uint8_t got_ack=0;


/*---------------------------------------------------------------------------*/
#if  WITH_STRAWMAN
static int get_random_length(void) {
  int len;
  int r;
#define UNIFORM 0
#if UNIFORM
  len = ((random_rand()/2)%VOTE_MAX_LENGTH);
  len *= VOTE_INTERVAL;
  len %= VOTE_MAX_LENGTH;
#else /* UNIFORM */
  /* GEOMETRIC DISTRIBUTION (p=0.798):
   * This appears to be the optimal values for 2 contenders! */
  r = (random_rand()/3)%1024;
  if (r <= 209) {
    len = 0;
  } else if (r <= 377) {
    len = 7;
  } else if (r <= 511) {
    len = 14;
  } else if (r <= 618) {
    len = 21;
  } else if (r <= 704) {
    len = 28;
  } else if (r <= 772) {
    len = 35;
  } else if (r <= 827) {
    len = 42;
  } else if (r <= 871) {
    len = 49;
  } else if (r <= 906) {
    len = 56;
  } else if (r <= 935) {
    len = 63;
  } else if (r <= 957) {
    len = 70;
  } else if (r <= 975) {
    len = 77;
  } else if (r <= 989) {
    len = 84;
  } else if (r <= 1001) {
    len = 91;
  } else if (r <= 1010) {
    len = 98;
  } else if (r <= 1018) {
    len = 105;
  } else if (r <= 1024 /*duh!*/) {
    len = 112;
  } else {
    len = -1;
  }
#endif /* UNIFORM */

  return len;
}



/*---------------------------------------------------------------------------*/
#endif  /* WITH_STRAWMAN */
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
  is_signal = (fcf & 7) ==4;
  is_probe = (fcf & 7) == 7;
  ack_required = (fcf >> 5) & 1;
  dest_addr = frame + 3 + 2;
  seqno = frame[2];
  *code=SOFTACK_NULL;//by default nothing has to be done


  if(is_ack && contikimac_sending()){
    rimeaddr_t dest;
    memcpy(&dest, frame+3, 8);
    //COOJA_DEBUG_PRINTF("softack : ack from %u %u\n",node_id_from_rimeaddr(&dest),fcf);
    //COOJA_DEBUG_PRINTF("softack : ack from %u %u\n",frame[10],fcf);//id for cooja
    if(rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                      &dest) && contikimac_sending()){
     got_ack=1;
     uint16_t phase;
     memcpy(&phase,frame+11,2);
     phaselock_target=phase;
     COOJA_DEBUG_PRINTF("softack: ack from %u\n",node_id_from_rimeaddr(&dest));
     //printf("softack : ack from %u phase %lu\n",node_id_from_rimeaddr(&dest),\n(,unsigned long)((unsigned long)phase* 1000/RTIMER_ARCH_SECOND));
    }
    else{//we don't have sent the packet data we don't have to handle the phase
      //COOJA_DEBUG_PRINTF("softack : ack from %u \n",node_id_from_rimeaddr(&dest));
    }
    do_ack=0;
  }

#if WITH_STRAWMAN
  else if(is_probe){
    if(contikimac_sending()){
      straw_code_competing++;
      if(! contikimac_broadcasting()){
        last_vote_len=get_random_length();
        *ackbufptr = votebuf;
        *acklen = sizeof(votebuf)+last_vote_len;
        votebuf[2]=seqno;
        rimeaddr_copy((rimeaddr_t*)(votebuf+3), &rimeaddr_node_addr);
        COOJA_DEBUG_PRINTF("straw: coll %u \n",last_vote_len);
        *code=SOFTACK_VOTE;
        do_vote=1;
        //contikimac handle after checking is_competing
      }
      else{
        COOJA_DEBUG_PRINTF("straw: coll bcast\n");
      }
    }
    else{
      COOJA_DEBUG_PRINTF("straw: coll wtf\n");
    }
  }
  else if(is_signal){
    if(is_competing()){
      uint8_t len;
      len=frame[11];
      COOJA_DEBUG_PRINTF("straw: result %u\n",len);
      reset_competition();
    }
  }
#endif /* WITH_STRAWMAN */

  else if(is_data) {
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

  if(do_ack) { /* Prepare ack */
    //leds_on(LEDS_RED);
      uint16_t phase = 0;
      *ackbufptr = ackbuf;
      *acklen = sizeof(ackbuf);
      ackbuf[2] = seqno;
      /* Append our address to the standard 802.15.4 ack */
      rimeaddr_copy((rimeaddr_t*)(ackbuf+3), &rimeaddr_node_addr);
      /* Append our rank to the ack */
      if(node_id!= 1 && RTIMER_CLOCK_LT(encounter_time, current_cycle_start_time)){ //not needed for the root
      phase=encounter_time-current_cycle_start_time;//WHAT IF nodes wake up planned during this reception?
      }
      memcpy(ackbuf+11,&phase, 2);//two bytes needed to store the phase info
      //ackbuf[3+8] = phase & 0xff;
      //ackbuf[3+8+1] = (curr_edc >> 8)& 0xff;
      COOJA_DEBUG_PRINTF("phase %lu\n",(unsigned long)((unsigned long)phase* 100*1000/RTIMER_ARCH_SECOND));
      *code=SOFTACK_ACK;
      //leds_off(LEDS_RED);
    } else if(!do_vote) {
      *acklen = 0;
    }
}

/*---------------------------------------------------------------------------*/
#if WITH_STRAWMAN
rtimer_clock_t wt;
static void
softack_coll_callback(uint8_t **probebufptr, uint8_t *probelen)
{
  if(contikimac_checking()){
    wt=RTIMER_NOW();
    while(RTIMER_CLOCK_LT(RTIMER_NOW(),(wt  +(RTIMER_ARCH_SECOND / 5000 )))){};
    *probebufptr = probebuf;
    *probelen = sizeof(probebuf);
    probebuf[2]=42;
    rimeaddr_copy((rimeaddr_t*)(probebuf+3), &rimeaddr_node_addr);
    COOJA_DEBUG_PRINTF("straw: probe \n");
  }
  else{
    *probelen=0;
  }
}

static void
softack_vote_callback(uint8_t **signalbufptr, uint8_t *signallen, uint8_t len)
{
    *signalbufptr = signalbuf;
    *signallen = sizeof(signalbuf);
    signalbuf[2]=42;
    rimeaddr_copy((rimeaddr_t*)(signalbuf+3), &rimeaddr_node_addr);
    signalbuf[11]=len;
    COOJA_DEBUG_PRINTF("straw: signal %u\n",len);
}
#endif /* WITH_STRAWMAN */

/*---------------------------------------------------------------------------*/

/* Anycast-specific inits */
void
softack_init()
{
  /* Subscribe to 802.15.4 softack driver */
#if WITH_STRAWMAN
  cc2420_softack_subscribe_strawman(softack_input_callback,softack_coll_callback,softack_vote_callback);
 // preload_voteprobe();
#else
  cc2420_softack_subscribe(orpl_softack_input_callback);
#endif /* WITH_STRAWMAN */
}

