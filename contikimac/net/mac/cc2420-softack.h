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
 *         CC2420-softack driver header file
 * \ref
 *        based on the work of Simon Duquennoy <simonduq@sics.se> from SICS
 * \author
 *        Mathieu Michel <mathieu.michel@umons.ac.be>
 */

#ifndef __CC2420_SOFTACK_H__
#define __CC2420_SOFTACK_H__

#include "dev/cc2420.h"
#include "net/rpl/rpl.h"


#define SOFTACK_ACK 1
#define SOFTACK_VOTE 2
#define SOFTACK_SIGNAL 3
#define SOFTACK_RESULT 4
#define SOFTACK_NULL 0;
typedef void(softack_input_callback_f)(const uint8_t *frame, uint8_t framelen, uint8_t **ackbufptr, uint8_t *acklen, uint8_t *code);
typedef void(softack_coll_callback_f)(uint8_t **probebufptr, uint8_t *probelen);
typedef void(softack_vote_callback_f)(uint8_t **signalbufptr, uint8_t *signallen, uint16_t len);
typedef void(softack_acked_callback_f)(const uint8_t *frame, uint8_t framelen);

/* Subscribe with two callbacks called from FIFOP interrupt */
void cc2420_softack_subscribe_strawman(softack_input_callback_f *input_callback,softack_coll_callback_f *coll_callback, softack_vote_callback_f *vote_callback);
void cc2420_softack_subscribe(softack_input_callback_f *input_callback);
void flushrx();
#endif /* __CC2420_SOFTACK_H__ */
