#
# $FreeBSD: $
#
# Makefile for shared CAPI access library
#

LIB=		umidi20
SHLIB_MAJOR=	2
SHLIB_MINOR=	0
CFLAGS+=	-Wall 

SRCS=		umidi20.c umidi20.h umidi20_file.c umidi20_assert.c

MKLINT=		no

NOGCCERROR=

MAN=	# no manual pages at the moment

.include <bsd.lib.mk>
