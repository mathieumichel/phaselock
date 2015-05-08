/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
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
 *         Implementation of the ContikiMAC power-saving radio duty cycling protocol
 * \author
 *         Adam Dunkels <adam@sics.se>
 *         Niclas Finne <nfi@sics.se>
 *         Joakim Eriksson <joakime@sics.se>
 */

#include "contiki-conf.h"
#include "dev/leds.h"
#include "dev/radio.h"
#include "dev/watchdog.h"
#include "lib/random.h"
#include "net/mac/contikimac.h"
#include "net/netstack.h"
#include "net/rime.h"
#include "sys/compower.h"
#include "sys/pt.h"
#include "sys/rtimer.h"
#include "cooja-debug.h"
#include "frame802154.h"
#include "softack.h"
#include "tools/rpl-log.h"
#include <string.h>


volatile unsigned char we_are_broadcasting = 0;
#if WITH_STRAWMAN
volatile unsigned char we_are_checking = 0;
extern int coll_count,probe_count,probe_count_b;
#endif /* WITH_STRAWMAN */

/* TX/RX cycles are synchronized with neighbor wake periods */
#ifdef CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
#define WITH_PHASE_OPTIMIZATION      CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
#else /* CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION */
#define WITH_PHASE_OPTIMIZATION      1
#endif /* CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION */
/* Two byte header added to allow recovery of padded short packets */
/* Wireshark will not understand such packets at present */
#ifdef CONTIKIMAC_CONF_WITH_CONTIKIMAC_HEADER
#define WITH_CONTIKIMAC_HEADER       CONTIKIMAC_CONF_WITH_CONTIKIMAC_HEADER
#else
#define WITH_CONTIKIMAC_HEADER       1
#endif
/* More aggressive radio sleeping when channel is busy with other traffic */
#ifndef WITH_FAST_SLEEP
#define WITH_FAST_SLEEP              1
#endif
/* Radio does CSMA and autobackoff */
#ifndef RDC_CONF_HARDWARE_CSMA
#define RDC_CONF_HARDWARE_CSMA       0
#endif
/* Radio returns TX_OK/TX_NOACK after autoack wait */
#ifndef RDC_CONF_HARDWARE_ACK
#define RDC_CONF_HARDWARE_ACK        0
#endif
/* MCU can sleep during radio off */
#ifndef RDC_CONF_MCU_SLEEP
#define RDC_CONF_MCU_SLEEP           0
#endif

#if NETSTACK_RDC_CHANNEL_CHECK_RATE >= 64
#undef WITH_PHASE_OPTIMIZATION
#define WITH_PHASE_OPTIMIZATION 0
#endif

#if WITH_CONTIKIMAC_HEADER
#define CONTIKIMAC_ID 0x00

struct hdr {
  uint8_t id;
  uint8_t len;
};
#endif /* WITH_CONTIKIMAC_HEADER */

/* CYCLE_TIME for channel cca checks, in rtimer ticks. */
#ifdef CONTIKIMAC_CONF_CYCLE_TIME
#define CYCLE_TIME (CONTIKIMAC_CONF_CYCLE_TIME)
#else
#define CYCLE_TIME (RTIMER_ARCH_SECOND / NETSTACK_RDC_CHANNEL_CHECK_RATE)
#endif

/* CHANNEL_CHECK_RATE is enforced to be a power of two.
 * If RTIMER_ARCH_SECOND is not also a power of two, there will be an inexact
 * number of channel checks per second due to the truncation of CYCLE_TIME.
 * This will degrade the effectiveness of phase optimization with neighbors that
 * do not have the same truncation error.
 * Define SYNC_CYCLE_STARTS to ensure an integral number of checks per second.
 */
#if RTIMER_ARCH_SECOND & (RTIMER_ARCH_SECOND - 1)
#define SYNC_CYCLE_STARTS                    1
#endif

/* Are we currently receiving a burst? */
static int we_are_receiving_burst = 0;

/* INTER_PACKET_DEADLINE is the maximum time a receiver waits for the
   next packet of a burst when FRAME_PENDING is set. */
#define INTER_PACKET_DEADLINE               CLOCK_SECOND / 32

/* ContikiMAC performs periodic channel checks. Each channel check
   consists of two or more CCA checks. CCA_COUNT_MAX is the number of
   CCAs to be done for each periodic channel check. The default is
   two.*/
#ifdef CONTIKIMAC_CONF_CCA_COUNT_MAX
#define CCA_COUNT_MAX                      (CONTIKIMAC_CONF_CCA_COUNT_MAX)
#else
#define CCA_COUNT_MAX                      2
#endif

/* Before starting a transmission, Contikimac checks the availability
   of the channel with CCA_COUNT_MAX_TX consecutive CCAs */
#ifdef CONTIKIMAC_CONF_CCA_COUNT_MAX_TX
#define CCA_COUNT_MAX_TX                   (CONTIKIMAC_CONF_CCA_COUNT_MAX_TX)
#else
#define CCA_COUNT_MAX_TX                   6
#endif

/* CCA_CHECK_TIME is the time it takes to perform a CCA check. */
/* Note this may be zero. AVRs have 7612 ticks/sec, but block until cca is done */
#ifdef CONTIKIMAC_CONF_CCA_CHECK_TIME
#define CCA_CHECK_TIME                     (CONTIKIMAC_CONF_CCA_CHECK_TIME)
#else
#define CCA_CHECK_TIME                     RTIMER_ARCH_SECOND / 8192
#endif

/* CCA_SLEEP_TIME is the time between two successive CCA checks. */
/* Add 1 when rtimer ticks are coarse */
#if RTIMER_ARCH_SECOND > 8000
#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 600
#else
#define CCA_SLEEP_TIME                     (RTIMER_ARCH_SECOND / 600) + 1
#endif

/* CHECK_TIME is the total time it takes to perform CCA_COUNT_MAX
   CCAs. */
