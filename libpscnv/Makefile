all: libpscnv.a

libpscnv.a: libpscnv.o libpscnv_ib.o
	ar cru libpscnv.a libpscnv.o libpscnv_ib.o
	ranlib libpscnv.a

%.o: %.c ../pscnv/pscnv_drm.h
	gcc -O3 -I../pscnv -I/usr/include/libdrm -c -o $@ $< -g

clean:
	rm -f *.o libpscnv.a
