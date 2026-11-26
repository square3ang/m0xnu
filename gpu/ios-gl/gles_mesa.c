/*
 * OpenGL ES 2.0 -> Mesa st_mesa -> Mali-400 (lima) bridge for iOS 9.3.5.
 *
 * Installs ARMv7 Thumb-2 trampolines into EAGLContextPrivate's GLIFunctionDispatch
 * table (+0x14) and swizzles EAGLContext's IOSurface/present entry points so
 * QuartzCore (backboardd/SpringBoard) renders on Mali-400 hardware into IOSurfaces.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/time.h>

extern uint32_t mali_iokit_conn;
extern void mali_wait_idle(void);
extern void mali_wait_seq(uint32_t target_seq);
extern uint32_t mali_get_last_enq_seq(void);
extern void mali_zero_wc(void *dst, size_t n);
extern __thread int lima_force_linear;
extern __thread uint32_t lima_external_va;
extern __thread int lima_skip_bo_zero;
extern uint32_t lima_next_bo_gen(void);
extern int IOConnectCallScalarMethod(uint32_t connection, uint32_t selector,
                                     const uint64_t *input, uint32_t inputCnt,
                                     uint64_t *output, uint32_t *outputCnt);
extern int IOConnectCallStructMethod(uint32_t connection, uint32_t selector,
                                     const void *inputStruct, size_t inputStructCnt,
                                     void *outputStruct, size_t *outputStructCnt);
extern const char *getprogname(void);

static uint32_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000u + (uint32_t)(tv.tv_usec / 1000u));
}

void kperf_log(const char *fmt, ...)
{
    if (!mali_iokit_conn) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
        size_t out_sz = 0;
        IOConnectCallStructMethod(mali_iokit_conn, 6u, buf, (size_t)n + 1u, NULL, &out_sz);
    }
}

void mali_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
        (void)write(2, buf, (size_t)n);
    }
}

#include "frontend/api.h"
#include "state_tracker/st_context.h"
#include "util/u_inlines.h"
struct lima_context;
struct lima_screen;
#include "lima_resource.h"
#include "lima_bo.h"
extern void lima_flush_job_accessing_bo(struct lima_context *ctx, struct lima_bo *bo, bool write);
extern struct lima_bo *lima_bo_create_external(struct lima_screen *screen, uint32_t va, uint32_t size);
#define MALI_LIMA_GEM_WAIT_WRITE 0x02u
#define MALI_ZC_MAX_BYTES        0x00500000u

static uint32_t map_iosurf_zc_va(void *surf, uint32_t w, uint32_t h, uint32_t *out_bpr);
#include "dispatch.h"

/* ObjC runtime (libobjc.A.dylib) */
extern void *objc_getClass(const char *name);
extern void *sel_registerName(const char *str);
extern void *class_getInstanceMethod(void *cls, void *sel);
extern void *method_getImplementation(void *m);
extern void *method_setImplementation(void *m, void *imp);
extern void *objc_msgSend(void *self, void *sel, ...);

/* IOSurface (IOSurface.framework) & CoreFoundation */
extern int    IOSurfaceLock(void *surf, uint32_t options, uint32_t *seed);
extern int    IOSurfaceUnlock(void *surf, uint32_t options, uint32_t *seed);
extern void  *IOSurfaceGetBaseAddress(void *surf);
extern void  *IOSurfaceGetBaseAddressOfPlane(void *surf, size_t plane);
extern size_t IOSurfaceGetBytesPerRow(void *surf);
extern size_t IOSurfaceGetBytesPerRowOfPlane(void *surf, size_t plane);
extern size_t IOSurfaceGetPlaneCount(void *surf);
extern size_t IOSurfaceGetWidth(void *surf);
extern size_t IOSurfaceGetHeight(void *surf);
extern void  *CFRetain(void *cf);
extern void   CFRelease(void *cf);

/* Mesa ES2 core functions called directly by wrappers */
extern void   _mesa_BindFramebuffer(GLenum target, GLuint fb);
extern void   _mesa_BindRenderbuffer(GLenum target, GLuint rb);
extern void   _mesa_BindTexture(GLenum target, GLuint tex);
extern void   _mesa_FramebufferRenderbuffer(GLenum target, GLenum att, GLenum rbtarget, GLuint rb);
extern void   _mesa_FramebufferTexture2D(GLenum target, GLenum att, GLenum textarget, GLuint tex, GLint level);
extern void   _mesa_DeleteFramebuffers(GLsizei n, const GLuint *fbs);
extern void   _mesa_DeleteRenderbuffers(GLsizei n, const GLuint *rbs);
extern void   _mesa_DeleteTextures(GLsizei n, const GLuint *texs);
extern void   _mesa_Clear(GLbitfield mask);
extern void   _mesa_DrawArrays(GLenum mode, GLint first, GLsizei count);
extern void   _mesa_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
extern void   _mesa_Flush(void);
extern void   _mesa_Finish(void);
extern void   _mesa_TexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                               GLint border, GLenum fmt, GLenum type, const void *pixels);
extern void   _mesa_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                  GLenum fmt, GLenum type, const void *pixels);
extern void   _mesa_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *pixels);
extern void   _mesa_RenderbufferStorage(GLenum target, GLenum ifmt, GLsizei w, GLsizei h);
extern void   _mesa_PixelStorei(GLenum pname, GLint param);
extern void   _mesa_GetIntegerv(GLenum pname, GLint *params);
extern const GLubyte *_mesa_GetString(GLenum name);
extern void   _mesa_Enable(GLenum cap);
extern void   _mesa_Disable(GLenum cap);
extern void   _mesa_Scissor(GLint x, GLint y, GLsizei w, GLsizei h);

/* Custom wrapper declarations (referenced by gl_disp_entries.h) */
void wrap_Enable(GLenum cap);
void wrap_Disable(GLenum cap);
void wrap_Scissor(GLint x, GLint y, GLsizei w, GLsizei h);
void wrap_BindFramebuffer(GLenum target, GLuint fb);
void wrap_BindRenderbuffer(GLenum target, GLuint rb);
void wrap_BindTexture(GLenum target, GLuint tex);
void wrap_FramebufferRenderbuffer(GLenum target, GLenum att, GLenum rbtarget, GLuint rb);
void wrap_FramebufferTexture2D(GLenum target, GLenum att, GLenum textarget, GLuint tex, GLint level);
void wrap_DeleteFramebuffers(GLsizei n, const GLuint *fbs);
void wrap_DeleteRenderbuffers(GLsizei n, const GLuint *rbs);
void wrap_DeleteTextures(GLsizei n, const GLuint *texs);
void wrap_Clear(GLbitfield mask);
void wrap_DrawArrays(GLenum mode, GLint first, GLsizei count);
void wrap_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void wrap_Flush(void);
void wrap_Finish(void);
void wrap_TexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                     GLint border, GLenum fmt, GLenum type, const void *pixels);
void wrap_CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                               GLsizei width, GLsizei height, GLint border,
                               GLsizei imageSize, const void *data);
void wrap_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                        GLenum fmt, GLenum type, const void *pixels);
void wrap_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *pixels);
void wrap_RenderbufferStorage(GLenum target, GLenum ifmt, GLsizei w, GLsizei h);
void wrap_RenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum ifmt, GLsizei w, GLsizei h);
void wrap_ResolveMultisampleFramebufferAPPLE(void);
const GLubyte *wrap_GetString(GLenum name);
void wrap_VertexAttrib1f(GLuint idx, GLfloat x);
void wrap_VertexAttrib2f(GLuint idx, GLfloat x, GLfloat y);
void wrap_VertexAttrib3f(GLuint idx, GLfloat x, GLfloat y, GLfloat z);
void wrap_VertexAttrib4f(GLuint idx, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void wrap_VertexAttrib1fv(GLuint idx, const GLfloat *v);
void wrap_VertexAttrib2fv(GLuint idx, const GLfloat *v);
void wrap_VertexAttrib3fv(GLuint idx, const GLfloat *v);
void wrap_VertexAttrib4fv(GLuint idx, const GLfloat *v);
void wrap_GenFencesAPPLE(GLsizei n, GLuint *fences);
void wrap_DeleteFencesAPPLE(GLsizei n, const GLuint *fences);
void wrap_SetFenceAPPLE(GLuint fence);
GLboolean wrap_IsFenceAPPLE(GLuint fence);
GLboolean wrap_TestFenceAPPLE(GLuint fence);
void wrap_FinishFenceAPPLE(GLuint fence);
GLboolean wrap_TestObjectAPPLE(GLenum obj, GLuint name);
void wrap_FinishObjectAPPLE(GLenum obj, GLuint name);
void wrap_Noop(void);
void wrap_GetObjectLabelEXT(GLenum type, GLuint obj, GLsizei bufSize, GLsizei *length, char *label);
GLenum wrap_ClientWaitSync(void *sync, GLbitfield flags, uint32_t tlo, uint32_t thi);
void wrap_WaitSync(void *sync, GLbitfield flags, uint32_t tlo, uint32_t thi);

#include "gl_disp_entries.h"

#define MAX_CTX     32
#define MAX_IOSURF  512
#define MAX_FBOS    512

struct mali_gles_ctx {
    void *gli_ctx;
    void *eagl_priv;
    struct st_context *st;
    GLuint bound_fbo;
    GLuint bound_rb;
    GLuint bound_tex;
    int scissor_en;
    uint8_t scissor_en_valid;
    uint8_t scissor_valid;
    GLint sx, sy;
    GLsizei sw, sh;
    uint32_t draw_cnt;
    uint32_t first_draw_ms;
    GLuint dirty_fbo_id;
    uint32_t dirty_fbo_gen;
    struct pipe_resource *dirty_fbo_pt;
};

struct iosurf_binding {
    int used;
    int is_rb;
    GLuint obj_id;
    void *surf;
    struct pipe_resource *pt;
    uint32_t w, h, plane, invert;
    uint32_t fmt, type, seed;
    uint32_t bound_fpa;
    uint32_t zc_va;
    uint32_t zc_bpr;
    int dirty;
    uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;
};

struct fbo_attach {
    int used;
    GLuint fbo_id;
    int is_rb;
    GLuint obj_id;
};

static pthread_mutex_t gles_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct pipe_frontend_screen g_fscreen;
static struct mali_gles_ctx g_ctxs[MAX_CTX];
static struct iosurf_binding g_iosurfs[MAX_IOSURF];
static struct fbo_attach g_fbos[MAX_FBOS];
static int g_iosurf_hi = 0;
static int g_fbo_hi = 0;
static uint64_t g_iosurf_fbo_mask = 0;
static uint64_t g_iosurf_obj_bloom[4] = {0, 0, 0, 0};
static volatile uint32_t g_iosurf_gen = 1;
static __thread void *tls_cur_gli = NULL;
static __thread struct mali_gles_ctx *tls_cur_ctx = NULL;

static uint8_t *g_scratch = NULL;
static size_t g_scratch_cap = 0;

static uint8_t *ensure_scratch(size_t need)
{
    if (need > g_scratch_cap) {
        uint8_t *p = (uint8_t *)realloc(g_scratch, need);
        if (!p) return NULL;
        g_scratch = p;
        g_scratch_cap = need;
    }
    return g_scratch;
}

static struct mali_gles_ctx *find_ctx_locked(void *gli_ctx)
{
    for (int i = 0; i < MAX_CTX; i++)
        if (g_ctxs[i].gli_ctx == gli_ctx && g_ctxs[i].st)
            return &g_ctxs[i];
    return NULL;
}

extern void _mesa_Viewport_no_error(GLint x, GLint y, GLsizei w, GLsizei h);
extern void _mesa_ActiveTexture_no_error(GLenum tex);
extern void _mesa_BlendFunc_no_error(GLenum s, GLenum d);
extern void _mesa_BlendFuncSeparate_no_error(GLenum sr, GLenum dr, GLenum sa, GLenum da);
extern void _mesa_BlendEquationSeparate_no_error(GLenum mr, GLenum ma);
extern void _mesa_BindBuffer_no_error(GLenum t, GLuint b);
extern void _mesa_BufferData_no_error(GLenum t, GLsizeiptr s, const void *d, GLenum u);
extern void _mesa_BufferSubData_no_error(GLenum t, GLintptr o, GLsizeiptr s, const void *d);
extern void _mesa_UseProgram_no_error(GLuint p);
extern void _mesa_VertexAttribPointer_no_error(GLuint idx, GLint sz, GLenum type, GLboolean norm, GLsizei stride, const void *ptr);
extern void _mesa_EnableVertexAttribArray_no_error(GLuint idx);
extern void _mesa_DisableVertexAttribArray_no_error(GLuint idx);
extern void _mesa_Scissor_no_error(GLint x, GLint y, GLsizei w, GLsizei h);
extern void _mesa_BindTexture_no_error(GLenum t, GLuint tex);
extern void _mesa_Clear_no_error(GLbitfield mask);
extern void _mesa_FramebufferTexture2D_no_error(GLenum t, GLenum a, GLenum tt, GLuint tex, GLint lvl);
extern void _mesa_FramebufferRenderbuffer_no_error(GLenum t, GLenum a, GLenum rbt, GLuint rb);
extern void _mesa_CullFace_no_error(GLenum m);
extern void _mesa_FrontFace_no_error(GLenum m);
extern void _mesa_StencilFunc_no_error(GLenum f, GLint r, GLuint m);
extern void _mesa_StencilOp_no_error(GLenum f, GLenum zf, GLenum zp);
extern void _mesa_StencilFuncSeparate_no_error(GLenum fc, GLenum f, GLint r, GLuint m);
extern void _mesa_StencilOpSeparate_no_error(GLenum fc, GLenum f, GLenum zf, GLenum zp);
extern void _mesa_StencilMaskSeparate_no_error(GLenum fc, GLuint m);
extern void _mesa_PixelStorei_no_error(GLenum p, GLint v);
extern void _mesa_DepthFunc_no_error(GLenum f);
extern void _mesa_BindVertexArray_no_error(GLuint a);
extern void _mesa_BlitFramebuffer_no_error(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                                           GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                           GLbitfield mask, GLenum filter);
extern void _mesa_TexImage2D_no_error(GLenum t, GLint lvl, GLint ifmt, GLsizei w, GLsizei h,
                                      GLint b, GLenum fmt, GLenum type, const void *px);
extern GLint _mesa_GetUniformLocation_no_error(GLuint prog, const GLchar *name);
extern void *_mesa_MapBufferRange_no_error(GLenum t, GLintptr o, GLsizeiptr l, GLbitfield a);
extern GLboolean _mesa_UnmapBuffer_no_error(GLenum t);

static int is_backboardd_proc(void);
static void *g_fast_gli = NULL;
static struct mali_gles_ctx *g_fast_ctx = NULL;
static uintptr_t g_fast_tid = 0;
static void *g_tramp_targets[GLD_DISP_COUNT];
extern struct {
    void *gli;
    uintptr_t tid;
    void *const *targets;
} g_tramp_fast;

static inline uintptr_t fast_cur_tid(void)
{
    uintptr_t t;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(t));
    return t;
}

static inline struct mali_gles_ctx *fast_cur_ctx(void)
{
    if (__builtin_expect(g_fast_ctx && fast_cur_tid() == g_fast_tid, 1))
        return g_fast_ctx;
    return tls_cur_ctx;
}

