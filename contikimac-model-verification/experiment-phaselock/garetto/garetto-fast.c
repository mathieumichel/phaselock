/* this is for Garetto rts-cts. Some constants differ! */
/* here we try to avoid random_rand. We run the model and
   save the run into two arrays one with the states and
   one with the number of successive visits to that
   state */
#include <stdio.h>
#include "lib/random.h"
#include "contiki.h"
#include <limits.h>
#include "process.h"
#include "dev/watchdog.h"
#include "dev/spi.h"
#include "dev/cc2420.h"
#include "dev/leds.h"
#include "dev/cc2420_const.h"


/* OBS: P depends on stations */
//#define STATIONS 10
//#define P 0.28727
// for 2 stations
//#define P 0.057044
// for 5 stations
//#define P 0.17795
// for 20 stations
#define P 0.41020

#define bSTATE_MIN 0
#define BSTATE_MIN 10

/* we need to turn these values into clock_delay numbers,
   CA says "it looks like the execution of the instruction is 4 uS",
   so e.g. for TO, we do (20-4)*0.78 = 21 */
//#define TS 1.823  /* time for successful transmission of 1000 Bytes packet in ms */
//#define TS_DELAY 2332  /* in 0.78 slots */
#define TS 2.200  /* time for successful transmission of 1500 Bytes packet in ms */
//#define TS_DELAY 2815  /* 1500 Bytes payload packets */
#define TS_DELAY 2332  /* this is 1000 Bytes payload packet */
#define TC 0.656  /* time for collision */
#define TC_DELAY 836
/* not during collisions, radio is not always on, need
 to deducte EIFS which is 364 us */
#define TC_EIFS     461
#define TC_ON_DELAY 836-TC_EIFS
#define TO 0.02   /* slot time */
#define TO_DELAY 21


#define NRSTATES 8
static int b[NRSTATES];
static int B[NRSTATES];
static int bctr[NRSTATES];
static int Bctr[NRSTATES];
static int W[NRSTATES];
static float ALFA[NRSTATES];
static float BETA[NRSTATES];


// Define settings (CHANGE ME!)
#define MIN_POWER 0
#define MAX_POWER 31
#define INTERFERED_CHANNEL 15

// FAST SPI OPERATIONS
#define NOP_DELAY() \
  do {				    \
    _NOP(); _NOP(); _NOP(); _NOP(); \
    _NOP(); _NOP(); _NOP(); _NOP(); \
    _NOP(); _NOP(); _NOP(); _NOP(); \
    _NOP(); _NOP(); _NOP(); _NOP(); \
  } while (0);
/* -------------------------------------------------- */
#define SPI_SET_UNMODULATED(a,b,c,d)\
  do {				    \
    /* Enable SPI */				\
    /*SPI_ENABLE();\*/				\
    /* Setting CC2420_DACTST */			\
    SPI_TXBUF = CC2420_DACTST;			\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) ((a) >> 8));			\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) (a));				\
    NOP_DELAY();					\
    /* Setting CC2420_MANOR */				\
    SPI_TXBUF = CC2420_MANOR;				\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) ((b) >> 8));			\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) (b));				\
    NOP_DELAY();					\
    /* Setting CC2420_MDMCTRL1 */			\
    SPI_TXBUF = CC2420_MDMCTRL1;			\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) ((c) >> 8));			\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) (c));				\
    NOP_DELAY();					\
    /* Setting CC2420_TOPTST */				\
    SPI_TXBUF = CC2420_TOPTST;				\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) ((d) >> 8));			\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) (d));				\
    NOP_DELAY();					\
    /* STROBE STXON */					\
    SPI_TXBUF = CC2420_STXON;				\
    NOP_DELAY();					\
    /* Disable SPI */					\
    /*SPI_DISABLE();\*/					\
} while (0)

/* -------------------------------------------------- */
#define SPI_UNSET_CARRIER()\
  do {						\
    /* Enable SPI */				\
    /*SPI_ENABLE();\*/				\
    /* TURN OFF THE RADIO */			\
    SPI_TXBUF = CC2420_SRFOFF;			\
    NOP_DELAY();				\
    /* Disable SPI */				\
    /*SPI_DISABLE();\*/				\
  } while (0)
