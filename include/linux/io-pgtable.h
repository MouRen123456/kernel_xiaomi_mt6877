/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __IO_PGTABLE_H
#define __IO_PGTABLE_H
#include <linux/bitops.h>

#include <linux/scatterlist.h>

/*
 * Public API for use by IOMMU drivers
 */
enum io_pgtable_fmt {
	ARM_32_LPAE_S1,
	ARM_32_LPAE_S2,
	ARM_64_LPAE_S1,
	ARM_64_LPAE_S2,
	ARM_V7S,
	IO_PGTABLE_NUM_FMTS,
};

#define ARM_V8L_FAST ((unsigned int)-1)

/**
 * struct iommu_gather_ops - IOMMU callbacks for TLB and page table management.
 *
 * @tlb_flush_all: Synchronously invalidate the entire TLB context.
 * @tlb_add_flush: Queue up a TLB invalidation for a virtual address range.
 * @tlb_sync:      Ensure any queued TLB invalidation has taken effect, and
 *                 any corresponding page table updates are visible to the
 *                 IOMMU.
 * @alloc_pages_exact: Allocate page table memory (optional, defaults to
 *                     alloc_pages_exact)
 * @free_pages_exact:  Free page table memory (optional, defaults to
 *                     free_pages_exact)
 *
 * Note that these can all be called in atomic context and must therefore
 * not block.
 */
struct iommu_gather_ops {
	void (*tlb_flush_all)(void *cookie);
	void (*tlb_add_flush)(unsigned long iova, size_t size, size_t granule,
			      bool leaf, void *cookie);
	void (*tlb_sync)(void *cookie);
	void *(*alloc_pages_exact)(void *cookie, size_t size, gfp_t gfp_mask);
	void (*free_pages_exact)(void *cookie, void *virt, size_t size);
};

/**
 * struct io_pgtable_cfg - Configuration data for a set of page tables.
 *
 * @quirks:        A bitmap of hardware quirks that require some special
 *                 action by the low-level page table allocator.
 * @pgsize_bitmap: A bitmap of page sizes supported by this set of page
 *                 tables.
 * @ias:           Input address (iova) size, in bits.
 * @oas:           Output address (paddr) size, in bits.
 * @tlb:           TLB management callbacks for this set of tables.
 * @iommu_dev:     The device representing the DMA configuration for the
 *                 page table walker.
 */
