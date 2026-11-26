/*
 * libdrm -> Exynos Mali-400 IOAcceleratorES bridge for Mesa lima on iOS 9.
 * Translates DRM_IOCTL_LIMA_* calls to IOConnectCallMethod(MALI_CMD_*).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <xf86drm.h>
#include <drm/lima_drm.h>
#include "mali_abi.h"

#define MALI_FAKE_DRM_FD 999

extern uint32_t mach_task_self_;
extern int IOConnectMapMemory(uint32_t connect, uint32_t memoryType, uint32_t intoTask,
                              uintptr_t *atAddress, size_t *ofSize, uint32_t options);
extern int IOConnectCallScalarMethod(uint32_t connection, uint32_t selector,
                                     const uint64_t *input, uint32_t inputCnt,
                                     uint64_t *output, uint32_t *outputCnt);
extern int IOConnectCallStructMethod(uint32_t connection, uint32_t selector,
                                     const void *inputStruct, size_t inputStructCnt,
                                     void *outputStruct, size_t *outputStructCnt);

extern void mali_log(const char *fmt, ...);
/* Active IOAcceleratorES connection handle (set by mali_gld.c on gldCreateDevice). */
uint32_t mali_iokit_conn = 0;
uint32_t mali_work_pa = 0x5f000000u;
uint32_t mali_work_size = 0x00100000u;
static uintptr_t mali_work_va = 0;
static size_t mali_work_mapped_sz = 0;
static volatile uint32_t *mali_done_seq_va = NULL;
static int mali_done_map_tried = 0;

static inline void mali_ensure_done_map(void)
{
    if (mali_done_seq_va || mali_done_map_tried || !mali_iokit_conn)
        return;
    mali_done_map_tried = 1;
    uintptr_t va = 0;
    size_t sz = 0;
    if (IOConnectMapMemory(mali_iokit_conn, 2, mach_task_self_, &va, &sz, 1u) == 0 && va && sz >= 4u)
        mali_done_seq_va = (volatile uint32_t *)va;
}

void mali_zero_wc(void *dst, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    if (n >= 64u) {
        size_t chunks = n >> 6;
        __asm__ volatile(
            "vmov.i8 q0, #0\n"
            "vmov.i8 q1, #0\n"
            "1:\n"
            "vst1.8 {d0-d3}, [%0]!\n"
            "vst1.8 {d0-d3}, [%0]!\n"
            "subs %1, %1, #1\n"
            "bne 1b\n"
            : "+r"(d), "+r"(chunks) :: "q0", "q1", "cc", "memory");
        n &= 63u;
    }
#endif
    if (n)
        memset(d, 0, n);
}

static uintptr_t mali_ensure_work_map(void)
{
    if (mali_work_va)
        return mali_work_va;
    if (!mali_iokit_conn)
        return 0;
    uintptr_t va = 0;
    size_t sz = 0;
    /* kIOMapAnywhere = 1 */
    if (IOConnectMapMemory(mali_iokit_conn, 0, mach_task_self_, &va, &sz, 1u) != 0)
        return 0;
    mali_work_va = va;
    mali_work_mapped_sz = sz;
    return mali_work_va;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    if (fd == MALI_FAKE_DRM_FD) {
        uintptr_t base = mali_ensure_work_map();
        if (!base) {
            errno = EIO;
            return MAP_FAILED;
        }
        uint64_t uoff = (uint64_t)offset;
        if (uoff < mali_work_pa || (uoff - mali_work_pa) + len > mali_work_mapped_sz) {
            errno = EINVAL;
            return MAP_FAILED;
        }
        return (void *)(base + (size_t)(uoff - mali_work_pa));
    }
    typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);
    static mmap_fn real_mmap;
    if (!real_mmap)
        real_mmap = (mmap_fn)dlsym(RTLD_NEXT, "mmap");
    return real_mmap ? real_mmap(addr, len, prot, flags, fd, offset) : MAP_FAILED;
}

