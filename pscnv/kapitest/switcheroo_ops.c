#include "drmP.h"
#include "drm.h"
#include <linux/vga_switcheroo.h>

void dummy(struct drm_device *dev)
{
	static const struct vga_switcheroo_client_ops foo_switcheroo_ops = {
		.set_gpu_state = NULL,
		.reprobe = NULL,
		.can_switch = NULL,
	};

	vga_switcheroo_register_client(dev->pdev, &foo_switcheroo_ops);
}
