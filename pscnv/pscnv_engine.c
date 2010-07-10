#include "drm.h"
#include "drmP.h"
#include "nouveau_drv.h"
#include "pscnv_engine.h"
#include "pscnv_chan.h"

int pscnv_ioctl_obj_eng_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv) {
	struct drm_pscnv_obj_eng_new *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_chan *ch;
	int ret;
	int i;
	uint32_t oclass = req->oclass;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	for (i = 0; i < PSCNV_ENGINES_NUM; i++)
		if (dev_priv->engines[i]) {
			uint32_t *pclass = dev_priv->engines[i]->oclasses;
			if (!pclass)
				continue;
			while (*pclass) {
				if (*pclass == oclass)
					goto found;
				pclass++;
			}
		}
	return -ENODEV;

found:
	mutex_lock (&dev_priv->vm_mutex);

	ch = pscnv_get_chan(dev, file_priv, req->cid);
	if (!ch) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	if (!ch->engdata[i]) {
		ret = dev_priv->engines[i]->chan_alloc(dev_priv->engines[i], ch);
		if (ret) {
			mutex_unlock (&dev_priv->vm_mutex);
			return ret;
		}
	}

	ret = dev_priv->engines[i]->chan_obj_new(dev_priv->engines[i], ch, req->handle, oclass, req->flags);

	mutex_unlock (&dev_priv->vm_mutex);
	return ret;
}

