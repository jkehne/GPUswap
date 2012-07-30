#include "drmP.h"
#include "drm.h"
#include <linux/vga_switcheroo.h>

void dummy(struct drm_device *dev)
{
	/* reprobe arg was added in 2.6.38 for a total of 4 args */
	vga_switcheroo_register_client(dev->pdev, 0, 0, 0);
}
