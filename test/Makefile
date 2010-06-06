PROGS = get_param gem map chan

all: $(PROGS)

%: %.c ../pscnv/pscnv_drm.h
	gcc -I../pscnv -I/usr/include/libdrm -o $@ $< -ldrm -g

clean:
	rm -f $(PROGS)