int munmap(void *addr, size_t len)
{
    uintptr_t p = (uintptr_t)addr;
    if (mali_work_va && p >= mali_work_va && p < mali_work_va + mali_work_mapped_sz)
        return 0;
    typedef int (*munmap_fn)(void *, size_t);
    static munmap_fn real_munmap;
    if (!real_munmap)
        real_munmap = (munmap_fn)dlsym(RTLD_NEXT, "munmap");
    return real_munmap ? real_munmap(addr, len) : -1;
}

struct mali_bo_slot {
    uint32_t handle;
    uint32_t size;
    uint32_t va;
    uint32_t pa;
    uint64_t mmap_off;
    uint32_t last_seq;
};

struct mali_free_ext {
    uint32_t off;
    uint32_t size;
};

#define MAX_BOS 4096
static pthread_mutex_t bo_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct mali_bo_slot bos[MAX_BOS];
static struct mali_free_ext free_exts[MAX_BOS];
static int num_free_exts = 0;
static uint32_t next_handle = 1;
static uint32_t next_pool_off = 0x10000u; /* reserve 64 KiB on legacy 0x5f000000; 0 on per-client slices */
static int pool_va_mapped = 0;

static void drm_klog(const char *s)
{
    if (!mali_iokit_conn) return;
    size_t n = strlen(s), z = 0;
    if (n > 0 && n < 160)
        IOConnectCallStructMethod(mali_iokit_conn, 6u, s, n + 1u, NULL, &z);
}

static uint32_t mali_va_base(void)
{
    if (mali_work_pa >= 0x62000000u && mali_work_pa < 0x7e000000u)
        return 0x04000000u + (mali_work_pa - 0x62000000u);
    if (mali_work_pa >= 0x56000000u && mali_work_pa < 0x5f000000u)
        return 0x04000000u + (mali_work_pa - 0x56000000u);
    return 0x04000000u;
}

static int pool_alloc(uint32_t sz, uint32_t *out_off)
{
    int best = -1;
    for (int i = 0; i < num_free_exts; i++) {
        if (free_exts[i].size >= sz && (best < 0 || free_exts[i].size < free_exts[best].size))
            best = i;
    }
    if (best >= 0) {
        *out_off = free_exts[best].off;
        free_exts[best].off += sz;
        free_exts[best].size -= sz;
        if (free_exts[best].size == 0)
            free_exts[best] = free_exts[--num_free_exts];
        return 0;
    }
    if (next_pool_off + sz > mali_work_size) {
        static int oom_n = 0;
        if (oom_n++ < 8) {
            char obuf[128];
            snprintf(obuf, sizeof(obuf), "[MALI] OOM pa=0x%x sz=0x%x used=0x%x/0x%x exts=%d\n",
                     mali_work_pa, sz, next_pool_off, mali_work_size, num_free_exts);
            drm_klog(obuf);
            mali_log("%s", obuf);
        }
        return -ENOMEM;
    }
    *out_off = next_pool_off;
    next_pool_off += sz;
    return 0;
}

static void pool_free(uint32_t off, uint32_t sz)
{
    if (!sz) return;
    /* Coalesce with any adjacent free extents. */
    for (int i = 0; i < num_free_exts; i++) {
        if (free_exts[i].off + free_exts[i].size == off) {
            off = free_exts[i].off;
            sz += free_exts[i].size;
            free_exts[i] = free_exts[--num_free_exts];
            i = -1;
        } else if (off + sz == free_exts[i].off) {
            sz += free_exts[i].size;
            free_exts[i] = free_exts[--num_free_exts];
            i = -1;
        }
    }
    if (off + sz == next_pool_off) {
        next_pool_off = off;
        return;
    }
    if (num_free_exts < MAX_BOS) {
        free_exts[num_free_exts].off = off;
        free_exts[num_free_exts].size = sz;
        num_free_exts++;
    }
}

