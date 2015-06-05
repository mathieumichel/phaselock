all: net-test
CONTIKI=/home/macfly/Git/contiki

APPS += powertrace

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
CFLAGS += -DPERF_TEST=1 -DCONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION=1

PROJECT_SOURCEFILES = contikimac.c csma.c cooja-debug.c softack.c cc2420-softack.c


CONTIKI_WITH_RIME = 1
#include Makefile.include
include $(CONTIKI)/Makefile.include
