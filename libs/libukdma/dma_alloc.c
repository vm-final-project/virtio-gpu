#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <uk/dma.h>

static int valid_align(size_t align) { return align && ((align & (align - 1)) == 0); }

int uk_dma_alloc(struct uk_dma_buf *buf, size_t len, size_t align, uint32_t flags)
{
	void *p = NULL;
	if (!buf || !len || !valid_align(align))
		return -EINVAL;
	if (posix_memalign(&p, align, len) != 0)
		return -ENOMEM;
	if (flags & UK_DMA_F_ZEROED)
		memset(p, 0, len);
	buf->vaddr = p;
	buf->paddr_or_iova = (uintptr_t)p;
	buf->len = len;
	buf->align = align;
	buf->flags = flags;
	return 0;
}

void uk_dma_free(struct uk_dma_buf *buf)
{
	if (!buf || !buf->vaddr)
		return;
	free(buf->vaddr);
	memset(buf, 0, sizeof(*buf));
}

int uk_dma_build_sg(const struct uk_dma_buf *buf, struct uk_dma_sg *sg, size_t max_sg, size_t *nr_sg)
{
	if (!buf || !buf->vaddr || !sg || !nr_sg || max_sg == 0)
		return -EINVAL;
	sg[0].paddr_or_iova = buf->paddr_or_iova;
	sg[0].len = buf->len;
	*nr_sg = 1;
	return 0;
}

int uk_dma_sync_for_device(struct uk_dma_buf *buf, enum uk_dma_dir dir)
{
	(void)dir;
	return (buf && buf->vaddr) ? 0 : -EINVAL;
}

int uk_dma_sync_for_cpu(struct uk_dma_buf *buf, enum uk_dma_dir dir)
{
	(void)dir;
	return (buf && buf->vaddr) ? 0 : -EINVAL;
}