#define CHECK_TIME                         (CCA_COUNT_MAX * (CCA_CHECK_TIME + CCA_SLEEP_TIME))

/* CHECK_TIME_TX is the total time it takes to perform CCA_COUNT_MAX_TX
   CCAs. */
#define CHECK_TIME_TX                      (CCA_COUNT_MAX_TX * (CCA_CHECK_TIME + CCA_SLEEP_TIME))

/* LISTEN_TIME_AFTER_PACKET_DETECTED is the time that we keep checking
   for activity after a potential packet has been detected by a CCA
   check. */
#define LISTEN_TIME_AFTER_PACKET_DETECTED  RTIMER_ARCH_SECOND / 80

/* MAX_SILENCE_PERIODS is the maximum amount of periods (a period is
   CCA_CHECK_TIME + CCA_SLEEP_TIME) that we allow to be silent before
   we turn of the radio. */
#define MAX_SILENCE_PERIODS                5

/* MAX_NONACTIVITY_PERIODS is the maximum number of periods we allow
   the radio to be turned on without any packet being received, when
   WITH_FAST_SLEEP is enabled. */
#define MAX_NONACTIVITY_PERIODS            10



/* STROBE_TIME is the maximum amount of time a transmitted packet
   should be repeatedly transmitted as part of a transmission. */
#define STROBE_TIME                        (CYCLE_TIME + 2 * CHECK_TIME)

/* GUARD_TIME is the time before the expected phase of a neighbor that
   a transmitted should begin transmitting packets. */
#if WITH_ADVANCED_PHASELOCK
//#define GUARD_TIME                         10 * CHECK_TIME + CHECK_TIME_TX //(10 * (CHECK_TIME + CHECK_TIME_TX)) * 4  / 5  //not too small so thre receiver can be waked up with a fragment of strobe
#define GUARD_TIME                  ((CHECK_TIME + CHECK_TIME_TX) * 3)/5 + (random_rand() %(((CHECK_TIME + CHECK_TIME_TX) * 2)/5))
#else /* WITH_ADVANCED_PHASELOCK */
#define GUARD_TIME                         10 * CHECK_TIME + CHECK_TIME_TX  //MF prev (10 * CHECK_TIME + CHECK_TIME_TX)
#endif /* WITH_ADVANCED_PHASELOCK */

/* INTER_PACKET_INTERVAL is the interval between two successive packet transmissions */
#ifdef CONTIKIMAC_CONF_INTER_PACKET_INTERVAL
#define INTER_PACKET_INTERVAL              CONTIKIMAC_CONF_INTER_PACKET_INTERVAL
#else
#define INTER_PACKET_INTERVAL              RTIMER_ARCH_SECOND / 2500
#endif

/* AFTER_ACK_DETECTECT_WAIT_TIME is the time to wait after a potential
   ACK packet has been detected until we can read it out from the
   radio. */
#ifdef CONTIKIMAC_CONF_AFTER_ACK_DETECTECT_WAIT_TIME
#define AFTER_ACK_DETECTECT_WAIT_TIME      CONTIKIMAC_CONF_AFTER_ACK_DETECTECT_WAIT_TIME
#else
#define AFTER_ACK_DETECTECT_WAIT_TIME      RTIMER_ARCH_SECOND / 1500
#endif

/* MAX_PHASE_STROBE_TIME is the time that we transmit repeated packets
   to a neighbor for which we have a phase lock. */
#if WITH_ADVANCED_PHASELOCK
#define MAX_PHASE_STROBE_TIME              RTIMER_ARCH_SECOND / 60//20 // MF-prev=60
#else
#define MAX_PHASE_STROBE_TIME              RTIMER_ARCH_SECOND / 20 //due to the change in cca_sleep_time for softack
#endif

/* SHORTEST_PACKET_SIZE is the shortest packet that ContikiMAC
   allows. Packets have to be a certain size to be able to be detected
   by two consecutive CCA checks, and here is where we define this
   shortest size.
   Padded packets will have the wrong ipv6 checksum unless CONTIKIMAC_HEADER
   is used (on both sides) and the receiver will ignore them.
   With no header, reduce to transmit a proper multicast RPL DIS. */
#ifdef CONTIKIMAC_CONF_SHORTEST_PACKET_SIZE
#define SHORTEST_PACKET_SIZE  CONTIKIMAC_CONF_SHORTEST_PACKET_SIZE
#else
#define SHORTEST_PACKET_SIZE               125
#endif


#define ACK_LEN 3 + 10//EXTRA_ACK_LEN
#define PROBE_LEN 26//EXTRA_ACK_LEN
#include <stdio.h>
static struct rtimer rt;
static struct pt pt;

static volatile uint8_t contikimac_is_on = 0;
volatile uint8_t contikimac_keep_radio_on = 0;

static volatile unsigned char we_are_sending = 0;
static volatile unsigned char radio_is_on = 0;

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTF2(...) printf(__VA_ARGS__)
#define PRINTDEBUG(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#define PRINTF_MIN(...) printf(__VA_ARGS__)
#define PRINTDEBUG(...)
#endif

#if CONTIKIMAC_CONF_COMPOWER
static struct compower_activity current_packet;
#endif /* CONTIKIMAC_CONF_COMPOWER */

#if WITH_PHASE_OPTIMIZATION

#include "net/mac/phase.h"

#endif /* WITH_PHASE_OPTIMIZATION */

#define DEFAULT_STREAM_TIME (4 * CYCLE_TIME)

#ifndef MIN
#define MIN(a, b) ((a) < (b)? (a) : (b))
#endif /* MIN */

struct seqno {
  rimeaddr_t sender;
  uint8_t seqno;
};

#ifdef NETSTACK_CONF_MAC_SEQNO_HISTORY
#define MAX_SEQNOS NETSTACK_CONF_MAC_SEQNO_HISTORY
#else /* NETSTACK_CONF_MAC_SEQNO_HISTORY */
#define MAX_SEQNOS 16
#endif /* NETSTACK_CONF_MAC_SEQNO_HISTORY */
static struct seqno received_seqnos[MAX_SEQNOS];

