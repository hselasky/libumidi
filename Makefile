#
# Copyright (c) 2011-2022 Hans Petter Selasky. All rights reserved.
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
# Makefile for Universal MIDI library
#

PREFIX?=	/usr/local
LOCALBASE?=	/usr/local
BINDIR=		${PREFIX}/sbin
MANDIR=		${PREFIX}/man/man
LIBDIR=		${PREFIX}/lib
INCLUDEDIR=	${PREFIX}/include
MKLINT=		no
MK_WERROR=	no
PTHREAD_LIBS?=	-lpthread

LIB=		umidi20
SHLIB_MAJOR=	1
SHLIB_MINOR=	0
CFLAGS+=	-D_GNU_SOURCE
LDADD+=		${PTHREAD_LIBS}

WARNS=		3

SRCS+=		umidi20.c
SRCS+=		umidi20_file.c
SRCS+=		umidi20_gen.c
SRCS+=		umidi20_pipe.c
INCS=		umidi20.h

.if defined(HAVE_CDEV)
SRCS+=		umidi20_cdev.c
.else
SRCS+=		umidi20_cdev_dummy.c
.endif

.if defined(HAVE_ALSA)
SRCS+=		umidi20_alsa.c
LDADD+=		-L${PREFIX}/lib -lasound
CFLAGS+=	-I${PREFIX}/include
.else
SRCS+=		umidi20_alsa_dummy.c
.endif

.if defined(HAVE_JACK)
SRCS+=		umidi20_jack.c
LDADD+=		-L${PREFIX}/lib -ljack
CFLAGS+=	-I${PREFIX}/include
.else
SRCS+=		umidi20_jack_dummy.c
.endif

.if defined(HAVE_COREMIDI)
SRCS+=		umidi20_coremidi.c
LDADD+=		-framework CoreMIDI
CFLAGS+=	-I${PREFIX}/include
.else
SRCS+=		umidi20_coremidi_dummy.c
.endif

.if defined(HAVE_ANDROID)
LDADD+=		-L${PREFIX}/lib -ljvm
SRCS+=		umidi20_android.c
.else
SRCS+=		umidi20_android_dummy.c
.endif

.if defined(HAVE_DEBUG)
CFLAGS+=	-DHAVE_DEBUG
CFLAGS+=	-g
.endif

.if defined(HAVE_MAN)
MAN=		umidi20.3
.else
MAN=
.endif

.include <bsd.lib.mk>
