/*
 * Mali400GLDriverEX.bundle for iOS 9.3.5 (armv7 Mach-O).
 * Loaded by libGFXShared.dylib via IOGLESBundleName = "Mali400GLDriverEX".
 *
 * Exports all 129 mandatory gld* entry points required by _gfxInitializeLibrary
 * (plus gldInitializeLibrary / gldTerminateLibrary) matching iOS 9's
 * libGFXShared / libGPUSupportMercury ABI.
 *
 * shortcut: draw/shader/texture/FBO hooks are stubs until backboardd's EAGLContext
 * creation and render loop are confirmed alive; then wire draw/clear/present to lima.
 */
#include <stdint.h>
#include <stddef.h>
#include "mali_abi.h"
extern void mali_log(const char *fmt, ...);

extern void *calloc(size_t n, size_t sz);
extern void free(void *p);
extern void *memset(void *s, int c, size_t n);
extern uint32_t mach_task_self_;
extern uint32_t kIOMasterPortDefault;
extern void *IOServiceMatching(const char *name);
extern uint32_t IOServiceGetMatchingService(uint32_t masterPort, void *matching);
extern int IOServiceOpen(uint32_t service, uint32_t owningTask, uint32_t type, uint32_t *connect);
extern int IOServiceClose(uint32_t connect);
extern int IOObjectRelease(uint32_t object);
extern int IOConnectCallScalarMethod(uint32_t connection, uint32_t selector,
                                     const uint64_t *input, uint32_t inputCnt,
                                     uint64_t *output, uint32_t *outputCnt);

extern uint32_t mali_iokit_conn;
extern uint32_t mali_work_pa;
extern uint32_t mali_work_size;
extern void *lima_screen_create(int fd, const void *config, void *ro);
extern void *lima_context_create(void *pscreen, void *priv, unsigned flags);
extern void  mali_gles_init_screen(void *pipe_screen);
extern void *mali_gles_create_context(void *gli_ctx, void *shared_st);
extern void  mali_gles_destroy_context(void *gli_ctx);
extern void  mali_gles_reinstall_for_gli(void *gli_ctx);
extern void  mali_gles_install_dispatch(void *eagl_priv);

#define MALI_FAKE_DRM_FD 999

struct mali_device {
    uint32_t conn;
    uint32_t gpu_id;
    uint32_t num_pp;
    uint32_t work_pa;
    uint32_t mem_size;
    uint32_t display_mask;
    void *lima_screen;
};

struct mali_sharegroup {
    struct mali_device *dev;
    void *shared_st;
};

struct mali_ctx {
    struct mali_device *dev;
    struct mali_sharegroup *sg;
    void *lima_ctx;
    void *gli_ctx;
    void *st_ctx;
};

static struct mali_device *g_shared_dev;

struct mali_dri_opt_cache {
    void *info;
    void *values;
    unsigned int tableSize;
};

struct mali_pipe_screen_config {
    uint8_t driver_name_is_inferred;
    struct mali_dri_opt_cache *options;
    const struct mali_dri_opt_cache *options_info;
};

void gldInitializeLibrary(void *a, void *b, void *c, void *d, void *e, void *f)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}

void gldTerminateLibrary(void) {}

int gldGetVersion(int *major, int *minor, int *patch, int *driver_id)
{
    if (major) *major = 4;
    if (minor) *minor = 1;
    if (patch) *patch = 16;
    if (driver_id) *driver_id = 0x7000; /* matches IMGSGX543GLDriver renderer class */
    return 1;
}

