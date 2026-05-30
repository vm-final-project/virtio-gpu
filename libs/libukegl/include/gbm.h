#pragma once
/* Bounded GBM shim for kmscube Unikraft port.
 * Wraps uk_gbm_compat for surface/buffer management. */
#include <stdint.h>
#include <uk/gbm_compat.h>

struct gbm_device;
struct gbm_surface;
struct gbm_bo;

#define GBM_FORMAT_XRGB8888 UK_GBM_COMPAT_FORMAT_XRGB8888
#define GBM_FORMAT_ARGB8888 0x34325241u
#define GBM_BO_USE_SCANOUT  (1u << 0)
#define GBM_BO_USE_RENDERING (1u << 2)

/* A gbm_device is just a tag over our virtual gpu context. */
struct gbm_device *gbm_create_device(int fd);
void               gbm_device_destroy(struct gbm_device *dev);

struct gbm_surface *gbm_surface_create(struct gbm_device *dev,
                                        uint32_t width, uint32_t height,
                                        uint32_t format, uint32_t flags);
void                gbm_surface_destroy(struct gbm_surface *surf);

/* BO wraps a uk_gbm_compat_bo + DMA buffer. */
struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surf);
void            gbm_surface_release_buffer(struct gbm_surface *surf,
                                            struct gbm_bo *bo);
uint32_t        gbm_bo_get_stride(struct gbm_bo *bo);
uint32_t        gbm_bo_get_width(struct gbm_bo *bo);
uint32_t        gbm_bo_get_height(struct gbm_bo *bo);
uint32_t        gbm_bo_get_format(struct gbm_bo *bo);
int             gbm_bo_get_fd(struct gbm_bo *bo);
uint64_t        gbm_bo_get_modifier(struct gbm_bo *bo);
void *          gbm_bo_get_user_data(struct gbm_bo *bo);
void            gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                                      void (*destroy)(struct gbm_bo*, void*));
