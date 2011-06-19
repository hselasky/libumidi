#
# $FreeBSD: $
#
# Copyright (c) 2011 Hans Petter Selasky. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
#
# Makefile for USB MIDI library
#

.if exists(%%PREFIX%%)
PREFIX=		%%PREFIX%%
.else
PREFIX=		/usr/local
.endif

.if exists(${PREFIX}/include/jack/jack.h)
HAVE_JACK=
.endif

LIB=		umidi20
SHLIB_MAJOR=	2
SHLIB_MINOR=	0
CFLAGS+=	-Wall -O2 -O3
LDADD+=		-lpthread

SRCS+=		umidi20.c
SRCS+=		umidi20_file.c
SRCS+=		umidi20_assert.c
SRCS+=		umidi20_gen.c
INCS=		umidi20.h

.if defined(HAVE_JACK)
SRCS+=		umidi20_jack.c
LDADD+=		-L${PREFIX}/lib -ljack
CFLAGS+=	-I${PREFIX}/include
CFLAGS+=	-DHAVE_JACK
.else
SRCS+=		umidi20_jack_dummy.c
.endif

.if defined(HAVE_DEBUG)
CFLAGS+=	-DHAVE_DEBUG
.endif

MKLINT=		no

NOGCCERROR=
NO_PROFILE=

MAN=	# no manual pages at the moment

.include <bsd.lib.mk>