static void mali_ensure_current_by_gli(void *gli_ctx)
{
    if (!gli_ctx || gli_ctx == tls_cur_gli)
        return;
    pthread_mutex_lock(&gles_mtx);
    struct mali_gles_ctx *c = find_ctx_locked(gli_ctx);
    if (c && !g_tramp_fast.targets) {
        int bb_ne = is_backboardd_proc();
        for (uint32_t i = 0; i < GLD_DISP_COUNT; i++) {
            void *fn = gld_disp_entries[i].target;
            if (bb_ne) {
                if (fn == (void *)_mesa_Viewport) fn = (void *)_mesa_Viewport_no_error;
                else if (fn == (void *)_mesa_ActiveTexture) fn = (void *)_mesa_ActiveTexture_no_error;
                else if (fn == (void *)_mesa_BlendFunc) fn = (void *)_mesa_BlendFunc_no_error;
                else if (fn == (void *)_mesa_BlendFuncSeparate) fn = (void *)_mesa_BlendFuncSeparate_no_error;
                else if (fn == (void *)_mesa_BlendEquationSeparate) fn = (void *)_mesa_BlendEquationSeparate_no_error;
                else if (fn == (void *)_mesa_BindBuffer) fn = (void *)_mesa_BindBuffer_no_error;
                else if (fn == (void *)_mesa_BufferData) fn = (void *)_mesa_BufferData_no_error;
                else if (fn == (void *)_mesa_BufferSubData) fn = (void *)_mesa_BufferSubData_no_error;
                else if (fn == (void *)_mesa_UseProgram) fn = (void *)_mesa_UseProgram_no_error;
                else if (fn == (void *)_mesa_VertexAttribPointer) fn = (void *)_mesa_VertexAttribPointer_no_error;
                else if (fn == (void *)_mesa_EnableVertexAttribArray) fn = (void *)_mesa_EnableVertexAttribArray_no_error;
                else if (fn == (void *)_mesa_DisableVertexAttribArray) fn = (void *)_mesa_DisableVertexAttribArray_no_error;
                else if (fn == (void *)_mesa_CullFace) fn = (void *)_mesa_CullFace_no_error;
                else if (fn == (void *)_mesa_FrontFace) fn = (void *)_mesa_FrontFace_no_error;
                else if (fn == (void *)_mesa_StencilFunc) fn = (void *)_mesa_StencilFunc_no_error;
                else if (fn == (void *)_mesa_StencilOp) fn = (void *)_mesa_StencilOp_no_error;
                else if (fn == (void *)_mesa_StencilFuncSeparate) fn = (void *)_mesa_StencilFuncSeparate_no_error;
                else if (fn == (void *)_mesa_StencilOpSeparate) fn = (void *)_mesa_StencilOpSeparate_no_error;
                else if (fn == (void *)_mesa_StencilMaskSeparate) fn = (void *)_mesa_StencilMaskSeparate_no_error;
                else if (fn == (void *)_mesa_PixelStorei) fn = (void *)_mesa_PixelStorei_no_error;
                else if (fn == (void *)_mesa_DepthFunc) fn = (void *)_mesa_DepthFunc_no_error;
                else if (fn == (void *)_mesa_BindVertexArray) fn = (void *)_mesa_BindVertexArray_no_error;
                else if (fn == (void *)_mesa_BlitFramebuffer) fn = (void *)_mesa_BlitFramebuffer_no_error;
                else if (fn == (void *)_mesa_GetUniformLocation) fn = (void *)_mesa_GetUniformLocation_no_error;
                else if (fn == (void *)_mesa_MapBufferRange) fn = (void *)_mesa_MapBufferRange_no_error;
                else if (fn == (void *)_mesa_UnmapBuffer) fn = (void *)_mesa_UnmapBuffer_no_error;
            }
            g_tramp_targets[i] = fn;
        }
        __asm__ volatile("dmb ish" ::: "memory");
        g_tramp_fast.targets = g_tramp_targets;
    }
    pthread_mutex_unlock(&gles_mtx);
    if (c && c->st) {
        st_api_make_current(c->st, NULL, NULL);
        tls_cur_gli = gli_ctx;
        tls_cur_ctx = c;
        /* Keep MRU fast-path active even in multi-context processes (e.g. SpringBoard):
         * clear gli and tid before updating ctx so concurrent threads never observe a torn pair. */
        g_tramp_fast.gli = NULL;
        g_fast_gli = NULL;
        g_fast_tid = 0;
        g_tramp_fast.tid = 0;
        __asm__ volatile("dmb ish" ::: "memory");
        uintptr_t tid = fast_cur_tid();
        g_fast_ctx = c;
        __asm__ volatile("dmb ish" ::: "memory");
        g_fast_tid = tid;
        g_tramp_fast.tid = tid;
        __asm__ volatile("dmb ish" ::: "memory");
        g_fast_gli = gli_ctx;
        g_tramp_fast.gli = gli_ctx;
    }
}

void *mali_prepare_call(void *gli_ctx, uint32_t idx)
{
    if (__builtin_expect(!gli_ctx || (gli_ctx == g_fast_gli && fast_cur_tid() == g_fast_tid), 1)) {
        if (__builtin_expect(idx >= GLD_DISP_COUNT, 0))
            return (void *)wrap_Noop;
        return g_tramp_fast.targets ? g_tramp_fast.targets[idx] : gld_disp_entries[idx].target;
    }
    mali_ensure_current_by_gli(gli_ctx);
    if (idx >= GLD_DISP_COUNT)
        return (void *)wrap_Noop;
    return g_tramp_fast.targets ? g_tramp_fast.targets[idx] : gld_disp_entries[idx].target;
}

static int is_backboardd_proc(void);

static uint8_t hook_is_tp(void *self, void *cmd, void *ev, void *dig)
{
    (void)self; (void)cmd; (void)ev; (void)dig;
    return 0; /* built-in touchscreen, never Apple TV TouchPad */
}

typedef void (*ca_set_blanked_t)(void *self, void *cmd, uint8_t blanked);
static ca_set_blanked_t orig_set_blanked;
static void hook_set_blanked(void *self, void *cmd, uint8_t blanked)
{
    static uint32_t g_blk_log = 0;
    if (g_blk_log < 16u) {
        g_blk_log++;
        kperf_log("[GL] setBlanked(%u) pid=%d\n", (unsigned)blanked, (int)getpid());
    }
    /* shortcut: keep display active while no power-button wake driver exists on Galaxy S3. */
    if (blanked)
        return;
    if (orig_set_blanked)
        orig_set_blanked(self, cmd, blanked);
}

static void install_bk_touch_hooks(void)
{
    typedef void *(*get_cm_t)(void *, void *);
    get_cm_t get_cm = (get_cm_t)dlsym(RTLD_DEFAULT, "class_getClassMethod");
    void *c_tp = objc_getClass("BKTouchPadManager");
    void *m_tp = (c_tp && get_cm) ? get_cm(c_tp, sel_registerName("isTouchPadEvent:fromDigitizer:")) : NULL;
    if (m_tp)
        method_setImplementation(m_tp, (void *)hook_is_tp);
    void *c_wsd = objc_getClass("CAWindowServerDisplay");
    void *m_blk = c_wsd ? class_getInstanceMethod(c_wsd, sel_registerName("setBlanked:")) : NULL;
    if (m_blk) {
        orig_set_blanked = (ca_set_blanked_t)method_getImplementation(m_blk);
        method_setImplementation(m_blk, (void *)hook_set_blanked);
    }
}

static void hook_dim_timer(void *self, void *cmd)
{
    /* shortcut: keep screen awake while no PMU AC-power driver reports charging (prevents 8s LockScreen blackout). */
    (void)self; (void)cmd;
}

static long hook_ret0(void *self, void *cmd) { (void)self; (void)cmd; return 0; }
static double hook_ret0_dbl(void *self, void *cmd) { (void)self; (void)cmd; return 0.0; }
static uint8_t hook_ret1(void *self, void *cmd) { (void)self; (void)cmd; return 1; }

typedef void (*sb_wp_setup_t)(void *self, void *cmd, void *img, uint8_t treated, uint8_t force);
static sb_wp_setup_t orig_wp_setup;
static void hook_wp_setup(void *self, void *cmd, void *img, uint8_t treated, uint8_t force)
{
    /* iPhone4,1 is absent from DefaultWallpapers.plist so SpringBoard hands a black placeholder;
     * substitute stock Mars HERO wallpaper so Lock/Home screens and Dock translucency render properly. */
    void *c_ns = objc_getClass("NSString");
    void *c_im = objc_getClass("UIImage");
    if (c_ns && c_im) {
        void *path = ((void *(*)(void *, void *, const char *))objc_msgSend)(
            c_ns, sel_registerName("stringWithUTF8String:"),
            "/Library/Wallpaper/iPhone/100.Mars_HERO@2x~iphone.png");
        if (path) {
            void *mars = ((void *(*)(void *, void *, void *))objc_msgSend)(
                c_im, sel_registerName("imageWithContentsOfFile:"), path);
            if (mars) {
                img = mars;
                treated = 0;
            }
        }
    }
    if (orig_wp_setup) orig_wp_setup(self, cmd, img, treated, force);
}

static void install_sb_touch_hooks(void)
{
    static const char *const bl_cls[] = { "SBBacklightController", "SpringBoard", "SBLockScreenManager" };
    static const char *const bl_sels[] = {
        "_lockScreenDimTimerFired", "_lockScreenDimmed:", "_autoLockTimerFired:", "autoLock",
        "handleAutoLock", "_setBacklightFactorToZeroForProx", "setBacklightFactorToZeroForProx"
    };
    static const char *const dbl_sels[] = { "autoLockTime", "_requestedAutoLockTime" };
    for (unsigned ci = 0; ci < sizeof(bl_cls) / sizeof(bl_cls[0]); ci++) {
        void *cl = objc_getClass(bl_cls[ci]);
        if (!cl) continue;
        for (unsigned si = 0; si < sizeof(bl_sels) / sizeof(bl_sels[0]); si++) {
            void *m = class_getInstanceMethod(cl, sel_registerName(bl_sels[si]));
            if (m) method_setImplementation(m, (void *)hook_dim_timer);
        }
        for (unsigned si = 0; si < sizeof(dbl_sels) / sizeof(dbl_sels[0]); si++) {
            void *m = class_getInstanceMethod(cl, sel_registerName(dbl_sels[si]));
            if (m) method_setImplementation(m, (void *)hook_ret0_dbl);
        }
        void *m_relock = class_getInstanceMethod(cl, sel_registerName("canRelockForAutoDimTimer"));
        if (m_relock) method_setImplementation(m_relock, (void *)hook_ret0);
    }
    /* Disable 3.5" 640x960 classic letterboxing so iPhone4,1 stock apps (Calculator/Compass/Weather/Stocks) fill 720x1280. */
    void *c_app = objc_getClass("SBApplication");
    void *m_cm  = c_app ? class_getInstanceMethod(c_app, sel_registerName("_classicMode")) : NULL;
    void *m_dcm = c_app ? class_getInstanceMethod(c_app, sel_registerName("_defaultClassicMode")) : NULL;
    void *m_ic  = c_app ? class_getInstanceMethod(c_app, sel_registerName("isClassic")) : NULL;
    void *m_dis = c_app ? class_getInstanceMethod(c_app, sel_registerName("disablesClassicMode")) : NULL;
    if (m_cm)  method_setImplementation(m_cm,  (void *)hook_ret0);
    if (m_dcm) method_setImplementation(m_dcm, (void *)hook_ret0);
    if (m_ic)  method_setImplementation(m_ic,  (void *)hook_ret0);
    if (m_dis) method_setImplementation(m_dis, (void *)hook_ret1);
    void *c_wp = objc_getClass("SBFStaticWallpaperView");
    void *m_wp = c_wp ? class_getInstanceMethod(c_wp, sel_registerName("_setupWallpaperImage:treated:forceNoTreatment:")) : NULL;
    if (m_wp) { orig_wp_setup = (sb_wp_setup_t)method_getImplementation(m_wp); method_setImplementation(m_wp, (void *)hook_wp_setup); }
}

void mali_gles_init_screen(void *pipe_screen)
{
    pthread_mutex_lock(&gles_mtx);
    if (!g_fscreen.screen && pipe_screen)
        g_fscreen.screen = (struct pipe_screen *)pipe_screen;
    pthread_mutex_unlock(&gles_mtx);

    /* Spawn ExyTouchd from backboardd so it inherits a live System bootstrap_port (com.apple.iohideventsystem). */
    static int touchd_spawned = 0;
    if (!touchd_spawned && is_backboardd_proc()) {
        touchd_spawned = 1;
        install_bk_touch_hooks();
        int pid = fork();
        if (pid == 0) {
            setsid();
            char *argv[] = { "ExyTouchd", NULL };
            execv("/usr/libexec/ExyTouchd", argv);
            _exit(1);
        }
    }
    static int sb_hooked = 0;
    if (!sb_hooked) {
        const char *pn = getprogname();
        if (pn && strcmp(pn, "SpringBoard") == 0) {
            sb_hooked = 1;
            install_sb_touch_hooks();
        }
    }
}

void *mali_gles_create_context(void *gli_ctx, void *shared_st)
{
    pthread_mutex_lock(&gles_mtx);
    if (!g_fscreen.screen || !gli_ctx) {
        pthread_mutex_unlock(&gles_mtx);
        return NULL;
    }
    struct mali_gles_ctx *slot = NULL;
    for (int i = 0; i < MAX_CTX; i++) {
        if (!g_ctxs[i].st) {
            slot = &g_ctxs[i];
            break;
        }
    }
    if (!slot) {
        pthread_mutex_unlock(&gles_mtx);
        return NULL;
    }
    struct st_context_attribs att;
    memset(&att, 0, sizeof(att));
    att.profile = API_OPENGLES2;
    att.major = 2;
    att.minor = 0;
    if (is_backboardd_proc())
        att.flags |= ST_CONTEXT_FLAG_NO_ERROR;
    att.visual.buffer_mask = ST_ATTACHMENT_FRONT_LEFT_MASK | ST_ATTACHMENT_DEPTH_STENCIL_MASK;
    att.visual.color_format = PIPE_FORMAT_B8G8R8A8_UNORM;
    att.visual.depth_stencil_format = PIPE_FORMAT_Z24_UNORM_S8_UINT;
    enum st_context_error err = ST_CONTEXT_SUCCESS;
    struct st_context *st = st_api_create_context(&g_fscreen, &att, &err, (struct st_context *)shared_st);
    mali_log("[MALI] create_ctx gli=%p screen=%p st=%p err=%d\n", gli_ctx, g_fscreen.screen, st, (int)err);
    if (st) {
        memset(slot, 0, sizeof(*slot));
        slot->gli_ctx = gli_ctx;
        slot->st = st;
        slot->sw = -1;
        slot->sh = -1;
    }
    pthread_mutex_unlock(&gles_mtx);
    return st;
}

void mali_gles_destroy_context(void *gli_ctx)
{
    struct st_context *old_st = NULL;
    pthread_mutex_lock(&gles_mtx);
    struct mali_gles_ctx *c = find_ctx_locked(gli_ctx);
    if (c) {
        g_tramp_fast.gli = NULL;
        g_fast_gli = NULL;
        __asm__ volatile("dmb ish" ::: "memory");
        g_fast_ctx = NULL;
        g_fast_tid = 0;
        g_tramp_fast.tid = 0;
        old_st = c->st;
        if (tls_cur_gli == gli_ctx) {
            st_api_make_current(NULL, NULL, NULL);
            tls_cur_gli = NULL;
            tls_cur_ctx = NULL;
        }
        memset(c, 0, sizeof(*c));
    }
    pthread_mutex_unlock(&gles_mtx);
    if (old_st) {
        mali_wait_idle();
        st_destroy_context(old_st);
    }
}

void mali_gles_install_dispatch(void *eagl_priv)
{
    if (!eagl_priv)
        return;
    uint8_t *p = (uint8_t *)eagl_priv;
    if (*(void **)(p + gld_disp_entries[0].priv_off) == gld_disp_entries[0].stub)
        return;
    void *gli_ctx = *(void **)(p + 0x10);
    pthread_mutex_lock(&gles_mtx);
    struct mali_gles_ctx *c = find_ctx_locked(gli_ctx);
    void *first_gli = g_ctxs[0].gli_ctx;
    if (c)
        c->eagl_priv = eagl_priv;
    pthread_mutex_unlock(&gles_mtx);
    static int log_n = 0;
    if (log_n < 12) {
        log_n++;
        mali_log("[MALI] install priv=%p gli=%p found=%p ctx0_gli=%p\n", eagl_priv, gli_ctx, c, first_gli);
    }
    if (!c)
        return;
    for (int i = 0; i < GLD_DISP_COUNT; i++)
        *(void **)(p + gld_disp_entries[i].priv_off) = gld_disp_entries[i].stub;
}

void mali_gles_reinstall_for_gli(void *gli_ctx)
{
    pthread_mutex_lock(&gles_mtx);
    struct mali_gles_ctx *c = find_ctx_locked(gli_ctx);
    void *priv = c ? c->eagl_priv : NULL;
    pthread_mutex_unlock(&gles_mtx);
    if (priv)
        mali_gles_install_dispatch(priv);
}

/* --- IOSurface <-> FBO tracking and readback ------------------------------- */

static struct iosurf_binding *find_iosurf_locked(int is_rb, GLuint obj_id)
{
    if (!obj_id) return NULL;
    for (int i = 0; i < g_iosurf_hi; i++)
        if (g_iosurfs[i].used && g_iosurfs[i].is_rb == is_rb && g_iosurfs[i].obj_id == obj_id)
            return &g_iosurfs[i];
    return NULL;
}