/* iOS 9 libGFXShared ABI: returns 0 on success, stores device in *out_dev. */
int gldCreateDevice(void **out_dev, uint32_t display_mask, void *info)
{
    (void)info;
    if (!out_dev)
        return -1;
    if (g_shared_dev) {
        *out_dev = g_shared_dev;
        return 0;
    }
    struct mali_device *dev = (struct mali_device *)calloc(1, sizeof(*dev));
    if (!dev)
        return -1;
    dev->display_mask = display_mask ? display_mask : 1;

    uint32_t svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                               IOServiceMatching("SGXDriver543"));
    if (svc) {
        if (IOServiceOpen(svc, mach_task_self_, 0, &dev->conn) == 0) {
            uint64_t out[4] = {0};
            uint32_t cnt = 4;
            if (IOConnectCallScalarMethod(dev->conn, MALI_CMD_INFO, 0, 0, out, &cnt) == 0) {
                dev->gpu_id   = (uint32_t)out[0];
                dev->num_pp   = (uint32_t)out[1];
                dev->work_pa  = (uint32_t)out[2];
                dev->mem_size = (uint32_t)out[3];
                mali_iokit_conn = dev->conn;
                mali_work_pa    = dev->work_pa;
                mali_work_size  = dev->mem_size;
                static uint8_t dummy_info[64], dummy_val[64];
                static struct mali_dri_opt_cache opt_info = { dummy_info, dummy_val, 0 };
                static struct mali_dri_opt_cache opt_cache;
                struct mali_pipe_screen_config cfg = { 0, &opt_cache, &opt_info };
                dev->lima_screen = lima_screen_create(MALI_FAKE_DRM_FD, &cfg, 0);
                mali_gles_init_screen(dev->lima_screen);
                mali_log("[MALI] dev conn=%u work_pa=0x%x sz=0x%x lima_screen=%p\n",
                         dev->conn, dev->work_pa, dev->mem_size, dev->lima_screen);
            }
        }
        IOObjectRelease(svc);
    }
    g_shared_dev = dev;
    *out_dev = dev;
    return 0;
}

void gldDestroyDevice(void *p)
{
    /* Singleton device lives for process lifetime so repeated gldCreateDevice calls share one lima_screen. */
    (void)p;
}

int gldPopulateRendererInfo(void *p, uint32_t *info)
{
    struct mali_device *dev = (struct mali_device *)p;
    if (!info)
        return 0;
    memset(info, 0, 132);
    info[1]  = 0x01027000u; /* renderer_id */
    info[2]  = 0x0000a59bu; /* buffer modes */
    info[3]  = 0x0000001du; /* color modes */
    info[4]  = 0x00008400u; /* depth modes */
    info[5]  = 0x08000000u; /* stencil modes */
    info[6]  = 0x00001c01u; /* sample modes */
    info[7]  = 0x00000080u; /* sample buffers */
    info[8]  = dev ? dev->display_mask : 1u;
    info[18] = 16u;         /* video memory MB */
    info[20] = 16u;         /* texture memory MB */
    return 0;
}

/* 52-byte GLDPixelFormatRec matching iOS 9 GLEngine / libGPUSupportMercury */
struct gld_pixel_format {
    struct gld_pixel_format *next;  /* +0x00 */
    uint32_t renderer_id;           /* +0x04 */
    uint32_t os_support;            /* +0x08 */
    uint32_t buffer_modes;          /* +0x0c */
    uint32_t color_modes;           /* +0x10 */
    uint32_t accum_modes;           /* +0x14 */
    uint32_t depth_modes;           /* +0x18 */
    uint32_t stencil_modes;         /* +0x1c */
    uint16_t max_aux_buffers;       /* +0x20 */
    uint16_t max_sample_buffers;    /* +0x22 */
    uint16_t max_samples;           /* +0x24 */
    uint16_t sample_modes;          /* +0x26 */
    uint32_t pad28;                 /* +0x28 */
    uint8_t  color_float;           /* +0x2c */
    uint8_t  pad2d[3];              /* +0x2d..0x2f */
    uint32_t display_mask;          /* +0x30 */
};

int gldChoosePixelFormat(struct gld_pixel_format **out_pf, const int *attribs)
{
    (void)attribs;
    if (!out_pf)
        return 0;
    struct gld_pixel_format *pf = (struct gld_pixel_format *)calloc(1, sizeof(*pf));
    if (!pf)
        return 10002; /* kCGLBadAlloc */
    pf->next          = 0;
    pf->renderer_id   = 0x01027000u;
    pf->os_support    = 0x00000301u; /* bit 0x100 required by EAGLSharegroup initWithAPI:; bit 0x40 clear = HW */
    pf->buffer_modes  = 0x0000a59bu;
    pf->color_modes   = 0x00008480u; /* 32-bit RGBA8888 / RGB565 */
    pf->accum_modes   = 0u;
    pf->depth_modes   = 0x00001c01u;
    pf->stencil_modes = 0x00000081u;
    pf->max_sample_buffers = 1;
    pf->max_samples   = 4;
    pf->sample_modes  = 1;
    pf->display_mask  = 1u;
    *out_pf = pf;
    return 0;
}

