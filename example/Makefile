PROG= hps_0011
MAN=  # no manual page at the moment
SRCS= hps_0011.c
CFLAGS += -Wall -O2
LDADD+= -lumidi20
BINDIR?=/usr/sbin
.include <bsd.prog.mk>

play:
	timidity -A 600 hps_0011.mid
