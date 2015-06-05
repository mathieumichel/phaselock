/*
 * Copyright (c) 2007, Swedish Institute of Computer Science.
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
 */

#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include <string.h>
#include "node-id.h"
#include <stdio.h>
#include "process.h"
#include "dev/cc2420/cc2420.h"
#include "dev/cc2420/cc2420_const.h"
#include "dev/spi.h"
#include <msp430.h>
#include "dev/watchdog.h"
#include "sys/ctimer.h"

#define OFF_TIME INTERFERER_OFF_TIME
#define ON_TIME  INTERFERER_ON_TIME

/*---------------------------------------------------------------------------*/
PROCESS(interferer, "Interference generator");
AUTOSTART_PROCESSES(&interferer);
/*---------------------------------------------------------------------------*/
static struct ctimer if_timer;
static unsigned long idle_time;
static unsigned long interference_time;
/*---------------------------------------------------------------------------*/
static unsigned
getreg(enum cc2420_register regname)
{
  uint16_t value;

  CC2420_SPI_ENABLE();
  SPI_WRITE(regname | 0x40);
  value = (uint8_t)SPI_RXBUF;
  SPI_TXBUF = 0;
  SPI_WAITFOREORx();
  value = SPI_RXBUF << 8;
  SPI_TXBUF = 0;
  SPI_WAITFOREORx();
  value |= SPI_RXBUF;
  CC2420_SPI_DISABLE();

  return value;
}
/*---------------------------------------------------------------------------*/
static void
strobe(enum cc2420_register regname)
{
  CC2420_SPI_ENABLE();
  SPI_WRITE(regname);
  CC2420_SPI_DISABLE();
}
/*---------------------------------------------------------------------------*/
static void
setreg(enum cc2420_register regname, unsigned value)
{
  CC2420_SPI_ENABLE();
  SPI_WRITE_FAST(regname);
  SPI_WRITE_FAST((uint8_t) (value >> 8));
  SPI_WRITE_FAST((uint8_t) (value & 0xff));
  SPI_WAITFORTx_ENDED();
  SPI_WRITE(0);
  CC2420_SPI_DISABLE();
}
/*---------------------------------------------------------------------------*/
/* Normal mode */
static void
reset(void)
{
 setreg(CC2420_MDMCTRL1, 0x0500);
 setreg(CC2420_MANOR, 0x0000);
 setreg(CC2420_TOPTST, 0x0010);
 setreg(CC2420_DACTST, 0x0000);
 strobe(CC2420_STXON);
}
/*---------------------------------------------------------------------------*/
/* Modulated carrier */
static void
modulated(void)
{
 /*
  * The CC2420 has a built-in test pattern generator that can generate
  * pseudo random sequence using the CRC generator. This is enabled by
  * setting MDMCTRL1.TX_MODE to 3 and issue a STXON command strobe. The
  * modulated spectrum is then available on the RF pins. The low byte of
  * the CRC word is transmitted and the CRC is updated with 0xFF for each
  * new byte. The length of the transmitted data sequence is 65535 bits.
  * The transmitted data-sequence is then: [synchronisation header] [0x00,
  * 0x78, 0xb8, 0x4b, 0x99, 0xc3, 0xe9, …]
  */
 reset();
 setreg(CC2420_MDMCTRL1, 0x000C);
 strobe(CC2420_STXON);
}
/*---------------------------------------------------------------------------*/
static void
callback(void *ptr)
{
  static uint8_t generate_interference;
  clock_time_t wait_time;

  if(generate_interference) {
    modulated();
    printf("1\n");
/*    wait_time = 3 * ON_TIME / 4 + (random_rand() % (ON_TIME / 2));*/
    wait_time = ON_TIME;
    interference_time += wait_time;
  } else {
    reset();
    printf("0\n");
    wait_time = 3 * OFF_TIME / 4 + (random_rand() % (OFF_TIME / 2));
    idle_time += wait_time;
  }

  generate_interference = !generate_interference;

  ctimer_set(&if_timer, wait_time, callback, NULL);
#if DEBUG
  printf("Interferer: Wait %u timer ticks. GI flag = %u\n",
         (unsigned)wait_time, generate_interference);
#endif
}
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(interferer, ev, data)
{
  static struct etimer stats_timer;

  PROCESS_EXITHANDLER();
     
  PROCESS_BEGIN();

  cc2420_set_channel(RF_CHANNEL);
  cc2420_set_txpower(INTERFERER_TX_POWER);

  printf("Interferer off-time %u ticks, on-time %u ticks, CLOCK_SECOND %u\n",
         (unsigned)OFF_TIME, (unsigned)ON_TIME, (unsigned)CLOCK_SECOND);
  printf("Interferer tx power %d rf channel %d\n",
         INTERFERER_TX_POWER, RF_CHANNEL);

  ctimer_set(&if_timer, CLOCK_SECOND, callback, NULL);

  watchdog_stop();

  etimer_set(&stats_timer, CLOCK_SECOND * 10);

  while (1) {
    PROCESS_WAIT_EVENT();
    if(etimer_expired(&stats_timer)) {
      printf("ON %lu OFF %lu\n", interference_time, idle_time);
      etimer_reset(&stats_timer);
    }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

		
	
