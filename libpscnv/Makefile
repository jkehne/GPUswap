all: libpscnv.a

libpscnv.a: libpscnv.o
	ar cru libpscnv.a libpscnv.o
	ranlib libpscnv.a

%.o: %.c ../pscnv/pscnv_drm.h
	gcc -I../pscnv -I/usr/include/libdrm -c -o $@ $< -g

clean:
	rm -f *.o libpscnv.a