#if CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT
static struct timer broadcast_rate_timer;
static int broadcast_rate_counter;
#endif /* CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT */


/*---------------------------------------------------------------------------*/

int contikimac_sending(void)
{
  return we_are_sending==1;
}

int contikimac_broadcasting(void)
{
  return we_are_broadcasting==1;
}
#if WITH_STRAWMAN
int contikimac_checking(void)
{
  return we_are_checking==1;
}


#endif
/*---------------------------------------------------------------------------*/
static void
on(void)
{
  if(contikimac_is_on && radio_is_on == 0) {
    radio_is_on = 1;
    NETSTACK_RADIO.on();
  }
}
/*---------------------------------------------------------------------------*/
static void
off(void)
{
  if(contikimac_is_on && radio_is_on != 0 &&
     contikimac_keep_radio_on == 0) {
    radio_is_on = 0;
    NETSTACK_RADIO.off();
  }
}
/*---------------------------------------------------------------------------*/
static volatile rtimer_clock_t cycle_start;
volatile rtimer_clock_t current_cycle_start_time = 0;//MF to be used with the phaselock
//when the current cycle has started
static char powercycle(struct rtimer *t, void *ptr);
static void
schedule_powercycle(struct rtimer *t, rtimer_clock_t time)
{
  int r;

  if(contikimac_is_on) {

    if(RTIMER_CLOCK_LT(RTIMER_TIME(t) + time, RTIMER_NOW() + 2)) {
      time = RTIMER_NOW() - RTIMER_TIME(t) + 2;
    }

    r = rtimer_set(t, RTIMER_TIME(t) + time, 1,
                   (void (*)(struct rtimer *, void *))powercycle, NULL);
    if(r != RTIMER_OK) {
      PRINTF("schedule_powercycle: could not set rtimer\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
static void
schedule_powercycle_fixed(struct rtimer *t, rtimer_clock_t fixed_time)
{
  int r;
  if(contikimac_is_on) {

    if(RTIMER_CLOCK_LT(fixed_time, RTIMER_NOW() + 1)) {
      fixed_time = RTIMER_NOW() + 1;
    }

    r = rtimer_set(t, fixed_time, 1,
                   (void (*)(struct rtimer *, void *))powercycle, NULL);
    if(r != RTIMER_OK) {
      PRINTF("schedule_powercycle: could not set rtimer\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
#if WITH_STRAWMAN
uint8_t straw_was_waiting;
#endif /* WITH_STRAWMAN */
static void
powercycle_turn_radio_off(void)
{
#if CONTIKIMAC_CONF_COMPOWER
  uint8_t was_on = radio_is_on;
#endif /* CONTIKIMAC_CONF_COMPOWER */
#if WITH_STRAWMAN
  we_are_checking=0;
#endif
  if(we_are_sending==1){
    PRINTF_MIN("busy sending\n");
  }
  else if(we_are_sending == 0 && we_are_receiving_burst == 0 && straw_code_waiting==0 && straw_code_competing==0) {
    off();
#if CONTIKIMAC_CONF_COMPOWER
    if(was_on && !radio_is_on) {
      compower_accumulate(&compower_idle_activity);
    }
#endif /* CONTIKIMAC_CONF_COMPOWER */
  }
#if WITH_STRAWMAN
  if(straw_was_waiting){
    straw_code_waiting=0;
  }
#endif
}
/*---------------------------------------------------------------------------*/
static void
powercycle_turn_radio_on(void)
{
#if WITH_STRAWMAN
  straw_was_waiting=straw_code_waiting;
#endif
  if(we_are_sending == 0 && we_are_receiving_burst == 0 && straw_code_waiting==0 && straw_code_competing==0) {
#if WITH_STRAWMAN
    we_are_checking=1;
#endif
    on();
  }
}
/*---------------------------------------------------------------------------*/
static char
powercycle(struct rtimer *t, void *ptr)
{
#if SYNC_CYCLE_STARTS
  static volatile rtimer_clock_t sync_cycle_start;
  static volatile uint8_t sync_cycle_phase;
#endif

  PT_BEGIN(&pt);

#if SYNC_CYCLE_STARTS
  sync_cycle_start = RTIMER_NOW();
#else
  cycle_start = RTIMER_NOW();
#if 1//WITH_ADVANCED_PHASELOCK
  current_cycle_start_time = RTIMER_NOW();
#endif /* WITH_ADVANCED_PHASELOCK */
#endif

  while(1) {
    static uint8_t packet_seen;
    static rtimer_clock_t t0;
    static uint8_t count;
    
#if SYNC_CYCLE_STARTS
    /* Compute cycle start when RTIMER_ARCH_SECOND is not a multiple
       of CHANNEL_CHECK_RATE */
    if(sync_cycle_phase++ == NETSTACK_RDC_CHANNEL_CHECK_RATE) {
      sync_cycle_phase = 0;
      sync_cycle_start += RTIMER_ARCH_SECOND;
      cycle_start = sync_cycle_start;
    } else {
#if (RTIMER_ARCH_SECOND * NETSTACK_RDC_CHANNEL_CHECK_RATE) > 65535
      cycle_start = sync_cycle_start + ((unsigned long)(sync_cycle_phase*RTIMER_ARCH_SECOND))/NETSTACK_RDC_CHANNEL_CHECK_RATE;
#else
      cycle_start = sync_cycle_start + (sync_cycle_phase*RTIMER_ARCH_SECOND)/NETSTACK_RDC_CHANNEL_CHECK_RATE;
#endif
    }
#else
    cycle_start += CYCLE_TIME;
#endif

#if 1//WITH_ADVANCED_PHASELOCK
    current_cycle_start_time += CYCLE_TIME;
#if WITH_STRAWMAN
    if(coll_count!=0 || probe_count !=0 || probe_count_b !=0){
      PRINTF_MIN("stats %s - %s - %s\n",coll_count!=0 ? "coll" : "x", probe_count!=0 ? "probe" : "x", probe_count_b!=0 ? "probeB" : "x" );
    }
      coll_count=0;
      probe_count=0;
      probe_count_b=0;
#endif
#endif /* WITH_ADVANCED_PHASELOCK */

    packet_seen = 0;
    
    for(count = 0; count < CCA_COUNT_MAX; ++count) {
      t0 = RTIMER_NOW();
      if(we_are_sending == 0 && we_are_receiving_burst == 0) {
        powercycle_turn_radio_on();
        /* Check if a packet is seen in the air. If so, we keep the
             radio on for a while (LISTEN_TIME_AFTER_PACKET_DETECTED) to
             be able to receive the packet. We also continuously check
             the radio medium to make sure that we wasn't woken up by a
             false positive: a spurious radio interference that was not
             caused by an incoming packet. */
        if(NETSTACK_RADIO.channel_clear() == 0) {
          packet_seen = 1;
          break;
        }
        powercycle_turn_radio_off();
      }
      schedule_powercycle_fixed(t, RTIMER_NOW() + CCA_SLEEP_TIME);
      PT_YIELD(&pt);
    }

    if(packet_seen) {
      static rtimer_clock_t start;
      static uint8_t silence_periods, periods;
      start = RTIMER_NOW();

      periods = silence_periods = 0;
      while(we_are_sending == 0 && radio_is_on &&
          RTIMER_CLOCK_LT(RTIMER_NOW(),
                            (start + LISTEN_TIME_AFTER_PACKET_DETECTED))) {

        /* Check for a number of consecutive periods of
             non-activity. If we see two such periods, we turn the
             radio off. Also, if a packet has been successfully
             received (as indicated by the
             NETSTACK_RADIO.pending_packet() function), we stop
             snooping. */
#if !RDC_CONF_HARDWARE_CSMA
       /* A cca cycle will disrupt rx on some radios, e.g. mc1322x, rf230 */
       /*TODO: Modify those drivers to just return the internal RSSI when already in rx mode */
        if(NETSTACK_RADIO.channel_clear()) {
          ++silence_periods;
        } else {
          silence_periods = 0;
        }
#endif

        ++periods;

        if(NETSTACK_RADIO.receiving_packet()) {
          silence_periods = 0;
        }
        if(silence_periods > MAX_SILENCE_PERIODS) {
          powercycle_turn_radio_off();
          break;
        }
        if(WITH_FAST_SLEEP &&
            periods > MAX_NONACTIVITY_PERIODS &&
            !(NETSTACK_RADIO.receiving_packet() ||
              NETSTACK_RADIO.pending_packet())) {
          powercycle_turn_radio_off();
          break;
        }
        if(NETSTACK_RADIO.pending_packet()) {
          break;
        }

        schedule_powercycle(t, CCA_CHECK_TIME + CCA_SLEEP_TIME);
        PT_YIELD(&pt);
      }
      if(radio_is_on) {
        if(!(NETSTACK_RADIO.receiving_packet() ||
             NETSTACK_RADIO.pending_packet()) ||
             !RTIMER_CLOCK_LT(RTIMER_NOW(),
                 (start + LISTEN_TIME_AFTER_PACKET_DETECTED))) {
          powercycle_turn_radio_off();
        }
      }
    }
    
    if(RTIMER_CLOCK_LT(RTIMER_NOW() - cycle_start, CYCLE_TIME - CHECK_TIME * 4)) {
      /* Schedule the next powercycle interrupt, or sleep the mcu
	 until then.  Sleeping will not exit from this interrupt, so
	 ensure an occasional wake cycle or foreground processing will
	 be blocked until a packet is detected */
#if RDC_CONF_MCU_SLEEP
      static uint8_t sleepcycle;
      if((sleepcycle++ < 16) && !we_are_sending && !radio_is_on) {
        rtimer_arch_sleep(CYCLE_TIME - (RTIMER_NOW() - cycle_start));
      } else {
        sleepcycle = 0;
        schedule_powercycle_fixed(t, CYCLE_TIME + cycle_start);
        PT_YIELD(&pt);
      }
#else
      schedule_powercycle_fixed(t, CYCLE_TIME + cycle_start);
      PT_YIELD(&pt);
#endif
    }
  }

  PT_END(&pt);
}
/*---------------------------------------------------------------------------*/
static int
broadcast_rate_drop(void)
{
#if CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT
  if(!timer_expired(&broadcast_rate_timer)) {
    broadcast_rate_counter++;
    if(broadcast_rate_counter < CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT) {
      return 0;
    } else {
      return 1;
    }
  } else {
    timer_set(&broadcast_rate_timer, CLOCK_SECOND);
    broadcast_rate_counter = 0;
    return 0;
  }
#else /* CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT */
  return 0;
#endif /* CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT */
}

#if WITH_ADVANCED_PHASELOCK
extern rtimer_clock_t phaselock_target;
#endif
//static int
//send_packet(mac_callback_t mac_callback, void *mac_callback_ptr,
//	    struct rdc_buf_list *buf_list,
//            int is_receiver_awake)
static int
send_packet(mac_callback_t mac_callback, void *mac_callback_ptr,
      struct rdc_buf_list *buf_list,
            int is_receiver_awake, int bypass)
{
  rtimer_clock_t t0;
  rtimer_clock_t encounter_time = 0;
#if WITH_ADVANCED_PHASELOCK
  phaselock_target=0;
#endif
  int strobes;
  uint8_t got_strobe_ack = 0;
  int hdrlen,len=0;
  uint8_t is_broadcast = 0;
  uint8_t is_reliable = 0;
  uint8_t is_known_receiver = 0;
  uint8_t collisions;
  int transmit_len;
  int ret;
  uint8_t contikimac_was_on;
  uint8_t seqno;
  uint8_t was_competing=0;


#if WITH_CONTIKIMAC_HEADER
  struct hdr *chdr;
#endif /* WITH_CONTIKIMAC_HEADER */

  /* Exit if RDC and radio were explicitly turned off */
   if(!contikimac_is_on && !contikimac_keep_radio_on) {
    PRINTF("contikimac: radio is turned off\n");
    return MAC_TX_ERR_FATAL;
  }

  if(packetbuf_totlen() == 0) {
    PRINTF("contikimac: send_packet data len 0\n");
    return MAC_TX_ERR_FATAL;
  }

#if !NETSTACK_CONF_BRIDGE_MODE
  /* If NETSTACK_CONF_BRIDGE_MODE is set, assume PACKETBUF_ADDR_SENDER is already set. */
  packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &rimeaddr_node_addr);
#endif
  if(rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER), &rimeaddr_null)) {
    is_broadcast = 1;
    PRINTDEBUG("contikimac: send broadcast\n");

    if(broadcast_rate_drop()) {
      return MAC_TX_COLLISION;
    }
  } else {
#if UIP_CONF_IPV6
    PRINTDEBUG("contikimac: send unicast to %02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[1],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[2],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[3],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[4],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[5],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[6],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[7]);
#else /* UIP_CONF_IPV6 */
    PRINTDEBUG("contikimac: send unicast to %u.%u\n",
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[1]);
#endif /* UIP_CONF_IPV6 */
  }
  is_reliable = packetbuf_attr(PACKETBUF_ATTR_RELIABLE) ||
    packetbuf_attr(PACKETBUF_ATTR_ERELIABLE);

  packetbuf_set_attr(PACKETBUF_ATTR_MAC_ACK, 1);

#if WITH_CONTIKIMAC_HEADER
  hdrlen = packetbuf_totlen();
  if(packetbuf_hdralloc(sizeof(struct hdr)) == 0) {
    /* Failed to allocate space for contikimac header */
    PRINTF("contikimac: send failed, too large header\n");
    return MAC_TX_ERR_FATAL;
  }
  chdr = packetbuf_hdrptr();
  chdr->id = CONTIKIMAC_ID;
  chdr->len = hdrlen;

  /* Create the MAC header for the data packet. */
  hdrlen = NETSTACK_FRAMER.create();
  if(hdrlen < 0) {
    /* Failed to send */
    PRINTF("contikimac: send failed, too large header\n");
    packetbuf_hdr_remove(sizeof(struct hdr));
    return MAC_TX_ERR_FATAL;
  }
  hdrlen += sizeof(struct hdr);
#else
  /* Create the MAC header for the data packet. */
  hdrlen = NETSTACK_FRAMER.create();
  if(hdrlen < 0) {
    /* Failed to send */
    PRINTF("contikimac: send failed, too large header\n");
    return MAC_TX_ERR_FATAL;
  }
#endif

  /* Make sure that the packet is longer or equal to the shortest
     packet length. */
  transmit_len = packetbuf_totlen();
  if(transmit_len < SHORTEST_PACKET_SIZE) {
    /* Pad with zeroes */
    uint8_t *ptr;
    ptr = packetbuf_dataptr();
    memset(ptr + packetbuf_datalen(), 0, SHORTEST_PACKET_SIZE - packetbuf_totlen());

    PRINTF("contikimac: shorter than shortest (%d)\n", packetbuf_totlen());
    transmit_len = SHORTEST_PACKET_SIZE;
  }


  packetbuf_compact();

#ifdef NETSTACK_ENCRYPT
  NETSTACK_ENCRYPT();
#endif /* NETSTACK_ENCRYPT */

#if !WITH_CONTIKIMAC_HEADER
  transmit_len = packetbuf_totlen();
#endif /* !WITH_CONTIKIMAC_HEADER */

  NETSTACK_RADIO.prepare(packetbuf_hdrptr(), transmit_len);

  /* Remove the MAC-layer header since it will be recreated next time around. */
  packetbuf_hdr_remove(hdrlen);
  if(!is_broadcast && !is_receiver_awake && !bypass) {
#if WITH_PHASE_OPTIMIZATION
    ret = phase_wait(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                     CYCLE_TIME, GUARD_TIME,
                     mac_callback, mac_callback_ptr, buf_list);
    if(ret == PHASE_DEFERRED) {
      return MAC_TX_DEFERRED;
    }
    if(ret != PHASE_UNKNOWN) {
      is_known_receiver = 1;
    }
#endif /* WITH_PHASE_OPTIMIZATION */ 
  }



  /* By setting we_are_sending to one, we ensure that the rtimer
     powercycle interrupt do not interfere with us sending the packet. */
  we_are_sending = 1;
#if WITH_STRAWMAN
  if(is_broadcast){
    we_are_broadcasting=1;
  }
#endif /* WITH_STRAWMAN */

  /* If we have a pending packet in the radio, we should not send now,
     because we will trash the received packet. Instead, we signal
     that we have a collision, which lets the packet be received. This
     packet will be retransmitted later by the MAC protocol
     instread. */
  if(!bypass && (NETSTACK_RADIO.receiving_packet() || NETSTACK_RADIO.pending_packet())) {
    t0=RTIMER_NOW();
    we_are_sending = 0;
#if 1//  WITH_STRAWMAN
    we_are_broadcasting=0;
#endif /* WITH_STRAWMAN */
    //COOJA_DEBUG_PRINTF("contikimac: collision receiving %d, pending %d\n",
      //    NETSTACK_RADIO.receiving_packet(), NETSTACK_RADIO.pending_packet());

    return MAC_TX_COLLISION;
  }

  /* Switch off the radio to ensure that we didn't start sending while
     the radio was doing a channel check. */
  off();


  strobes = 0;

  /* Send a train of strobes until the receiver answers with an ACK. */
  collisions = 0;

  got_strobe_ack = 0;

  /* Set contikimac_is_on to one to allow the on() and off() functions
     to control the radio. We restore the old value of
     contikimac_is_on when we are done. */
  contikimac_was_on = contikimac_is_on;
  contikimac_is_on = 1;

#if !RDC_CONF_HARDWARE_CSMA
    /* Check if there are any transmissions by others. */
    /* TODO: why does this give collisions before sending with the mc1322x? */
  if(!bypass && is_receiver_awake == 0) {
    int i;
    for(i = 0; i < CCA_COUNT_MAX_TX; ++i) {
      t0 = RTIMER_NOW();
      on();
#if CCA_CHECK_TIME > 0
      while(RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + CCA_CHECK_TIME)) { }
#endif
      if(NETSTACK_RADIO.channel_clear() == 0) {
        collisions++;
        off();
        break;
      }
      off();
      t0 = RTIMER_NOW();
      while(RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + CCA_SLEEP_TIME)) { }
    }
  }

  if(collisions > 0) {
    we_are_sending = 0;
#if 1//  WITH_STRAWMAN
    we_are_broadcasting=0;
#endif /* WITH_STRAWMAN */
    off();
    //COOJA_DEBUG_PRINTF("contikimac: collisions before sending\n");
    contikimac_is_on = contikimac_was_on;
    return MAC_TX_COLLISION;
  }
#endif /* RDC_CONF_HARDWARE_CSMA */

#if !RDC_CONF_HARDWARE_ACK
  //if(!is_broadcast) {
    /* Turn radio on to receive expected unicast ack.  Not necessary
       with hardware ack detection, and may trigger an unnecessary cca
       or rx cycle */
     on();
  //}
#endif

     uint8_t ackbuf[ACK_LEN];
     rimeaddr_t dest;
     watchdog_periodic();
     t0 = RTIMER_NOW();
     seqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);

#if WITH_STRAWMAN
    for(strobes = 0, collisions = 0;
        got_strobe_ack==0 && collisions == 0 && (straw_code_competing==0||straw_code_winning==1) && RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + STROBE_TIME); strobes++) {
#else
      for(strobes = 0, collisions = 0;
          got_strobe_ack==0 && collisions == 0 &&
              RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + STROBE_TIME); strobes++) {
#endif

        watchdog_periodic();
//        if(!is_broadcast && (is_receiver_awake || is_known_receiver) &&
//            !RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + RTIMER_ARCH_SECOND / 100)) {
//          COOJA_DEBUG_PRINTF("miss to %d\n", packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0]);
//          break;
//        }

        {
          rtimer_clock_t wt;
          rtimer_clock_t txtime;
          int ret;
          txtime = RTIMER_NOW();
          ret = NETSTACK_RADIO.transmit(transmit_len);

#if RDC_CONF_HARDWARE_ACK
          /* For radios that block in the transmit routine and detect the
	ACK in hardware */
          if(ret == RADIO_TX_OK) {
            if(!is_broadcast) {
              got_strobe_ack = 1;
              encounter_time = txtime;
              break;
            }
          } else if (ret == RADIO_TX_NOACK) {
          } else if (ret == RADIO_TX_COLLISION) {
            PRINTF("contikimac: collisions while sending\n");
            collisions++;
          }
          wt = RTIMER_NOW();
          while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + INTER_PACKET_INTERVAL)) { }
#else /* RDC_CONF_HARDWARE_ACK */
          /* Wait for the ACK packet */
          wt = RTIMER_NOW();
          NETSTACK_RADIO.on();

          while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + INTER_PACKET_INTERVAL)) { }
          if(NETSTACK_RADIO.receiving_packet() ||
                               NETSTACK_RADIO.pending_packet() ||
                               NETSTACK_RADIO.channel_clear() == 0) {

              wt = RTIMER_NOW();


              while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + AFTER_ACK_DETECTECT_WAIT_TIME)) { }

