#include "drm.h"
#include "drmP.h"
#include "nouveau_drm.h"

struct drm_ioctl_desc nouveau_ioctls[] = {
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_INFO, NULL, DRM_AUTH)
};
