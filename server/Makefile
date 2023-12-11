# 4. Write a Makefile which includes:
#
# * A default target which builds the “writer” application
# * A clean target which removes the “writer” application and all .o files
# * Support for cross-compilation.  You should be able to generate an application for the native build platform when 
#    GNU make variable CROSS_COMPILE is not specified on the make command line.  When CROSS_COMPILE is specified with 
#    aarch64-none-linux-gnu- (note the trailing -)your makefile should compile successfully using the cross compiler 
#    installed in step 1.

# References
# * 21st Century C, 2nd edition, by Ben Klemens. O'Reilly, September 2014.
# * The GNU Make Manual, for GNU make version 4.4.1, by the Free Software 
#   Foundation. Self-published, February 2023.
# * The class's GitHub repository sample makefile, retrieved from
#   https://github.com/cu-ecen-aeld/ecen5013-hello-world/blob/master/Makefile
#   on December 9th, 2023.
#

# DONE: clean target and cross-compile target

P=aesdsocket
SOURCES= aesdsocket.c
OBJECTS= $(SOURCES:.c=.o)

all: $(P)

debug: CCFLAGS += -DDEBUG -g
debug: all

$(P).o: $(P).c
	$(CC) $(CCFLAGS) -Wall -std=c11 -D_POSIX_C_SOURCE=200809L -c $(SOURCES)

$(P): $(OBJECTS)
	$(CC) $(LDFLAGS) -lpthread -pthread $(OBJECTS) -o $(P)

clean:
	rm -f $(OBJECTS) $(P)

