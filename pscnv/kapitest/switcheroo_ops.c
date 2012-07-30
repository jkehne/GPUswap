#include "drmP.h"
#include "drm.h"
#include <linux/vga_switcheroo.h>

void dummy(struct drm_device *dev)
{
	/* 
	 * kernel 3.5 added the vga_switcheroo_client_ops struct to replace
	 * the 3 separate args to vga_switcheroo_register_client() bringing
	 * the total arg count down to 2
	 */

	static const struct vga_switcheroo_client_ops foo_switcheroo_ops = {
		.set_gpu_state = NULL,
		.reprobe = NULL,
		.can_switch = NULL,
	};

	vga_switcheroo_register_client(dev->pdev, &foo_switcheroo_ops);
}
