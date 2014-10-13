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

#include "nouveau_debugfs.h"
#include "nouveau_reg.h"

#include "pscnv_client.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"
#include "pscnv_dma.h"

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
nouveau_debugfs_chipset_info(struct seq_file *m, void *pos)
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
nouveau_debugfs_memory_info(struct seq_file *m, void *pos)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_nouveau_private *dev_priv = minor->dev->dev_private;
	struct pscnv_client *cur;

	seq_printf(m, "VRAM total: %dKiB\n", (int)(dev_priv->vram_size >> 10));
	seq_printf(m, "VRAM usage: %dKiB\n", (int)atomic64_read(&dev_priv->vram_usage) >> 10);
	seq_printf(m, "VRAM swapped: %dKiB\n", (int)atomic64_read(&dev_priv->vram_swapped) >> 10);
	seq_printf(m, "VRAM demand: %dKiB\n", (int)atomic64_read(&dev_priv->vram_demand) >> 10);
	
	mutex_lock(&dev_priv->clients->lock);
	if (!list_empty(&dev_priv->clients->list)) {
		seq_printf(m, "\n");
	}
	
	list_for_each_entry(cur, &dev_priv->clients->list, clients) {
		seq_printf(m, "client %d: used %dKiB, demand %dKiB, swapped %dKiB, swappable bo %d, swapped bo %d\n",
			cur->pid, (int)atomic64_read(&cur->vram_usage) >> 10,
				  (int)atomic64_read(&cur->vram_demand) >> 10,
			          (int)atomic64_read(&cur->vram_swapped) >> 10,
				  (int)(cur->swapping_options.size),
				  (int)(cur->already_swapped.size));
	}
	mutex_unlock(&dev_priv->clients->lock);
	return 0;
}

static int
nouveau_debugfs_channels_info(struct seq_file *m, void *pos)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_nouveau_private *dev_priv = minor->dev->dev_private;
	struct pscnv_client *client;
	struct pscnv_chan *ch;

	
	mutex_lock(&dev_priv->clients->lock);
	
	list_for_each_entry(client, &dev_priv->clients->list, clients) {
		seq_printf(m, "client %d:", client->pid);
		list_for_each_entry(ch, &client->channels, client_list) {
			seq_printf(m, " channel %d", ch->cid);
		}
		seq_printf(m, "\n");
	}
	mutex_unlock(&dev_priv->clients->lock);
	return 0;
}

static int
nouveau_debugfs_timetrack_info(struct seq_file *m, void *pos)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_nouveau_private *dev_priv = minor->dev->dev_private;
	struct pscnv_client *client;
	struct pscnv_client_timetrack *tt;

	
	mutex_lock(&dev_priv->clients->lock);
	
	list_for_each_entry(client, &dev_priv->clients->list, clients) {
		seq_printf(m, "== client %d\n", client->pid);
		list_for_each_entry(tt, &client->time_trackings, list) {
			seq_printf(m, " %s: start=%lld duration=%lld ns\n", 
				tt->type, tt->start, tt->duration);
		}
		seq_printf(m, "\n");
	}
	mutex_unlock(&dev_priv->clients->lock);
	return 0;
}

static int
nouveau_debugfs_vbios_image(struct seq_file *m, void *pos)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_nouveau_private *dev_priv = node->minor->dev->dev_private;
	int i;

	for (i = 0; i < dev_priv->vbios.length; i++)
		seq_printf(m, "%c", dev_priv->vbios.data[i]);
	return 0;
}

static int
nouveau_debugfs_pd_dump_bar1(struct seq_file *m, void *pos)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	if (dev_priv->vm->pd_dump_bar1) {
		dev_priv->vm->pd_dump_bar1(dev, m);
	} else {
		seq_printf(m, "PD dump not available\n");
	}
	
	return 0;
}

static int
nouveau_debugfs_pd_dump_bar3(struct seq_file *m, void *pos)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	if (dev_priv->vm->pd_dump_bar3) {
		dev_priv->vm->pd_dump_bar3(dev, m);
	} else {
		seq_printf(m, "PD dump not available\n");
	}
	
	return 0;
}

static struct drm_info_list nouveau_debugfs_list[] = {
	{ "chipset", nouveau_debugfs_chipset_info, 0, NULL },
	{ "memory", nouveau_debugfs_memory_info, 0, NULL },
	{ "vbios.rom", nouveau_debugfs_vbios_image, 0, NULL },
	{ "bar1_pd", nouveau_debugfs_pd_dump_bar1, 0, NULL },
	{ "bar3_pd", nouveau_debugfs_pd_dump_bar3, 0, NULL },
	{ "channels", nouveau_debugfs_channels_info, 0, NULL },
	{ "timetrack", nouveau_debugfs_timetrack_info, 0, NULL }
};
#define NOUVEAU_DEBUGFS_ENTRIES ARRAY_SIZE(nouveau_debugfs_list)

static int
pscnv_debugfs_vram_limit_get(void *data, u64 *val)
{
	struct drm_device *dev = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	*val = dev_priv->vram_limit >> 10;
	return 0;
}