#if WITH_STRAWMAN
                if(straw_code_competing==1){// || is_broadcast){
                  break;//if bcast we don't care about ack
                }
#endif /* WITH_STRAWMAN*/

                len = NETSTACK_RADIO.read(ackbuf, ACK_LEN);
                if(len == ACK_LEN && seqno == ackbuf[2]) {
                  got_strobe_ack = 1;
                  memcpy(&dest, ackbuf+3, 8);
                  encounter_time = txtime;
                  break;
                }
#if !WITH_STRAWMAN //could prevent to detect the probe
                else
                {
                  collisions++;
                }
#else /* !WITH_STRAWMAN*/
                //else if(len>ACK_LEN){//we wait to be able to detect a probe
                else{//wait to try detect a probe (time to send data at worst)
                  wt = RTIMER_NOW();
                  //while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + RTIMER_ARCH_SECOND/5000) && straw_code_competing==0) {};
                  while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + RTIMER_ARCH_SECOND/250) && straw_code_competing==0) {};
                  collisions++;
                }
#endif /* !WITH_STRAWMAN*/
            }
#if WITH_STRAWMAN
          else if(straw_code_competing==1){
            break;
          }
#endif /* WITH_STRAWMAN*/
#endif /* RDC_CONF_HARDWARE_ACK */
        }
      }

