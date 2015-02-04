CC 	= gcc
MKDIR	= mkdir
RM	= rm -fr
TARGET 	= lfq-test
OBJDIR 	= .objs
CSRC 	= $(shell ls *.c)
OBJS 	= $(patsubst %.c,$(OBJDIR)/%.o, $(CSRC))
LDFLAGS	= -lpthread
ARCH	= $(shell uname -m)
CFLAGS	= -ggdb -O3 -Wall -fno-strict-aliasing \
	  -funroll-loops -fomit-frame-pointer \
	  -DARCH=\"$(ARCH)\"
#CFLAGS	+= -mcx16 -DGCC_ATOMIC_BUILTINS 
#CFLAGS += -m64 -DGCC_ATOMIC_BUILTINS
#CFLAGS += -m32

all: $(OBJDIR) $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS) 

$(OBJDIR):
	$(MKDIR) $(OBJDIR)

$(OBJDIR)/%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean: 
	$(RM) $(OBJS)
	$(RM) $(TARGET)
	$(RM) $(OBJDIR)

