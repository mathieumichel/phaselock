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


#if WITH_STRAWMAN
#define AUX_LEN (0 + 2) //(CHECKSUM_LEN + FOOTER_LEN)
extern volatile unsigned char we_are_checking;
extern volatile unsigned char we_are_sending;
extern volatile unsigned char we_are_broadcasting;
int straw_code_competing=0;
int straw_code_winner=0;
static int vote_loaded=0;

static uint8_t *probebuf, probelen = 0;
struct strawman_hdr {
  uint8_t type; /* packet type */
  uint8_t seq; /* data sequence number, also used to ack data */ /* XXX Fairness? */
  rimeaddr_t sender; /* packet sender/source */
  rimeaddr_t ack; /* optional: data ack address, used in TYPE_VOTE_PROBE */
  rimeaddr_t receiver; /* packet receiver/destination (TYPE_DATA) OR size estimation*/
};

int is_competing(){
  return straw_code_competing;
}

void set_competing(int mode){
  if(is_competing() != mode){
    COOJA_DEBUG_PRINTF("straw: compet mode %u\n",mode);
    straw_code_competing=mode;
  }
}
#endif /* WITH_STRAWMAN */




rtimer_clock_t phaselock_target;
extern volatile rtimer_clock_t current_cycle_start_time;
/* A buffer where extended 802.15.4 are prepared */
static unsigned char ackbuf[3 + 10] = {0x02, 0x00};

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

static uint16_t
busywait_until_sfd_false()
{
#undef UNTIL_MAX_SAMPLES
#define UNTIL_MAX_SAMPLES 900 /* TODO Calibrate */

  int samples=0;

  /* Busy-wait until SFD is false */
  while (NETSTACK_RADIO.receiving_packet()) {
    samples++;
    if (samples >= UNTIL_MAX_SAMPLES) {
      break;
    }
  }

  if (samples >= UNTIL_MAX_SAMPLES) {
    COOJA_DEBUG_PRINTF("error: SFD sampling failed");
    return -1;
  }
  return samples;
}

/*---------------------------------------------------------------------------*/

static uint16_t
busywait_until_sfd_true()
{
#undef UNTIL_MAX_SAMPLES
#define UNTIL_MAX_SAMPLES 900 /* TODO Calibrate */

  int samples=0;

  /* Busy-wait until SFD is true */
  while ((!NETSTACK_RADIO.receiving_packet()) && samples++ <= UNTIL_MAX_SAMPLES);


  if (samples >= UNTIL_MAX_SAMPLES) {
    COOJA_DEBUG_PRINTF("error: sfd never true");
    return -1;
  }
  return samples;
}

/*---------------------------------------------------------------------------*/

static uint16_t
busywait_until_cca_true()
{
#undef UNTIL_MAX_SAMPLES
#define UNTIL_MAX_SAMPLES 800 /* TODO Calibrate */

  int samples=0;
leds_on(LEDS_BLUE);
  /* Busy-wait until CCA is true */
  while (!CC2420_CCA_IS_1) {
    samples++;
    if (samples >= UNTIL_MAX_SAMPLES) {
      break;
    }
  }
  leds_off(LEDS_BLUE);
  if (samples >= UNTIL_MAX_SAMPLES) {
    return -1;
  }

  return samples;
}

/*---------------------------------------------------------------------------*/

static int
samples_to_bytes(uint16_t samples)
{
#define SAMPLES_MIN 30 /* XXX Should be increased by 8 from 22 according to calib! */
  //#define SAMPLES_MIN 70 /* 2011-03.17. Contiki timers changed. */
#define SAMPLES_INTERVAL 35 /* samples per vote interval */
#define VOTE_INTERVAL 7
  if (samples < SAMPLES_MIN) {
    return -2;
  }
  /*printf("P --\n");*/
  /*printf("P got %d\n", samples);*/
  samples -= SAMPLES_MIN;
  /*printf("P -= %d\n", samples);*/
  samples /= SAMPLES_INTERVAL;
  /*printf("P /= %d\n", samples);*/
  /*printf("P ret %d\n", (samples*VOTE_INTERVAL));*/
  return samples*VOTE_INTERVAL;
}

/*---------------------------------------------------------------------------*/


static void
preload_voteprobe(uint8_t **probebufptr,uint8_t *probelen)
{
        packetbuf_clear();
        struct strawman_hdr *hdr =  packetbuf_dataptr();;
        hdr->type=FRAME802154_BEACONREQ;
        rimeaddr_copy(&hdr->sender, &rimeaddr_node_addr);
        rimeaddr_copy(&hdr->receiver, &rimeaddr_null);
        packetbuf_set_datalen(sizeof(struct strawman_hdr));
        /* Append our address to the standard 802.15.4 ack */
        NETSTACK_RADIO.prepare(packetbuf_hdrptr(), packetbuf_totlen());
        /* Append our rank to the ack */
        COOJA_DEBUG_PRINTF("straw: probe loaded %u\n",sizeof(struct strawman_hdr));
}

/*---------------------------------------------------------------------------*/

