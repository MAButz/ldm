
ldmwwmdir = $(libexecdir)/ldm
ldmwwm_PROGRAMS = wwm
wwm_SOURCES = wwm.h \
		wwm-defaults.h \
		client.c \
		events.c \
		main.c \
		mapfunctions.c \
		misc.c \
		new.c \
		screen.c \
		sendkey.c

wwm_LDADD = -L/usr/X11R6/lib \
			-lX11 -lXext

wwm_CFLAGS = -Wstrict-prototypes -Wpointer-arith \
			 -Wcast-align -Wcast-qual -Wshadow \
			 -Waggregate-return -Wnested-externs \
			 -Winline -Wwrite-strings -Wundef
