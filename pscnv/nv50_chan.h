#ifndef __NV50_CHAN_H__
#define __NV50_CHAN_H__

#include "drmP.h"
#include "drm.h"

#define NV50_CHAN_PD	0x1400
#define NV84_CHAN_PD	0x0200

extern int nv50_chan_new (struct pscnv_chan *ch);
extern void nv50_chan_init (struct pscnv_chan *ch);
extern int nv50_chan_iobj_new(struct pscnv_chan *, uint32_t size);
extern int nv50_chan_dmaobj_new(struct pscnv_chan *, uint32_t type, uint64_t start, uint64_t size);

#endif /* __NV50_CHAN_H__ */
