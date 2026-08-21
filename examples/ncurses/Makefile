all:
	ki object main.ki
	ki object -freestanding ncurses.ki
	gcc -g -no-pie -c wrapper.c
	gcc -znoexecstack -nostartfiles -no-pie main.o ncurses.o wrapper.o /usr/local/share/ki/libki.a -lncurses
	rm *.o