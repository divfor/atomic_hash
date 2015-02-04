# indent
# cproto
# gprof   -pg
######################################
### Customising
# Adjust the following if necessary; EXECUTABLE is the target
# executable's filename, and LIBS is a list of libraries to link in
# (e.g. alleg, stdcx, iostr, etc). You can override these on make's
# command line of course, if you prefer to do it that way.

EXECUTABLE := test
LIBS := m pthread

# Now alter any implicit rules' variables if you like, e.g.:
#
#CFLAGS := -O2 -g -Wall -D_M_IX86
#CFLAGS := -O2 -g -Wall -pg 
LINKFLAGS := 
CFLAGS := -O3 -Wall -march=native -msse4.2 -D_GNU_SOURCE $(LINKFLAGS)
CXXFLAGS := $(CFLAGS)
RM-F := rm -f

# You shouldn't need to change anything below this point.
#

# SOURCE: source files (all .c and .cc in path)
SOURCE := $(wildcard *.c) $(wildcard *.cc)

# OBJS: list of all .o ( <- .c and <- .cc )
OBJS := $(patsubst %.c,%.o,$(patsubst %.cc,%.o,$(SOURCE)))

# DEPS: list of all .d ( <-.c and <- .cc )
DEPS := $(patsubst %.o,%.d,$(OBJS))

# MISSING_DEPS: missing .d files to srouce files in path
MISSING_DEPS := $(filter-out $(wildcard $(DEPS)),$(DEPS))

# MISSING_DEPS_SOURCES: source files in path but no .d file
MISSING_DEPS_SOURCES := $(wildcard $(patsubst %.d,%.c,$(MISSING_DEPS)) \
$(patsubst %.d,%.cc,$(MISSING_DEPS)))

CPPFLAGS += -MD

.PHONY : all deps objs clean rebuild

all : $(EXECUTABLE)

deps : $(DEPS)

objs : $(OBJS)

clean :
	@$(RM-F) *.o
	@$(RM-F) *.d
	@$(RM-F) $(EXECUTABLE)

rebuild: clean all

ifneq ($(MISSING_DEPS),)
$(MISSING_DEPS) :
	@$(RM-F) $(patsubst %.d,%.o,$@)
endif

-include $(DEPS)

ifeq ($(LIBS),)
$(EXECUTABLE) : $(OBJS)
	gcc $(LINKFLAGS) -o $(EXECUTABLE) $(OBJS)
else
$(EXECUTABLE) : $(OBJS)
	gcc $(LINKFLAGS) -o $(EXECUTABLE) $(OBJS) $(addprefix -l,$(LIBS))
endif

