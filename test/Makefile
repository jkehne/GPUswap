PROGS = get_param gem map m2mf loop subc0 ib mem_test 902d bo_refcnt

all: $(PROGS)

../libpscnv/libpscnv.a:
	make -C ../libpscnv libpscnv.a

%: %.c ../libpscnv/libpscnv.h ../libpscnv/libpscnv.a
	gcc -O3 -I../libpscnv -I/usr/include/libdrm -o $@ $< ../libpscnv/libpscnv.a -ldrm -g

clean:
	rm -f $(PROGS)