/* -------------------------------------------------- */
#define SPI_SET_MODULATED(c)\
  do {						\
    /* Enable SPI */				\
    /*SPI_ENABLE();\*/				\
    /* Setting MDMCTRL1 */			\
    SPI_TXBUF = CC2420_MDMCTRL1;		\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) ((c) >> 8));			\
    NOP_DELAY();					\
    SPI_TXBUF = ((u8_t) (c));				\
    NOP_DELAY();					\
    /* STROBE STXON */					\
    SPI_TXBUF = CC2420_STXON;				\
    NOP_DELAY();					\
    /* Disable SPI */					\
    /*SPI_DISABLE();\*/					\
  } while (0)         

// MACROS for switching on/off the modulated carrier
static void reset_modulated() // Resets a modulated carrier
{
 SPI_UNSET_CARRIER(); //SPI_SET_MODULATED(0x0500);
}
static void reset_unmodulated() // Resets an unmodulated carrier
{
 SPI_UNSET_CARRIER(); //SPI_SET_UNMODULATED(0x0000,0x0000,0x0500,0x0010);
}
static void unmodulated() // Sends an unmodulated carrier
{
 SPI_SET_UNMODULATED(0x1800,0x0100,0x0508,0x0004);
}
static void modulated() // Sends a modulated carrier
{
 SPI_SET_MODULATED(0x050C);
}


static int intf_on = 0;    // global flag to denote if interferer is on

/*---------------------------------------------------------------------------*/
PROCESS(markov_process, "WLAN emulated Interferer");
AUTOSTART_PROCESSES(&markov_process);
/*---------------------------------------------------------------------------*/
// Controls the jammer. interference_type: 0 = Modulated, !0 = Unmodulated; on_off: 0 = OFF, 1 = ON
void jammer_control(int interference_type, int on_off)
{
  if(interference_type == 0){
    if(on_off == 0){
      if (intf_on) {
	//printf("off\n");
	leds_off(LEDS_RED);
	intf_on = 0;
	reset_modulated();
      }
    }
    else {
      if (!intf_on) {
	//printf("on\n");
	leds_on(LEDS_RED);
	intf_on = 1;
	modulated();
      }
    }
  }
  else {
    if(on_off == 0){
      if (intf_on) {
	//printf("off\n");
	leds_off(LEDS_RED);
	intf_on = 0;
	reset_unmodulated();
      }
    }
    else {
      if (!intf_on) {
	//printf("on\n");
	leds_on(LEDS_RED);
	intf_on = 1;
	unmodulated();
      }
    }
  }
}
/*---------------------------------------------------------------------------*/

static int oldstate=0;
static int same = 0;
static int newstate = 0;
/* this is for saving state */

#define SAVESTATES 1000  // was 2600
static uint8_t sstate[SAVESTATES];
static uint8_t visits[SAVESTATES];
static int cur_state = 0;
static int cur_visits = 0;

int maxsame=0;
int maxmax = 0;
int cs = 0;       /* current state */
int statevs = 0;  /* visits to this state */

/*---------------------------------------------------------------------------*/
int compute_state(int cstate) {
  //float r = rand()/((float)(RAND_MAX)+1);   orig c code
  float r = (float)(random_rand()%32768)/(32768); 
  float p1, p2, p3, p4;
  int i;


  //printf("state %d\n", state);
  if (oldstate == cstate) {
    //printf("old %d = new %d, same=%d maxmax %d\n", oldstate, cstate, same+1, maxmax+1);
    same++;    
    maxsame++;
    if (maxsame > maxmax) {
      //printf("new maxmax %d\n", maxmax);
      maxmax = maxsame;
    }
    visits[cs]++;
    if (visits[cs] == 254) { 
      // we have maximum for uint8_t */
      //printf("new update!!!\n");
      cs++;
      sstate[cs] = cstate;
      visits[cs] = 1;
    }
  }
  else {
    newstate++;
    cs++;
    sstate[cs] = cstate;
    visits[cs] = 1;
    //printf("new state %d old was %d or %d\n", cstate, sstate[cs-1],oldstate);
    maxsame = 0;
    same = 0;
  }
  oldstate = cstate;

  if (r<0 || r> 1.0)
    printf("================ ERROR ===========================================\n");

  /* b states, except for b[MAX] */
  if (cstate >= bSTATE_MIN && cstate < bSTATE_MIN+NRSTATES) {
    int s = cstate-bSTATE_MIN;

    //printf("%d\n",s); 
    //printf("in b state %d s %d B[s] %d b[s] %d B[s+1] %d \n", cstate, s, 
    //B[s], b[s], B[s+1]);
    /* when we come to this state, we turn on the interferer, if not
       already turned on, for timeb0. This time depends on next state */
    bctr[s]++;
    p1 = (1-P)*ALFA[0];
    p2 = (1-P)*(1-ALFA[0]);
    p3 = P*ALFA[s+1];
    p4 = P*(1-ALFA[s+1]);
    if (r < p1) {
      return (b[0]);
    }	
    else if (r>= p1 && r < p1+p2) {
      return (B[0]);
    }
    else if (r>= p1+p2 && r < p1+p2+p3) {
      return (b[s+1]);
    }
    else {
      return (B[s+1]);
    }
  } 
  /* state b[max] */
  if (cstate == bSTATE_MIN + NRSTATES) {
    int s = bSTATE_MIN + NRSTATES;
    //printf("%d\n",s); 
    bctr[s]++;
    if (r<ALFA[0]) {
      return (b[0]);
    }
    else {
      return (B[0]);
    }
  }
  else if (cstate >= BSTATE_MIN && cstate <= BSTATE_MIN+NRSTATES) {
    int s = cstate-BSTATE_MIN;
    //printf("%d\n",s); 
    //printf("in B ctate %d s %d B[s] %d b[s] %d\n", cstate, s, B[s], b[s]);
    Bctr[s]++;
    if (r<BETA[s]) {
      return (b[s]);
    }
    else {
      return (B[s]);
    }
  }
  else
    printf("compute-state: EORROR STATE EORROR STATE EORROR STATE %d\n", cstate);
}

