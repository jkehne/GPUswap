PROGS = get_param gem map

all: $(PROGS)

%: %.c ../pscnv/pscnv_drm.h
	gcc -I../pscnv -I/usr/include/libdrm -o $@ $< -ldrm

clean:
	rm -f $(PROGS)