#if WITH_STRAWMAN
      if(bypass){
        if(got_strobe_ack==1){
        collisions=0;
        straw_code_winning=0;
        straw_code_competing=0;
        }
        else{
          collisions++;
          straw_code_competing=0;
          straw_code_winning=0;//if not ACK we have lost
        }
      }

      if(is_broadcast){
        straw_code_competing=0;//bcast stops but don't really compete
      }
      if(straw_code_competing==1)
      {
        was_competing=1;
        t0=RTIMER_NOW();
        while(RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + MAX_PHASE_STROBE_TIME/4) && straw_code_winning==0){}

        if(straw_code_winning==1){
          //ret = NETSTACK_RADIO.transmit(transmit_len);
          //t0=RTIMER_NOW();
          //while(RTIMER_CLOCK_LT(RTIMER_NOW(), t0 + (RTIMER_ARCH_SECOND/5000) * (random_rand()%8)));
          return MAC_TX_BYPASS;//resend packet without any cca check
        }
        else{
          straw_code_competing=0;
          collisions++;//collision given destination will not be awake anymore except with good timing
        }
      }

#endif /*WITH_STRAWMAN*/
      //packetbuf_hdr_remove(hdrlen);
  off();

  if(!is_broadcast){

#if WITH_ADVANCED_PHASELOCK
    PRINTF_MIN("contikimac: send (strobes=%u, len=%u, %s, %s, phase=%lu, %s, %s, %u), done\n",strobes,
               packetbuf_totlen(),
               got_strobe_ack ? "ack" : "no ack",
                   collisions ? "collision" : "no collision",
                       (unsigned long)((unsigned long)phaselock_target* 1000/RTIMER_ARCH_SECOND),
                       was_competing? "straw" : "-",
                       bypass? "bypass" : "-",
                       seqno);

  }