static int ctr = 0;


/*---------------------------------------------------------------------------*/
void update_state() {
  static int cstate;              /* current state */
  static int next_state;
  static int same=0;

  while (1) {
    ctr = 0;
    while (ctr < SAVESTATES-1) {
      cstate = sstate[ctr];
      next_state = sstate[ctr];
      
      /* XXX check if correct when full */
      if (cur_visits == visits[ctr]-1)
	next_state = sstate[ctr+1];
      
      //printf("in %d, next %d\n", cstate, next_state); 
      //printf("curvisits %d, visits[%d]=%d\n", cur_visits, ctr, visits[ctr]); 
      if (cstate == next_state) {
	same++;
	//printf("same %d\n", same);
      }
      else
	same = 0;
      
      if (cstate >= bSTATE_MIN && cstate < bSTATE_MIN+NRSTATES) {
	int s = cstate-bSTATE_MIN;
	
	if (!intf_on)
	  jammer_control(0,1); //modulated();
	
	//printf("in b state %d s %d B[s] %d b[s] %d B[s+1] %d \n", cstate, s, 
	//B[s], b[s], B[s+1]);
	/* when we come to this state, we turn on the interferer, if not
	   already turned on, for timeb0. This time depends on next state */
	bctr[s]++;
	if (next_state == b[0]) {
	  //printf("TS_DELAY\n");
	  clock_delay(TS_DELAY);
	}
	else if (next_state == B[0])
	  clock_delay(TS_DELAY);
	else if (next_state == b[s+1]) {
	  clock_delay(TC_DELAY);
	  // turn off jammer
	  jammer_control(0,0); //modulated();
	  clock_delay(TC_EIFS);
	}
	else if (next_state == B[s+1]) {
	  clock_delay(TC_DELAY);
	  // turn off jammer
	  jammer_control(0,0); //modulated();
	  clock_delay(TC_EIFS);
	}
	else 
	  printf("ERROR states bi\n");
      }
      /* state b[max] */
      else if (cstate == bSTATE_MIN + NRSTATES) {
	int s = bSTATE_MIN + NRSTATES;
	
	if (!intf_on)
	  jammer_control(0,1); //modulated();
	bctr[s]++;
    
	if (next_state == b[0]) {
	  clock_delay(TC_DELAY);
	  // turn off jammer
	  jammer_control(0,0); //modulated();
	  clock_delay(TC_EIFS);
	}
	else if (next_state == B[0]) {
	  clock_delay(TC_DELAY);
	  // turn off jammer
	  jammer_control(0,0); //modulated();
	  clock_delay(TC_EIFS);
	}
	else 
	  printf("ERROR states bi-max\n");
      }
      else if (cstate >= BSTATE_MIN && cstate <= BSTATE_MIN+NRSTATES) {
	int s = cstate-BSTATE_MIN;
	
	if (intf_on)
	  jammer_control(0,0); //modulated();
	//printf("in B ctate %d s %d B[s] %d b[s] %d\n", cstate, s, B[s], b[s]);
	Bctr[s]++;
	if (next_state == b[s])
	  clock_delay(TO_DELAY);
	else if (next_state == B[s])
	  clock_delay(TO_DELAY);
	else 
	  printf("ERROR states Bi\n");
      }
      else
	printf("EORROR STATE EORROR STATE EORROR STATE %d\n", cstate);
      
      cur_visits++;
      /* now update states */
      if (cur_visits == visits[ctr]) {
	ctr++;
	cur_visits = 0;
	//printf("new state\n");
      }
    }
  }
}