struct io_pgtable_cfg {
	/*
	 * IO_PGTABLE_QUIRK_ARM_NS: (ARM formats) Set NS and NSTABLE bits in
	 *	stage 1 PTEs, for hardware which insists on validating them
	 *	even in	non-secure state where they should normally be ignored.
	 *
	 * IO_PGTABLE_QUIRK_NO_PERMS: Ignore the IOMMU_READ, IOMMU_WRITE and
	 *	IOMMU_NOEXEC flags and map everything with full access, for
	 *	hardware which does not implement the permissions of a given
	 *	format, and/or requires some format-specific default value.
	 *
	 * IO_PGTABLE_QUIRK_TLBI_ON_MAP: If the format forbids caching invalid
	 *	(unmapped) entries but the hardware might do so anyway, perform
	 *	TLB maintenance when mapping as well as when unmapping.
	 *
	 * IO_PGTABLE_QUIRK_ARM_MTK_4GB: (ARM v7s format) MediaTek IOMMUs extend
	 *	to support up to 34 bits PA where the bit32 and bit33 are
	 *	encoded in the bit9 and bit4 of the PTE respectively.
	 *

	 * IO_PGTABLE_QUIRK_NO_DMA: Guarantees that the tables will only ever
	 *	be accessed by a fully cache-coherent IOMMU or CPU (e.g. for a
	 *	software-emulated IOMMU), such that pagetable updates need not
	 *	be treated as explicit DMA data.
	 *

	 * IO_PGTABLE_QUIRK_QSMMUV500_NON_SHAREABLE:
	 *	Having page tables which are non coherent, but cached in a
	 *	system cache requires SH=Non-Shareable. This applies to the
	 *	qsmmuv500 model. For data buffers SH=Non-Shareable is not
	 *	required.

	 * IO_PGTABLE_QUIRK_QCOM_USE_UPSTREAM_HINT: Override the attributes
	 *	set in TCR for the page table walker. Use attributes specified
	 *	by the upstream hw instead.
	 *
	 * IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA: Override the attributes
	 *	set in TCR for the page table walker with Write-Back,
	 *	no Write-Allocate cacheable encoding.
	 *
	 */
	#define IO_PGTABLE_QUIRK_ARM_NS		BIT(0)
	#define IO_PGTABLE_QUIRK_NO_PERMS	BIT(1)
	#define IO_PGTABLE_QUIRK_TLBI_ON_MAP	BIT(2)
	#define IO_PGTABLE_QUIRK_ARM_MTK_4GB	BIT(3)
	#define IO_PGTABLE_QUIRK_NO_DMA		BIT(4)
	#define IO_PGTABLE_QUIRK_QSMMUV500_NON_SHAREABLE BIT(5)
	#define IO_PGTABLE_QUIRK_QCOM_USE_UPSTREAM_HINT	BIT(6)
	#define IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA	BIT(7)
	unsigned long			quirks;
	unsigned long			pgsize_bitmap;
	unsigned int			ias;
	unsigned int			oas;
	const struct iommu_gather_ops	*tlb;
	struct device			*iommu_dev;
	dma_addr_t			iova_base;
	dma_addr_t			iova_end;

	/* Low-level data specific to the table format */
	union {
		struct {
			u64	ttbr[2];
			u64	tcr;
			u64	mair[2];
		} arm_lpae_s1_cfg;

		struct {
			u64	vttbr;
			u64	vtcr;
		} arm_lpae_s2_cfg;

		struct {
			u32	ttbr[2];
			u32	tcr;
			u32	nmrr;
			u32	prrr;
		} arm_v7s_cfg;

		struct {
			u64	ttbr[2];
			u64	tcr;
			u64	mair[2];
			void	*pmds;
		} av8l_fast_cfg;
	};
};

/**
 * struct io_pgtable_ops - Page table manipulation API for IOMMU drivers.
 *
 * @map:		Map a physically contiguous memory region.
 * @map_sg:		Map a scatterlist.  Returns the number of bytes mapped,
 *			or -ve val on failure.  The size parameter contains the
 *			size of the partial mapping in case of failure.
 * @unmap:		Unmap a physically contiguous memory region.
 * @iova_to_phys:	Translate iova to physical address.
 * @is_iova_coherent:	Checks coherency of given IOVA. Returns True if coherent
 *			and False if non-coherent.
 * @iova_to_pte:	Translate iova to Page Table Entry (PTE).
 *
 * These functions map directly onto the iommu_ops member functions with
 * the same names.
 */
struct io_pgtable_ops {
	int (*map)(struct io_pgtable_ops *ops, unsigned long iova,
		   phys_addr_t paddr, size_t size, int prot);
	size_t (*unmap)(struct io_pgtable_ops *ops, unsigned long iova,
			size_t size);
	int (*map_sg)(struct io_pgtable_ops *ops, unsigned long iova,
		      struct scatterlist *sg, unsigned int nents,
		      int prot, size_t *size);
	phys_addr_t (*iova_to_phys)(struct io_pgtable_ops *ops,
				    unsigned long iova);
	bool (*is_iova_coherent)(struct io_pgtable_ops *ops,
				unsigned long iova);
	uint64_t (*iova_to_pte)(struct io_pgtable_ops *ops,
		    unsigned long iova);

};