static struct iosurf_binding *find_fbo_iosurf_locked(GLuint fbo_id)
{
    if (!fbo_id) return NULL;
    for (int i = 0; i < g_fbo_hi; i++) {
        if (g_fbos[i].used && g_fbos[i].fbo_id == fbo_id)
            return find_iosurf_locked(g_fbos[i].is_rb, g_fbos[i].obj_id);
    }
    return NULL;
}

static void rebuild_iosurf_fbo_mask_locked(void)
{
    uint64_t m = 0;
    uint64_t bm[4] = {0, 0, 0, 0};
    for (int i = 0; i < g_iosurf_hi; i++) {
        if (g_iosurfs[i].used) {
            uint32_t id = g_iosurfs[i].obj_id;
            bm[(id >> 6) & 3u] |= (1ull << (id & 63u));
        }
    }
    for (int i = 0; i < g_fbo_hi; i++) {
        if (g_fbos[i].used && g_fbos[i].fbo_id < 64u &&
            find_iosurf_locked(g_fbos[i].is_rb, g_fbos[i].obj_id))
            m |= (1ull << g_fbos[i].fbo_id);
    }
    g_iosurf_obj_bloom[0] = bm[0];
    g_iosurf_obj_bloom[1] = bm[1];
    g_iosurf_obj_bloom[2] = bm[2];
    g_iosurf_obj_bloom[3] = bm[3];
    g_iosurf_fbo_mask = m;
}

static void memcpy_neon(void *dst_p, const void *src_p, size_t n)
{
    uint8_t *d = (uint8_t *)dst_p;
    const uint8_t *s = (const uint8_t *)src_p;
    while (n >= 64u) {
        __asm__ volatile(
            "vld1.8 {q0, q1}, [%1]!\n"
            "vld1.8 {q2, q3}, [%1]!\n"
            "vst1.8 {q0, q1}, [%0]!\n"
            "vst1.8 {q2, q3}, [%0]!\n"
            : "+r"(d), "+r"(s)
            :: "q0", "q1", "q2", "q3", "memory");
        n -= 64u;
    }
    while (n >= 16u) {
        __asm__ volatile(
            "vld1.8 {q0}, [%1]!\n"
            "vst1.8 {q0}, [%0]!\n"
            : "+r"(d), "+r"(s)
            :: "q0", "memory");
        n -= 16u;
    }
    if (n)
        memcpy(d, s, n);
}

static int iosurf_pipe_fmt_ok(enum pipe_format pf, GLint ifmt, GLenum fmt, GLenum type)
{
    if (type != GL_UNSIGNED_BYTE)
        return 0;
    int need_alpha = (ifmt != GL_RGB);
    if (fmt == 0x80e1u /* GL_BGRA_EXT */)
        return pf == PIPE_FORMAT_B8G8R8A8_UNORM || (!need_alpha && pf == PIPE_FORMAT_B8G8R8X8_UNORM);
    if (fmt == GL_RGBA)
        return pf == PIPE_FORMAT_R8G8B8A8_UNORM || (!need_alpha && pf == PIPE_FORMAT_R8G8B8X8_UNORM);
    if (fmt == GL_ALPHA)
        return pf == PIPE_FORMAT_A8_UNORM;
    if (fmt == GL_LUMINANCE)
        return pf == PIPE_FORMAT_L8_UNORM;
    if (fmt == 0x1903u /* GL_RED_EXT */)
        return pf == PIPE_FORMAT_R8_UNORM;
    return 0;
}

static void rgba_to_bgra_row(uint32_t *dst, const uint32_t *src, uint32_t count)
{
    uint32_t n = count;
    while (n >= 8u) {
        __asm__ volatile(
            "vld4.8 {d0, d1, d2, d3}, [%1]!\n"
            "vswp d0, d2\n"
            "vst4.8 {d0, d1, d2, d3}, [%0]!\n"
            : "+r"(dst), "+r"(src)
            :: "d0", "d1", "d2", "d3", "memory");
        n -= 8u;
    }
    while (n--) {
        uint32_t p = *src++;
        *dst++ = (p & 0xff00ff00u) | ((p & 0x00ff0000u) >> 16) | ((p & 0x000000ffu) << 16);
    }
}

static struct pipe_resource *g_scanout_res[2];
static uint32_t g_scanout_idx = 0;
static int g_is_backboardd = -1;

static int is_backboardd_proc(void)
{
    if (g_is_backboardd < 0) {
        const char *pn = getprogname();
        g_is_backboardd = (pn && strcmp(pn, "backboardd") == 0) ? 1 : 0;
    }
    return g_is_backboardd;
}

static struct pipe_resource *ensure_scanout_res(struct pipe_screen *scr, uint32_t idx)
{
    if (idx >= 2u || !scr)
        return NULL;
    if (g_scanout_res[idx])
        return g_scanout_res[idx];
    struct pipe_resource tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    tmpl.target = PIPE_TEXTURE_2D;
    tmpl.format = PIPE_FORMAT_B8G8R8A8_UNORM;
    tmpl.width0 = 720;
    tmpl.height0 = 1280;
    tmpl.depth0 = 1;
    tmpl.array_size = 1;
    tmpl.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_LINEAR;
    /* Zero-pool-byte external BO pointing directly at double-buffered FIMD0 scanout (0x03000000/0x03400000). */
    lima_external_va = 0x03000000u + idx * 0x00400000u;
    struct pipe_resource *r = scr->resource_create(scr, &tmpl);
    lima_external_va = 0u;
    if (!r)
        return NULL;
    struct lima_resource *lres = lima_resource(r);
    if (!lres->bo) {
        pipe_resource_reference(&r, NULL);
        return NULL;
    }
    lres->bo->va = 0x03000000u + idx * 0x00400000u;
    lres->tiled = false;
    lres->levels[0].stride = 720u * 4u;
    lres->levels[0].offset = 0u;
    g_scanout_res[idx] = r;
    return r;
}

struct dirty_snap {
    void *surf;
    struct pipe_resource *pt;
    int is_rb;
    uint32_t w, h, invert, fmt;
    uint32_t rx, ry, rw, rh;
    uint32_t prev_fpa;
    uint32_t zc_va;
    uint32_t zc_bpr;
    int slot_idx;
};

static struct pipe_resource *g_zc_blit_res = NULL;
static uint32_t g_zc_blit_w = 0, g_zc_blit_h = 0;

static struct pipe_resource *ensure_zc_blit_res(struct pipe_screen *scr, uint32_t w, uint32_t h,
                                                uint32_t zc_va, uint32_t bpr)
{
    if (!scr || !w || !h || !zc_va || !bpr)
        return NULL;
    if (g_zc_blit_res && (g_zc_blit_w != w || g_zc_blit_h != h)) {
        pipe_resource_reference(&g_zc_blit_res, NULL);
        g_zc_blit_w = 0;
        g_zc_blit_h = 0;
    }
    if (!g_zc_blit_res) {
        struct pipe_resource tmpl;
        memset(&tmpl, 0, sizeof(tmpl));
        tmpl.target = PIPE_TEXTURE_2D;
        tmpl.format = PIPE_FORMAT_B8G8R8A8_UNORM;
        tmpl.width0 = w;
        tmpl.height0 = h;
        tmpl.depth0 = 1;
        tmpl.array_size = 1;
        tmpl.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_LINEAR;
        lima_external_va = zc_va;
        struct pipe_resource *r = scr->resource_create(scr, &tmpl);
        lima_external_va = 0u;
        if (!r)
            return NULL;
        struct lima_resource *lr = lima_resource(r);
        if (!lr || !lr->bo) {
            pipe_resource_reference(&r, NULL);
            return NULL;
        }
        lr->bo->cacheable = false;
        lr->tiled = false;
        lr->levels[0].offset = 0u;
        g_zc_blit_res = r;
        g_zc_blit_w = w;
        g_zc_blit_h = h;
    }
    struct lima_resource *lr = lima_resource(g_zc_blit_res);
    lr->bo->va = zc_va;
    lr->levels[0].stride = bpr;
    lr->reload = 0;
    return g_zc_blit_res;
}

static uint32_t g_bb_bindfbs, g_bb_clears;
static uint32_t g_sc_log = 0, g_fl_log = 0, g_fl_checks = 0;

