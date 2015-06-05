/*
 * Copyright (c) 2010, University of Luebeck, Germany.
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
 * Author: Carlo Alberto Boano <cboano@iti.uni-luebeck.de>
 *
 * Fast scan with all dBm values
 *
 */

#include "contiki.h"
#include <stdio.h>
#include <signal.h>
#include "dev/leds.h"
#include "dev/cc2420.h"
#include "dev/watchdog.h"
#include "interferer_settings.h"

/*---------------------------------------------------------------------------*/
#define CHANNEL 26				// Channel to be used

// Buffers
#define BUFFER_SIZE 8192		// Size of the buffer 
static uint8_t buffer0[BUFFER_SIZE/2+1] = {0};	// First I store here all the elements and then the number of occurrencies in buffer1
static uint8_t buffer1[BUFFER_SIZE/2+1] = {0};	// Stores the amount of times buffer[0] happened

// Periodicity of the scan
#define PERIOD_TIME (CLOCK_SECOND*2)

/*---------------------------------------------------------------------------*/
// CPU Boosting 
uint16_t cpu1, cpu2;

void boost_cpu() // Temporarily boost CPU speed
{
 cpu1 = DCOCTL;
 cpu2 = BCSCTL1;
 DCOCTL = 0xff;
 BCSCTL1 |= 0x07;
}	
void restore_cpu() // Restore CPU speed
{
 DCOCTL = cpu1;
 BCSCTL1 = cpu2;
}
/*---------------------------------------------------------------------------*/

PROCESS(scanning, "Scanning @ full precision");
AUTOSTART_PROCESSES(&scanning);
PROCESS_THREAD(scanning, ev, data)
{
 static unsigned long samples;

 PROCESS_BEGIN();
 
 // Initial operations
 leds_off(LEDS_ALL);
 watchdog_stop(); 
 
 // Avoiding wrong RSSI readings
 unsigned temp;
 unsigned j;

 CC2420_READ_REG(CC2420_AGCTST1, temp);
 CC2420_WRITE_REG(CC2420_AGCTST1, (temp + (1 << 8) + (1 << 13))); 
 
 // Selecting the channel	
 SPI_SETCHANNEL_SUPERFAST(357+((CHANNEL-11)*5));
 
 // Avoiding the initial wrong readings by discarding the wrong readings
 CC2420_SPI_ENABLE();
 int k=0;
 for (k=0; k<=15; k++) {MY_FASTSPI_GETRSSI(temp);}
 CC2420_SPI_DISABLE(); 
 
 static struct etimer et;
 while(1){
	 
	 // Resetting everything
	 for(k=0;k<(BUFFER_SIZE/2);k++){	
		buffer1[k] = 0;
		buffer0[k] = 0;
	 }
         samples = 0;
	
	 printf("#START [dBm: occurrencies] start %lu, ticks per second %lu\n",
                (unsigned long)RTIMER_NOW(), (unsigned long)RTIMER_SECOND);
	 
	 dint();				// Disable interrupts
	 boost_cpu(); 			// Temporarily boost CPU speed
	 CC2420_SPI_ENABLE(); 	// Enable SPI
	
	 // Actual scanning 
	 static signed char rssi;
	 int current = 0;
	 int previous = 0;
	 int cnt = 1;
	 for(k=0; k<(BUFFER_SIZE/2);){		
		// Sample the RSSI fast
		MY_FASTSPI_GETRSSI(rssi);	
                samples++;
		current = rssi + 55;		
		if((current == previous)&&(cnt<255)){
			cnt++;
		}
		else {
			buffer0[k] = previous;
			buffer1[k++] = cnt;	
			cnt = 1;
			previous = current;
		}
	 }	
 
	 CC2420_SPI_DISABLE();	// Disable SPI
	 restore_cpu();			// Restore CPU speed
	 eint(); 				// Re-enable interrupts

         printf("\n#STATS Sampled %lu values in %lu rtimer ticks\n",
                samples, (unsigned long)RTIMER_NOW());
 
	 // Printing the stored values in compressed form 
	 for(temp=0; temp<(BUFFER_SIZE/2); temp++) {
		printf("%d: %d\n",buffer0[temp] - 100, buffer1[temp]);
/*		clock_delay(30000);*/
	 }
	 
	 printf("#END clock_time = %lu\n", (unsigned long)clock_time());
	 
	 // Waiting for timer
	 etimer_set(&et, PERIOD_TIME);
	 PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));	
 
 }
 
 PROCESS_WAIT_EVENT();
 
 PROCESS_END();
}
/*---------------------------------------------------------------------------*/