static struct mali_bo_slot *find_bo(uint32_t h)
{
    if (!h || (h & 0x80000000u))
        return NULL;
    uint32_t idx = h & (MAX_BOS - 1u);
    if (bos[idx].handle == h)
        return &bos[idx];
    return NULL;
}

drmVersionPtr drmGetVersion(int fd)
{
    (void)fd;
    drmVersionPtr v = (drmVersionPtr)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->version_major = 1;
    v->version_minor = 0; /* fixed 1 MiB tile heap (fits in 16 MiB WORK_PA pool) */
    v->name = strdup("lima");
    v->name_len = 4;
    v->date = strdup("20261120");
    v->date_len = 8;
    v->desc = strdup("Exynos4412 Mali-400 MP4 (iOS 9)");
    v->desc_len = (int)strlen(v->desc);
    return v;
}

void drmFreeVersion(drmVersionPtr v)
{
    if (!v) return;
    free(v->name); free(v->date); free(v->desc); free(v);
}

int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device)
{
    (void)fd; (void)flags;
    if (!device)
        return -EINVAL;
    drmDevicePtr d = (drmDevicePtr)calloc(1, sizeof(*d));
    if (!d)
        return -ENOMEM;
    d->bustype = DRM_BUS_PLATFORM;
    *device = d;
    return 0;
}

void drmFreeDevice(drmDevicePtr *device)
{
    if (device && *device) {
        free(*device);
        *device = NULL;
    }
}

static __thread int tls_has_gp = 0;
static __thread uint32_t tls_gp[6];
static __thread uint32_t tls_gp_bos[256];
static __thread uint32_t tls_gp_nr_bos = 0;
static __thread int tls_gp_bos_overflow = 0;

static int bo_in_pending_gp(uint32_t handle)
{
    if (!tls_has_gp || !handle)
        return 0;
    if (tls_gp_bos_overflow)
        return 1;
    for (uint32_t i = 0; i < tls_gp_nr_bos; i++) {
        if (tls_gp_bos[i] == handle)
            return 1;
    }
    return 0;
}

static uint32_t g_last_enq_seq = 0;
static uint32_t g_last_done_seq = 0;
static int g_any_inflight = 0;

static inline void refresh_done_seq_locked(void)
{
    mali_ensure_done_map();
    if (mali_done_seq_va) {
        uint32_t hw = *mali_done_seq_va;
        if ((int32_t)(hw - g_last_done_seq) > 0)
            g_last_done_seq = hw;
        if (g_any_inflight && (int32_t)(g_last_enq_seq - g_last_done_seq) <= 0)
            g_any_inflight = 0;
    }
}

static inline int seq_is_busy_locked(uint32_t seq)
{
    return seq != 0u && (int32_t)(seq - g_last_done_seq) > 0;
}

static int bo_is_inflight_locked(uint32_t handle)
{
    if (!g_any_inflight)
        return 0;
    refresh_done_seq_locked();
    if (!g_any_inflight)
        return 0;
    struct mali_bo_slot *b = find_bo(handle);
    if (!b)
        return 0;
    return seq_is_busy_locked(b->last_seq);
}

uint32_t g_mali_sub_us;
int g_mali_bench_en;
extern uint64_t mach_absolute_time(void);

static int mali_submit_gpp_timed(uint32_t sel, const void *in, size_t sz, uint32_t out_seqs[2])
{
    size_t out_sz = 8;
    out_seqs[0] = 0;
    out_seqs[1] = 0;
    if (__builtin_expect(!g_mali_bench_en, 1))
        return IOConnectCallStructMethod(mali_iokit_conn, sel, in, sz, out_seqs, &out_sz);
    uint64_t t0 = mach_absolute_time();
    int kr = IOConnectCallStructMethod(mali_iokit_conn, sel, in, sz, out_seqs, &out_sz);
    g_mali_sub_us += (uint32_t)((mach_absolute_time() - t0) / 24u);
    return kr;
}

