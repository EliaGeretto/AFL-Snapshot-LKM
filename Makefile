.PHONY: all clean code-format test install

all:
	cd src && $(MAKE)
	cd lib && $(MAKE)
	cd test && $(MAKE)

clean:
	cd src && $(MAKE) clean
	cd lib && $(MAKE) clean
	cd test && $(MAKE) clean

code-format:
	./.custom-format.py -i src/*.c
	./.custom-format.py -i src/*.h
	./.custom-format.py -i lib/*.c
	./.custom-format.py -i lib/*.h
	./.custom-format.py -i include/*.h

test: all
	sudo rmmod afl_snapshot || echo "Not loaded anyways..."
	sudo insmod src/afl_snapshot.ko
	cd test && $(MAKE) test

install: all
	cd src && $(MAKE) modules_install
	cd lib && $(MAKE) install