static int
pscnv_debugfs_vram_limit_set(void *data, u64 val)
{
	struct drm_device *dev = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	dev_priv->vram_limit = val << 10;
	
	if (dev_priv->vram_limit == 0) {
		NV_INFO(dev, "disabled vram_limit\n");
	} else {
		char vram_limit_str[16];
		pscnv_mem_human_readable(vram_limit_str, dev_priv->vram_limit);
		NV_INFO(dev, "vram_limit set to %s\n", vram_limit_str);
	}
	return 0;
}

static int
pscnv_debugfs_pause_set(void *data, u64 val)
{
	struct drm_device *dev = data;
	/* don't grow the kernel stack too much */
	struct pscnv_chan *chans[32];
	int i;
	int n_chans = 0;
	int res;
	
	if (val != 1) {
		return 0;
	}
	
	for (i = 0; i < 128; i++) {
		struct pscnv_chan *ch = pscnv_chan_chid_lookup(dev, i);

		if (ch && !(ch->flags & PSCNV_CHAN_KERNEL)) {
			chans[n_chans] = ch;
			n_chans++;
			
			pscnv_chan_ref(ch);
			res = pscnv_chan_pause(ch);
			if (res && res != -EALREADY) {
				NV_INFO(dev, "pscnv_chan_pause returned %d on "
					"channel %d\n", res, ch->cid);
			}
		}
		
		if (n_chans == 32) {
			NV_INFO(dev, "pscnv_debugfs_pause: too many pausable "
				"channels");
			break;
		}
	}
	
	if (n_chans == 0) {
		NV_INFO(dev, "pscnv_debugfs_pause: no channels to pause!\n");
		return 0;
	}
	
	for (i = 0; i < n_chans; i++) {
		struct pscnv_chan *ch = chans[i];
		
		res = pscnv_chan_pause_wait(chans[i]);
		if (res) {
			NV_INFO(dev, "pscnv_chan_pause_wait returned %d on "
				"channel %d\n", res, ch->cid);
		}
	}
	
	ssleep(1);
	
	for (i = 0; i < n_chans; i++) {
		struct pscnv_chan *ch = chans[i];
		
		res = pscnv_chan_continue(ch);
		if (res) {
			NV_INFO(dev, "pscnv_chan_continue returned %d on "
				"channel %d\n", res, ch->cid);
		}
		pscnv_chan_unref(ch);
	}
	
	return 0;
}

static int
pscnv_debugfs_memacc_test_set(void *data, u64 val)
{
	struct drm_device *dev = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct pscnv_bo *src, *src2, *dst;
	struct pscnv_vspace *vs = dev_priv->vm->vspaces[126];
	uint32_t word[4];
	int i, res;
	
	if (val != 1) {
		return 0;
	}
	
	NV_INFO(dev, "=== memacc_test: begin\n");
	
	if (!dev_priv->dma) {
		pscnv_dma_init(dev);
	}
	
	if (!dev_priv->dma) {
		NV_ERROR(dev, "memacc_test: no DMA available\n");
		res = -EINVAL;
		goto fail;
	}
	
	src = pscnv_mem_alloc(dev, 0x32000,
			    PSCNV_MAP_KERNEL | PSCNV_GEM_VRAM_LARGE,
			    0 /* tile flags */,
			    0xa1de0000,
			    NULL /*client */);
	
	if (!src) {
		NV_ERROR(dev, "memacc_test: failed to allocate src\n");
		res = -ENOMEM;
		goto fail;
	}
	
	for (i = 0; i < src->size; i+= 4) {
		nv_wv32(src, i, 0x42);
	}
	
	src->flags |= PSCNV_GEM_READONLY;
	
	src2 = pscnv_mem_alloc(dev, 0x1000,
			    PSCNV_MAP_KERNEL,
			    0 /* tile flags */,
			    0xa1de0002,
			    NULL /*client */);
	
	if (!src2) {
		NV_ERROR(dev, "memacc_test: failed to allocate src2\n");
		res = -ENOMEM;
		goto fail_src2;
	}
	
	for (i = 0; i < src2->size; i+= 4) {
		nv_wv32(src2, i, 0xa1de);
	}
	
	src2->flags |= PSCNV_GEM_READONLY;
	
	dev_priv->vm->do_map(vs, src2, 0x20300000);
	
	dst = pscnv_mem_alloc(dev, 0x32000,
			    PSCNV_MAP_KERNEL | PSCNV_GEM_VRAM_LARGE,
			    0 /* tile flags */,
			    0xa1de0001,
			    NULL /*client */);
	
	if (!dst) {
		NV_ERROR(dev, "memacc_test: failed to allocate dst\n");
		res = -ENOMEM;
		goto fail_dst;
	}

	res = pscnv_dma_bo_to_bo(dst, src, PSCNV_DMA_DEBUG);
	
	if (res) {
		NV_INFO(dev, "memacc_test: failed to DMA- Transfer!\n");
		goto fail_dma;
	}
	
	for (i = 0; i < 16; i+= 4) {
		word[i/4] = nv_rv32(dst, i);
	}
	
	NV_INFO(dev, "memacc_test: dst[0] = %08x %08x %08x %08x...\n",
		word[0], word[1], word[2], word[3]);
	
	for (i = 0; i < 16; i+= 4) {
		word[i/4] = nv_rv32(dst, i + 0x1000);
	}
	
	NV_INFO(dev, "memacc_test: dst[1] = %08x %08x %08x %08x...\n",
		word[0], word[1], word[2], word[3]);
	
	/* fall through and cleanup */
fail_dma:
	pscnv_mem_free(dst);

fail_dst:
	pscnv_mem_free(src2);

fail_src2:
	pscnv_mem_free(src);

fail:	
	NV_INFO(dev, "=== memacc_test: end\n");
	
	return res;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_vram_limit, pscnv_debugfs_vram_limit_get,
					 pscnv_debugfs_vram_limit_set,
					 "%llu");