static int flush_pending_gp(void)
{
    if (!tls_has_gp || !mali_iokit_conn) {
        tls_has_gp = 0;
        tls_gp_nr_bos = 0;
        tls_gp_bos_overflow = 0;
        return 0;
    }
    tls_has_gp = 0;
    tls_gp_nr_bos = 0;
    tls_gp_bos_overflow = 0;
    size_t out_sz = 0;
    int kr = IOConnectCallStructMethod(mali_iokit_conn, MALI_CMD_SUBMIT_GP,
                                       tls_gp, sizeof(tls_gp), NULL, &out_sz);
    if (kr != 0) {
        static int ge = 0;
        if (ge++ < 5)
            mali_log("[MALI] GP err=0x%x vs=%x..%x pl=%x..%x th=%x..%x\n",
                     kr, tls_gp[0], tls_gp[1], tls_gp[2], tls_gp[3], tls_gp[4], tls_gp[5]);
        return -EIO;
    }
    return 0;
}

uint32_t mali_get_last_enq_seq(void)
{
    pthread_mutex_lock(&bo_mtx);
    uint32_t seq = g_last_enq_seq;
    pthread_mutex_unlock(&bo_mtx);
    return seq;
}

void mali_wait_seq(uint32_t target_seq)
{
    if (!target_seq || !mali_iokit_conn)
        return;
    pthread_mutex_lock(&bo_mtx);
    refresh_done_seq_locked();
    int busy = seq_is_busy_locked(target_seq);
    pthread_mutex_unlock(&bo_mtx);
    if (!busy)
        return;
    uint64_t in[1] = { (uint64_t)target_seq };
    uint32_t z = 0;
    if (IOConnectCallScalarMethod(mali_iokit_conn, MALI_CMD_WAIT_SEQ, in, 1, NULL, &z) != 0)
        IOConnectCallScalarMethod(mali_iokit_conn, MALI_CMD_WAIT_IDLE, NULL, 0, NULL, &z);
    pthread_mutex_lock(&bo_mtx);
    refresh_done_seq_locked();
    if ((int32_t)(target_seq - g_last_done_seq) > 0)
        g_last_done_seq = target_seq;
    if (g_any_inflight && (int32_t)(g_last_enq_seq - g_last_done_seq) <= 0)
        g_any_inflight = 0;
    pthread_mutex_unlock(&bo_mtx);
}

void mali_wait_idle(void)
{
    flush_pending_gp();
    if (!mali_iokit_conn)
        return;
    pthread_mutex_lock(&bo_mtx);
    refresh_done_seq_locked();
    uint32_t target = g_last_enq_seq;
    int busy = g_any_inflight;
    pthread_mutex_unlock(&bo_mtx);
    if (!busy)
        return;
    uint32_t z = 0;
    IOConnectCallScalarMethod(mali_iokit_conn, MALI_CMD_WAIT_IDLE, NULL, 0, NULL, &z);
    pthread_mutex_lock(&bo_mtx);
    if ((int32_t)(target - g_last_done_seq) > 0)
        g_last_done_seq = target;
    if (g_last_enq_seq == target)
        g_any_inflight = 0;
    pthread_mutex_unlock(&bo_mtx);
}

