override CFLAGS = -I../include -g
override LDLIBS = -lpthread

SRCS = \
       test1.c \
       test2.c \
       test3.c \
       test4.c \
       test5.c \
       test6.c \
       test7.c \
       test8.c \
       test9.c \
       test10.c \
       test11.c

BINS = $(SRCS:.c=)

.PHONY: all
all: $(BINS)

$(BINS): ../lib/libaflsnapshot.o

.PHONY: clean
clean:
	$(RM) $(BINS)

.PHONY: test
test: all
	for test in test*; \
	do \
		if [ -x $${test} ]; \
		then \
			./$$test; \
		fi; \
	done
