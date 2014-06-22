#ifndef NOUVEAU_DEBUGFS_H
#define NOUVEAU_DEBUGFS_H

#include "nouveau_drv.h"
#include <linux/debugfs.h>

#define NV_DUMP(d, m, fmt, arg...) if (m) {                          \
	seq_printf((m), (fmt), ##arg);                                   \
} else {                                                             \
	NV_INFO(d, fmt, ##arg);                                          \
}

void
pscnv_debugfs_add_chan(struct pscnv_chan *ch);

void
pscnv_debugfs_remove_chan(struct pscnv_chan *ch);

#endif /* end of include guard: NOUVEAU_DEBUGFS_H */
