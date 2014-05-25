/*
 * Copyright (C) 2009 Red Hat <bskeggs@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Authors:
 *  Ben Skeggs <bskeggs@redhat.com>
 */

#include <linux/debugfs.h>

#include "nouveau_drv.h"
#include "nouveau_reg.h"

#include "pscnv_client.h"

#if 0
static int
nouveau_debugfs_channel_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct nouveau_channel *chan = node->info_ent->data;

	seq_printf(m, "channel id    : %d\n", chan->id);

	seq_printf(m, "cpu fifo state:\n");
	seq_printf(m, "          base: 0x%08x\n", chan->pushbuf_base);
	seq_printf(m, "           max: 0x%08x\n", chan->dma.max << 2);
	seq_printf(m, "           cur: 0x%08x\n", chan->dma.cur << 2);
	seq_printf(m, "           put: 0x%08x\n", chan->dma.put << 2);
	seq_printf(m, "          free: 0x%08x\n", chan->dma.free << 2);
	if (chan->dma.ib_max) {
		seq_printf(m, "        ib max: 0x%08x\n", chan->dma.ib_max);
		seq_printf(m, "        ib put: 0x%08x\n", chan->dma.ib_put);
		seq_printf(m, "       ib free: 0x%08x\n", chan->dma.ib_free);
	}

	seq_printf(m, "gpu fifo state:\n");
	seq_printf(m, "           get: 0x%08x\n",
					nvchan_rd32(chan, chan->user_get));
	seq_printf(m, "           put: 0x%08x\n",
					nvchan_rd32(chan, chan->user_put));
	if (chan->dma.ib_max) {
		seq_printf(m, "        ib get: 0x%08x\n",
			   nvchan_rd32(chan, 0x88));
		seq_printf(m, "        ib put: 0x%08x\n",
			   nvchan_rd32(chan, 0x8c));
	}

	seq_printf(m, "last fence    : %d\n", chan->fence.sequence);
	seq_printf(m, "last signalled: %d\n", chan->fence.sequence_ack);
	return 0;
}

int
nouveau_debugfs_channel_init(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct drm_minor *minor = chan->dev->primary;
	int ret;

	if (!dev_priv->debugfs.channel_root) {
		dev_priv->debugfs.channel_root =
			debugfs_create_dir("channel", minor->debugfs_root);
		if (!dev_priv->debugfs.channel_root)
			return -ENOENT;
	}

	snprintf(chan->debugfs.name, 32, "%d", chan->id);
	chan->debugfs.info.name = chan->debugfs.name;
	chan->debugfs.info.show = nouveau_debugfs_channel_info;
	chan->debugfs.info.driver_features = 0;
	chan->debugfs.info.data = chan;

	ret = drm_debugfs_create_files(&chan->debugfs.info, 1,
				       dev_priv->debugfs.channel_root,
				       chan->dev->primary);
	if (ret == 0)
		chan->debugfs.active = true;
	return ret;
}

void
nouveau_debugfs_channel_fini(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;

	if (!chan->debugfs.active)
		return;

	drm_debugfs_remove_files(&chan->debugfs.info, 1, chan->dev->primary);
	chan->debugfs.active = false;

	if (chan == dev_priv->channel) {
		debugfs_remove(dev_priv->debugfs.channel_root);
		dev_priv->debugfs.channel_root = NULL;
	}
}
#endif
static int
nouveau_debugfs_chipset_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *dev = minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t ppci_0;

	ppci_0 = nv_rd32(dev, dev_priv->chipset >= 0x40 ? 0x88000 : 0x1800);

	seq_printf(m, "PMC_BOOT_0: 0x%08x\n", nv_rd32(dev, NV03_PMC_BOOT_0));
	seq_printf(m, "PCI ID    : 0x%04x:0x%04x\n",
		   ppci_0 & 0xffff, ppci_0 >> 16);
	return 0;
}

static int
nouveau_debugfs_memory_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_nouveau_private *dev_priv = minor->dev->dev_private;
	struct pscnv_client *cur;

	seq_printf(m, "VRAM total: %dKiB\n", (int)(dev_priv->vram_size >> 10));
	seq_printf(m, "VRAM usage: %dKiB\n", (int)(dev_priv->vram_usage >> 10));
	seq_printf(m, "VRAM swapped: %dKiB\n", (int)(dev_priv->vram_swapped >> 10));
	
	mutex_lock(&dev_priv->clients->lock);
	if (!list_empty(&dev_priv->clients->list)) {
		seq_printf(m, "\n");
	}
	
	list_for_each_entry(cur, &dev_priv->clients->list, clients) {
		seq_printf(m, "client %d: used %dKiB, swapped %dKiB\n",
			cur->pid, (int)(cur->vram_usage >> 10),
			          (int)(cur->vram_swapped >> 10));
	}
	mutex_unlock(&dev_priv->clients->lock);
	return 0;
}

static int
nouveau_debugfs_vbios_image(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_nouveau_private *dev_priv = node->minor->dev->dev_private;
	int i;

	for (i = 0; i < dev_priv->vbios.length; i++)
		seq_printf(m, "%c", dev_priv->vbios.data[i]);
	return 0;
}

static struct drm_info_list nouveau_debugfs_list[] = {
	{ "chipset", nouveau_debugfs_chipset_info, 0, NULL },
	{ "memory", nouveau_debugfs_memory_info, 0, NULL },
	{ "vbios.rom", nouveau_debugfs_vbios_image, 0, NULL },
};
#define NOUVEAU_DEBUGFS_ENTRIES ARRAY_SIZE(nouveau_debugfs_list)

static int
pscnv_debugfs_vram_limit_get(void *data, u64 *val)
{
	struct drm_device *dev = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	*val = dev_priv->vram_limit;
	return 0;
}

static int
pscnv_debugfs_vram_limit_set(void *data, u64 val)
{
	struct drm_device *dev = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	dev_priv->vram_limit = val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_vram_limit, pscnv_debugfs_vram_limit_get,
					 pscnv_debugfs_vram_limit_set,
					 "%llu");

static struct dentry *pscnv_debugfs_vram_limit_entry = NULL;

int
nouveau_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	
	// watchout: this code is run, before pscnv gets initialized, so
	// dev->dev_private == NULL, at this point.
	// So, we can not use debugfs_create_u32 at this point.
	
	drm_debugfs_create_files(nouveau_debugfs_list, NOUVEAU_DEBUGFS_ENTRIES,
				 minor->debugfs_root, minor);
	
	pscnv_debugfs_vram_limit_entry =
		debugfs_create_file("vram_limit", S_IFREG | S_IRUGO | S_IWUSR,
				minor->debugfs_root, dev, &fops_vram_limit);
	
	if (!pscnv_debugfs_vram_limit_entry) {
		NV_INFO(dev, "Cannot create /sys/kernel/debug/dri/%s/vram_limit\n",
				minor->debugfs_root->d_name.name);
		return -ENOENT;
	}
	
	return 0;
}

void
nouveau_debugfs_takedown(struct drm_minor *minor)
{
	debugfs_remove(pscnv_debugfs_vram_limit_entry);
		
	drm_debugfs_remove_files(nouveau_debugfs_list, NOUVEAU_DEBUGFS_ENTRIES,
				 minor);
}
