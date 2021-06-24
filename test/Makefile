override CFLAGS = -I../include -g

SRCS = test1.c test2.c test3.c test4.c test5.c
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