/*
 * Copyright (c) 2013, Swedish Institute of Computer Science.
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
 */
/**
 * \file
 *         Override node_id_restore to node-id based on ds2411 ID
 *         in a testbed-specific manner
 *
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#include "contiki-conf.h"
#include "deployment.h"

unsigned short node_id = 0;

#if IN_UMONS
unsigned char node_mac[8];
#endif

/*---------------------------------------------------------------------------*/
#if IN_UMONS
void
node_id_restore(void)
{
  unsigned char buf[12];
  xmem_pread(buf, 12, NODE_ID_XMEM_OFFSET);
  if(buf[0] == 0xad &&
     buf[1] == 0xde) {
    node_id = (buf[2] << 8) | buf[3];
    memcpy(node_mac, &buf[4], 8);
  } else {
    node_id = 0;
  }
}
/*---------------------------------------------------------------------------*/
void
node_id_burn(unsigned short id)
{
  unsigned char buf[12];
  buf[0] = 0xad;
  buf[1] = 0xde;
  buf[2] = id >> 8;
  buf[3] = id & 0xff;
  memcpy(&buf[4], node_mac, 8);
  xmem_erase(XMEM_ERASE_UNIT_SIZE, NODE_ID_XMEM_OFFSET);
  xmem_pwrite(buf, 12, NODE_ID_XMEM_OFFSET);
}
#else /* IN_UMONS */
//
///*---------------------------------------------------------------------------*/
void
node_id_restore(void)
{
  node_id = get_node_id();
}
///*---------------------------------------------------------------------------*/
void
node_id_burn(unsigned short id)
{
}
///*---------------------------------------------------------------------------*/
#endif /* IN_UMONS */
