# config.mk
# common make rules

PRJDIR = $(HOME)/rlc_xph
CROSS_COMPILE = 

AS  = $(CROSS_COMPILE)as
LD  = $(CROSS_COMPILE)ld
CC  = $(CROSS_COMPILE)gcc
CPP = $(CC) -E
AR  = $(CROSS_COMPILE)ar
NM  = $(CROSS_COMPILE)nm
STRIP   = $(CROSS_COMPILE)strip
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
RANLIB  = $(CROSS_COMPILE)RANLIB

MKDIR	= mkdir
CP	= cp
RM	= rm -f

INCL   = . -I$(PRJDIR)/include
LIBDIR = $(PRJDIR)/lib -L.
BINDIR = $(PRJDIR)/bin

RELFLAGS = -Wall
DBGFLAGS = 
OPTFLAGS = -g

CPPFLAGS = $(DBGFLAGS) $(OPTFLAGS) $(RELFLAGS) -I$(INCL)
CFLAGS   := $(CPPFLAGS)
AFLAGS   := rcv
LDFLAGS  = -L$(LIBDIR)

# Common rules
%.o : %.s
	$(CC) $(AFLAGS) -c $<
%.o : %.S
	$(CC) $(AFLAGS) -c $<
%.o : %.c
	$(CC) $(CFLAGS) -c $<
