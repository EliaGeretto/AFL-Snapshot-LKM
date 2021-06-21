override CFLAGS += -I../include -fPIC
override LDFLAGS += -shared

TARGET_LIB = libaflsnapshot.so

SRCS = libaflsnapshot.c
OBJS = $(SRCS:.c=.o)

.PHONY: all
all: $(TARGET_LIB)

$(TARGET_LIB): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

.PHONY: clean
clean:
	$(RM) $(OBJS) $(TARGET_LIB)