int drmIoctl(int fd, unsigned long request, void *arg)
{
    (void)fd;
    if (!arg)
        return -EINVAL;

    switch (request) {
    case DRM_IOCTL_LIMA_GET_PARAM: {
        struct drm_lima_get_param *p = (struct drm_lima_get_param *)arg;
        uint64_t out[4] = { 0x0400, 4, mali_work_pa, mali_work_size };
        uint32_t cnt = 4;
        if (mali_iokit_conn)
            IOConnectCallScalarMethod(mali_iokit_conn, MALI_CMD_INFO, NULL, 0, out, &cnt);
        switch (p->param) {
        case DRM_LIMA_PARAM_GPU_ID:  p->value = DRM_LIMA_PARAM_GPU_ID_MALI400; return 0;
        case DRM_LIMA_PARAM_NUM_PP:  p->value = out[1] ? out[1] : 4; return 0;
        case DRM_LIMA_PARAM_GP_VERSION:
        case DRM_LIMA_PARAM_PP_VERSION: p->value = 0x00010101u; return 0;
        default: return -EINVAL;
        }
    }

    case DRM_IOCTL_LIMA_GEM_CREATE: {
        struct drm_lima_gem_create *c = (struct drm_lima_gem_create *)arg;
        uint32_t sz = (c->size + 0xfffu) & ~0xfffu;
        if (!sz) sz = 0x1000u;
        pthread_mutex_lock(&bo_mtx);
        uint32_t va_base = mali_va_base();
        if (!pool_va_mapped && mali_iokit_conn && mali_work_size > 0x10000u) {
            uint32_t start_off = (mali_work_pa == 0x5f000000u) ? 0x10000u : 0u;
            if (start_off == 0u && next_pool_off == 0x10000u)
                next_pool_off = 0u;
            uint64_t in[3] = { va_base + start_off, mali_work_pa + start_off, mali_work_size - start_off };
            uint32_t z = 0;
            if (IOConnectCallScalarMethod(mali_iokit_conn, MALI_CMD_MAP_VA, in, 3, NULL, &z) == 0)
                pool_va_mapped = 1;
        }
        int ret = -ENOMEM;
        for (int i = 0; i < MAX_BOS; i++) {
            if (!bos[i].handle) {
                uint32_t off = 0;
                if (pool_alloc(sz, &off) != 0)
                    break;
                next_handle = (next_handle + 1u) & 0x7ffffu;
                if (!next_handle)
                    next_handle = 1u;
                bos[i].handle = (next_handle << 12) | (uint32_t)i;
                bos[i].size = sz;
                bos[i].va = va_base + off;
                bos[i].pa = mali_work_pa + off;
                bos[i].mmap_off = bos[i].pa;
                bos[i].last_seq = 0;
                uintptr_t wbase = mali_ensure_work_map();
                if (wbase && off + sz <= mali_work_mapped_sz &&
                    !(c->flags & (LIMA_BO_FLAG_HEAP | 0x80000000u)))
                    mali_zero_wc((void *)(wbase + off), sz);
                if (!pool_va_mapped && mali_iokit_conn) {
                    uint64_t in[3] = { bos[i].va, bos[i].pa, sz };
                    uint32_t z = 0;
                    IOConnectCallScalarMethod(mali_iokit_conn, MALI_CMD_MAP_VA, in, 3, NULL, &z);
                }
                c->handle = bos[i].handle;
                ret = 0;
                break;
            }
        }
        pthread_mutex_unlock(&bo_mtx);
        return ret;
    }

    case DRM_IOCTL_LIMA_GEM_INFO: {
        struct drm_lima_gem_info *info = (struct drm_lima_gem_info *)arg;
        pthread_mutex_lock(&bo_mtx);
        struct mali_bo_slot *b = find_bo(info->handle);
        if (!b) {
            pthread_mutex_unlock(&bo_mtx);
            return -ENOENT;
        }
        info->va = b->va;
        info->offset = b->mmap_off;
        pthread_mutex_unlock(&bo_mtx);
        return 0;
    }

    case DRM_IOCTL_GEM_CLOSE: {
        struct drm_gem_close *cl = (struct drm_gem_close *)arg;
        if (cl->handle & 0x80000000u)
            return 0;
        if (bo_in_pending_gp(cl->handle))
            flush_pending_gp();
        pthread_mutex_lock(&bo_mtx);
        uint32_t seq = 0u;
        int busy = 0;
        if (g_any_inflight) {
            refresh_done_seq_locked();
            struct mali_bo_slot *b0 = find_bo(cl->handle);
            if (b0 && seq_is_busy_locked(b0->last_seq)) {
                seq = b0->last_seq;
                busy = 1;
            }
        }
        pthread_mutex_unlock(&bo_mtx);
        if (busy)
            mali_wait_seq(seq);
        pthread_mutex_lock(&bo_mtx);
        struct mali_bo_slot *b = find_bo(cl->handle);
        if (b) {
            pool_free(b->pa - mali_work_pa, b->size);
            memset(b, 0, sizeof(*b));
        }
        pthread_mutex_unlock(&bo_mtx);
        return 0;
    }

    case DRM_IOCTL_LIMA_CTX_CREATE: {
        struct drm_lima_ctx_create *cc = (struct drm_lima_ctx_create *)arg;
        cc->id = 1;
        return 0;
    }

    case DRM_IOCTL_LIMA_CTX_FREE:
        return 0;

    case DRM_IOCTL_LIMA_GEM_WAIT: {
        struct drm_lima_gem_wait *gw = (struct drm_lima_gem_wait *)arg;
        if (gw && gw->timeout_ns == 0) {
            /* Non-blocking poll (e.g. bo_cache_get mid-job): NEVER flush pending GP! */
            if (bo_in_pending_gp(gw->handle)) {
                errno = ETIMEDOUT;
                return -1;
            }
            if (!g_any_inflight)
                return 0;
            pthread_mutex_lock(&bo_mtx);
            int busy = bo_is_inflight_locked(gw->handle);
            pthread_mutex_unlock(&bo_mtx);
            if (busy) {
                errno = ETIMEDOUT;
                return -1;
            }
            return 0;
        }
        if (tls_has_gp && (!gw || tls_gp_nr_bos >= 256u || bo_in_pending_gp(gw->handle))) {
            int r = flush_pending_gp();
            if (r != 0)
                return r;
        }
        if (!g_any_inflight)
            return 0;
        pthread_mutex_lock(&bo_mtx);
        refresh_done_seq_locked();
        uint32_t seq = 0u;
        int busy = 0;
        if (g_any_inflight) {
            if (gw) {
                struct mali_bo_slot *b = find_bo(gw->handle);
                if (b && seq_is_busy_locked(b->last_seq)) {
                    seq = b->last_seq;
                    busy = 1;
                }
            } else {
                seq = g_last_enq_seq;
                busy = seq_is_busy_locked(seq);
            }
        }
        pthread_mutex_unlock(&bo_mtx);
        if (!busy)
            return 0;
        mali_wait_seq(seq);
        return 0;
    }

    case DRM_IOCTL_LIMA_GEM_SUBMIT: {
        struct drm_lima_gem_submit *sub = (struct drm_lima_gem_submit *)arg;
        if (!mali_iokit_conn)
            return 0;
        if (sub->pipe == LIMA_PIPE_GP) {
            const struct drm_lima_gp_frame *gf = (const struct drm_lima_gp_frame *)(uintptr_t)sub->frame;
            const struct drm_lima_gem_submit_bo *gbos =
                (const struct drm_lima_gem_submit_bo *)(uintptr_t)sub->bos;
            if (tls_has_gp) {
                int r = flush_pending_gp();
                if (r != 0)
                    return r;
            }
            memcpy(tls_gp, gf->frame, sizeof(tls_gp));
            uint32_t n = gbos ? sub->nr_bos : 0u;
            tls_gp_bos_overflow = (n > 256u);
            if (n > 256u)
                n = 256u;
            for (uint32_t i = 0; i < n; i++)
                tls_gp_bos[i] = gbos[i].handle;
            tls_gp_nr_bos = n;
            tls_has_gp = 1;
            return 0;
        }
        if (sub->pipe == LIMA_PIPE_PP) {
            const struct drm_lima_m400_pp_frame *pf = (const struct drm_lima_m400_pp_frame *)(uintptr_t)sub->frame;
            const struct drm_lima_gem_submit_bo *sbos =
                (const struct drm_lima_gem_submit_bo *)(uintptr_t)sub->bos;

            struct mali_gp_pp_submit gpp;
            gpp.pp.num_pp = pf->num_pp ? pf->num_pp : 1;
            memcpy(gpp.pp.plbu, pf->plbu_array_address, sizeof(gpp.pp.plbu));
            memcpy(gpp.pp.stack, pf->fragment_stack_address, sizeof(gpp.pp.stack));
            memcpy(gpp.pp.frame, pf->frame, sizeof(gpp.pp.frame));
            memcpy(gpp.pp.wb, pf->wb, sizeof(gpp.pp.wb));
            uint32_t out_seqs[2] = { 0, 0 };
            uint32_t gp_nr = 0;
            int gp_overflow = 0;
            int kr;
            if (tls_has_gp) {
                memcpy(gpp.gp, tls_gp, sizeof(gpp.gp));
                gp_nr = tls_gp_nr_bos;
                gp_overflow = tls_gp_bos_overflow;
                tls_gp_nr_bos = 0;
                tls_gp_bos_overflow = 0;
                tls_has_gp = 0;
                kr = mali_submit_gpp_timed(MALI_CMD_SUBMIT_GP_PP, &gpp, sizeof(gpp), out_seqs);
            } else {
                kr = mali_submit_gpp_timed(MALI_CMD_SUBMIT_PP, &gpp.pp, sizeof(gpp.pp), out_seqs);
            }
            /* Record exact per-BO kernel sequence numbers (GP + PP) and refresh completed done_seq. */
            pthread_mutex_lock(&bo_mtx);
            if (kr == 0 && out_seqs[0]) {
                uint32_t seq = out_seqs[0];
                if ((int32_t)(out_seqs[1] - g_last_done_seq) > 0)
                    g_last_done_seq = out_seqs[1];
                if ((int32_t)(seq - g_last_enq_seq) > 0)
                    g_last_enq_seq = seq;
                g_any_inflight = ((int32_t)(g_last_enq_seq - g_last_done_seq) > 0);
                if (gp_overflow) {
                    for (int i = 0; i < MAX_BOS; i++) {
                        if (bos[i].handle)
                            bos[i].last_seq = seq;
                    }
                } else {
                    for (uint32_t i = 0; i < gp_nr; i++) {
                        struct mali_bo_slot *b = find_bo(tls_gp_bos[i]);
                        if (b)
                            b->last_seq = seq;
                    }
                    if (sbos && sub->nr_bos > 0) {
                        for (uint32_t i = 0; i < sub->nr_bos; i++) {
                            struct mali_bo_slot *b = find_bo(sbos[i].handle);
                            if (b)
                                b->last_seq = seq;
                        }
                    }
                }
            }
            pthread_mutex_unlock(&bo_mtx);
            if (kr != 0) {
                static int pe = 0;
                if (pe++ < 5)
                    mali_log("[MALI] PP err=0x%x n=%u plbu0=%x rsw=%x wb0=%x:%x st0=%x\n",
                             kr, gpp.pp.num_pp, gpp.pp.plbu[0], gpp.pp.frame[1], gpp.pp.wb[0], gpp.pp.wb[1], gpp.pp.stack[0]);
            }
            return kr == 0 ? 0 : -EIO;
        }
        return -EINVAL;
    }

    default:
        return -ENOTTY;
    }
}

