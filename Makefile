.POSIX:

PREFIX = /usr/local/

CFLAGS = -Wall -g -DVERSION=\"1.3\" -D_XOPEN_SOURCE=700

SRC = econf.c
OBJ = econf.o

${CC} = c89

all: clean econf

econf: ${OBJ}
	${CC} ${OBJ} -o econf ${LDFLAGS}

${OBJ}:
	${CC} -c ${SRC} ${CFLAGS}

install:
	cp -f econf ${PREFIX}/bin

clean:
	rm -f ${OBJ} econf

uninstall:
	rm -f ${PREFIX}/bin/econf