static int flush_current_fbo_iosurf(char tag)
{
    struct mali_gles_ctx *c = tls_cur_ctx;
    if (!c || !c->st)
        return 0;

    uint32_t draws = c->draw_cnt;
    c->draw_cnt = 0;
    struct dirty_snap snaps[16];
    int nsnaps = 0;

    pthread_mutex_lock(&gles_mtx);
    for (int i = 0; i < g_iosurf_hi && nsnaps < 16; i++) {
        struct iosurf_binding *b = &g_iosurfs[i];
        if (!b->used || !b->dirty || !b->surf || !b->w || !b->h)
            continue;
        uint32_t rx = b->dirty_x0, ry = b->dirty_y0;
        uint32_t rw = (b->dirty_x1 > rx) ? (b->dirty_x1 - rx) : 0;
        uint32_t rh = (b->dirty_y1 > ry) ? (b->dirty_y1 - ry) : 0;
        b->dirty = 0;
        if (rx >= b->w || ry >= b->h || !rw || !rh)
            continue;
        if (rx + rw > b->w) rw = b->w - rx;
        if (ry + rh > b->h) rh = b->h - ry;
        snaps[nsnaps].surf = b->surf;
        snaps[nsnaps].pt = NULL;
        pipe_resource_reference(&snaps[nsnaps].pt, b->pt);
        snaps[nsnaps].is_rb = b->is_rb;
        snaps[nsnaps].w = b->w;
        snaps[nsnaps].h = b->h;
        snaps[nsnaps].invert = b->invert;
        snaps[nsnaps].fmt = b->fmt;
        snaps[nsnaps].rx = rx;
        snaps[nsnaps].ry = ry;
        snaps[nsnaps].rw = rw;
        snaps[nsnaps].rh = rh;
        snaps[nsnaps].prev_fpa = b->bound_fpa;
        snaps[nsnaps].zc_va = b->zc_va;
        snaps[nsnaps].zc_bpr = b->zc_bpr;
        snaps[nsnaps].slot_idx = i;
        CFRetain(b->surf);
        nsnaps++;
    }
    if (nsnaps)
        g_iosurf_gen++;
    pthread_mutex_unlock(&gles_mtx);
    if (!nsnaps)
        return 0;

    int finished = 0;
    int is_bb = is_backboardd_proc();

    for (int k = 0; k < nsnaps; k++) {
        struct dirty_snap *s = &snaps[k];
        uint32_t t0 = (g_sc_log < 24u || (g_fl_log < 16u && g_fl_checks < 64u)) ? now_ms() : 0u;

        /* Fast Path A: backboardd 720x1280 display swapchain -> zero-copy direct FIMD0 scanout. */
        if (is_bb && s->w == 720u && s->h == 1280u && s->pt && c->st->pipe && mali_iokit_conn) {
            struct lima_resource *src_lres = lima_resource(s->pt);
            uint32_t direct_pa = 0;
            if (!s->invert && src_lres && !src_lres->tiled && src_lres->bo &&
                src_lres->levels[0].stride == 2880u &&
                (s->pt->format == PIPE_FORMAT_B8G8R8A8_UNORM || s->pt->format == PIPE_FORMAT_B8G8R8X8_UNORM)) {
                uint32_t pa = (uint32_t)src_lres->bo->offset + src_lres->levels[0].offset;
                if (pa >= 0x62000000u && pa < 0x7e000000u && (pa & 0xfffu) == 0u)
                    direct_pa = pa;
            }
            if (direct_pa) {
                /* Zero-blit page flip: mid-frame PP jobs run async overlapped with CPU;
                 * sync final scanout PP here before IOMobileFramebufferSwapSubmit flips FIMD0.
                 * Note: keep src_lres->reload intact because CARenderServer uses partial dirty-rect
                 * updates (glScissor) on triple-buffered scanout surfaces. */
                if (!finished) {
                    _mesa_Flush();
                    finished = 1;
                }
                if (tag != 'F')
                    mali_wait_idle();
                uint64_t spa = 0;
                if (s->prev_fpa != direct_pa) {
                    if (IOSurfaceLock(s->surf, 1, NULL) == 0) {
                        uintptr_t base = (uintptr_t)IOSurfaceGetBaseAddress(s->surf);
                        if (base) {
                            uint64_t in[2] = { (uint64_t)base, (uint64_t)direct_pa };
                            uint32_t cnt = 1;
                            if (IOConnectCallScalarMethod(mali_iokit_conn, 7u, in, 2, &spa, &cnt) == 0) {
                                pthread_mutex_lock(&gles_mtx);
                                if (g_iosurfs[s->slot_idx].used && g_iosurfs[s->slot_idx].surf == s->surf)
                                    g_iosurfs[s->slot_idx].bound_fpa = direct_pa;
                                pthread_mutex_unlock(&gles_mtx);
                            }
                        }
                        IOSurfaceUnlock(s->surf, 1, NULL);
                    }
                }
                if (g_sc_log < 24u) {
                    uint32_t t1 = now_ms();
                    g_sc_log++;
                    kperf_log("[GL] flip#%u %c fb=%u d=%u clr=%u bfb=%u dt=%ums fpa=0x%x\n",
                              g_sc_log, tag, c->bound_fbo, draws, g_bb_clears,
                              g_bb_bindfbs, t1 - t0, direct_pa);
                } else {
                    static int bench_fps = -1;
                    static uint32_t win_n, win_t0, win_d;
                    if (bench_fps < 0) {
                        extern int g_mali_bench_en;
                        char ba[256];
                        size_t bl = sizeof(ba) - 1;
                        int (*sbn)(const char *, void *, size_t *, void *, size_t) =
                            (int (*)(const char *, void *, size_t *, void *, size_t))dlsym(RTLD_DEFAULT, "sysctlbyname");
                        bench_fps = (sbn && sbn("kern.bootargs", ba, &bl, NULL, 0) == 0 &&
                                     (ba[bl] = '\0', strstr(ba, "bench=") != NULL)) ? 1 : 0;
                        g_mali_bench_en = bench_fps;
                    }
                    if (bench_fps) {
                        extern uint32_t lima_stat_jobs, lima_stat_reloads, lima_stat_reload_skips;
                        extern uint32_t g_mali_sub_us;
                        static uint32_t win_j0, win_r0, win_s0, win_sub0, win_act;
                        uint32_t t1 = now_ms();
                        if (win_n == 0u) {
                            win_t0 = t1;
                            win_j0 = lima_stat_jobs;
                            win_r0 = lima_stat_reloads;
                            win_s0 = lima_stat_reload_skips;
                            win_sub0 = g_mali_sub_us;
                            win_act = 0u;
                        }
                        if (draws && c->first_draw_ms && t1 >= c->first_draw_ms)
                            win_act += (t1 - c->first_draw_ms);
                        c->first_draw_ms = 0u;
                        win_d += draws;
                        (void)t0;
                        if (++win_n == 30u) {
                            uint32_t dt = t1 - win_t0;
                            uint32_t fps10 = dt ? (300000u / dt) : 0u;
                            uint32_t sub_ms = (g_mali_sub_us - win_sub0) / 1000u;
                            kperf_log("[FPS] 30f=%ums (%u.%u) act=%u sub=%u d=%u j=%u rl=%u/%u\n",
                                      dt, fps10 / 10u, fps10 % 10u, win_act, sub_ms, win_d,
                                      lima_stat_jobs - win_j0,
                                      lima_stat_reloads - win_r0,
                                      lima_stat_reload_skips - win_s0);
                            win_n = 0u; win_d = 0u; win_act = 0u;
                        }
                    }
                }
                pipe_resource_reference(&s->pt, NULL);
                CFRelease(s->surf);
                continue;
            }
            uint32_t fb_idx = g_scanout_idx;
            struct pipe_resource *dst_res = ensure_scanout_res(c->st->pipe->screen, fb_idx);
            if (dst_res) {
                if (!finished) {
                    _mesa_Flush();
                    finished = 1;
                }
                g_scanout_idx ^= 1u;
                struct pipe_blit_info bi;
                memset(&bi, 0, sizeof(bi));
                bi.src.resource = s->pt;
                bi.src.format = s->pt->format;
                bi.src.box.x = 0;
                bi.src.box.y = s->invert ? (int)s->h : 0;
                bi.src.box.width = (int)s->w;
                bi.src.box.height = s->invert ? -(int)s->h : (int)s->h;
                bi.src.box.depth = 1;
                bi.dst.resource = dst_res;
                bi.dst.format = PIPE_FORMAT_B8G8R8A8_UNORM;
                bi.dst.box.x = 0;
                bi.dst.box.y = 0;
                bi.dst.box.width = (int)s->w;
                bi.dst.box.height = (int)s->h;
                bi.dst.box.depth = 1;
                bi.mask = PIPE_MASK_RGBA;
                bi.filter = PIPE_TEX_FILTER_NEAREST;
                c->st->pipe->blit(c->st->pipe, &bi);
                _mesa_Flush();
                mali_wait_idle();
                finished = 1;

                uint64_t spa = 0;
                if (IOSurfaceLock(s->surf, 1, NULL) == 0) {
                    uintptr_t base = (uintptr_t)IOSurfaceGetBaseAddress(s->surf);
                    if (base) {
                        uint64_t in[2] = { (uint64_t)base, (uint64_t)fb_idx };
                        uint32_t cnt = 1;
                        IOConnectCallScalarMethod(mali_iokit_conn, 7u, in, 2, &spa, &cnt);
                    }
                    IOSurfaceUnlock(s->surf, 1, NULL);
                }
                uint32_t t1 = now_ms();
                static uint32_t sc_log = 0;
                if (sc_log < 24u) {
                    sc_log++;
                    kperf_log("[GL] scan#%u dt=%ums fb=%u spa=0x%x inv=%u\n",
                              sc_log, t1 - t0, fb_idx, (unsigned)spa, s->invert);
                }
                pipe_resource_reference(&s->pt, NULL);
                CFRelease(s->surf);
                continue;
            }
        }

        /* Zero-copy app/SpringBoard IOSurface renderbuffer via Mali MMU (0x80000000+slot*4M):
         * - invert == 0: Mali PP wrote directly into IOSurface physical RAM; pipeline CPU/GPU on P/F.
         * - invert == 1: single Mali HW Y-flip blit from tiled RB directly into IOSurface RAM (0 CPU readback). */
        if (s->zc_va) {
            if (!finished) {
                _mesa_Flush();
                finished = 1;
            }
            if (s->invert && s->pt && c->st->pipe && s->zc_bpr >= s->w * 4u) {
                struct pipe_resource *dst_res =
                    ensure_zc_blit_res(c->st->pipe->screen, s->w, s->h, s->zc_va, s->zc_bpr);
                if (dst_res) {
                    struct pipe_blit_info bi;
                    memset(&bi, 0, sizeof(bi));
                    bi.src.resource = s->pt;
                    bi.src.format = s->pt->format;
                    bi.src.box.x = 0;
                    bi.src.box.y = (int)s->h;
                    bi.src.box.width = (int)s->w;
                    bi.src.box.height = -(int)s->h;
                    bi.src.box.depth = 1;
                    bi.dst.resource = dst_res;
                    bi.dst.format = PIPE_FORMAT_B8G8R8A8_UNORM;
                    bi.dst.box.x = 0;
                    bi.dst.box.y = 0;
                    bi.dst.box.width = (int)s->w;
                    bi.dst.box.height = (int)s->h;
                    bi.dst.box.depth = 1;
                    bi.mask = PIPE_MASK_RGBA;
                    bi.filter = PIPE_TEX_FILTER_NEAREST;
                    c->st->pipe->blit(c->st->pipe, &bi);
                    _mesa_Flush();
                }
            }
            if (tag != 'P' && tag != 'F')
                mali_wait_idle();
            static uint32_t g_zc_log = 0;
            if (g_zc_log < 16u) {
                g_zc_log++;
                kperf_log("[GL] zc#%u pid=%d %ux%u inv=%u va=0x%x d=%u\n",
                          g_zc_log, (int)getpid(), s->w, s->h, s->invert, s->zc_va, draws);
            }
            {
                static int app_bench = -1;
                static uint32_t aw_t0 = 0, aw_n = 0, aw_d = 0, aw_j0 = 0;
                if (app_bench < 0) {
                    char ba[256]; size_t bl = sizeof(ba) - 1;
                    int (*sbn)(const char *, void *, size_t *, void *, size_t) =
                        (int (*)(const char *, void *, size_t *, void *, size_t))dlsym(RTLD_DEFAULT, "sysctlbyname");
                    app_bench = (sbn && sbn("kern.bootargs", ba, &bl, NULL, 0) == 0 &&
                                 (ba[bl] = '\0', strstr(ba, "bench=") != NULL)) ? 1 : 0;
                }
                if (app_bench) {
                    extern uint32_t lima_stat_jobs;
                    uint32_t t1 = now_ms();
                    if (aw_n == 0u) { aw_t0 = t1; aw_j0 = lima_stat_jobs; }
                    aw_d += draws;
                    if (++aw_n == 30u) {
                        uint32_t dt = t1 - aw_t0;
                        uint32_t fps10 = dt ? (300000u / dt) : 0u;
                        kperf_log("[APP_FPS] pid=%d 30f=%ums (%u.%u) d=%u j=%u\n",
                                  (int)getpid(), dt, fps10 / 10u, fps10 % 10u, aw_d, lima_stat_jobs - aw_j0);
                        aw_n = 0u; aw_d = 0u;
                    }
                }
            }
            pipe_resource_reference(&s->pt, NULL);
            CFRelease(s->surf);
            continue;
        }

        /* Fast Path B: offscreen / app IOSurfaces -> direct linear BO map copy (no _mesa_ReadPixels). */
        if (!finished) {
            _mesa_Flush();
            finished = 1;
        }
        mali_wait_idle();
        int copied_direct = 0;
        if (s->pt) {
            struct lima_resource *lres = lima_resource(s->pt);
            if (lres && !lres->tiled && lres->bo && !(lres->bo->handle & 0x80000000u)) {
                uint32_t src_stride = lres->levels[0].stride;
                uint32_t src_off = lres->levels[0].offset;
                uint8_t *src_base = ((size_t)src_off + (size_t)s->h * src_stride <= lres->bo->size)
                    ? (uint8_t *)lima_bo_map(lres->bo)
                    : NULL;
                int is_bgra = (s->pt->format == PIPE_FORMAT_B8G8R8A8_UNORM ||
                               s->pt->format == PIPE_FORMAT_B8G8R8X8_UNORM);
                int is_1bpp = (s->pt->format == PIPE_FORMAT_A8_UNORM ||
                               s->pt->format == PIPE_FORMAT_L8_UNORM ||
                               s->pt->format == PIPE_FORMAT_R8_UNORM ||
                               s->pt->format == PIPE_FORMAT_I8_UNORM);
                if (src_base && IOSurfaceLock(s->surf, 0, NULL) == 0) {
                    uint8_t *dst_base = (uint8_t *)IOSurfaceGetBaseAddress(s->surf);
                    size_t dst_bpr = IOSurfaceGetBytesPerRow(s->surf);
                    if (!is_1bpp && src_stride >= s->w * 4u && dst_base && dst_bpr >= (size_t)s->w * 4u) {
                        for (uint32_t j = 0; j < s->rh; j++) {
                            uint32_t sy = s->ry + j;
                            uint32_t dst_y = s->invert ? (s->h - 1u - sy) : sy;
                            const uint32_t *src = (const uint32_t *)(src_base + src_off + sy * src_stride) + s->rx;
                            uint32_t *dst = (uint32_t *)(dst_base + dst_y * dst_bpr) + s->rx;
                            if (is_bgra)
                                memcpy_neon(dst, src, (size_t)s->rw * 4u);
                            else
                                rgba_to_bgra_row(dst, src, s->rw);
                        }
                        copied_direct = 1;
                    } else if (is_1bpp && src_stride >= s->w && dst_base && dst_bpr >= s->w) {
                        for (uint32_t j = 0; j < s->rh; j++) {
                            uint32_t sy = s->ry + j;
                            uint32_t dst_y = s->invert ? (s->h - 1u - sy) : sy;
                            const uint8_t *src = src_base + src_off + sy * src_stride + s->rx;
                            uint8_t *dst = dst_base + dst_y * dst_bpr + s->rx;
                            memcpy_neon(dst, src, s->rw);
                        }
                        copied_direct = 1;
                    } else if (!is_1bpp && src_stride >= s->w * 4u && dst_base && dst_bpr >= s->w) {
                        uint32_t ch = (s->fmt == GL_ALPHA) ? 3u : 0u;
                        for (uint32_t j = 0; j < s->rh; j++) {
                            uint32_t sy = s->ry + j;
                            uint32_t dst_y = s->invert ? (s->h - 1u - sy) : sy;
                            const uint8_t *src = src_base + src_off + sy * src_stride + s->rx * 4u;
                            uint8_t *dst = dst_base + dst_y * dst_bpr + s->rx;
                            for (uint32_t x = 0; x < s->rw; x++)
                                dst[x] = src[x * 4u + ch];
                        }
                        copied_direct = 1;
                    }
                    IOSurfaceUnlock(s->surf, 0, NULL);
                }
            }
        }
        if (!copied_direct) {
            size_t sub_row_bytes = (size_t)s->rw * 4u;
            pthread_mutex_lock(&gles_mtx);
            uint8_t *tmp = ensure_scratch(sub_row_bytes * s->rh);
            if (tmp) {
                GLint prev_pack = 4;
                _mesa_GetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);
                _mesa_PixelStorei(GL_PACK_ALIGNMENT, 1);
                _mesa_ReadPixels((GLint)s->rx, (GLint)s->ry, (GLsizei)s->rw, (GLsizei)s->rh, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
                _mesa_PixelStorei(GL_PACK_ALIGNMENT, prev_pack);
                if (IOSurfaceLock(s->surf, 0, NULL) == 0) {
                    uint8_t *dst_base = (uint8_t *)IOSurfaceGetBaseAddress(s->surf);
                    size_t dst_bpr = IOSurfaceGetBytesPerRow(s->surf);
                    if (dst_base && dst_bpr >= (size_t)s->w * 4u) {
                        for (uint32_t j = 0; j < s->rh; j++) {
                            uint32_t dst_y = s->invert ? (s->h - 1u - (s->ry + j)) : (s->ry + j);
                            const uint32_t *src = (const uint32_t *)(tmp + j * sub_row_bytes);
                            uint32_t *dst = (uint32_t *)(dst_base + dst_y * dst_bpr) + s->rx;
                            rgba_to_bgra_row(dst, src, s->rw);
                        }
                    } else if (dst_base && dst_bpr >= s->w) {
                        uint32_t ch = (s->fmt == GL_ALPHA) ? 3u : 0u;
                        for (uint32_t j = 0; j < s->rh; j++) {
                            uint32_t dst_y = s->invert ? (s->h - 1u - (s->ry + j)) : (s->ry + j);
                            const uint8_t *src = tmp + j * sub_row_bytes;
                            uint8_t *dst = dst_base + dst_y * dst_bpr + s->rx;
                            for (uint32_t x = 0; x < s->rw; x++)
                                dst[x] = src[x * 4u + ch];
                        }
                    }
                    IOSurfaceUnlock(s->surf, 0, NULL);
                }
            }
            pthread_mutex_unlock(&gles_mtx);
        }
        if (g_fl_log < 16u && g_fl_checks < 64u) {
            g_fl_checks++;
            uint32_t t2 = now_ms();
            if ((t2 - t0) >= 15u) {
                uint32_t mid_px = 0;
                if (s->pt) {
                    struct lima_resource *lr = lima_resource(s->pt);
                    if (lr && !lr->tiled && lr->bo) {
                        uint8_t *sb = (uint8_t *)lima_bo_map(lr->bo);
                        if (sb && lr->levels[0].stride >= s->w * 4u)
                            mid_px = *(const uint32_t *)(sb + lr->levels[0].offset + (s->h / 2u) * lr->levels[0].stride + (s->w / 2u) * 4u);
                    }
                }
                g_fl_log++;
                kperf_log("[GL] fl#%u pid=%d %ux%u inv=%u dir=%d px=0x%08x\n",
                          g_fl_log, (int)getpid(), s->w, s->h, s->invert, copied_direct, mid_px);
            }
        }
        if (tag == 'P' && !is_bb) {
            static int fl_bench = -1;
            static uint32_t fw_t0 = 0, fw_n = 0, fw_d = 0, fw_j0 = 0;
            if (fl_bench < 0) {
                char ba[256]; size_t bl = sizeof(ba) - 1;
                int (*sbn)(const char *, void *, size_t *, void *, size_t) =
                    (int (*)(const char *, void *, size_t *, void *, size_t))dlsym(RTLD_DEFAULT, "sysctlbyname");
                fl_bench = (sbn && sbn("kern.bootargs", ba, &bl, NULL, 0) == 0 &&
                            (ba[bl] = '\0', strstr(ba, "bench=") != NULL)) ? 1 : 0;
            }
            if (fl_bench) {
                extern uint32_t lima_stat_jobs;
                uint32_t t1 = now_ms();
                if (fw_n == 0u) { fw_t0 = t1; fw_j0 = lima_stat_jobs; }
                fw_d += draws;
                if (++fw_n == 30u) {
                    uint32_t dt = t1 - fw_t0;
                    uint32_t fps10 = dt ? (300000u / dt) : 0u;
                    kperf_log("[APP_FPS] pid=%d 30f=%ums (%u.%u) d=%u j=%u path=B\n",
                              (int)getpid(), dt, fps10 / 10u, fps10 % 10u, fw_d, lima_stat_jobs - fw_j0);
                    fw_n = 0u; fw_d = 0u;
                }
            }
        }
        pipe_resource_reference(&s->pt, NULL);
        CFRelease(s->surf);
    }
    return finished;
}

static void mark_current_fbo_dirty(void)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (!c || !c->bound_fbo)
        return;
    if (c->draw_cnt == 0u) {
        extern int g_mali_bench_en;
        if (__builtin_expect(g_mali_bench_en, 0))
            c->first_draw_ms = now_ms();
    }
    c->draw_cnt++;
    if (__builtin_expect(c->bound_fbo < 64u && !(g_iosurf_fbo_mask & (1ull << c->bound_fbo)), 1))
        return;
    struct pipe_resource *cur_pt = NULL;
    if (c->st && c->st->ctx && c->st->ctx->DrawBuffer) {
        struct gl_renderbuffer *rb = c->st->ctx->DrawBuffer->Attachment[BUFFER_COLOR0].Renderbuffer;
        if (rb)
            cur_pt = rb->texture;
    }
    if (c->dirty_fbo_id == c->bound_fbo && c->dirty_fbo_gen == g_iosurf_gen && c->dirty_fbo_pt == cur_pt)
        return;
    pthread_mutex_lock(&gles_mtx);
    struct iosurf_binding *b = find_fbo_iosurf_locked(c->bound_fbo);
    if (b && b->w && b->h) {
        b->seed = 0;
        if (cur_pt && cur_pt != b->pt)
            pipe_resource_reference(&b->pt, cur_pt);
        uint32_t x0 = 0, y0 = 0, x1 = b->w, y1 = b->h;
        if (c->scissor_en && c->sw >= 0 && c->sh >= 0) {
            int32_t sx0 = c->sx, sy0 = c->sy;
            int32_t sx1 = c->sx + c->sw, sy1 = c->sy + c->sh;
            if (sx0 < 0) sx0 = 0;
            if (sy0 < 0) sy0 = 0;
            if (sx1 > (int32_t)b->w) sx1 = (int32_t)b->w;
            if (sy1 > (int32_t)b->h) sy1 = (int32_t)b->h;
            if (sx0 >= sx1 || sy0 >= sy1) {
                pthread_mutex_unlock(&gles_mtx);
                return;
            }
            x0 = (uint32_t)sx0; y0 = (uint32_t)sy0;
            x1 = (uint32_t)sx1; y1 = (uint32_t)sy1;
        }
        if (!b->dirty) {
            b->dirty = 1;
            b->dirty_x0 = x0; b->dirty_y0 = y0;
            b->dirty_x1 = x1; b->dirty_y1 = y1;
        } else {
            if (x0 < b->dirty_x0) b->dirty_x0 = x0;
            if (y0 < b->dirty_y0) b->dirty_y0 = y0;
            if (x1 > b->dirty_x1) b->dirty_x1 = x1;
            if (y1 > b->dirty_y1) b->dirty_y1 = y1;
        }
        if ((is_backboardd_proc() && b->w == 720u && b->h == 1280u) ||
            (b->dirty_x0 == 0 && b->dirty_y0 == 0 && b->dirty_x1 == b->w && b->dirty_y1 == b->h)) {
            c->dirty_fbo_id = c->bound_fbo;
            c->dirty_fbo_gen = g_iosurf_gen;
            c->dirty_fbo_pt = cur_pt;
        }
    } else if (!b) {
        c->dirty_fbo_id = c->bound_fbo;
        c->dirty_fbo_gen = g_iosurf_gen;
        c->dirty_fbo_pt = cur_pt;
    }
    pthread_mutex_unlock(&gles_mtx);
}

