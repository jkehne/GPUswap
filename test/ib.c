#include "libpscnv.h"
#include "libpscnv_ib.h"
#include <xf86drm.h>

int
main()
{
	int fd;
	int ret;
	struct pscnv_ib_chan *ch;
	int i;
        fd = drmOpen("pscnv", 0);
	if (fd == -1) {
		perror("drmOpen");
		return 1;
	}

	ret = pscnv_ib_chan_new(fd, 0, &ch, 0xdeadbeef, 0, 0);
	if (ret) {
		perror("chan_new");
		return 1;
	}

	for (i = 1; i < 0x1000000; i++) {
		BEGIN_RING50(ch, 0, 0x50, 1);
		OUT_RING(ch, i);
		FIRE_RING(ch);
		while (ch->chmap[0x48/4] != i);
	}


	return 0;
}
