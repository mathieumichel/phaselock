CONTIKI=/home/macfly/Git/contiki

CONTIKI_PROJECT= app-collect-only
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

#CFLAGS+= -DUIP_CONF_IPV6_RPL
TARGET=sky

WITH_UIP6=1
NETSTACK_CONF_WITH_IPV6=1

PROJECTDIRS += tools net net/rpl
PROJECT_SOURCEFILES =deployment.c simple-energest.c rpl-log.c phase.c csma.c contikimac.c csma.c softack.c cc2420-softack.c
all: $(CONTIKI_PROJECT)


include Makefile.include
