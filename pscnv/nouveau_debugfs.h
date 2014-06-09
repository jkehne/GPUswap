#ifndef NOUVEAU_DEBUGFS_H
#define NOUVEAU_DEBUGFS_H

#include "nouveau_drv.h"
#include <linux/debugfs.h>

#define NV_DUMP(d, m, fmt, arg...) if (m) {                          \
	seq_printf((m), (fmt), ##arg);                                   \
} else {                                                             \
	NV_INFO(d, fmt, ##arg);                                          \
}

#endif /* end of include guard: NOUVEAU_DEBUGFS_H */