int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
    (void)fd; (void)handle; (void)flags;
    if (prime_fd) *prime_fd = -1;
    return -ENOSYS;
}

int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
    (void)fd; (void)prime_fd;
    if (handle) *handle = 0;
    return -ENOSYS;
}

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
    (void)fd; (void)flags;
    if (handle) *handle = 1;
    return 0;
}

int drmSyncobjDestroy(int fd, uint32_t handle) { (void)fd; (void)handle; return 0; }
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec,
                   unsigned flags, uint32_t *first_signaled)
{
    (void)fd; (void)handles; (void)num_handles; (void)timeout_nsec; (void)flags;
    mali_wait_idle();
    if (first_signaled) *first_signaled = 0;
    return 0;
}
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd) { (void)fd; (void)handle; (void)sync_file_fd; return 0; }
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
    /* Mali submits are synchronous; return success with fd=-1 so lima_pipe_flush creates a
     * non-NULL fence handle for Gallium util_throttle_memory_usage (sync_wait(-1) returns 0,
     * and lima_fence_destroy skips close() when fd < 0). */
    (void)fd; (void)handle;
    flush_pending_gp();
    if (sync_file_fd) *sync_file_fd = -1;
    return 0;
}

/* iOS 9.3.5 libc shims (clock_gettime added in iOS 10, open_memstream in iOS 11). */
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    (void)clk_id;
    if (!tp) {
        errno = EINVAL;
        return -1;
    }
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
        return -1;
    tp->tv_sec = tv.tv_sec;
    tp->tv_nsec = tv.tv_usec * 1000L;
    return 0;
}

