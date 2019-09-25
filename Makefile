all: jarchufi config.h

CFLAGS = -DLINUX

clean:
	rm -f *.exe *.o jarchufi
	rm -f -R Debug Release
	rm -f *.ncb *.opt *.plg *.ilk

install: jarchufi
	cp -f jarchufi /usr/bin/

OBJLIST = bitchin.o blockio.o blockio_ufi.o main.o rsapi.o

jarchufi: $(OBJLIST)
	gcc -o jarchufi -lusb $(OBJLIST)

main.o: main.c
	gcc $(CFLAGS) -c -o main.o main.c

rsapi.o: rsapi.c
	gcc $(CFLAGS) -c -o rsapi.o rsapi.c

bitchin.o: bitchin.c
	gcc $(CFLAGS) -c -o bitchin.o bitchin.c

blockio.o: blockio.c
	gcc $(CFLAGS) -c -o blockio.o blockio.c

blockio_ufi.o: blockio_ufi.c
	gcc $(CFLAGS) -c -o blockio_ufi.o blockio_ufi.c

again:
	make clean && make