void wrap_Enable(GLenum cap)
{
    if (cap == GL_SCISSOR_TEST) {
        struct mali_gles_ctx *c = fast_cur_ctx();
        if (c) {
            if (c->scissor_en_valid && c->scissor_en == 1)
                return;
            c->scissor_en = 1;
            c->scissor_en_valid = 1;
        }
    }
    _mesa_Enable(cap);
}

void wrap_Disable(GLenum cap)
{
    if (cap == GL_SCISSOR_TEST) {
        struct mali_gles_ctx *c = fast_cur_ctx();
        if (c) {
            if (c->scissor_en_valid && c->scissor_en == 0)
                return;
            c->scissor_en = 0;
            c->scissor_en_valid = 1;
        }
    }
    _mesa_Disable(cap);
}

void wrap_Scissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c) {
        if (c->scissor_valid && c->sx == x && c->sy == y && c->sw == w && c->sh == h)
            return;
        c->sx = x;
        c->sy = y;
        c->sw = w;
        c->sh = h;
        c->scissor_valid = (w >= 0 && h >= 0) ? 1 : 0;
    }
    if (is_backboardd_proc()) _mesa_Scissor_no_error(x, y, w, h);
    else _mesa_Scissor(x, y, w, h);
}

/* --- Custom GLES2 wrappers ------------------------------------------------- */

void wrap_BindFramebuffer(GLenum target, GLuint fb)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && target != 0x8CA8u /* GL_READ_FRAMEBUFFER */) {
        c->bound_fbo = fb;
        g_bb_bindfbs++;
    }
    _mesa_BindFramebuffer(target, fb);
}

void wrap_BindRenderbuffer(GLenum target, GLuint rb)
{
    if (target == GL_RENDERBUFFER) {
        struct mali_gles_ctx *c = fast_cur_ctx();
        if (c) c->bound_rb = rb;
    }
    _mesa_BindRenderbuffer(target, rb);
}

void wrap_BindTexture(GLenum target, GLuint tex)
{
    if (target == GL_TEXTURE_2D) {
        struct mali_gles_ctx *c = fast_cur_ctx();
        if (c) c->bound_tex = tex;
    }
    if (is_backboardd_proc()) _mesa_BindTexture_no_error(target, tex);
    else _mesa_BindTexture(target, tex);
}

static void set_fbo_color0(GLuint fbo, int is_rb, GLuint obj_id)
{
    if (!fbo) return;
    if (__builtin_expect(!is_rb && is_backboardd_proc() && fbo < 64u && !(g_iosurf_fbo_mask & (1ull << fbo)) &&
        obj_id && !(g_iosurf_obj_bloom[(obj_id >> 6) & 3u] & (1ull << (obj_id & 63u))), 1))
        return;
    pthread_mutex_lock(&gles_mtx);
    int slot_idx = -1;
    for (int i = 0; i < g_fbo_hi; i++) {
        if (g_fbos[i].used && g_fbos[i].fbo_id == fbo) { slot_idx = i; break; }
        if (!g_fbos[i].used && slot_idx < 0) slot_idx = i;
    }
    if (slot_idx < 0 && g_fbo_hi < MAX_FBOS)
        slot_idx = g_fbo_hi;
    if (slot_idx >= 0) {
        struct fbo_attach *slot = &g_fbos[slot_idx];
        if (obj_id) {
            slot->used = 1;
            slot->fbo_id = fbo;
            slot->is_rb = is_rb;
            slot->obj_id = obj_id;
            if (slot_idx >= g_fbo_hi) g_fbo_hi = slot_idx + 1;
        } else {
            slot->used = 0;
        }
        rebuild_iosurf_fbo_mask_locked();
        g_iosurf_gen++;
    }
    pthread_mutex_unlock(&gles_mtx);
}

void wrap_FramebufferRenderbuffer(GLenum target, GLenum att, GLenum rbtarget, GLuint rb)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && att == GL_COLOR_ATTACHMENT0)
        set_fbo_color0(c->bound_fbo, 1, rb);
    if (is_backboardd_proc()) _mesa_FramebufferRenderbuffer_no_error(target, att, rbtarget, rb);
    else _mesa_FramebufferRenderbuffer(target, att, rbtarget, rb);
}

void wrap_FramebufferTexture2D(GLenum target, GLenum att, GLenum textarget, GLuint tex, GLint level)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && att == GL_COLOR_ATTACHMENT0 && level == 0)
        set_fbo_color0(c->bound_fbo, 0, tex);
    if (is_backboardd_proc()) _mesa_FramebufferTexture2D_no_error(target, att, textarget, tex, level);
    else _mesa_FramebufferTexture2D(target, att, textarget, tex, level);

    /* Promote non-backboardd GL_TEXTURE_2D IOSurface FBO color targets (e.g. SpringBoard 720x1280/744x1392)
     * to zero-copy zc_va so Mali PP renders directly into IOSurface RAM instead of WC CPU readback. */
    if (!is_backboardd_proc() && c && c->st && c->st->ctx && att == GL_COLOR_ATTACHMENT0 &&
        level == 0 && tex != 0 && mali_iokit_conn) {
        void *surf = NULL;
        uint32_t w = 0, h = 0, invert = 0;
        pthread_mutex_lock(&gles_mtx);
        struct iosurf_binding *b = find_iosurf_locked(0, tex);
        if (b && b->surf && !b->zc_va && b->fmt == 0x80e1u && b->type == GL_UNSIGNED_BYTE &&
            (uint64_t)b->w * (uint64_t)b->h >= 65536ull) {
            surf = b->surf;
            w = b->w;
            h = b->h;
            invert = b->invert;
            CFRetain(surf);
        }
        pthread_mutex_unlock(&gles_mtx);
        if (surf) {
            uint32_t zc_bpr = 0u;
            uint32_t zc_va = map_iosurf_zc_va(surf, w, h, &zc_bpr);
            if (zc_va) {
                struct pipe_resource *pt = NULL;
                if (c->st->ctx->DrawBuffer) {
                    struct gl_renderbuffer *rb = c->st->ctx->DrawBuffer->Attachment[BUFFER_COLOR0].Renderbuffer;
                    if (rb) pt = rb->texture;
                }
                struct lima_resource *lres = pt ? lima_resource(pt) : NULL;
                if (!invert && lres && !lres->tiled && lres->bo) {
                    if (!(lres->bo->handle & 0x80000000u)) {
                        lima_flush_job_accessing_bo((struct lima_context *)c->st->pipe, lres->bo, true);
                        struct lima_bo *ext = lima_bo_create_external((struct lima_screen *)pt->screen,
                                                                      zc_va, lres->bo->size);
                        if (ext) {
                            lima_bo_unreference(lres->bo);
                            lres->bo = ext;
                            lres->levels[0].stride = zc_bpr;
                            lres->levels[0].offset = 0u;
                            lres->bo_gen = lima_next_bo_gen();
                        } else {
                            zc_va = 0u;
                        }
                    } else {
                        lres->bo->va = zc_va;
                        lres->levels[0].stride = zc_bpr;
                        lres->levels[0].offset = 0u;
                        lres->bo_gen = lima_next_bo_gen();
                    }
                }
                if (zc_va) {
                    pthread_mutex_lock(&gles_mtx);
                    struct iosurf_binding *b2 = find_iosurf_locked(0, tex);
                    if (b2 && b2->surf == surf) {
                        b2->zc_va = zc_va;
                        b2->zc_bpr = zc_bpr;
                        if (pt && pt != b2->pt)
                            pipe_resource_reference(&b2->pt, pt);
                    }
                    pthread_mutex_unlock(&gles_mtx);
                }
            }
            CFRelease(surf);
        }
    }
}

void wrap_DeleteFramebuffers(GLsizei n, const GLuint *fbs)
{
    if (fbs && n > 0) {
        pthread_mutex_lock(&gles_mtx);
        for (GLsizei j = 0; j < n; j++) {
            for (int i = 0; i < g_fbo_hi; i++)
                if (g_fbos[i].used && g_fbos[i].fbo_id == fbs[j])
                    g_fbos[i].used = 0;
        }
        rebuild_iosurf_fbo_mask_locked();
        g_iosurf_gen++;
        pthread_mutex_unlock(&gles_mtx);
    }
    _mesa_DeleteFramebuffers(n, fbs);
}

static void delete_iosurf_objs(int is_rb, GLsizei n, const GLuint *ids)
{
    if (!ids || n <= 0) return;
    pthread_mutex_lock(&gles_mtx);
    int changed = 0;
    for (GLsizei j = 0; j < n; j++) {
        struct iosurf_binding *b = find_iosurf_locked(is_rb, ids[j]);
        if (b) {
            if (b->pt) pipe_resource_reference(&b->pt, NULL);
            if (b->surf) CFRelease(b->surf);
            memset(b, 0, sizeof(*b));
            g_iosurf_gen++;
            changed = 1;
        }
    }
    if (changed) rebuild_iosurf_fbo_mask_locked();
    pthread_mutex_unlock(&gles_mtx);
}

void wrap_DeleteRenderbuffers(GLsizei n, const GLuint *rbs)
{
    delete_iosurf_objs(1, n, rbs);
    _mesa_DeleteRenderbuffers(n, rbs);
}

void wrap_DeleteTextures(GLsizei n, const GLuint *texs)
{
    delete_iosurf_objs(0, n, texs);
    _mesa_DeleteTextures(n, texs);
}

void wrap_Clear(GLbitfield mask)
{
    g_bb_clears++;
    if (is_backboardd_proc()) _mesa_Clear_no_error(mask);
    else _mesa_Clear(mask);
    mark_current_fbo_dirty();
}

void wrap_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    _mesa_DrawArrays(mode, first, count);
    mark_current_fbo_dirty();
}

void wrap_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    _mesa_DrawElements(mode, count, type, indices);
    mark_current_fbo_dirty();
}

void wrap_Flush(void)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    /* Mid-frame glFlush() in backboardd (bound_fbo != 0) splits the 720x1280 scanout batch
     * into 15+ full-screen reload/writeback passes; Mesa Lima flushes FBO->texture and
     * self-feedback dependencies on demand, and frame-end glFlush() runs with bound_fbo == 0. */
    if (is_backboardd_proc() && c && c->bound_fbo != 0)
        return;
    if (!flush_current_fbo_iosurf('F'))
        _mesa_Flush();
}

void wrap_Finish(void)
{
    if (!flush_current_fbo_iosurf('N'))
        _mesa_Finish();
    mali_wait_idle();
}

static void fix_ios_tex_fmt(GLint *ifmt, GLenum *fmt, GLenum *type)
{
    /* iOS passes GL_UNSIGNED_INT_8_8_8_8_REV (0x8367) for little-endian BGRA/RGBA8888. */
    if (*type == 0x8367u)
        *type = GL_UNSIGNED_BYTE;
    if (ifmt) {
        if (*fmt == 0x80e1u /* GL_BGRA_EXT */ && (*ifmt == GL_RGBA || *ifmt == 0x8058 || *ifmt == 0x93a1))
            *ifmt = 0x80e1;
        else if (*ifmt == 0x8058 /* GL_RGBA8 */ || *ifmt == 0x93a1 /* GL_BGRA8_EXT */ ||
                 *ifmt == 0x8056 /* GL_RGBA4 */ || *ifmt == 0x8057 /* GL_RGB5_A1 */)
            *ifmt = GL_RGBA;
        else if (*ifmt == 0x8051 /* GL_RGB8 */ || *ifmt == 0x8d62 /* GL_RGB565 */)
            *ifmt = GL_RGB;
        else if (*ifmt == 0x8229 /* GL_R8 */)
            *ifmt = 0x1903; /* GL_RED_EXT */
        else if (*ifmt == 0x822b /* GL_RG8 */)
            *ifmt = 0x8227; /* GL_RG_EXT */
        else if (*ifmt == 0x803c /* GL_ALPHA8 */)
            *ifmt = GL_ALPHA;
        else if (*ifmt == 0x8040 /* GL_LUMINANCE8 */)
            *ifmt = GL_LUMINANCE;
        else if (*ifmt == 0x8045 /* GL_LUMINANCE8_ALPHA8 */)
            *ifmt = GL_LUMINANCE_ALPHA;
        else if (*ifmt == 0x8c43 /* GL_SRGB8_ALPHA8 */)
            *ifmt = (*fmt == 0x8c42u) ? 0x8c42 : GL_RGBA;
    }
}

static GLuint current_tex2d_id(void)
{
    if (!tls_cur_ctx)
        return 0;
    struct gl_context *glctx = tls_cur_ctx->st ? tls_cur_ctx->st->ctx : NULL;
    if (glctx) {
        struct gl_texture_object *t = glctx->Texture.Unit[glctx->Texture.CurrentUnit].CurrentTex[TEXTURE_2D_INDEX];
        if (t && t->Name)
            return t->Name;
    }
    return tls_cur_ctx->bound_tex;
}

static void invalidate_bound_tex_seed(void)
{
    GLuint tid = current_tex2d_id();
    if (!tid)
        return;
    pthread_mutex_lock(&gles_mtx);
    struct iosurf_binding *b = find_iosurf_locked(0, tid);
    if (b)
        b->seed = 0;
    pthread_mutex_unlock(&gles_mtx);
}

void wrap_TexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                     GLint border, GLenum fmt, GLenum type, const void *pixels)
{
    if (target == GL_TEXTURE_2D)
        invalidate_bound_tex_seed();
    fix_ios_tex_fmt(&ifmt, &fmt, &type);
    if (is_backboardd_proc()) _mesa_TexImage2D_no_error(target, level, ifmt, w, h, border, fmt, type, pixels);
    else _mesa_TexImage2D(target, level, ifmt, w, h, border, fmt, type, pixels);
}

extern int pvrtc_decode_rgba(const uint8_t *src, size_t src_sz, uint32_t w, uint32_t h,
                             int is_2bpp, int force_opaque, uint8_t *dst);
extern int pvrtc_decode_rgb565(const uint8_t *src, size_t src_sz, uint32_t w, uint32_t h,
                               int is_2bpp, uint16_t *dst);