/**
 * alloc_io_pgtable_ops() - Allocate a page table allocator for use by an IOMMU.
 *
 * @fmt:    The page table format.
 * @cfg:    The page table configuration. This will be modified to represent
 *          the configuration actually provided by the allocator (e.g. the
 *          pgsize_bitmap may be restricted).
 * @cookie: An opaque token provided by the IOMMU driver and passed back to
 *          the callback routines in cfg->tlb.
 */
struct io_pgtable_ops *alloc_io_pgtable_ops(enum io_pgtable_fmt fmt,
					    struct io_pgtable_cfg *cfg,
					    void *cookie);

/**
 * free_io_pgtable_ops() - Free an io_pgtable_ops structure. The caller
 *                         *must* ensure that the page table is no longer
 *                         live, but the TLB can be dirty.
 *
 * @ops: The ops returned from alloc_io_pgtable_ops.
 */
void free_io_pgtable_ops(struct io_pgtable_ops *ops);


/*
 * Internal structures for page table allocator implementations.
 */

/**
 * struct io_pgtable - Internal structure describing a set of page tables.
 *
 * @fmt:    The page table format.
 * @cookie: An opaque token provided by the IOMMU driver and passed back to
 *          any callback routines.
 * @cfg:    A copy of the page table configuration.
 * @ops:    The page table operations in use for this set of page tables.
 */
struct io_pgtable {
	enum io_pgtable_fmt	fmt;
	void			*cookie;
	struct io_pgtable_cfg	cfg;
	struct io_pgtable_ops	ops;
};

#define io_pgtable_ops_to_pgtable(x) container_of((x), struct io_pgtable, ops)

static inline void io_pgtable_tlb_flush_all(struct io_pgtable *iop)
{
	if (!iop->cfg.tlb)
		return;
	iop->cfg.tlb->tlb_flush_all(iop->cookie);
}

static inline void io_pgtable_tlb_add_flush(struct io_pgtable *iop,
		unsigned long iova, size_t size, size_t granule, bool leaf)
{
	if (!iop->cfg.tlb)
		return;
	iop->cfg.tlb->tlb_add_flush(iova, size, granule, leaf, iop->cookie);
}

static inline void io_pgtable_tlb_sync(struct io_pgtable *iop)
{
	if (!iop->cfg.tlb)
		return;
	iop->cfg.tlb->tlb_sync(iop->cookie);
}

/**
 * struct io_pgtable_init_fns - Alloc/free a set of page tables for a
 *                              particular format.
 *
 * @alloc: Allocate a set of page tables described by cfg.
 * @free:  Free the page tables associated with iop.
 */
struct io_pgtable_init_fns {
	struct io_pgtable *(*alloc)(struct io_pgtable_cfg *cfg, void *cookie);
	void (*free)(struct io_pgtable *iop);
};

extern struct io_pgtable_init_fns io_pgtable_arm_32_lpae_s1_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_32_lpae_s2_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_64_lpae_s1_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_64_lpae_s2_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_v7s_init_fns;
extern struct io_pgtable_init_fns io_pgtable_av8l_fast_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_msm_secure_init_fns;

/**
 * io_pgtable_alloc_pages_exact:
 *	allocate an exact number of physically-contiguous pages.
 * @size: the number of bytes to allocate
 * @gfp_mask: GFP flags for the allocation
 *
 * Like alloc_pages_exact(), but with some additional accounting for debug
 * purposes.
 */
void *io_pgtable_alloc_pages_exact(struct io_pgtable_cfg *cfg, void *cookie,
				   size_t size, gfp_t gfp_mask);

/**
 * io_pgtable_free_pages_exact:
 *	release memory allocated via io_pgtable_alloc_pages_exact()
 * @virt: the value returned by alloc_pages_exact.
 * @size: size of allocation, same value as passed to alloc_pages_exact().
 *
 * Like free_pages_exact(), but with some additional accounting for debug
 * purposes.
 */
void io_pgtable_free_pages_exact(struct io_pgtable_cfg *cfg, void *cookie,
				 void *virt, size_t size);

#endif /* __IO_PGTABLE_H */
