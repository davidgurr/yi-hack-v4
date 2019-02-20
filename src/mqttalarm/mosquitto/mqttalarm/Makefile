include ../config.mk

.PHONY: all install uninstall reallyclean clean static static_alarm

ifeq ($(WITH_SHARED_LIBRARIES),yes)
SHARED_DEP:=../lib/libmosquitto.so.${SOVERSION}
endif

ifeq ($(WITH_SHARED_LIBRARIES),yes)
ALL_DEPS:= alarm
else
ifeq ($(WITH_STATIC_LIBRARIES),yes)
ALL_DEPS:= static_alarm
endif
endif

all : ${ALL_DEPS}

static : static_alarm
	# This makes mosquitto_pub/sub versions that are statically linked with
	# libmosquitto only.

static_alarm : mqttalarm.o ../lib/libmosquitto.a
	${CROSS_COMPILE}${CC} $^ -o mqttalarm ${CLIENT_LDFLAGS}

mqttalarm.o : mqttalarm.c ${SHARED_DEP}
	${CROSS_COMPILE}${CC} -c $< -o $@ ${CLIENT_CFLAGS}

../lib/libmosquitto.so.${SOVERSION} :
	$(MAKE) -C ../lib

../lib/libmosquitto.a :
	$(MAKE) -C ../lib libmosquitto.a

install : all
	$(INSTALL) -d "${DESTDIR}$(prefix)/bin"
	$(INSTALL) ${STRIP_OPTS} mqttalarm "${DESTDIR}${prefix}/bin/mqttalarm"

uninstall :
	-rm -f "${DESTDIR}${prefix}/bin/mqttalarm

reallyclean : clean

clean : 
	-rm -f *.o mqttalarm

