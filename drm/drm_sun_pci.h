/* BEGIN CSTYLED */
#ifndef __DRM_SUN_PCI_H__
#define __DRM_SUN_PCI_H__

#include <sys/sunddi.h>
#include "drm_linux.h"
#define PCI_CONFIG_REGION_NUMS 6

struct pci_config_region {
	unsigned long start;
	unsigned long size;
};

struct pci_dev {
	struct drm_device *dev;
	ddi_acc_handle_t pci_cfg_acc_handle;

	uint16_t vendor;
	uint16_t device;
	struct pci_config_region regions[PCI_CONFIG_REGION_NUMS];
	int domain;
	int bus;
	int slot;
	int func;
	int irq;

	ddi_iblock_cookie_t intr_block;

	int msi_enabled;
	ddi_intr_handle_t *msi_handle;
	int msi_size;
	int msi_actual;
	uint_t msi_pri;
	int msi_flag;
};

#define pci_resource_start(pdev, bar) ((pdev)->regions[(bar)].start)
#define pci_resource_len(pdev, bar) ((pdev)->regions[(bar)].size)
#define pci_resource_end(pdev, bar)                    \
	((pci_resource_len((pdev), (bar)) == 0 &&      \
	pci_resource_start((pdev), (bar)) == 0) ? 0 :  \
	(pci_resource_start((pdev), (bar)) +           \
	pci_resource_len((pdev), (bar)) - 1))

extern uint8_t* pci_map_rom(struct pci_dev *pdev, size_t *size);
extern void pci_unmap_rom(struct pci_dev *pdev, uint8_t *base);
extern int pci_read_config_byte(struct pci_dev *dev, int where, u8 *val);
extern int pci_read_config_word(struct pci_dev *dev, int where, u16 *val);
extern int pci_read_config_dword(struct pci_dev *dev, int where, u32 *val);
extern int pci_write_config_byte(struct pci_dev *dev, int where, u8 val);
extern int pci_write_config_word(struct pci_dev *dev, int where, u16 val);
extern int pci_write_config_dword(struct pci_dev *dev, int where, u32 val);

extern int pci_find_capability(struct pci_dev *pdev, int capid);
extern struct pci_dev * pci_dev_create(struct drm_device *dev);
extern void pci_dev_destroy(struct pci_dev *pdev);

#endif
