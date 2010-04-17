/* BEGIN CSTYLED */
#ifndef __DRM_LINUX_H__
#define __DRM_LINUX_H__

#define DRM_MEM_CACHED	0
#define DRM_MEM_UNCACHED 	1
#define DRM_MEM_WC 		2

#define ioremap_wc(base,size) drm_sun_ioremap((base), (size), DRM_MEM_WC)
#define ioremap(base, size)   drm_sun_ioremap((base), (size), DRM_MEM_UNCACHED)
#define iounmap(addr)         drm_sun_iounmap((addr))

#define spinlock_t                       kmutex_t
#define	spin_lock_init(l)                mutex_init((l), NULL, MUTEX_DRIVER, NULL);
#define	spin_lock(l)	                 mutex_enter(l)
#define	spin_unlock(u)                   mutex_exit(u)
#define	spin_lock_irqsave(l, flag)       mutex_enter(l)
#define	spin_unlock_irqrestore(u, flag)  mutex_exit(u)

#define mutex_lock(l)                    mutex_enter(l)
#define mutex_unlock(u)                  mutex_exit(u)

#define kmalloc           kmem_alloc
#define kzalloc           kmem_zalloc
#define kcalloc(x, y, z)  kzalloc((x)*(y), z)
#define kfree             kmem_free

#define do_gettimeofday   (void) uniqtime
#define msleep_interruptible(s)  DRM_UDELAY(s)

#define GFP_KERNEL KM_SLEEP
#define GFP_ATOMIC KM_SLEEP
#define udelay drv_usecwait
#define mdelay(x) drv_usecwait((x)*1000)
#define msecs_to_jiffies(x) drv_usectohz((x)*1000)
#define msleep(x) delay(drv_usectohz(x*000))
#define time_after(a,b) ((long)(b) - (long)(a) < 0) 

#define	jiffies	ddi_get_lbolt()

#define cpu_to_le16(x) (x) 
#define le16_to_cpu(x) (x)

#define abs(x) ({				\
		long __x = (x);			\
		(__x < 0) ? -__x : __x;		\
	})

#define div_u64(x, y) ((unsigned long long)(x))/((unsigned long long)(y))  /* XXX FIXME */
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))

#define put_user(val,ptr) DRM_COPY_TO_USER(ptr,(&val),sizeof(val))
#define get_user(x,ptr) DRM_COPY_FROM_USER((&x),ptr,sizeof(x))
#define copy_to_user DRM_COPY_TO_USER
#define copy_from_user DRM_COPY_FROM_USER
#define unlikely(a)  (a)

#define NOTIFY_DONE		0x0000		/* Don't care */
#define NOTIFY_OK		0x0001		/* Suits me */
#define NOTIFY_STOP_MASK	0x8000		/* Don't call further */
#define NOTIFY_BAD		(NOTIFY_STOP_MASK|0x0002)

#define EBADHANDLE	521	/* Illegal NFS file handle */
#define ENOTSYNC	522	/* Update synchronization mismatch */
#define EBADCOOKIE	523	/* Cookie is stale */
#define ENOTSUPP	524	/* Operation is not supported */
#define ETOOSMALL	525	/* Buffer or request is too small */
#define ESERVERFAULT	526	/* An untranslatable error occurred */
#define EBADTYPE	527	/* Type not supported by server */
#define EJUKEBOX	528	/* Request initiated, but will not complete before timeout */
#define EIOCBQUEUED	529	/* iocb queued, will get completion event */
#define EIOCBRETRY	530	/* iocb queued, will trigger a retry */

#define BUG_ON(a)	ASSERT(!(a))

#define ALIGN(x, a)	(((x) + ((a) - 1)) & ~((a) - 1))

typedef unsigned long dma_addr_t;
typedef uint64_t	u64;
typedef uint32_t	u32;
typedef uint16_t	u16;
typedef uint8_t		u8;
typedef uint_t		irqreturn_t;

typedef int		bool;

#define true		(1)
#define false		(0)

#define __init
#define __exit
#define __iomem

#if defined(__i386)
typedef u32 resource_size_t;
#else
typedef u64 resource_size_t;
#endif

struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long val, void *data);
};

typedef struct kref {
	atomic_t refcount;
}kref_t;

inline void kref_init(struct kref *kref)
{
	atomic_set(&kref->refcount, 1);
}
inline void kref_get(struct kref *kref)
{
	atomic_inc(&kref->refcount);
}
inline void kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
	if (!atomic_dec_uint_nv(&kref->refcount))
		release(kref);
}

inline unsigned int hweight16(unsigned int w)
{
	w = (w & 0x5555) + ((w >> 1) & 0x5555);
	w = (w & 0x3333) + ((w >> 2) & 0x3333);
	w = (w & 0x0F0F) + ((w >> 4) & 0x0F0F);
	w = (w & 0x00FF) + ((w >> 8) & 0x00FF);
	return (w);
}

inline long IS_ERR(const void *ptr)
{
	return ((unsigned long)ptr >= (unsigned long)-255);
}

#endif