void gldDestroyPixelFormat(void *pf) { free(pf); }

int gldCreateShareGroup(void *dev, void **out_sg, uint32_t flags)
{
    (void)flags;
    if (!out_sg)
        return -1;
    struct mali_sharegroup *sg = (struct mali_sharegroup *)calloc(1, sizeof(*sg));
    if (!sg)
        return -1;
    sg->dev = (struct mali_device *)dev;
    *out_sg = sg;
    return 0;
}

void gldDestroyShareGroup(void *sg) { free(sg); }

int gldCreateContext(void *sg_p, void **out_ctx, void *pf, void *limits_p, void *state_p, void *arg5)
{
    (void)pf; (void)arg5;
    if (!out_ctx)
        return -1;
    struct mali_sharegroup *sg = (struct mali_sharegroup *)sg_p;
    struct mali_ctx *ctx = (struct mali_ctx *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return -1;
    ctx->sg  = sg;
    ctx->dev = sg ? sg->dev : 0;
    /* GLEngine::_gliCreateContextWithShared passes state_p = gli_ctx + 0x6a0 and limits_p = r5 + 8 (gli_ctx + 0x6988). */
    ctx->gli_ctx = state_p ? (void *)((uint8_t *)state_p - 0x6a0) : 0;
    mali_log("[MALI] gldCreateContext sg=%p dev=%p gli=%p lim=%p st=%p\n",
             sg, ctx->dev, ctx->gli_ctx, limits_p, state_p);
    if (ctx->dev && ctx->dev->lima_screen) {
        if (ctx->gli_ctx) {
            ctx->st_ctx = mali_gles_create_context(ctx->gli_ctx, sg ? sg->shared_st : 0);
            if (sg && !sg->shared_st && ctx->st_ctx)
                sg->shared_st = ctx->st_ctx;
        } else {
            ctx->lima_ctx = lima_context_create(ctx->dev->lima_screen, 0, 0);
        }
    }
    /* Populate GLEngine context limits (limits_p == gli_ctx + 8; offsets verified at GLEngine+0x1ac2) */
    if (limits_p) {
        uint8_t *lim = (uint8_t *)limits_p;
        *(uint32_t *)(lim + 4)   = 4096; /* max viewport w (gli+12) */
        *(uint32_t *)(lim + 8)   = 4096; /* max viewport h (gli+16) */
        *(uint8_t  *)(lim + 21)  = 1;    /* gli+29 */
        *(uint8_t  *)(lim + 22)  = 1;    /* gli+30 */
        *(uint16_t *)(lim + 154) = 8;    /* max tex units (gli+162) */
        *(uint16_t *)(lim + 156) = 8;    /* max vert tex units (gli+164) */
        *(uint16_t *)(lim + 160) = 4096; /* max texture size (gli+168) */
        *(uint16_t *)(lim + 166) = 4096; /* max cubemap size (gli+174) */
        *(uint16_t *)(lim + 168) = 4096; /* max renderbuffer size (gli+176) */
        *(uint8_t  *)(lim + 170) = 4;    /* max color attachments (gli+178) */
        *(uint8_t  *)(lim + 175) = 8;    /* max combined tex units (gli+183) */
        *(uint16_t *)(lim + 332) = 256;  /* max frag uniform vecs (gli+340) */
        *(uint32_t *)(lim + 344) = 512;  /* max vert uniform vecs (gli+352) */
        *(uint32_t *)(lim + 448) = 16;   /* max vert attribs (gli+456) */
    }
    *out_ctx = ctx;
    return 0;
}

void gldReclaimContext(void *ctx) { (void)ctx; }
void gldDestroyContext(void *p)
{
    struct mali_ctx *ctx = (struct mali_ctx *)p;
    if (!ctx) return;
    if (ctx->gli_ctx)
        mali_gles_destroy_context(ctx->gli_ctx);
    free(ctx);
}

const char *gldGetDeviceString(void *dev, uint32_t name)
{
    (void)dev;
    switch (name) {
    case 0x1F00: return "ARM / Exynos4412 (Lima)";
    case 0x1F01: return "Mali-400 MP4";
    case 0x1F02: return "OpenGL ES 2.0 Mali400EX";
    default:     return "";
    }
}

static void gld_noop(void) {}

void gldPopulateContextDispatch(void *ctx, void **disp)
{
    (void)ctx;
    if (!disp)
        return;
    for (int i = 0; i < 25; i++)
        disp[i] = (void *)gld_noop;
}

void gldUpdateDispatch(void *p, void *disp, void *state)
{
    struct mali_ctx *ctx = (struct mali_ctx *)p;
    (void)state;
    if (!ctx || !ctx->gli_ctx)
        return;
    if (disp && *(void **)((uint8_t *)disp - 4) == ctx->gli_ctx)
        mali_gles_install_dispatch((uint8_t *)disp - 0x14);
    else
        mali_gles_reinstall_for_gli(ctx->gli_ctx);
}

int gldAttachDrawable(void *ctx, uint32_t type, void *draw, uint32_t opts)
{
    (void)ctx; (void)type; (void)draw; (void)opts;
    return 0;
}

int gldGetError(void *ctx) { (void)ctx; return 0; }
int gldSetInteger(void *ctx, uint32_t pname, const int *val) { (void)ctx; (void)pname; (void)val; return 0; }
int gldGetInteger(void *ctx, uint32_t pname, int *val) { (void)ctx; (void)pname; if (val) *val = 0; return 0; }

/* Helpers for mali_test */
void *mali_gld_get_lima_screen(void *p) { return p ? ((struct mali_device *)p)->lima_screen : 0; }
void *mali_gld_get_lima_ctx(void *p)    { return p ? ((struct mali_ctx *)p)->lima_ctx : 0; }

/* Object creation stubs that return a valid non-NULL dummy handle in *out (arg 2). */
static int stub_create_obj(void *a, void **out, void *c, void *d)
{
    (void)a; (void)c; (void)d;
    if (out)
        *out = calloc(1, 64);
    return 0;
}

static void stub_destroy_obj(void *a, void *obj)
{
    (void)a;
    free(obj);
}

#define STUB_ZERO(name) int name(void) { return 0; }
#define STUB_CREATE(name) int name(void *a, void **out, void *c, void *d) { return stub_create_obj(a, out, c, d); }
#define STUB_DESTROY(name) void name(void *a, void *obj) { stub_destroy_obj(a, obj); }

/* Remaining mandatory symbols from libGFXShared's 129-entry table */
STUB_ZERO(gldCreateDevicePartitionGroup)
STUB_ZERO(gldDestroyDevicePartitionGroup)
STUB_ZERO(gldCreateDevicePartition)
STUB_ZERO(gldDestroyDevicePartition)
STUB_ZERO(gldGetDevicePartitionInfo)

STUB_ZERO(gldUpdateReadFramebuffer)
STUB_ZERO(gldUpdateDrawFramebuffer)
STUB_ZERO(gldUpdateTransformFeedbackState)

STUB_ZERO(gldClearFramebufferData)
STUB_ZERO(gldBlitFramebufferData)
STUB_ZERO(gldReadFramebufferData)
STUB_ZERO(gldPresentFramebufferData)
STUB_ZERO(gldFlushContext)
STUB_ZERO(gldFinishContext)
STUB_ZERO(gldWaitForContext)

STUB_ZERO(gldTestObject)
STUB_ZERO(gldFlushObject)
STUB_ZERO(gldFinishObject)
STUB_ZERO(gldWaitForObject)

STUB_CREATE(gldCreateTexture)
STUB_ZERO(gldIsTextureResident)
STUB_ZERO(gldModifyTexture)
STUB_ZERO(gldGenerateTexMipmaps)
STUB_ZERO(gldLoadTexture)
STUB_ZERO(gldUnbindTexture)
STUB_ZERO(gldReclaimTexture)
STUB_DESTROY(gldDestroyTexture)
STUB_ZERO(gldCopyTexSubImage)
STUB_ZERO(gldModifyTexSubImage)
STUB_ZERO(gldGetTextureLevelInfo)
STUB_ZERO(gldRestoreTextureData)
STUB_ZERO(gldGetTextureAllocationIdentifiers)
STUB_ZERO(gldCopyTextureData)

STUB_CREATE(gldCreateBuffer)
STUB_ZERO(gldBufferSubData)
STUB_ZERO(gldCopyBufferSubData)
STUB_ZERO(gldLoadBuffer)
STUB_ZERO(gldRestoreBufferData)
STUB_ZERO(gldUnbindBuffer)
STUB_ZERO(gldReclaimBuffer)
STUB_DESTROY(gldDestroyBuffer)
STUB_ZERO(gldGetBufferAllocationIdentifiers)

STUB_ZERO(gldGetMemoryPlugin)
STUB_ZERO(gldSetMemoryPlugin)
STUB_ZERO(gldTestMemoryPlugin)
STUB_ZERO(gldFlushMemoryPlugin)
STUB_ZERO(gldDestroyMemoryPlugin)

STUB_CREATE(gldCreateFramebuffer)
STUB_ZERO(gldLoadFramebuffer)
STUB_ZERO(gldUnbindFramebuffer)
STUB_ZERO(gldDiscardFramebuffer)
STUB_ZERO(gldReclaimFramebuffer)
STUB_DESTROY(gldDestroyFramebuffer)

STUB_CREATE(gldCreatePipelineProgram)
STUB_ZERO(gldGetPipelineProgramInfo)
STUB_ZERO(gldModifyPipelineProgram)
STUB_ZERO(gldUnbindPipelineProgram)
STUB_DESTROY(gldDestroyPipelineProgram)
STUB_CREATE(gldCreateProgram)
STUB_DESTROY(gldDestroyProgram)

STUB_CREATE(gldCreateVertexArray)
STUB_ZERO(gldModifyVertexArray)
STUB_ZERO(gldFlushVertexArray)
STUB_ZERO(gldUnbindVertexArray)
STUB_ZERO(gldReclaimVertexArray)
STUB_DESTROY(gldDestroyVertexArray)

STUB_CREATE(gldCreateFence)
STUB_ZERO(gldSetFenceOnContext)
STUB_ZERO(gldSetFenceOnQueue)
STUB_ZERO(gldClearFence)
STUB_ZERO(gldFlushFenceOnQueue)
STUB_ZERO(gldFinishFenceOnQueue)
STUB_ZERO(gldWaitForFenceOnContext)
STUB_ZERO(gldWaitForFenceOnQueue)
STUB_ZERO(gldGetFenceStatus)
STUB_ZERO(gldGetFenceProfilingInfo)
STUB_DESTROY(gldDestroyFence)

STUB_CREATE(gldCreateQuery)
STUB_ZERO(gldModifyQuery)
STUB_ZERO(gldGetQueryInfo)
STUB_DESTROY(gldDestroyQuery)

STUB_ZERO(gldObjectPurgeable)
STUB_ZERO(gldObjectUnpurgeable)

STUB_CREATE(gldCreateSampler)
STUB_DESTROY(gldDestroySampler)

STUB_ZERO(gldCreateComputeContext)
STUB_ZERO(gldDestroyComputeContext)
STUB_ZERO(gldCreateQueue)
STUB_ZERO(gldDestroyQueue)
STUB_ZERO(gldFlushQueue)
STUB_ZERO(gldFinishQueue)
STUB_ZERO(gldCreateComputeModule)
STUB_ZERO(gldDestroyComputeModule)
STUB_ZERO(gldCompileComputeModule)
STUB_ZERO(gldLinkComputeModules)
STUB_ZERO(gldWriteComputeModuleBinary)
STUB_ZERO(gldCreateComputeProgram)
STUB_ZERO(gldDestroyComputeProgram)
STUB_ZERO(gldLinkComputeProgram)
STUB_ZERO(gldBuildComputeProgram)
STUB_ZERO(gldWriteComputeProgramBinary)
STUB_ZERO(gldCreateKernel)
STUB_ZERO(gldDestroyKernel)
STUB_ZERO(gldExecuteKernel)
STUB_ZERO(gldExecuteNativeKernel)
STUB_ZERO(gldCopyBufferDataWithQueue)
STUB_ZERO(gldCopyTextureDataWithQueue)
STUB_ZERO(gldCopyBufferDataToTextureWithQueue)
STUB_ZERO(gldCopyTextureDataToBufferWithQueue)
STUB_ZERO(gldReadTextureData)
STUB_ZERO(gldFinishBufferCPUAccess)