void wrap_CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                               GLsizei width, GLsizei height, GLint border,
                               GLsizei imageSize, const void *data)
{
    if (internalformat >= 0x8c00u && internalformat <= 0x8c03u) {
        if (target == GL_TEXTURE_2D)
            invalidate_bound_tex_seed();
        int is_2bpp = (internalformat == 0x8c01u || internalformat == 0x8c03u);
        int opaque  = (internalformat == 0x8c00u || internalformat == 0x8c01u);
        if (width <= 0 || height <= 0 || !data || imageSize <= 0) {
            if (opaque)
                _mesa_TexImage2D(target, level, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
            else
                _mesa_TexImage2D(target, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            return;
        }
        if (opaque) {
            size_t need = (size_t)width * (size_t)height * 2u;
            uint16_t *rgb565 = (uint16_t *)malloc(need);
            if (rgb565) {
                if (pvrtc_decode_rgb565((const uint8_t *)data, (size_t)imageSize,
                                        (uint32_t)width, (uint32_t)height, is_2bpp, rgb565) == 0)
                    _mesa_TexImage2D(target, level, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, rgb565);
                else
                    _mesa_TexImage2D(target, level, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
                free(rgb565);
            }
            return;
        }
        size_t need = (size_t)width * (size_t)height * 4u;
        uint8_t *rgba = (uint8_t *)malloc(need);
        if (rgba) {
            if (pvrtc_decode_rgba((const uint8_t *)data, (size_t)imageSize,
                                  (uint32_t)width, (uint32_t)height, is_2bpp, 0, rgba) == 0) {
                _mesa_TexImage2D(target, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            } else {
                _mesa_TexImage2D(target, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            }
            free(rgba);
        }
        return;
    }
    ((void (*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *))_mesa_CompressedTexImage2D)(
        target, level, internalformat, width, height, border, imageSize, data);
}

void wrap_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                        GLenum fmt, GLenum type, const void *pixels)
{
    if (target == GL_TEXTURE_2D)
        invalidate_bound_tex_seed();
    fix_ios_tex_fmt(NULL, &fmt, &type);
    _mesa_TexSubImage2D(target, level, x, y, w, h, fmt, type, pixels);
}

void wrap_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *pixels)
{
    fix_ios_tex_fmt(NULL, &fmt, &type);
    mali_wait_idle();
    _mesa_ReadPixels(x, y, w, h, fmt, type, pixels);
}

void wrap_RenderbufferStorage(GLenum target, GLenum ifmt, GLsizei w, GLsizei h)
{
    if (ifmt == 0x93a1u /* GL_BGRA8_EXT */ || ifmt == 0x80e1u /* GL_BGRA_EXT */)
        ifmt = GL_RGBA8;
    _mesa_RenderbufferStorage(target, ifmt, w, h);
}

void wrap_RenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum ifmt, GLsizei w, GLsizei h)
{
    if (ifmt == 0x93a1u /* GL_BGRA8_EXT */ || ifmt == 0x80e1u /* GL_BGRA_EXT */)
        ifmt = GL_RGBA8;
    ((void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei))_mesa_RenderbufferStorageMultisample)(
        target, samples, ifmt, w, h);
}

void wrap_ResolveMultisampleFramebufferAPPLE(void)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (!c || !c->st || !c->st->ctx)
        return;
    struct gl_context *glctx = c->st->ctx;
    struct gl_framebuffer *rfb = glctx->ReadBuffer;
    struct gl_framebuffer *dfb = glctx->DrawBuffer;
    if (!rfb || !dfb || !rfb->Width || !rfb->Height || !dfb->Width || !dfb->Height)
        return;
    ((void (*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))_mesa_BlitFramebuffer)(
        0, 0, (GLint)rfb->Width, (GLint)rfb->Height,
        0, 0, (GLint)dfb->Width, (GLint)dfb->Height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    mark_current_fbo_dirty();
}

static char g_ext_buf[4096];

const GLubyte *wrap_GetString(GLenum name)
{
    const GLubyte *s = _mesa_GetString(name);
    if (name == GL_EXTENSIONS && s) {
        if (!g_ext_buf[0]) {
            size_t len = strlen((const char *)s);
            const char *extra = " GL_APPLE_texture_format_BGRA8888 GL_APPLE_framebuffer_multisample GL_IMG_texture_compression_pvrtc";
            if (len + strlen(extra) + 1 < sizeof(g_ext_buf)) {
                memcpy(g_ext_buf, s, len);
                strcpy(g_ext_buf + len, extra);
            }
        }
        if (g_ext_buf[0])
            return (const GLubyte *)g_ext_buf;
    }
    return s;
}

void wrap_VertexAttrib1f(GLuint idx, GLfloat x)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && c->st) CALL_VertexAttrib1fARB(c->st->ctx->Dispatch.Current, (idx, x));
}
void wrap_VertexAttrib2f(GLuint idx, GLfloat x, GLfloat y)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && c->st) CALL_VertexAttrib2fARB(c->st->ctx->Dispatch.Current, (idx, x, y));
}
void wrap_VertexAttrib3f(GLuint idx, GLfloat x, GLfloat y, GLfloat z)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && c->st) CALL_VertexAttrib3fARB(c->st->ctx->Dispatch.Current, (idx, x, y, z));
}
void wrap_VertexAttrib4f(GLuint idx, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && c->st) CALL_VertexAttrib4fARB(c->st->ctx->Dispatch.Current, (idx, x, y, z, w));
}
void wrap_VertexAttrib1fv(GLuint idx, const GLfloat *v)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && c->st) CALL_VertexAttrib1fvARB(c->st->ctx->Dispatch.Current, (idx, v));
}
void wrap_VertexAttrib2fv(GLuint idx, const GLfloat *v)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && c->st) CALL_VertexAttrib2fvARB(c->st->ctx->Dispatch.Current, (idx, v));
}
void wrap_VertexAttrib3fv(GLuint idx, const GLfloat *v)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && c->st) CALL_VertexAttrib3fvARB(c->st->ctx->Dispatch.Current, (idx, v));
}
void wrap_VertexAttrib4fv(GLuint idx, const GLfloat *v)
{
    struct mali_gles_ctx *c = fast_cur_ctx();
    if (c && c->st) CALL_VertexAttrib4fvARB(c->st->ctx->Dispatch.Current, (idx, v));
}

static GLuint next_fence_id = 1;
void wrap_GenFencesAPPLE(GLsizei n, GLuint *fences) { for (GLsizei i = 0; i < n; i++) fences[i] = next_fence_id++; }
void wrap_DeleteFencesAPPLE(GLsizei n, const GLuint *fences) { (void)n; (void)fences; }
void wrap_SetFenceAPPLE(GLuint fence) { (void)fence; }
GLboolean wrap_IsFenceAPPLE(GLuint fence) { return fence != 0; }
GLboolean wrap_TestFenceAPPLE(GLuint fence) { (void)fence; return GL_TRUE; }
void wrap_FinishFenceAPPLE(GLuint fence) { (void)fence; flush_current_fbo_iosurf('E'); _mesa_Finish(); mali_wait_idle(); }
GLboolean wrap_TestObjectAPPLE(GLenum obj, GLuint name) { (void)obj; (void)name; return GL_TRUE; }
void wrap_FinishObjectAPPLE(GLenum obj, GLuint name) { (void)obj; (void)name; flush_current_fbo_iosurf('O'); _mesa_Finish(); mali_wait_idle(); }
void wrap_Noop(void) {}
void wrap_GetObjectLabelEXT(GLenum type, GLuint obj, GLsizei bufSize, GLsizei *length, char *label)
{
    (void)type; (void)obj;
    if (length) *length = 0;
    if (label && bufSize > 0) label[0] = '\0';
}
GLenum wrap_ClientWaitSync(void *sync, GLbitfield flags, uint32_t tlo, uint32_t thi)
{
    (void)sync; (void)flags; (void)tlo; (void)thi;
    flush_current_fbo_iosurf('C');
    _mesa_Finish();
    mali_wait_idle();
    return 0x911Au; /* GL_ALREADY_SIGNALED */
}
void wrap_WaitSync(void *sync, GLbitfield flags, uint32_t tlo, uint32_t thi)
{
    (void)sync; (void)flags; (void)tlo; (void)thi;
    flush_current_fbo_iosurf('W');
    _mesa_Flush();
}

/* --- Objective-C EAGLContext swizzling ------------------------------------- */

static void *sel_getMacroContextPrivate;
static void *(*orig_commonInitWithAPI)(void *self, void *cmd, uint32_t api, void *props);
static void *(*orig_getMacroContextPrivate)(void *self, void *cmd);
static uint8_t (*orig_setCurrentContext)(void *cls, void *cmd, void *ctx);
static uint8_t (*orig_presentRenderbuffer)(void *self, void *cmd, uint32_t target);
static uint8_t (*orig_renderbufferStorageFromDrawable)(void *self, void *cmd, uint32_t target, void *drawable);

extern void *class_getClassMethod(void *cls, void *sel);

static void *get_priv(void *eagl_ctx)
{
    if (!eagl_ctx)
        return NULL;
    void *macro = orig_getMacroContextPrivate
        ? orig_getMacroContextPrivate(eagl_ctx, sel_getMacroContextPrivate)
        : (sel_getMacroContextPrivate
           ? ((void *(*)(void *, void *))objc_msgSend)(eagl_ctx, sel_getMacroContextPrivate)
           : NULL);
    /* -[EAGLContext getMacroContextPrivate] returns eagl_priv + 0x10 (&eagl_priv->gli_ctx). */
    return macro ? (void *)((uint8_t *)macro - 0x10) : NULL;
}

static uint8_t swiz_setCurrentContext(void *cls, void *cmd, void *ctx)
{
    uint8_t ok = orig_setCurrentContext ? orig_setCurrentContext(cls, cmd, ctx) : 0;
    if (ok && ctx) {
        void *priv = get_priv(ctx);
        if (priv) {
            mali_gles_install_dispatch(priv);
            void *gli_ctx = *(void **)((uint8_t *)priv + 0x10);
            if (gli_ctx)
                mali_ensure_current_by_gli(gli_ctx);
        }
    }
    return ok;
}

static void *swiz_getMacroContextPrivate(void *self, void *cmd)
{
    void *macro = orig_getMacroContextPrivate ? orig_getMacroContextPrivate(self, cmd) : NULL;
    if (macro)
        mali_gles_install_dispatch((uint8_t *)macro - 0x10);
    return macro;
}

static void *swiz_commonInitWithAPI(void *self, void *cmd, uint32_t api, void *props)
{
    void *res = orig_commonInitWithAPI ? orig_commonInitWithAPI(self, cmd, api, props) : NULL;
    if (res) {
        void *priv = get_priv(res);
        if (priv)
            mali_gles_install_dispatch(priv);
    }
    return res;
}

static size_t format_bpp(uint32_t fmt, uint32_t type)
{
    if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
        type == GL_UNSIGNED_SHORT_5_5_5_1)
        return 2;
    switch (fmt) {
    case GL_ALPHA:
    case GL_LUMINANCE:
    case 0x1903: /* GL_RED */
        return 1;
    case GL_LUMINANCE_ALPHA:
    case 0x8227: /* GL_RG */
        return 2;
    case GL_RGB:
        return 3;
    default:
        return 4;
    }
}

static void bind_iosurf_slot(int is_rb, GLuint obj_id, void *surf, uint32_t w, uint32_t h,
                             uint32_t plane, uint32_t invert, uint32_t fmt, uint32_t type,
                             uint32_t seed, struct pipe_resource *pt, uint32_t zc_va, uint32_t zc_bpr)
{
    if (!obj_id || !surf)
        return;
    pthread_mutex_lock(&gles_mtx);
    struct iosurf_binding *slot = find_iosurf_locked(is_rb, obj_id);
    if (!slot) {
        for (int i = 0; i < g_iosurf_hi; i++) {
            if (!g_iosurfs[i].used) { slot = &g_iosurfs[i]; break; }
        }
        if (!slot && g_iosurf_hi < MAX_IOSURF)
            slot = &g_iosurfs[g_iosurf_hi++];
    }
    if (slot) {
        if (slot->used && slot->pt != pt)
            pipe_resource_reference(&slot->pt, pt);
        else if (!slot->used && pt)
            pipe_resource_reference(&slot->pt, pt);
        if (slot->used && slot->surf && slot->surf != surf)
            CFRelease(slot->surf);
        if (!slot->used || slot->surf != surf)
            CFRetain(surf);
        slot->used = 1;
        slot->is_rb = is_rb;
        slot->obj_id = obj_id;
        slot->surf = surf;
        slot->w = w;
        slot->h = h;
        slot->plane = plane;
        slot->invert = invert;
        slot->fmt = fmt;
        slot->type = type;
        slot->seed = seed;
        slot->bound_fpa = 0;
        slot->zc_va = zc_va;
        slot->zc_bpr = zc_bpr;
        slot->dirty = 0;
        rebuild_iosurf_fbo_mask_locked();
        g_iosurf_gen++;
    }
    pthread_mutex_unlock(&gles_mtx);
}

extern void _mesa_update_renderbuffer_surface(struct gl_context *ctx, struct gl_renderbuffer *rb);

static struct {
    void *surf;
    struct pipe_resource *pt;
} g_bb_swap[8];
static uint32_t g_bb_swap_next = 0;