PROCESS_THREAD(markov_process, ev, data) {
  int new_state = b[0];
  int i,j;

  PROCESS_BEGIN();

  //etimer_set(&et, CLOCK_SECOND);
  //PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

  // Initial configurations  on CC2420
  cc2420_set_txpower(MAX_POWER);
  cc2420_set_channel(INTERFERED_CHANNEL);
  watchdog_stop();
  
  printf("RAND_MAX %d INTMAX %d\n", RANDOM_RAND_MAX, INT_MAX);
  random_init(19);

  
  /* XXX should use formula */
  W[0]= 31;
  W[1]= 62;
  W[2]= 124;
  W[3]= 248;
  W[4]= 496;
  W[5]= 992;
  W[6]= 1023;
  W[7]= 1023;

  for(i=0;i<NRSTATES;i++) {
    b[i] = bSTATE_MIN+i;
    B[i] = BSTATE_MIN+i;
    bctr[i] = 0;
    Bctr[i] = 0;
    ALFA[i] = 2.0/((float)W[i]);
    BETA[i] = 2.0/((float)(W[i]-1));
  }

  printf("a %d, b %d, c %d d %d BETA1 %d\n", 
	 100*((1-P)*ALFA[0]),
	 100*((1-P)*(1-ALFA[0])),
	 100*(P*ALFA[1]),
	 100*(P*(1-ALFA[1])),
	 100*BETA[1]);


  /* this is the first "dry" run where we compute the state for
     the real run */
  new_state = b[0];
  for(i=0;i<SAVESTATES;i++) {
    sstate [i] = 0;
    visits[i] = 0;
  }
  while (cs<SAVESTATES) {
    new_state = compute_state(new_state);
  }
  new_state = b[0];

  /* now we start the real run */
  cur_state = 0;
  cur_visits = 0;

  CC2420_SPI_ENABLE();

#if 0
  while (cs<) {
    ctr++;
    if (ctr % 100 == 0) {
      int ctr_sum = 0;
      printf("time %f\n", time);
      float b = 0;
      for(i=bSTATE_MIN;i<bSTATE_MIN+NRSTATES;i++) {
	printf("counter[%d]: %d\n", i, bctr[i]);  
	ctr_sum += bctr[i];
	b += timeb[i];
      }
      for(i= 0;i<NRSTATES;i++) {
	printf("counter[%d]: %d\n", i, Bctr[i]);  
	ctr_sum += Bctr[i];
      }
      b = b / time;
      printf("counter %d percent in b: %f\n", ctr_sum, b);
    }
    if (ctr > MAX) {
      int ctr_sum = 0;
      float b = 0;
      for(i=bSTATE_MIN;i<bSTATE_MIN+NRSTATES;i++) {
	printf("counter[%d]: %d\n", i, bctr[i]);  
	ctr_sum += bctr[i];
      }
      for(i= 0;i<NRSTATES;i++) {
	printf("counter[%d]: %d\n", i, Bctr[i]);  
	ctr_sum += Bctr[i];
      }
      printf("counter %d percent in b: %f\n", ctr_sum, b);
      printf("ctrs: b0 %d B0 %d b1 %d B1 %d b5 %d B5 %d\n", 
	     bctr[0], Bctr[0], bctr[1], Bctr[1], bctr[5], Bctr[5]); 
      exit(1);
    }
    update_state();
  }
#endif
  printf("after modelling\n");
  j=0;
  for(i=0;i<SAVESTATES;i++) {
    //printf("state %d visits% d\n", sstate[i], visits[i]);
    j += visits[i];
  }
  printf("sum visits %d\n", j);
  /* correct very first state */
  if (visits[0] == 0)
    visits[0] = 1;

  ctr = 0;
  printf("--------- real run -------------\n");
  update_state();

  CC2420_SPI_DISABLE();
  PROCESS_END();
}


