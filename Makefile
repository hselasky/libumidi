#
# Copyright (c) 2011-2013 Hans Petter Selasky. All rights reserved.
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

VERSION=2.0.14

PREFIX?=	/usr/local
LOCALBASE?=	/usr/local
BINDIR=		${PREFIX}/sbin
MANDIR=		${PREFIX}/man/man
LIBDIR=		${PREFIX}/lib
INCLUDEDIR=	${PREFIX}/include
MKLINT=		no
NO_WERROR=
NOGCCERROR=
NO_PROFILE=
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
INCS=		umidi20.h

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

.if defined(HAVE_DEBUG)
CFLAGS+=	-DHAVE_DEBUG
CFLAGS+=	-g
.endif

.if defined(HAVE_MAN)
MAN=		umidi20.3
.else
MAN=
.endif

package:

	make clean cleandepend HAVE_MAN=YES

	tar -cvf temp.tar --exclude="*~" --exclude="*#" \
		--exclude=".svn" --exclude="*.orig" --exclude="*.rej" \
		Makefile umidi20.3 umidi20*.[ch]

	rm -rf libumidi-${VERSION}

	mkdir libumidi-${VERSION}

	tar -xvf temp.tar -C libumidi-${VERSION}

	rm -rf temp.tar

	tar --uid=0 --gid=0 -jcvf libumidi-${VERSION}.tar.bz2 libumidi-${VERSION}

.include <bsd.lib.mk>