#else
  PRINTF("contikimac: send (strobes=%u, len=%u, %s, %s, %s, %s, %u), done\n",strobes,
             packetbuf_totlen(),
             got_strobe_ack ? "ack" : "no ack",
                 collisions ? "collision" : "no collision",
                     was_competing? "straw" : "-",
                     bypass? "bypass" : "-",
                     seqno);

}
#endif
  else{
   // COOJA_DEBUG_PRINTF("contikimac: send broadcast\n");
#if 1//  WITH_STRAWMAN
    we_are_broadcasting=0;
#endif /* WITH_STRAWMAN */
  }



#if CONTIKIMAC_CONF_COMPOWER
  /* Accumulate the power consumption for the packet transmission. */
  compower_accumulate(&current_packet);

  /* Convert the accumulated power consumption for the transmitted
     packet to packet attributes so that the higher levels can keep
     track of the amount of energy spent on transmitting the
     packet. */
  compower_attrconv(&current_packet);

  /* Clear the accumulated power consumption so that it is ready for
     the next packet. */
  compower_clear(&current_packet);
#endif /* CONTIKIMAC_CONF_COMPOWER */

  contikimac_is_on = contikimac_was_on;
  we_are_sending = 0;
/* WITH_STRAWMAN */
  /* Determine the return value that we will return from the
     function. We must pass this value to the phase module before we
     return from the function.  */
  if(collisions > 0) {
    ret = MAC_TX_COLLISION;
  } else if(!is_broadcast && !got_strobe_ack) {
    ret = MAC_TX_NOACK;
  } else {
    ret = MAC_TX_OK;
  }