static int last_vote_len = -1;
static uint8_t tmp_buffer[128];
struct strawman_hdr* hdr;
static int preload_vote(void)//can't use packetbuf
{
  /* Function is executed from interrupt; it must not acccess packetbuf! */
  packetbuf_clear();
 hdr = (struct strawman_hdr*)tmp_buffer;
  hdr->type=FRAME802154_BEACONFRAME;
  rimeaddr_copy(&hdr->sender, &rimeaddr_node_addr);
  rimeaddr_copy(&hdr->receiver, &rimeaddr_null);
  last_vote_len = get_random_length();
  NETSTACK_RADIO.prepare(tmp_buffer, sizeof(struct strawman_hdr) + last_vote_len);
  return last_vote_len;
}

/*---------------------------------------------------------------------------*/

static int
rimac_wait(int req_bytes)
{
  int samples=0;

  while (req_bytes > 0) {
    if (!CC2420_CCA_IS_1) {
      return 0; /* detected energy */
    }
    req_bytes--;
    clock_delay(57);
  }
  return 1; /* winner */
}
#endif /* WITH_STRAWMAN */
/*---------------------------------------------------------------------------*/
/* Called for every incoming frame from interrupt if concerned we ack and then add our phase*/
rtimer_clock_t t;
static void
orpl_softack_input_callback(const uint8_t *frame, uint8_t framelen, uint8_t **ackbufptr, uint8_t *acklen)
{
  rtimer_clock_t encounter_time = RTIMER_NOW();
  uint8_t fcf, is_data, is_ack, ack_required, is_probe, seqno;
  uint8_t *dest_addr = NULL;
  int do_ack = 0;

  fcf = frame[0];
  is_data = (fcf & 7) == 1;
  is_ack = fcf == 2;
  is_probe = fcf == 7;
  ack_required = (fcf >> 5) & 1;
  dest_addr = frame + 3 + 2;
  seqno = frame[2];


  if(is_ack){
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

#if WITH_STRAWMAN
  else if(is_probe){
    //COOJA_DEBUG_PRINTF("straw: send vote");
    if(we_are_sending  && !is_competing() && !we_are_broadcasting){
      int len=0;
      int winner;
      straw_code_competing=1;
      len=preload_vote();
      NETSTACK_RADIO.transmit(0);
      COOJA_DEBUG_PRINTF("straw: vote loaded %u\n",len);//,((unsigned long)diff* 100000/RTIMER_ARCH_SECOND));
      t=RTIMER_NOW();
      while (RTIMER_CLOCK_LT(RTIMER_NOW(),t + RTIMER_ARCH_SECOND/750));
      straw_code_competing=0;
    }
//    else{
//      COOJA_DEBUG_PRINTF("straw: not interrested\n");
//    }
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
      if(node_id!= 1){ //not needed for the root
      phase=encounter_time-current_cycle_start_time;
      }
      memcpy(ackbuf+11,&phase, 2);//two bytes needed to store the phase info
      //ackbuf[3+8] = phase & 0xff;
      //ackbuf[3+8+1] = (curr_edc >> 8)& 0xff;
      COOJA_DEBUG_PRINTF("phase %lu\n",(unsigned long)((unsigned long)phase* 100*1000/RTIMER_ARCH_SECOND));
      //leds_off(LEDS_RED);
    } else {
      *acklen = 0;
    }
}

/*---------------------------------------------------------------------------*/
#if WITH_STRAWMAN
rtimer_clock_t wt1;
rtimer_clock_t wt2;
static void
softack_coll_callback()
{
  if(we_are_checking && !we_are_sending){
    //COOJA_DEBUG_PRINTF("straw: coll detected\n");
    wt1=RTIMER_NOW();
    RTIMER_CLOCK_LT(RTIMER_NOW(),(wt1  +(RTIMER_ARCH_SECOND / 5000 + (random_rand() %(RTIMER_ARCH_SECOND / 2500)))));
    preload_voteprobe(&probebuf,&probelen);
    NETSTACK_RADIO.transmit(0);
    //busywait_until_sfd_true(); /* wait for transmission to start */
    //busywait_until_sfd_false(); /* wait for transmission to finish */
    wt2=RTIMER_NOW();
    while (RTIMER_CLOCK_LT(RTIMER_NOW(),wt2 + RTIMER_ARCH_SECOND/750));


    int samples=0;
    leds_on(LEDS_BLUE);
    /* Busy-wait until CCA is true */
    while (samples<800 && CC2420_CCA_IS_1) {
      samples++;
    }
    if(samples!=800){
      leds_on(LEDS_GREEN);
      uint16_t cca_samples=1;
      while (cca_samples<800 && !CC2420_CCA_IS_1) {
        cca_samples++;
      }
      leds_off(LEDS_GREEN);
      leds_off(LEDS_BLUE);
      //int size=samples_to_bytes(cca_samples);
      int winner = cca_samples/13;
      if(winner <=26){
        winner =0;
      }
      else{
        winner=winner-26;
      }
      COOJA_DEBUG_PRINTF("straw: winner %u\n",winner);//-26 (size of struct hdr, -1 type - 1 longueur)
    }
  }
  else{
    //COOJA_DEBUG_PRINTF("straw: not checking\n");
  }
}
#endif /* WITH_STRAWMAN */

/*---------------------------------------------------------------------------*/

/* Anycast-specific inits */
void
softack_init()
{
  /* Subscribe to 802.15.4 softack driver */
#if WITH_STRAWMAN
  cc2420_softack_subscribe_strawman(orpl_softack_input_callback,softack_coll_callback);
#else
  cc2420_softack_subscribe(orpl_softack_input_callback);
#endif /* WITH_STRAWMAN */
}

