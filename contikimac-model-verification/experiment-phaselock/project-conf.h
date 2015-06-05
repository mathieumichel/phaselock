#ifndef __PROJECT_CONF_H__
#define __PROJECT_CONF_H__

#define INTERFERER_ON_TIME  CLOCK_SECOND / 2
#define INTERFERER_OFF_TIME INTERFERER_ON_TIME * 1//9
/* CC2420 power levels: 3, 7, 11, 15, 19, 23, 27, 31. */
#define INTERFERER_TX_POWER 15

#define WITH_ADVANCED_PHASELOCK 1
#undef WITH_PHASE_OPTIMIZATION
#define WITH_PHASE_OPTIMIZATION      1

#define USING_COOJA 0




#undef RF_CHANNEL
#define RF_CHANNEL 26

#define WITH_NULLMAC 1
#ifndef WITH_NULLMAC
#define WITH_NULLMAC 0
#endif


#if WITH_NULLMAC
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC nullmac_driver

#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC contikimac_driver

#undef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER framer_802154

/* Contiki netstack: RADIO */
#undef NETSTACK_CONF_RADIO
#define NETSTACK_CONF_RADIO   cc2420_softack_driver

#undef CC2420_CONF_AUTOACK
#define CC2420_CONF_AUTOACK 0


/* Our softack implementation for cc2420 requires to disable DCO synch */
#undef DCOSYNCH_CONF_ENABLED
#define DCOSYNCH_CONF_ENABLED 0

/* Our softack implementation for cc2420 requires SFD timestamps */
#undef CC2420_CONF_SFD_TIMESTAMPS
#define CC2420_CONF_SFD_TIMESTAMPS 1


/* Disable compression threshold for consistent and predictable compression */
#undef SICSLOWPAN_CONF_COMPRESSION_THRESHOLD
#define SICSLOWPAN_CONF_COMPRESSION_THRESHOLD 0

/* Enable ContikiMAC header for MAC padding */
#undef CONTIKIMAC_CONF_WITH_CONTIKIMAC_HEADER
#define CONTIKIMAC_CONF_WITH_CONTIKIMAC_HEADER 0
#endif

#undef CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
#define CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION 1

/* Space saving */
#undef UIP_CONF_TCP
#define UIP_CONF_TCP             0
#undef SICSLOWPAN_CONF_FRAG
#define SICSLOWPAN_CONF_FRAG     0
#undef SICSLOWPAN_CONF_FRAG
#define UIP_CONF_DS6_ADDR_NBU    1
#undef UIP_CONF_BUFFER_SIZE
#define UIP_CONF_BUFFER_SIZE   160
#undef UIP_CONF_UDP_CONNS
#define UIP_CONF_UDP_CONNS       2
#undef UIP_CONF_FWCACHE_SIZE
#define UIP_CONF_FWCACHE_SIZE    4

#endif /* __PROJECT_CONF_H__ */
