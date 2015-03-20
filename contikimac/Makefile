CONTIKI=/home/macfly/contiki-2.7

CONTIKI_PROJECT = app-collect-only
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
TARGET=sky

WITH_UIP6=1
UIP_CONF_IPV6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL

PROJECTDIRS += tools net net/mac
PROJECT_SOURCEFILES += deployment.c simple-energest.c rpl-tools.c phase.c contikimac.c csma.c sicslowpan.c cc2420-softack.c softack.c
all: $(CONTIKI_PROJECT)

include Makefile.include