DEFINE_SIMPLE_ATTRIBUTE(fops_pause, NULL, pscnv_debugfs_pause_set, "%llu");
DEFINE_SIMPLE_ATTRIBUTE(fops_memacc_test, NULL, pscnv_debugfs_memacc_test_set, "%llu");

static int
pscnv_debugfs_chan_pd_show(struct seq_file *m, void *data)
{
	struct pscnv_chan *ch = m->private;
	struct drm_device *dev;
	struct drm_nouveau_private *dev_priv;
	
	if (!ch) {
		seq_printf(m, "Oops, ch == NULL\n");
		return 0;
	}
	
	dev = ch->dev;
	dev_priv = dev->dev_private;
	
	if (!dev_priv->chan->pd_dump_chan) {
		seq_printf(m, "page table dump not supported on this device\n");
		return 0;
	}
	
	dev_priv->chan->pd_dump_chan(dev, m, ch->cid);
	
	return 0;
}

static int
pscnv_debugfs_single_open(struct inode *inode, struct file *file)
{
	return single_open(file, pscnv_debugfs_chan_pd_show, inode->i_private);
}

static const struct file_operations pscnv_debugfs_single_fops = {
	.owner = THIS_MODULE,
	.open = pscnv_debugfs_single_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *pscnv_debugfs_vram_limit_entry = NULL;
static struct dentry *pscnv_debugfs_pause_entry = NULL;
static struct dentry *pscnv_debugfs_chan_dir = NULL;
static struct dentry *pscnv_debugfs_memacc_test_entry = NULL;

void
pscnv_debugfs_add_chan(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	
	if (!pscnv_debugfs_chan_dir) {
		return;
	}
	
	if (!ch->name[0]) {
		NV_ERROR(dev, "debugfs: channel %d lacks a name\n", ch->cid);
		return;
	}
	
	ch->debugfs_dir = debugfs_create_dir(ch->name, pscnv_debugfs_chan_dir);
	if (!ch->debugfs_dir) {
		NV_ERROR(dev, "debugfs: can not create chan/%s\n", ch->name);
		return;
	}
	ch->debugfs_pd = debugfs_create_file("pd", S_IFREG | S_IRUGO,
				ch->debugfs_dir, ch, &pscnv_debugfs_single_fops);
}

void
pscnv_debugfs_remove_chan(struct pscnv_chan *ch)
{
	debugfs_remove(ch->debugfs_pd);
	ch->debugfs_pd = NULL;
	debugfs_remove(ch->debugfs_dir);
	ch->debugfs_dir = NULL;
}

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
	
	pscnv_debugfs_pause_entry =
		debugfs_create_file("pause", S_IFREG | S_IRUGO | S_IWUSR,
				minor->debugfs_root, dev, &fops_pause);
	
	if (!pscnv_debugfs_pause_entry) {
		NV_INFO(dev, "Cannot create /sys/kernel/debug/dri/%s/pause\n",
				minor->debugfs_root->d_name.name);
		return -ENOENT;
	}
	
	pscnv_debugfs_memacc_test_entry =
		debugfs_create_file("memacc_test", S_IFREG | S_IRUGO | S_IWUSR,
				minor->debugfs_root, dev, &fops_memacc_test);
	
	if (!pscnv_debugfs_memacc_test_entry) {
		NV_INFO(dev, "Cannot create /sys/kernel/debug/dri/%s/memacc_test\n",
				minor->debugfs_root->d_name.name);
		return -ENOENT;
	}
	
	pscnv_debugfs_chan_dir =
		debugfs_create_dir("chan", minor->debugfs_root);
	
	if (!pscnv_debugfs_chan_dir) {
		NV_INFO(dev, "Cannot create /sys/kernel/debug/dri/%s/chan",
				minor->debugfs_root->d_name.name);
		return -ENOENT;
	}
	
	return 0;
}

void
nouveau_debugfs_takedown(struct drm_minor *minor)
{
	debugfs_remove(pscnv_debugfs_vram_limit_entry);
	debugfs_remove(pscnv_debugfs_pause_entry);
	debugfs_remove(pscnv_debugfs_chan_dir);
	debugfs_remove(pscnv_debugfs_memacc_test_entry);
		
	drm_debugfs_remove_files(nouveau_debugfs_list, NOUVEAU_DEBUGFS_ENTRIES,
				 minor);
}
