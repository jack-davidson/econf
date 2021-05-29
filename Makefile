.POSIX:

SRC = econf.c
OBJ = econf.o

${CC} = c89

all: clean econf

econf: ${OBJ}
	${CC} ${OBJ} -o econf ${LDFLAGS}

${OBJ}:
	${CC} -c ${SRC} ${CFLAGS}

clean:
	rm -f ${OBJ} econf
