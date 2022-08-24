CROSS_COMPILE ?=
AS		?= $(CROSS_COMPILE)as
LD		?= $(CROSS_COMPILE)ld
CC		?= $(CROSS_COMPILE)gcc
CPP		?= $(CROSS_COMPILE)g++
AR		?= $(CROSS_COMPILE)ar
NM		?= $(CROSS_COMPILE)nm
STRIP	?= $(CROSS_COMPILE)strip
OBJCOPY	?= $(CROSS_COMPILE)objcopy
OBJDUMP	?= $(CROSS_COMPILE)objdump

LIB = libhal_bluetooth.so
SRCS = src/bluetooth.c src/bluetoothctl.c src/bluez.c

SRCDIR = src
OBJDIR = obj
OBJECTS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

TEST_PROGRAM = $(basename $(wildcard test/*.c))
EXAMPLE_PROGRAM = $(basename $(wildcard example/*.c))

CFLAGS += -I./$(SRCDIR) -I./include $$(pkg-config --cflags dbus-1)
CFLAGS += -O2
CFLAGS += -Wall -Wextra -Wno-stringop-truncation -fPIC
LDFLAGS += $$(pkg-config --libs dbus-1)

.PHONY: all
all: $(LIB) test example

.PHONY: test
test: $(TEST_PROGRAM)

.PHONY: example
example: $(EXAMPLE_PROGRAM)

.PHONY: clean
clean:
	rm -rf $(LIB) $(OBJDIR) $(TEST_PROGRAM)

test/%: test/%.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

example/%: example/%.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

$(OBJECTS): | $(OBJDIR)

$(OBJDIR):
	mkdir $(OBJDIR)

$(LIB): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared $^ -o $@ 
	$(STRIP) -s $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(LDFLAGS) -c $< -o $@