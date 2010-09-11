#include "drm.h"
#include "drmP.h"
#include "nouveau_drv.h"
#include "pscnv_fifo.h"
#include "pscnv_chan.h"

int pscnv_ioctl_fifo_init(struct drm_device *dev, void *data,
						struct drm_file *file_priv) {
	struct drm_pscnv_fifo_init *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_chan *ch;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	if (!dev_priv->fifo || !dev_priv->fifo->chan_init_dma)
		return -ENODEV;

	ch = pscnv_get_chan(dev, file_priv, req->cid);
	if (!ch)
		return -ENOENT;

	ret = dev_priv->fifo->chan_init_dma(ch, req->pb_handle, req->flags, req->slimask, req->pb_start);

	pscnv_chan_unref(ch);

	return ret;
}

int pscnv_ioctl_fifo_init_ib(struct drm_device *dev, void *data,
						struct drm_file *file_priv) {
	struct drm_pscnv_fifo_init_ib *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_chan *ch;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	if (!dev_priv->fifo || !dev_priv->fifo->chan_init_ib)
		return -ENODEV;

	ch = pscnv_get_chan(dev, file_priv, req->cid);
	if (!ch)
		return -ENOENT;

	ret = dev_priv->fifo->chan_init_ib(ch, req->pb_handle, req->flags, req->slimask, req->ib_start, req->ib_order);

	pscnv_chan_unref(ch);

	return ret;
}
