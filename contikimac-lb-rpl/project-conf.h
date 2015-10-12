#ifndef __PROJECT_CONF_H__
#define __PROJECT_CONF_H__



#define WITH_ADVANCED_PHASELOCK 1
#undef CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
#define CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION     1

#define WITH_CSMA_ADVANCED 0
#define IN_UMONS 1


#define WITH_LB 0
#define WITH_DIO_TARGET 0

/* The IEEE 802.15.4 channel in use */
#undef RF_CHANNEL
#define RF_CHANNEL            26//15

/* The cc2420 transmission power (min:0, max: 31) */
#define RF_POWER                31

/* The cc2420 RSSI threshold (-32 is the reset value for -77 dBm) */
#define RSSI_THR        (-32-14)

/* The ContikiMAC wakeup interval */
#define CONTIKIMAC_CONF_CYCLE_TIME (RTIMER_ARCH_SECOND/2)

//#define RTIMER_CONF_SECOND (4096UL*8)
typedef uint32_t rtimer_clock_t;
#define RTIMER_CLOCK_LT(a,b)     ((int32_t)(((rtimer_clock_t)a)-((rtimer_clock_t)b)) < 0)

/* Makes RPL more reactive */
#define RPL_CONF_INIT_LINK_METRIC 2
/* To equal RPL_DAG_MC_ETX_DIVISOR */
#define RPL_CONF_MIN_HOPRANKINC 128
/* Use Contikimac phase lock */
#undef CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
#define CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION 1
/* RPL without downward routes. Modify if you need more than upward-only. */
#undef RPL_CONF_MOP
#define RPL_CONF_MOP RPL_MOP_NO_DOWNWARD_ROUTES  //collect-only
//#define RPL_CONF_MOP RPL_MOP_STORING_NO_MULTICAST  //any-to-any
/* Run without IPv6 NA/ND */
#undef UIP_CONF_ND6_SEND_NA
#define UIP_CONF_ND6_SEND_NA 0
/* Disable DIS sending */
#undef RPL_DIS_SEND_CONF
#define RPL_DIS_SEND_CONF 0



#define RPL_CONF_MAX_INSTANCES    1 /* default 1 */

/* The current ORPL implementation assumes a single DAG */
#define RPL_CONF_MAX_DAG_PER_INSTANCE 1 /* default 2 */

#undef CONTIKIMAC_CONF_CCA_COUNT_MAX_TX
#define CONTIKIMAC_CONF_CCA_COUNT_MAX_TX 2 /* default 6 */

/* Our softack implementation for cc2420 requires to disable DCO synch */
#undef DCOSYNCH_CONF_ENABLED
#define DCOSYNCH_CONF_ENABLED 0

#undef UIP_CONF_IPV6
#define UIP_CONF_IPV6 1

/* Our softack implementation for cc2420 requires SFD timestamps */
#undef CC2420_CONF_SFD_TIMESTAMPS
#define CC2420_CONF_SFD_TIMESTAMPS 1

/* Contiki netstack: MAC */
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC     csma_driver

/* Contiki netstack: RDC */
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC     contikimac_driver

/* Contiki netstack: RADIO */
#undef NETSTACK_CONF_RADIO
#define NETSTACK_CONF_RADIO   cc2420_softack_driver

#undef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER framer_802154

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
#define CONTIKIMAC_CONF_WITH_CONTIKIMAC_HEADER 1

#undef CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
#define CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION 1


/* The neighbor table size */
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#define NBR_TABLE_CONF_MAX_NEIGHBORS 10//48


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


/* Disable UDP checksum, needed as we have mutable fields (hop count and fpcount) in the data packet */
#undef UIP_CONF_UDP_CHECKSUMS
#define UIP_CONF_UDP_CHECKSUMS   0

#endif /* __PROJECT_CONF_H__ */
