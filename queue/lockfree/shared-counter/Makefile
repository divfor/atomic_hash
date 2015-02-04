CC 	= gcc
MKDIR	= mkdir
RM	= rm -fr
T1	= normal
T2	= locked
T3	= aaf
T4	= cas
T5	= dcas
T6	= aba
T7	= aba-avoidance
OBJDIR 	= .objs
CSRC 	= $(shell ls *.c)
OBJS 	= $(patsubst %.c,$(OBJDIR)/%.o, $(CSRC))
LDFLAGS	= -lpthread
ARCH	= $(shell uname -m)
CFLAGS	= -ggdb -O2 -Wall -fno-strict-aliasing \
	  -funroll-loops -fomit-frame-pointer \
	  -DARCH=\"$(ARCH)\"
TIME    = time -f "%E real,%U user,%S sys"
#CFLAGS	+= -mcx16 -DGCC_ATOMIC_BUILTINS 
#CFLAGS += -m64 -DGCC_ATOMIC_BUILTINS
#CFLAGS += -m32

all: $(OBJDIR) $(OBJS)
	$(CC) $(CFLAGS) -o $(T1) $(OBJDIR)/$(T1).o $(LDFLAGS) 
	$(CC) $(CFLAGS) -o $(T2) $(OBJDIR)/$(T2).o $(LDFLAGS) 
	$(CC) $(CFLAGS) -o $(T3) $(OBJDIR)/$(T3).o $(LDFLAGS) 
	$(CC) $(CFLAGS) -o $(T4) $(OBJDIR)/$(T4).o $(LDFLAGS) 
	$(CC) $(CFLAGS) -o $(T5) $(OBJDIR)/$(T5).o $(LDFLAGS) 
	$(CC) $(CFLAGS) -o $(T6) $(OBJDIR)/$(T6).o $(LDFLAGS) 
	$(CC) $(CFLAGS) -o $(T7) $(OBJDIR)/$(T7).o $(LDFLAGS) 

$(OBJDIR):
	$(MKDIR) $(OBJDIR)

$(OBJDIR)/%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean: 
	$(RM) $(OBJS)
	$(RM) $(T1) $(T2) $(T3) $(T4) $(T5) $(T6) $(T7) 
	$(RM) $(OBJDIR)

test:
	$(TIME)	nice -n 20 ./normal 64 10000000
	$(TIME) nice -n 20 ./locked 64 10000000
	$(TIME) nice -n 20 ./cas 64 10000000
	$(TIME) nice -n 20 ./dcas 64 10000000
	$(TIME) nice -n 20 ./aaf 64 10000000

