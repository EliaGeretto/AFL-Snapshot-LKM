override CFLAGS += -I../include -fPIC
override LDFLAGS += -shared

TARGET_LIB = libaflsnapshot.so

SRCS = libaflsnapshot.c
OBJS = $(SRCS:.c=.o)

PREFIX ?= /usr/local

.PHONY: all clean install

all: $(TARGET_LIB)

$(TARGET_LIB): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	$(RM) $(OBJS) $(TARGET_LIB)

install: $(TARGET_LIB)
	install -m 644 $(TARGET_LIB) $(DESTDIR)$(PREFIX)/lib
	ldconfig

	install -m 644 ../include/* $(DESTDIR)$(PREFIX)/include