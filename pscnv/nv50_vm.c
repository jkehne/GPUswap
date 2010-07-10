#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nv50_vm.h"
#include "pscnv_vm.h"

int
nv50_vm_flush(struct drm_device *dev, int unit) {
	nv_wr32(dev, 0x100c80, unit << 16 | 1);
	if (!nouveau_wait_until(dev, 2000000000ULL, 0x100c80, 1, 0)) {
		NV_ERROR(dev, "TLB flush fail on unit %d!\n", unit);
		return -EIO;
	}
	return 0;
}

/* VM trap handling on NV50 is some kind of a fucking joke.
 *
 * So, there's this little bugger called MMU, which is in PFB area near
 * 0x100c80 and contains registers to flush the TLB caches, and to report
 * VM traps.
 *
 * And you have several units making use of that MMU. The known ones atm
 * include PGRAPH, PFIFO, the BARs, and the PEEPHOLE. Each of these has its
 * own TLBs. And most of them have several subunits, each having a separate
 * MMU access path.
 *
 * Now, if you use an address that is bad in some way, the MMU responds "NO
 * PAGE!!!11!1". And stores the relevant address + unit + channel into
 * 0x100c90 area, where you can read it. However, it does NOT report an
 * interrupt - this is done by the faulting unit.
 *
 * Now, if you get several page faults at once, which is not that uncommon
 * if you fuck up something in your code, all but the first trap is lost.
 * The unit reporting the trap may or may not also store the address on its
 * own.
 *
 * So we report the trap in two pieces. First we go through all the possible
 * faulters and report their status, which may range anywhere from full access
 * info [like TPDMA] to just "oh! a trap!" [like VFETCH]. Then we ask the MMU
 * for whatever trap it remembers. Then the user can look at dmesg and maybe
 * match them using the MMU status field. Which we should decode someday, but
 * meh for now.
 *
 * As for the Holy Grail of Demand Paging - hah. Who the hell knows. Given the
 * fucked up reporting, the only hope lies in getting all individual units to
 * cooperate. BAR accesses quite obviously cannot be demand paged [not a big
 * problem - that's what host page tables are for]. PFIFO accesses all seem
 * restartable just fine. As for PGRAPH... some, like TPDMA, are already dead
 * when they happen, but maybe there's a DEBUG bit somewhere that changes it.
 * Some others, like M2MF, hang on fault, and are therefore promising. But
 * this requires shitloads of RE repeated for every unit. Have fun.
 *
 */

struct pscnv_enumval {
	int value;
	char *name;
	void *data;
};