#if WITH_PHASE_OPTIMIZATION
  if(is_known_receiver && got_strobe_ack) {
    PRINTF("no miss %d wake-ups %d\n",
	   packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
           strobes);
  }

  if(!is_broadcast) {
#if WITH_ADVANCED_PHASELOCK
    if(collisions == 0 && is_receiver_awake == 0) {
      phase_update(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
       encounter_time-phaselock_target, ret);
    }
#else /* WITH_ADVANCED_PHASELOCK */
    if(collisions == 0 && is_receiver_awake == 0) {
      phase_update(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
       encounter_time, ret);
    }
#endif /* WITH_ADVANCED_PHASELOCK */

  }
#endif /* WITH_PHASE_OPTIMIZATION */

  return ret;
}
/*---------------------------------------------------------------------------*/

extern unsigned char bypass_allowed;
static void
qsend_packet(mac_callback_t sent, void *ptr)
{
  int ret = send_packet(sent, ptr, NULL, 0,bypass_allowed);
  if(ret != MAC_TX_DEFERRED) {
    mac_call_sent_callback(sent, ptr, ret, 1);
  }
}
/*---------------------------------------------------------------------------*/
static void
qsend_list(mac_callback_t sent, void *ptr, struct rdc_buf_list *buf_list)
{
  struct rdc_buf_list *curr = buf_list;
  struct rdc_buf_list *next;
  int ret;
  int is_receiver_awake;
  
  if(curr == NULL) {
    return;
  }
  /* Do not send during reception of a burst */
  if(we_are_receiving_burst) {
    /* Prepare the packetbuf for callback */
    queuebuf_to_packetbuf(curr->buf);
    /* Return COLLISION so the MAC may try again later */
    mac_call_sent_callback(sent, ptr, MAC_TX_COLLISION, 1);
    return;
  }
  /* The receiver needs to be awoken before we send */
  is_receiver_awake = 0;
  do { /* A loop sending a burst of packets from buf_list */
    //next = list_item_next(curr);
	next=NULL;//MF disable busrt
    /* Prepare the packetbuf */
    queuebuf_to_packetbuf(curr->buf);
//    if(next != NULL) {
//      packetbuf_set_attr(PACKETBUF_ATTR_PENDING, 1);
//    }

    /* Send the current packet */
    ret = send_packet(sent, ptr, curr, is_receiver_awake,bypass_allowed);
    if(ret != MAC_TX_DEFERRED) {
      mac_call_sent_callback(sent, ptr, ret, 1);
    }

    if(ret == MAC_TX_OK) {
      if(next != NULL) {
        /* We're in a burst, no need to wake the receiver up again */
        is_receiver_awake = 1;
        curr = next;
      }
    } else {
      /* The transmission failed, we stop the burst */
      next = NULL;
    }
  } while(next != NULL);
}
/*---------------------------------------------------------------------------*/
/* Timer callback triggered when receiving a burst, after having
   waited for a next packet for a too long time. Turns the radio off
   and leaves burst reception mode */