FILE *open_memstream(char **bufp, size_t *sizep)
{
    (void)bufp; (void)sizep;
    errno = ENOSYS;
    return NULL;
}

/* Mesa xmlconfig / renderonly stubs (not used on iOS 9 IOAcceleratorES). */
void driParseConfigFiles(void *cache, const void *info, const void *params)
{
    (void)cache; (void)info; (void)params;
}

void *renderonly_create_gpu_import_for_resource(void *rsc, void *ro, void *out_handle)
{
    (void)rsc; (void)ro; (void)out_handle;
    return NULL;
}

void renderonly_scanout_destroy(void *scanout, void *ro)
{
    (void)scanout; (void)ro;
}

/* Mach-O lacks ELF NT_GNU_BUILD_ID; provide a stable 20-byte ID for Mesa disk_cache. */
static const uint8_t mali_build_id[20] = {
    0x4d, 0x61, 0x6c, 0x69, 0x34, 0x30, 0x30, 0x45, 0x58, 0x39,
    0x33, 0x35, 0x4c, 0x69, 0x6d, 0x61, 0x32, 0x30, 0x32, 0x36,
};

const void *build_id_find_nhdr_for_addr(const void *symbol_address)
{
    (void)symbol_address;
    return mali_build_id;
}

unsigned build_id_length(const void *note)
{
    return note ? sizeof(mali_build_id) : 0;
}