static uint8_t swiz_texImageIOSurface(void *self, void *cmd,
                                      void *surf, uint32_t target, uint32_t ifmt,
                                      uint32_t w, uint32_t h, uint32_t fmt,
                                      uint32_t type, uint32_t plane, uint32_t invert)
{
    (void)cmd;
    void *priv = get_priv(self);
    if (!priv || !surf || !w || !h)
        return 0;
    void *gli_ctx = *(void **)((uint8_t *)priv + 0x10);
    mali_ensure_current_by_gli(gli_ctx);
    if (!tls_cur_ctx)
        return 0;

    if (target == GL_RENDERBUFFER) {
        struct gl_context *glctx = tls_cur_ctx->st ? tls_cur_ctx->st->ctx : NULL;
        /* Keep one persistent non-cacheable BO per backboardd 720x1280 swapchain IOSurface.
         * Without this, reusing rb=1 frees & immediately pops the SAME single BO from bo_cache,
         * aliasing all 3 triple-buffered surfaces to 1 PA and zeroing live scanout on every frame! */
        if (is_backboardd_proc() && w == 720u && h == 1280u && !invert && glctx) {
            struct pipe_resource *cached_pt = NULL;
            pthread_mutex_lock(&gles_mtx);
            for (int i = 0; i < 8; i++) {
                if (g_bb_swap[i].surf == surf && g_bb_swap[i].pt) {
                    pipe_resource_reference(&cached_pt, g_bb_swap[i].pt);
                    break;
                }
            }
            pthread_mutex_unlock(&gles_mtx);
            struct gl_renderbuffer *rb = glctx->CurrentRenderbuffer;
            if (cached_pt) {
                if (!rb || rb->Width != 720 || rb->Height != 1280 || !rb->texture) {
                    int prev_lin = lima_force_linear;
                    lima_force_linear = 1;
                    _mesa_RenderbufferStorage(GL_RENDERBUFFER, 0x93a1, 720, 1280);
                    lima_force_linear = prev_lin;
                    rb = glctx->CurrentRenderbuffer;
                }
                if (rb) {
                    pipe_resource_reference(&rb->texture, cached_pt);
                    _mesa_update_renderbuffer_surface(glctx, rb);
                    glctx->NewState |= _NEW_BUFFERS;
                    if (tls_cur_ctx->bound_fbo)
                        _mesa_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                      GL_RENDERBUFFER, tls_cur_ctx->bound_rb);
                }
                bind_iosurf_slot(1, tls_cur_ctx->bound_rb, surf, w, h, plane, invert, 0x80e1u, GL_UNSIGNED_BYTE, 0u, cached_pt, 0u, 0u);
                pipe_resource_reference(&cached_pt, NULL);
                return 1;
            }
            int prev_lin = lima_force_linear;
            lima_force_linear = 1;
            _mesa_RenderbufferStorage(GL_RENDERBUFFER, 0x93a1, 720, 1280);
            lima_force_linear = prev_lin;
            rb = glctx->CurrentRenderbuffer;
            struct pipe_resource *new_pt = rb ? rb->texture : NULL;
            if (new_pt) {
                struct lima_resource *lres = lima_resource(new_pt);
                if (lres && lres->bo)
                    lres->bo->cacheable = false;
                pthread_mutex_lock(&gles_mtx);
                int slot = -1;
                for (int i = 0; i < 8; i++)
                    if (!g_bb_swap[i].surf) { slot = i; break; }
                if (slot < 0) {
                    slot = (int)(g_bb_swap_next++ & 7u);
                    if (g_bb_swap[slot].surf) CFRelease(g_bb_swap[slot].surf);
                    pipe_resource_reference(&g_bb_swap[slot].pt, NULL);
                }
                CFRetain(surf);
                g_bb_swap[slot].surf = surf;
                pipe_resource_reference(&g_bb_swap[slot].pt, new_pt);
                pthread_mutex_unlock(&gles_mtx);
            }
            bind_iosurf_slot(1, tls_cur_ctx->bound_rb, surf, w, h, plane, invert, 0x80e1u, GL_UNSIGNED_BYTE, 0u, new_pt, 0u, 0u);
            return 1;
        }

        uint32_t zc_va = 0u, zc_bpr = 0u;
        struct pipe_resource *rb_pt = NULL;
        if (!is_backboardd_proc() && glctx)
            zc_va = map_iosurf_zc_va(surf, w, h, &zc_bpr);
        /* For invert==1 + zc_va, keep app RB tiled for faster Mali-400 3D rendering and HW Y-flip blit to zc_va.
         * For invert==0 + zc_va, allocate 0-pool-byte external BO pointing directly at zc_va. */
        int prev_lin = lima_force_linear;
        lima_force_linear = (zc_va && invert) ? 0 : 1;
        if (zc_va && !invert)
            lima_external_va = zc_va;
        _mesa_RenderbufferStorage(GL_RENDERBUFFER, 0x93a1 /* GL_BGRA8_EXT */, (GLsizei)w, (GLsizei)h);
        lima_external_va = 0u;
        lima_force_linear = prev_lin;
        if (zc_va && glctx) {
            struct gl_renderbuffer *rb = glctx->CurrentRenderbuffer;
            struct pipe_resource *pt = rb ? rb->texture : NULL;
            struct lima_resource *lres = pt ? lima_resource(pt) : NULL;
            if (lres && lres->bo) {
                if (!invert && !lres->tiled) {
                    lres->bo->va = zc_va;
                    lres->bo->cacheable = false;
                    lres->levels[0].stride = zc_bpr;
                    lres->levels[0].offset = 0u;
                }
                rb_pt = pt;
            } else {
                zc_va = 0u;
                zc_bpr = 0u;
            }
        }
        bind_iosurf_slot(1, tls_cur_ctx->bound_rb, surf, w, h, plane, invert, 0x80e1u, GL_UNSIGNED_BYTE, 0u, rb_pt, zc_va, zc_bpr);
        {
            static uint32_t g_rbb_log = 0;
            if (!is_backboardd_proc() && g_rbb_log < 8u) {
                g_rbb_log++;
                kperf_log("[GL] rb_bind pid=%d rb=%u %ux%u inv=%u zc=0x%x\n",
                          (int)getpid(), tls_cur_ctx->bound_rb, w, h, invert, zc_va);
            }
        }
        return 1;
    }

    if (target == GL_TEXTURE_2D) {
        GLint m_ifmt = (GLint)ifmt;
        GLenum m_fmt = (GLenum)fmt;
        GLenum m_type = (GLenum)type;
        fix_ios_tex_fmt(&m_ifmt, &m_fmt, &m_type);

        struct gl_context *glctx = tls_cur_ctx->st ? tls_cur_ctx->st->ctx : NULL;
        struct gl_texture_object *texObj = glctx
            ? glctx->Texture.Unit[glctx->Texture.CurrentUnit].CurrentTex[TEXTURE_2D_INDEX]
            : NULL;

        GLuint tex_id = (texObj && texObj->Name) ? texObj->Name : tls_cur_ctx->bound_tex;
        uint32_t cur_seed = 0;
        struct pipe_resource *tex_pt = NULL;
        if (IOSurfaceLock(surf, 1 /* kIOSurfaceLockReadOnly */, &cur_seed) == 0) {
            int skip_unchanged = 0;
            if (cur_seed != 0u && texObj && texObj->pt && tex_id) {
                pthread_mutex_lock(&gles_mtx);
                struct iosurf_binding *slot = find_iosurf_locked(0, tex_id);
                if (slot && slot->surf == surf && slot->w == w && slot->h == h &&
                    slot->plane == plane && slot->invert == invert &&
                    slot->fmt == m_fmt && slot->type == m_type &&
                    slot->seed == cur_seed && slot->pt == texObj->pt) {
                    skip_unchanged = 1;
                }
                pthread_mutex_unlock(&gles_mtx);
            }
            if (skip_unchanged) {
                IOSurfaceUnlock(surf, 1, NULL);
                return 1;
            }

            size_t nplanes = IOSurfaceGetPlaneCount(surf);
            const uint8_t *base = (const uint8_t *)(nplanes > 0
                ? IOSurfaceGetBaseAddressOfPlane(surf, plane)
                : IOSurfaceGetBaseAddress(surf));
            size_t bpr = nplanes > 0
                ? IOSurfaceGetBytesPerRowOfPlane(surf, plane)
                : IOSurfaceGetBytesPerRow(surf);
            size_t bpp = format_bpp(m_fmt, m_type);
            size_t row_bytes = (size_t)w * bpp;
            if (base && bpr >= row_bytes) {
                int uploaded_direct = 0;
                /* Zero-copy lookup ONLY for large surfaces created as GPU ZC (>=65536 px, <=5 MiB). */
                uint64_t zc_tex_va = 0;
                if (is_backboardd_proc() && !invert && nplanes == 0 && m_fmt == 0x80e1u &&
                    m_type == GL_UNSIGNED_BYTE && (uint64_t)w * (uint64_t)h >= 65536ull &&
                    ((uintptr_t)base & 0xfffu) == 0u && (bpr & 7u) == 0u && mali_iokit_conn) {
                    size_t img_bytes = (size_t)h * bpr;
                    size_t sz = (img_bytes + 0xfffu) & ~0xfffu;
                    if (img_bytes > 0u && sz > 0u && sz <= MALI_ZC_MAX_BYTES) {
                        (void)((volatile const uint8_t *)base)[0];
                        (void)((volatile const uint8_t *)base)[(sz >> 1) & ~0xfffu];
                        (void)((volatile const uint8_t *)base)[img_bytes - 1u];
                        uint64_t in[3] = { (uint64_t)(uintptr_t)base, (uint64_t)sz, 0u };
                        uint32_t cnt = 1;
                        (void)IOConnectCallScalarMethod(mali_iokit_conn, 10u, in, 3, &zc_tex_va, &cnt);
                    }
                }
                if (texObj && tls_cur_ctx->st->pipe && m_type == GL_UNSIGNED_BYTE &&
                    (m_fmt == 0x80e1u || m_fmt == GL_RGBA || m_fmt == GL_ALPHA ||
                     m_fmt == GL_LUMINANCE || m_fmt == 0x1903u)) {
                    struct gl_texture_image *timg = texObj->Image[0][0];
                    struct pipe_resource *pt = (timg && timg->pt) ? timg->pt : texObj->pt;
                    int pt_was_zc = (pt && lima_resource(pt)->bo &&
                                     ((lima_resource(pt)->bo->handle & 0x80000000u) ||
                                      lima_resource(pt)->bo->va >= 0x80000000u));
                    int force_realloc = (pt && (lima_resource(pt)->tiled || (!zc_tex_va && pt_was_zc)));
                    if (!pt || pt->width0 != w || pt->height0 != h || pt->last_level != 0 ||
                        !timg || timg->pt != pt || force_realloc ||
                        !iosurf_pipe_fmt_ok(pt->format, m_ifmt, m_fmt, m_type)) {
                        if (force_realloc) {
                            /* st_AllocTextureImageBuffer reuses texObj->pt when dims/format match;
                             * drop stale external/tiled pt first so Mesa allocates a fresh linear BO. */
                            struct lima_resource *old_lres = lima_resource(pt);
                            if (old_lres && old_lres->bo)
                                lima_flush_job_accessing_bo((struct lima_context *)tls_cur_ctx->st->pipe,
                                                            old_lres->bo, false);
                            pipe_resource_reference(&texObj->pt, NULL);
                            if (timg)
                                pipe_resource_reference(&timg->pt, NULL);
                        }
                        int prev_lin = lima_force_linear;
                        int prev_sz = lima_skip_bo_zero;
                        lima_force_linear = 1;
                        if (zc_tex_va)
                            lima_external_va = (uint32_t)zc_tex_va;
                        else
                            lima_skip_bo_zero = 1;
                        _mesa_TexImage2D(GL_TEXTURE_2D, 0, m_ifmt, (GLsizei)w, (GLsizei)h, 0, m_fmt, m_type, NULL);
                        lima_external_va = 0u;
                        lima_skip_bo_zero = prev_sz;
                        lima_force_linear = prev_lin;
                        timg = texObj->Image[0][0];
                        pt = (timg && timg->pt) ? timg->pt : texObj->pt;
                    }
                    if (pt && pt->width0 == w && pt->height0 == h && timg && timg->pt == pt &&
                        iosurf_pipe_fmt_ok(pt->format, m_ifmt, m_fmt, m_type)) {
                        struct lima_resource *lres = lima_resource(pt);
                        if (zc_tex_va && lres && !lres->tiled && lres->bo) {
                            if (lres->bo->va != (uint32_t)zc_tex_va || !(lres->bo->handle & 0x80000000u)) {
                                lima_flush_job_accessing_bo((struct lima_context *)tls_cur_ctx->st->pipe, lres->bo, false);
                                if (!(lres->bo->handle & 0x80000000u)) {
                                    struct lima_bo *ext = lima_bo_create_external((struct lima_screen *)pt->screen,
                                                                                  (uint32_t)zc_tex_va, lres->bo->size);
                                    if (ext) {
                                        lima_bo_unreference(lres->bo);
                                        lres->bo = ext;
                                    }
                                }
                                lres->bo->va = (uint32_t)zc_tex_va;
                                lres->bo_gen = lima_next_bo_gen();
                            }
                            lres->bo->cacheable = false;
                            lres->levels[0].stride = (uint32_t)bpr;
                            lres->levels[0].offset = 0u;
                            tex_pt = pt;
                            uploaded_direct = 1;
                        } else if (lres && !lres->tiled && lres->bo &&
                                   !(lres->bo->handle & 0x80000000u) &&
                                   lres->levels[0].stride >= row_bytes &&
                                   (size_t)lres->levels[0].offset + (size_t)h * lres->levels[0].stride <= lres->bo->size) {
                            lima_flush_job_accessing_bo((struct lima_context *)tls_cur_ctx->st->pipe,
                                                        lres->bo, true);
                            if (!lima_bo_wait(lres->bo, MALI_LIMA_GEM_WAIT_WRITE, 0)) {
                                struct lima_bo *new_bo = lres->bo->cacheable
                                    ? lima_bo_create((struct lima_screen *)pt->screen,
                                                     lres->bo->size, lres->bo->flags | 0x80000000u)
                                    : NULL;
                                if (new_bo) {
                                    lima_bo_unreference(lres->bo);
                                    lres->bo = new_bo;
                                    lres->bo_gen = lima_next_bo_gen();
                                } else {
                                    lima_bo_wait(lres->bo, MALI_LIMA_GEM_WAIT_WRITE, UINT64_MAX);
                                }
                            }
                            uint8_t *dst_base = (uint8_t *)lima_bo_map(lres->bo);
                            if (dst_base) {
                                uint32_t dst_stride = lres->levels[0].stride;
                                uint8_t *dst = dst_base + lres->levels[0].offset;
                                if (!invert && bpr == dst_stride) {
                                    memcpy_neon(dst, base, (size_t)h * bpr);
                                } else {
                                    for (uint32_t y = 0; y < h; y++) {
                                        uint32_t sy = invert ? (h - 1u - y) : y;
                                        memcpy_neon(dst + y * dst_stride, base + sy * bpr, row_bytes);
                                    }
                                }
                                tex_pt = pt;
                                uploaded_direct = 1;
                            }
                        }
                    }
                }
                if (!uploaded_direct) {
                    const void *src = base;
                    pthread_mutex_lock(&gles_mtx);
                    if (bpr != row_bytes || invert) {
                        uint8_t *packed = ensure_scratch(row_bytes * h);
                        if (packed) {
                            for (uint32_t y = 0; y < h; y++) {
                                uint32_t sy = invert ? (h - 1u - y) : y;
                                memcpy_neon(packed + y * row_bytes, base + sy * bpr, row_bytes);
                            }
                            src = packed;
                        }
                    }
                    GLint prev_unpack = 4;
                    _mesa_GetIntegerv(GL_UNPACK_ALIGNMENT, &prev_unpack);
                    _mesa_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
                    int prev_lin = lima_force_linear;
                    lima_force_linear = 1;
                    _mesa_TexImage2D(GL_TEXTURE_2D, 0, m_ifmt, (GLsizei)w, (GLsizei)h, 0, m_fmt, m_type, src);
                    lima_force_linear = prev_lin;
                    _mesa_PixelStorei(GL_UNPACK_ALIGNMENT, prev_unpack);
                    pthread_mutex_unlock(&gles_mtx);
                    if (texObj)
                        tex_pt = texObj->pt;
                }
            }
            IOSurfaceUnlock(surf, 1, NULL);
        }
        bind_iosurf_slot(0, tex_id, surf, w, h, plane, invert, m_fmt, m_type, cur_seed, tex_pt, 0u, 0u);
        return 1;
    }
    return 0;
}

/* --- EAGLNativeWindowObject bridge for CAEAGLLayer (3D iOS apps) ----------- */
struct eagl_nw_callbacks {
    void *data;
    int  (*cb1)(void *data);
    void (*cb2)(void *data, void *surf);
    int  (*cb3)(void *data, uint32_t a, uint32_t b, uint32_t c);
};

struct eagl_native_window {
    uint32_t version;
    int      (*attach)(void *win, uint32_t fourcc, const struct eagl_nw_callbacks *cbs);
    void     (*detach)(void *win);
    void    *(*begin)(void *win);
    int      (*swap)(void *win, uint32_t flags);
    void    *(*collect)(void *win);
    uint32_t (*properties)(void *win);
};

#define MAX_NW_BINDS 8
struct nw_bind {
    int used;
    GLuint rb_id;
    struct eagl_native_window *win;
    uint32_t w, h;
    int need_begin;
    uint32_t prev_seq;
};
static struct nw_bind g_nw_binds[MAX_NW_BINDS];

static int  nw_cb1(void *d) { (void)d; return 1; }
static void nw_cb2(void *d, void *s) { (void)d; (void)s; }
static int  nw_cb3(void *d, uint32_t a, uint32_t b, uint32_t c) { (void)d; (void)a; (void)b; (void)c; return 0; }

static uint32_t map_iosurf_zc_va(void *surf, uint32_t w, uint32_t h, uint32_t *out_bpr)
{
    uint32_t zc_va = 0u, zc_bpr = 0u;
    if (!surf || !mali_iokit_conn || (uint64_t)w * (uint64_t)h < 65536ull || IOSurfaceGetPlaneCount(surf) != 0) {
        if (out_bpr) *out_bpr = 0u;
        return 0u;
    }
    if (IOSurfaceLock(surf, 0, NULL) == 0) {
        uintptr_t ubase = (uintptr_t)IOSurfaceGetBaseAddress(surf);
        size_t bpr = IOSurfaceGetBytesPerRow(surf);
        size_t sz = ((size_t)h * bpr + 0xfffu) & ~0xfffu;
        if (ubase && (ubase & 0xfffu) == 0u && bpr >= (size_t)w * 4u &&
            (bpr & 7u) == 0u && sz > 0u && sz <= MALI_ZC_MAX_BYTES) {
            for (size_t off = 0; off < sz; off += 4096u)
                ((volatile uint8_t *)ubase)[off] = ((volatile uint8_t *)ubase)[off];
            uint64_t in[3] = { (uint64_t)ubase, (uint64_t)sz, 1u };
            uint64_t gva = 0;
            uint32_t cnt = 1;
            if (IOConnectCallScalarMethod(mali_iokit_conn, 10u, in, 3, &gva, &cnt) == 0 && gva) {
                zc_va = (uint32_t)gva;
                zc_bpr = (uint32_t)bpr;
            }
        }
        IOSurfaceUnlock(surf, 0, NULL);
    }
    if (out_bpr) *out_bpr = zc_bpr;
    return zc_va;
}

