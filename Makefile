#
# $FreeBSD: $
#
# Makefile for MIDI library
#

LIB=		umidi20
SHLIB_MAJOR=	2
SHLIB_MINOR=	0
CFLAGS+=	-Wall -O2 -O3
LDADD+=		-lpthread

SRCS=		umidi20.c umidi20_file.c umidi20_assert.c umidi20_gen.c
INCS=		umidi20.h

MKLINT=		no

NOGCCERROR=
NO_PROFILE=

MAN=	# no manual pages at the moment

.include <bsd.lib.mk>