static struct pscnv_enumval vm_trap_reasons[] = {
	{ 0, "PT_NOT_PRESENT", 0},
	{ 1, "PT_TOO_SHORT", 0 },
	{ 2, "PAGE_NOT_PRESENT", 0 },
	/* 3 is magic flag 0x40 set in PTE */
	{ 4, "PAGE_READ_ONLY", 0 },
	/* 5 never seen */
	{ 6, "NULL_DMAOBJ", 0 },
	/* 7-0xa never seen */
	{ 0xb, "VRAM_LIMIT", 0 },
	/* 0xc-0xe never seen */
	{ 0xf, "DMAOBJ_LIMIT", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_dispatch_subsubunits[] = {
	{ 0, "GRCTX", 0 },
	{ 1, "NOTIFY", 0 },
	{ 2, "QUERY", 0 },
	{ 3, "COND", 0 },
	{ 4, "M2M_IN", 0 },
	{ 5, "M2M_OUT", 0 },
	{ 6, "M2M_NOTIFY", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_ccache_subsubunits[] = {
	{ 0, "CB", 0 },
	{ 1, "TIC", 0 },
	{ 2, "TSC", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_tprop_subsubunits[] = {
	{ 0, "RT0", 0 },
	{ 1, "RT1", 0 },
	{ 2, "RT2", 0 },
	{ 3, "RT3", 0 },
	{ 4, "RT4", 0 },
	{ 5, "RT5", 0 },
	{ 6, "RT6", 0 },
	{ 7, "RT7", 0 },
	{ 8, "ZETA", 0 },
	{ 9, "LOCAL", 0 },
	{ 0xa, "GLOBAL", 0 },
	{ 0xb, "STACK", 0 },
	{ 0xc, "DST2D", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_pgraph_subunits[] = {
	{ 0, "STRMOUT", 0 },
	{ 3, "DISPATCH", vm_dispatch_subsubunits },
	{ 5, "CCACHE", vm_ccache_subsubunits },
	{ 7, "CLIPID", 0 },
	{ 9, "VFETCH", 0 },
	{ 0xa, "TEXTURE", 0 },
	{ 0xb, "TPROP", vm_tprop_subsubunits },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_pfifo_subsubunits[] = {
	{ 0, "PUSHBUF", 0 },
	{ 1, "SEMAPHORE", 0 },
	/* 3 seen. also on semaphore. but couldn't reproduce. */
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_pfifo_subunits[] = {
	/* curious. */
	{ 8, "FIFO", vm_pfifo_subsubunits },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_peephole_subunits[] = {
	/* even more curious. */
	{ 4, "WRITE", 0 },
	{ 8, "READ", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_bar_subsubunits[] = {
	{ 0, "FB", 0 },
	{ 1, "IN", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_bar_subunits[] = {
	/* even more curious. */
	{ 4, "WRITE", vm_bar_subsubunits },
	{ 8, "READ", vm_bar_subsubunits },
	/* 0xa also seen. some kind of write. */
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_units[] = {
	{ 0, "PGRAPH", vm_pgraph_subunits },
	{ 1, "PVP", 0 },
	/* 2, 3 never seen */
	{ 4, "PEEPHOLE", vm_peephole_subunits },
	{ 5, "PFIFO", vm_pfifo_subunits },
	{ 6, "BAR", vm_bar_subunits },
	/* 7 never seen */
	{ 8, "PPPP", 0 },
	{ 9, "PBSP", 0 },
	{ 0xa, "PCRYPT", 0 },
	/* 0xb, 0xc never seen */
	{ 0xd, "PVUNK104", 0 },
	/* 0xe: UNK10a000??? */
	{ 0, 0, 0 },
};

static struct pscnv_enumval *pscnv_enum_find (struct pscnv_enumval *list, int val) {
	while (list->value != val && list->name)
		list++;
	if (list->name)
		return list;
	else
		return 0;
}

void nv50_vm_trap(struct drm_device *dev) {
	/* XXX: go through existing channels and match the address */
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t trap[6];
	int i;
	uint32_t idx = nv_rd32(dev, 0x100c90);
	uint32_t s0, s1, s2, s3;
	char reason[50];
	char unit1[50];
	char unit2[50];
	char unit3[50];
	struct pscnv_enumval *ev;
	if (idx & 0x80000000) {
		idx &= 0xffffff;
		for (i = 0; i < 6; i++) {
			nv_wr32(dev, 0x100c90, idx | i << 24);
			trap[i] = nv_rd32(dev, 0x100c94);
		}
		if (dev_priv->chipset < 0xa3 || dev_priv->chipset >= 0xaa) {
			s0 = trap[0] & 0xf;
			s1 = (trap[0] >> 4) & 0xf;
			s2 = (trap[0] >> 8) & 0xf;
			s3 = (trap[0] >> 12) & 0xf;
		} else {
			s0 = trap[0] & 0xff;
			s1 = (trap[0] >> 8) & 0xff;
			s2 = (trap[0] >> 16) & 0xff;
			s3 = (trap[0] >> 24) & 0xff;
		}
		ev = pscnv_enum_find(vm_trap_reasons, s1);
		if (ev)
			snprintf(reason, sizeof(reason), "%s", ev->name);
		else
			snprintf(reason, sizeof(reason), "0x%x", s1);
		ev = pscnv_enum_find(vm_units, s0);
		if (ev)
			snprintf(unit1, sizeof(unit1), "%s", ev->name);
		else
			snprintf(unit1, sizeof(unit1), "0x%x", s0);
		if (ev && (ev = ev->data) && (ev = pscnv_enum_find(ev, s2)))
			snprintf(unit2, sizeof(unit2), "%s", ev->name);
		else
			snprintf(unit2, sizeof(unit2), "0x%x", s2);
		if (ev && (ev = ev->data) && (ev = pscnv_enum_find(ev, s3)))
			snprintf(unit3, sizeof(unit3), "%s", ev->name);
		else
			snprintf(unit3, sizeof(unit3), "0x%x", s3);
		NV_INFO(dev, "VM: Trapped %s at %02x%04x%04x channel %04x%04x on %s/%s/%s, reason %s\n",
				(trap[5]&0x100?"read":"write"),
				trap[5]&0xff, trap[4]&0xffff,
				trap[3]&0xffff, trap[2], trap[1], unit1, unit2, unit3, reason);
		nv_wr32(dev, 0x100c90, idx | 0x80000000);
	}
}