static uint8_t swiz_presentRenderbuffer(void *self, void *cmd, uint32_t target)
{
    void *priv = get_priv(self);
    if (priv) {
        void *gli_ctx = *(void **)((uint8_t *)priv + 0x10);
        mali_ensure_current_by_gli(gli_ctx);
        struct mali_gles_ctx *c = tls_cur_ctx;
        struct nw_bind *nb = NULL;
        if (c && !is_backboardd_proc()) {
            pthread_mutex_lock(&gles_mtx);
            for (int i = 0; i < MAX_NW_BINDS; i++) {
                if (g_nw_binds[i].used && (!c->bound_rb || g_nw_binds[i].rb_id == c->bound_rb)) {
                    nb = &g_nw_binds[i];
                    break;
                }
            }
            GLuint rb_match = nb ? nb->rb_id : c->bound_rb;
            for (int i = 0; i < g_iosurf_hi; i++) {
                struct iosurf_binding *b = &g_iosurfs[i];
                if (b->used && b->is_rb && (!rb_match || b->obj_id == rb_match)) {
                    b->dirty = 1;
                    b->dirty_x0 = 0; b->dirty_y0 = 0;
                    b->dirty_x1 = b->w; b->dirty_y1 = b->h;
                }
            }
            pthread_mutex_unlock(&gles_mtx);
        }
        static uint32_t g_nws_log = 0;
        int want_log = (g_nws_log < 12u);
        uint32_t beg_ms = 0;
        void *beg_surf = NULL;
        if (nb && nb->win && nb->need_begin && nb->win->begin) {
            uint32_t tb0 = want_log ? now_ms() : 0u;
            void *rs = nb->win->begin(nb->win);
            if (want_log) beg_ms = now_ms() - tb0;
            beg_surf = rs;
            if (rs) {
                nb->need_begin = 0;
                uint32_t nw = (uint32_t)IOSurfaceGetWidth(rs);
                uint32_t nh = (uint32_t)IOSurfaceGetHeight(rs);
                if (nb->win->version >= 2u && nb->win->properties && (nb->win->properties(nb->win) & 4u)) {
                    uint32_t t = nw; nw = nh; nh = t;
                }
                if (nw == nb->w && nh == nb->h && nw && nh) {
                    uint32_t zc_bpr = 0u;
                    uint32_t zc_va = map_iosurf_zc_va(rs, nw, nh, &zc_bpr);
                    pthread_mutex_lock(&gles_mtx);
                    struct iosurf_binding *b = find_iosurf_locked(1, nb->rb_id);
                    if (b) {
                        if (b->surf != rs) { CFRetain(rs); if (b->surf) CFRelease(b->surf); b->surf = rs; }
                        b->zc_va = zc_va; b->zc_bpr = zc_bpr;
                    }
                    pthread_mutex_unlock(&gles_mtx);
                } else if (nw && nh) {
                    nb->w = nw; nb->h = nh;
                    swiz_texImageIOSurface(self, NULL, rs, GL_RENDERBUFFER, GL_RGBA8,
                                           nw, nh, 0x80e1u, GL_UNSIGNED_BYTE, 0, 1);
                }
            }
        }
        flush_current_fbo_iosurf('P');
        if (nb && nb->win) {
            uint32_t ts0 = want_log ? now_ms() : 0u;
            uint32_t cur_seq = mali_get_last_enq_seq();
            void *cur_surf = NULL;
            int has_zc = 0;
            pthread_mutex_lock(&gles_mtx);
            struct iosurf_binding *cb = find_iosurf_locked(1, nb->rb_id);
            if (cb && cb->surf) {
                cur_surf = cb->surf;
                has_zc = (cb->zc_va != 0u);
                CFRetain(cur_surf);
            }
            pthread_mutex_unlock(&gles_mtx);
            /* Pipeline zero-copy CAEAGLLayer swaps with 1 frame in flight (FIFO mali_q + disp.c wait_seq);
             * keep full current-frame wait when !has_zc (CPU readback / fallback blit). */
            if (has_zc) {
                if (nb->prev_seq)
                    mali_wait_seq(nb->prev_seq);
                nb->prev_seq = cur_seq;
            } else {
                mali_wait_seq(cur_seq);
                nb->prev_seq = 0u;
            }
            uint32_t ts1 = want_log ? now_ms() : 0u;
            /* Bump IOSurface seed cleanly (zero CPU writes) so backboardd always sees the new GPU frame. */
            unsigned iu = 9u;
            if (cur_surf) {
                if (IOSurfaceLock(cur_surf, 0, NULL) == 0)
                    IOSurfaceUnlock(cur_surf, 0, NULL);
            }
            int swap_rc = (!nb->need_begin && nb->win->swap) ? nb->win->swap(nb->win, 1u) : -1;
            uint32_t ts2 = want_log ? now_ms() : 0u;
            if (cur_surf) {
                if (want_log) {
                    static uint8_t (*fn_inuse)(void *);
                    if (!fn_inuse) fn_inuse = (uint8_t (*)(void *))dlsym(RTLD_DEFAULT, "IOSurfaceIsInUse");
                    if (fn_inuse) iu = (unsigned)fn_inuse(cur_surf);
                }
                CFRelease(cur_surf);
            }
            if (swap_rc == 1)
                nb->need_begin = 1;
            if (want_log) {
                g_nws_log++;
                uint32_t sh0 = 0, sh12 = 0, sh16 = 0, st0 = 0, st1 = 0, st2 = 0, ty0 = 0, ty1 = 0, ty2 = 0;
                uint8_t *w8 = (uint8_t *)nb->win;
                uint8_t *iq = *(uint8_t **)(w8 + 76);
                if (iq) {
                    uint8_t *shobj = *(uint8_t **)(iq + 28);
                    uint8_t *sh = shobj ? *(uint8_t **)(shobj + 12) : NULL;
                    if (sh) {
                        sh0  = *(uint32_t *)(sh + 0);
                        sh12 = *(uint32_t *)(sh + 12);
                        sh16 = *(uint32_t *)(sh + 16);
                        st0 = *(uint32_t *)(sh + 232 + 0 * 80 + 32);
                        st1 = *(uint32_t *)(sh + 232 + 1 * 80 + 32);
                        st2 = *(uint32_t *)(sh + 232 + 2 * 80 + 32);
                        ty0 = *(uint32_t *)(sh + 232 + 0 * 80 + 40);
                        ty1 = *(uint32_t *)(sh + 232 + 1 * 80 + 40);
                        ty2 = *(uint32_t *)(sh + 232 + 2 * 80 + 40);
                    }
                }
                kperf_log("[GL] nw_swap#%u rc=%d beg_s=%p beg=%u w=%u sw=%u sh=%x,%x,%x st=%x,%x,%x ty=%u,%u,%u iu=%u\n",
                          g_nws_log, swap_rc, beg_surf, beg_ms, ts1 - ts0, ts2 - ts1,
                          sh0, sh12, sh16, st0, st1, st2, ty0, ty1, ty2, iu);
            }
            return 1;
        }
    }
    return orig_presentRenderbuffer ? orig_presentRenderbuffer(self, cmd, target) : 1;
}

static uint8_t swiz_renderbufferStorageFromDrawable(void *self, void *cmd, uint32_t target, void *drawable)
{
    if (target != GL_RENDERBUFFER)
        return 0;
    void *priv = get_priv(self);
    if (!priv)
        return orig_renderbufferStorageFromDrawable ? orig_renderbufferStorageFromDrawable(self, cmd, target, drawable) : 0;
    void *gli_ctx = *(void **)((uint8_t *)priv + 0x10);
    mali_ensure_current_by_gli(gli_ctx);
    struct mali_gles_ctx *c = tls_cur_ctx;
    if (!c)
        return 0;

    GLuint rb_id = c->bound_rb;
    int slot = -1;
    pthread_mutex_lock(&gles_mtx);
    for (int i = 0; i < MAX_NW_BINDS; i++) {
        if (g_nw_binds[i].used && g_nw_binds[i].rb_id == rb_id) { slot = i; break; }
        if (!g_nw_binds[i].used && slot < 0) slot = i;
    }
    struct eagl_native_window *old_win = (slot >= 0 && g_nw_binds[slot].used) ? g_nw_binds[slot].win : NULL;
    pthread_mutex_unlock(&gles_mtx);

    if (!drawable) {
        if (old_win && old_win->detach) old_win->detach(old_win);
        if (slot >= 0) {
            pthread_mutex_lock(&gles_mtx);
            g_nw_binds[slot].used = 0;
            g_nw_binds[slot].win = NULL;
            g_nw_binds[slot].prev_seq = 0u;
            pthread_mutex_unlock(&gles_mtx);
        }
        return 1;
    }

    static void *sel_resp, *sel_nw;
    if (!sel_resp) sel_resp = sel_registerName("respondsToSelector:");
    if (!sel_nw)   sel_nw   = sel_registerName("nativeWindow");
    uint8_t has_nw = ((uint8_t (*)(void *, void *, void *))objc_msgSend)(drawable, sel_resp, sel_nw);
    struct eagl_native_window *win = has_nw
        ? ((struct eagl_native_window *(*)(void *, void *))objc_msgSend)(drawable, sel_nw)
        : NULL;
    if (!win || !win->attach || !win->begin)
        return orig_renderbufferStorageFromDrawable ? orig_renderbufferStorageFromDrawable(self, cmd, target, drawable) : 0;

    if (old_win && old_win->detach)
        old_win->detach(old_win);

    struct eagl_nw_callbacks cbs = { (slot >= 0) ? (void *)&g_nw_binds[slot] : (void *)c, nw_cb1, nw_cb2, nw_cb3 };
    int att_rc = win->attach(win, 0x42475241u /* 'BGRA' */, &cbs);
    if (att_rc != 1) {
        kperf_log("[GL] nw_attach FAIL pid=%d rb=%u rc=%d\n", (int)getpid(), rb_id, att_rc);
        return 0;
    }
    void *surf = win->begin(win);
    if (!surf) {
        if (win->detach) win->detach(win);
        kperf_log("[GL] nw_begin NULL pid=%d rb=%u\n", (int)getpid(), rb_id);
        return 0;
    }
    uint32_t w = (uint32_t)IOSurfaceGetWidth(surf);
    uint32_t h = (uint32_t)IOSurfaceGetHeight(surf);
    if (win->version >= 2u && win->properties && (win->properties(win) & 4u)) {
        uint32_t t = w; w = h; h = t;
    }
    if (slot >= 0) {
        pthread_mutex_lock(&gles_mtx);
        g_nw_binds[slot].used = 1;
        g_nw_binds[slot].rb_id = rb_id;
        g_nw_binds[slot].win = win;
        g_nw_binds[slot].w = w;
        g_nw_binds[slot].h = h;
        g_nw_binds[slot].need_begin = 0;
        g_nw_binds[slot].prev_seq = 0u;
        pthread_mutex_unlock(&gles_mtx);
    }
    {
        static uint32_t g_nwb_log = 0;
        if (g_nwb_log < 8u) {
            g_nwb_log++;
            kperf_log("[GL] nw_bind pid=%d rb=%u win=%p %ux%u surf=%p\n",
                      (int)getpid(), rb_id, (void *)win, w, h, surf);
        }
    }
    return swiz_texImageIOSurface(self, NULL, surf, GL_RENDERBUFFER, GL_RGBA8,
                                  w, h, 0x80e1u, GL_UNSIGNED_BYTE, 0, 1);
}

static void (*orig_NSLogv)(const void *fmt, va_list ap);
static const char *(*fn_CFStringGetCStringPtr)(const void *str, uint32_t enc);

static void hook_NSLog(const void *fmt, ...)
{
    if (fmt && fn_CFStringGetCStringPtr) {
        const char *s = fn_CFStringGetCStringPtr(fmt, 0x0600u /* ASCII */);
        if (!s)
            s = fn_CFStringGetCStringPtr(fmt, 0x08000100u /* UTF8 */);
        if (s && (strncmp(s, "BKSHID: event with no senderID", 30) == 0 ||
                  strstr(s, "CTServerConnection returned error") != NULL))
            return;
    }
    va_list ap;
    va_start(ap, fmt);
    if (orig_NSLogv)
        orig_NSLogv(fmt, ap);
    va_end(ap);
}

static void silence_backboard_nslog_spam(void)
{
    const char *p = getprogname();
    if (!p || (strcmp(p, "backboardd") != 0 && strcmp(p, "SpringBoard") != 0))
        return;
    orig_NSLogv = (void (*)(const void *, va_list))dlsym(RTLD_DEFAULT, "NSLogv");
    fn_CFStringGetCStringPtr = (const char *(*)(const void *, uint32_t))dlsym(RTLD_DEFAULT, "CFStringGetCStringPtr");
    const void *(*get_hdr)(uint32_t) = (const void *(*)(uint32_t))dlsym(RTLD_DEFAULT, "_dyld_get_image_header");
    intptr_t (*get_slide)(uint32_t) = (intptr_t (*)(uint32_t))dlsym(RTLD_DEFAULT, "_dyld_get_image_vmaddr_slide");
    if (!orig_NSLogv || !fn_CFStringGetCStringPtr || !get_hdr || !get_slide)
        return;

    struct mo_hdr { uint32_t magic, cpu, sub, ft, ncmds, szcmds, flags; };
    struct mo_lc  { uint32_t cmd, cmdsize; };
    struct mo_seg { uint32_t cmd, cmdsize; char name[16]; uint32_t vmaddr, vmsize, fileoff, filesize, maxp, initp, nsects, flags; };
    struct mo_sec { char sect[16], seg[16]; uint32_t addr, size, off, align, reloff, nreloc, flags, res1, res2; };
    struct mo_sym { uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize; };
    struct mo_dys { uint32_t cmd, cmdsize, pad[12], indirectsymoff, nindirectsyms; };
    struct mo_nl  { uint32_t strx; uint8_t type, sect; uint16_t desc; uint32_t val; };

    const struct mo_hdr *hdr = (const struct mo_hdr *)get_hdr(0);
    if (!hdr || hdr->magic != 0xfeedfaceu)
        return;
    intptr_t slide = get_slide(0);
    const struct mo_seg *linkedit = NULL, *data_seg = NULL;
    const struct mo_sym *symcmd = NULL;
    const struct mo_dys *dyscmd = NULL;
    const uint8_t *cur = (const uint8_t *)hdr + sizeof(*hdr);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        const struct mo_lc *lc = (const struct mo_lc *)cur;
        if (lc->cmd == 0x1u) {
            const struct mo_seg *seg = (const struct mo_seg *)cur;
            if (strcmp(seg->name, "__LINKEDIT") == 0) linkedit = seg;
            else if (strcmp(seg->name, "__DATA") == 0) data_seg = seg;
        } else if (lc->cmd == 0x2u) {
            symcmd = (const struct mo_sym *)cur;
        } else if (lc->cmd == 0xbu) {
            dyscmd = (const struct mo_dys *)cur;
        }
        cur += lc->cmdsize;
    }
    if (!linkedit || !data_seg || !symcmd || !dyscmd)
        return;
    uintptr_t le_base = (uintptr_t)((intptr_t)linkedit->vmaddr + slide - (intptr_t)linkedit->fileoff);
    const struct mo_nl *syms = (const struct mo_nl *)(le_base + symcmd->symoff);
    const char *strs = (const char *)(le_base + symcmd->stroff);
    const uint32_t *ind = (const uint32_t *)(le_base + dyscmd->indirectsymoff);
    const struct mo_sec *secs = (const struct mo_sec *)((const uint8_t *)data_seg + sizeof(*data_seg));
    for (uint32_t i = 0; i < data_seg->nsects; i++) {
        uint32_t stype = secs[i].flags & 0xffu;
        if (stype != 6u && stype != 7u)
            continue;
        void **ptrs = (void **)((intptr_t)secs[i].addr + slide);
        uint32_t n = secs[i].size >> 2;
        for (uint32_t j = 0; j < n; j++) {
            uint32_t idx = secs[i].res1 + j;
            if (idx >= dyscmd->nindirectsyms)
                break;
            uint32_t sidx = ind[idx];
            if (sidx < symcmd->nsyms && syms[sidx].strx < symcmd->strsize &&
                strcmp(strs + syms[sidx].strx, "_NSLog") == 0) {
                ptrs[j] = (void *)hook_NSLog;
            }
        }
    }
}

__attribute__((constructor))
static void mali_eagl_swizzle_init(void)
{
    /* Pin this bundle in memory so libGFXShared's probe dlclose() cannot unmap swizzled IMPs. */
    Dl_info dli;
    if (dladdr((void *)mali_eagl_swizzle_init, &dli) && dli.dli_fname)
        dlopen(dli.dli_fname, RTLD_LAZY | RTLD_NODELETE);
    silence_backboard_nslog_spam();

    void *cls = objc_getClass("EAGLContext");
    if (!cls)
        return;
    sel_getMacroContextPrivate = sel_registerName("getMacroContextPrivate");

    void *m0 = class_getInstanceMethod(cls, sel_getMacroContextPrivate);
    if (m0) {
        orig_getMacroContextPrivate = (void *(*)(void *, void *))method_getImplementation(m0);
        method_setImplementation(m0, (void *)swiz_getMacroContextPrivate);
    }
    void *m1 = class_getInstanceMethod(cls, sel_registerName("commonInitWithAPI:properties:"));
    if (m1) {
        orig_commonInitWithAPI = (void *(*)(void *, void *, uint32_t, void *))method_getImplementation(m1);
        method_setImplementation(m1, (void *)swiz_commonInitWithAPI);
    }
    void *m2 = class_getInstanceMethod(cls, sel_registerName("texImageIOSurface:target:internalFormat:width:height:format:type:plane:invert:"));
    if (m2)
        method_setImplementation(m2, (void *)swiz_texImageIOSurface);

    void *m3 = class_getInstanceMethod(cls, sel_registerName("presentRenderbuffer:"));
    if (m3) {
        orig_presentRenderbuffer = (uint8_t (*)(void *, void *, uint32_t))method_getImplementation(m3);
        method_setImplementation(m3, (void *)swiz_presentRenderbuffer);
    }
    void *m4 = class_getInstanceMethod(cls, sel_registerName("renderbufferStorage:fromDrawable:"));
    if (m4) {
        orig_renderbufferStorageFromDrawable = (uint8_t (*)(void *, void *, uint32_t, void *))method_getImplementation(m4);
        method_setImplementation(m4, (void *)swiz_renderbufferStorageFromDrawable);
    }
    void *m5 = class_getClassMethod(cls, sel_registerName("setCurrentContext:"));
    if (m5) {
        orig_setCurrentContext = (uint8_t (*)(void *, void *, void *))method_getImplementation(m5);
        method_setImplementation(m5, (void *)swiz_setCurrentContext);
    }
}
