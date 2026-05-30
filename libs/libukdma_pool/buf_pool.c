#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <uk/buf.h>

struct uk_buf_pool {
	struct uk_buf_pool_cfg cfg;
	struct uk_dma_buf *bufs;
	unsigned char *in_use;
};

int uk_buf_pool_create(struct uk_buf_pool **pool, const struct uk_buf_pool_cfg *cfg)
{
	struct uk_buf_pool *p;
	if (!pool || !cfg || !cfg->object_size || !cfg->object_align || !cfg->object_count)
		return -EINVAL;
	p = calloc(1, sizeof(*p));
	if (!p)
		return -ENOMEM;
	p->cfg = *cfg;
	p->bufs = calloc(cfg->object_count, sizeof(*p->bufs));
	p->in_use = calloc(cfg->object_count, sizeof(*p->in_use));
	if (!p->bufs || !p->in_use)
		goto nomem;
	for (size_t i = 0; i < cfg->object_count; i++) {
		int rc = uk_dma_alloc(&p->bufs[i], cfg->object_size, cfg->object_align, cfg->dma_flags);
		if (rc)
			goto nomem;
	}
	*pool = p;
	return 0;
nomem:
	if (p) {
		if (p->bufs) for (size_t i = 0; i < cfg->object_count; i++) uk_dma_free(&p->bufs[i]);
		free(p->bufs); free(p->in_use); free(p);
	}
	return -ENOMEM;
}

void uk_buf_pool_destroy(struct uk_buf_pool *pool)
{
	if (!pool) return;
	for (size_t i = 0; i < pool->cfg.object_count; i++)
		uk_dma_free(&pool->bufs[i]);
	free(pool->bufs);
	free(pool->in_use);
	free(pool);
}

int uk_buf_get(struct uk_buf_pool *pool, struct uk_dma_buf **buf)
{
	if (!pool || !buf) return -EINVAL;
	for (size_t i = 0; i < pool->cfg.object_count; i++) {
		if (!pool->in_use[i]) {
			pool->in_use[i] = 1;
			*buf = &pool->bufs[i];
			return 0;
		}
	}
	return -ENOSPC;
}

void uk_buf_put(struct uk_buf_pool *pool, struct uk_dma_buf *buf)
{
	if (!pool || !buf) return;
	for (size_t i = 0; i < pool->cfg.object_count; i++) {
		if (buf == &pool->bufs[i]) {
			pool->in_use[i] = 0;
			return;
		}
	}
}