const uint8_t *build_id_data(const void *note)
{
    return (const uint8_t *)note;
}

void copy_build_id_to_sha1(uint8_t *sha1, const void *note)
{
    if (sha1 && note)
        memcpy(sha1, note, sizeof(mali_build_id));
}

void _mesa_sha1_format(char *buf, const unsigned char *sha1)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 20; i++) {
        buf[i * 2]     = hex[sha1[i] >> 4];
        buf[i * 2 + 1] = hex[sha1[i] & 0xf];
    }
    buf[40] = '\0';
}

/* x86 SSE4.1 helper referenced by Mesa util; plain memcpy on ARMv7. */
void util_streaming_load_memcpy(void *dst, void *src, size_t len)
{
    memcpy(dst, src, len);
}

/* Bridge Zig/Clang libc++ inline namespace (_LIBCPP_ABI_NAMESPACE) to iOS 9 libc++.1 (__1). */
extern size_t _ZNSt3__112__next_primeEm(size_t n);
extern void   _ZNSt3__15mutex4lockEv(void *m);
extern void   _ZNSt3__15mutex6unlockEv(void *m);
extern void   _ZNSt3__15mutexD1Ev(void *m);

size_t _ZNSt21_LIBCPP_ABI_NAMESPACE12__next_primeEm(size_t n) { return _ZNSt3__112__next_primeEm(n); }
void   _ZNSt21_LIBCPP_ABI_NAMESPACE5mutex4lockEv(void *m)      { _ZNSt3__15mutex4lockEv(m); }
void   _ZNSt21_LIBCPP_ABI_NAMESPACE5mutex6unlockEv(void *m)    { _ZNSt3__15mutex6unlockEv(m); }
void   _ZNSt21_LIBCPP_ABI_NAMESPACE5mutexD1Ev(void *m)         { _ZNSt3__15mutexD1Ev(m); }