static void
recv_burst_off(void *ptr)
{
  off();
  we_are_receiving_burst = 0;
}
/*---------------------------------------------------------------------------*/
static void
input_packet(void)
{
  static struct ctimer ct;
  if(!we_are_receiving_burst) {
    off();
  }

  /*  printf("cycle_start 0x%02x 0x%02x\n", cycle_start, cycle_start % CYCLE_TIME);*/

#ifdef NETSTACK_DECRYPT
  NETSTACK_DECRYPT();
#endif /* NETSTACK_DECRYPT */

  if(packetbuf_totlen() > 0 && NETSTACK_FRAMER.parse() >= 0) {

#if WITH_CONTIKIMAC_HEADER
    struct hdr *chdr;
    chdr = packetbuf_dataptr();
    if(chdr->id != CONTIKIMAC_ID) {
      PRINTF("contikimac: failed to parse hdr (%u)\n", packetbuf_totlen());
      return;
    }
    packetbuf_hdrreduce(sizeof(struct hdr));
    packetbuf_set_datalen(chdr->len);
#endif /* WITH_CONTIKIMAC_HEADER */

    if(packetbuf_datalen() > 0 &&
        packetbuf_totlen() > 0 &&
        (rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                      &rimeaddr_node_addr) ||
            rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                         &rimeaddr_null))) {
      /* This is a regular packet that is destined to us or to the
         broadcast address. */
      if(rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                      &rimeaddr_node_addr)){
       printf("Cmac: input from %d\n",
               node_id_from_rimeaddr(packetbuf_addr(PACKETBUF_ADDR_SENDER))
        );
//#if DEBUG
//        rpl_trace(rpl_dataptr_from_packetbuf());
//#endif
//      }
      /* If FRAME_PENDING is set, we are receiving a packets in a burst */
      we_are_receiving_burst = packetbuf_attr(PACKETBUF_ATTR_PENDING);
      if(we_are_receiving_burst) {
        on();
        /* Set a timer to turn the radio off in case we do not receive
	   a next packet */
        ctimer_set(&ct, INTER_PACKET_DEADLINE, recv_burst_off, NULL);
      } else {
        off();
        ctimer_stop(&ct);
      }

      /* Check for duplicate packet by comparing the sequence number
         of the incoming packet with the last few ones we saw. */
      {
        int i;
        for(i = 0; i < MAX_SEQNOS; ++i) {
          if(packetbuf_attr(PACKETBUF_ATTR_PACKET_ID) == received_seqnos[i].seqno &&
              rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_SENDER),
                           &received_seqnos[i].sender)) {
            /* Drop the packet. */
            //printf("Drop duplicate ContikiMAC layer packet\n");
            return;
          }
        }
        for(i = MAX_SEQNOS - 1; i > 0; --i) {
          memcpy(&received_seqnos[i], &received_seqnos[i - 1],
                 sizeof(struct seqno));
        }
        received_seqnos[0].seqno = packetbuf_attr(PACKETBUF_ATTR_PACKET_ID);
        rimeaddr_copy(&received_seqnos[0].sender,
                      packetbuf_addr(PACKETBUF_ADDR_SENDER));
      }

#if CONTIKIMAC_CONF_COMPOWER
      /* Accumulate the power consumption for the packet reception. */
      compower_accumulate(&current_packet);
      /* Convert the accumulated power consumption for the received
         packet to packet attributes so that the higher levels can
         keep track of the amount of energy spent on receiving the
         packet. */
      compower_attrconv(&current_packet);

      /* Clear the accumulated power consumption so that it is ready
         for the next packet. */
      compower_clear(&current_packet);
#endif /* CONTIKIMAC_CONF_COMPOWER */


     if(appdataptr_from_packetbuf() != NULL) {
        LOG_INC_HOPCOUNT_FROM_PACKETBUF();
      printf("cmac: data from %u\n",
             LOG_NODEID_FROM_RIMEADDR(packetbuf_addr(PACKETBUF_ADDR_SENDER)));
      }
      NETSTACK_MAC.input();
      return;
    } else {
      //printf("contikimac: data not for us\n");
    }
  } else {
    //printf("contikimac: failed to parse (%u)\n", packetbuf_totlen());
  }
}
/*---------------------------------------------------------------------------*/
static void
init(void)
{
  radio_is_on = 0;
  PT_INIT(&pt);

  rtimer_set(&rt, RTIMER_NOW() + CYCLE_TIME, 1,
             (void (*)(struct rtimer *, void *))powercycle, NULL);

  contikimac_is_on = 1;

#if WITH_PHASE_OPTIMIZATION
  phase_init();
#endif /* WITH_PHASE_OPTIMIZATION */
#if WITH_ADVANCED_PHASELOCK
  softack_init();//MF
#endif

}
/*---------------------------------------------------------------------------*/
static int
turn_on(void)
{
  if(contikimac_is_on == 0) {
    contikimac_is_on = 1;
    contikimac_keep_radio_on = 0;
    rtimer_set(&rt, RTIMER_NOW() + CYCLE_TIME, 1,
               (void (*)(struct rtimer *, void *))powercycle, NULL);
  }
  return 1;
}
/*---------------------------------------------------------------------------*/
static int
turn_off(int keep_radio_on)
{
  contikimac_is_on = 0;
  contikimac_keep_radio_on = keep_radio_on;
  if(keep_radio_on) {
    radio_is_on = 1;
    return NETSTACK_RADIO.on();
  } else {
    radio_is_on = 0;
    return NETSTACK_RADIO.off();
  }
}
/*---------------------------------------------------------------------------*/
static unsigned short
duty_cycle(void)
{
  return (1ul * CLOCK_SECOND * CYCLE_TIME) / RTIMER_ARCH_SECOND;
}
/*---------------------------------------------------------------------------*/
const struct rdc_driver contikimac_driver = {
  "ContikiMAC",
  init,
  qsend_packet,
  qsend_list,
  input_packet,
  turn_on,
  turn_off,
  duty_cycle,
};
/*---------------------------------------------------------------------------*/
uint16_t
contikimac_debug_print(void)
{
  return 0;
}
/*---------------------------------------------------------------------------*/
