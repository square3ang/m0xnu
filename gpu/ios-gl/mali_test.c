/*
 * iOS 9 userspace test & 3D OpenGL ES 2.0 + EAGL benchmark for Exynos Mali-400.
 * Default: probes SGXDriver543 + Mali400GLDriverEX.
 * --bench3d: renders a 720x1280 lit textured depth-buffered 3D scene via real
 *            EAGLContext + OpenGLES.framework + IOSurface across 4 3D scenarios.
 */
#include <stdint.h>
#include <stddef.h>
#include "mali_abi.h"

extern void *malloc(size_t n);
extern void free(void *p);
extern void *memset(void *p, int c, size_t n);
extern size_t strlen(const char *s);
extern int strcmp(const char *a, const char *b);
extern char *strstr(const char *h, const char *n);
extern int printf(const char *fmt, ...);
extern int snprintf(char *buf, size_t sz, const char *fmt, ...);

extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *symbol);
extern const char *dlerror(void);
extern uint32_t mach_task_self_;
extern uint64_t mach_absolute_time(void);
extern const uint32_t kIOMasterPortDefault;
extern void *IOServiceMatching(const char *name);
extern uint32_t IOServiceGetMatchingService(uint32_t masterPort, void *matching);
extern int IOServiceOpen(uint32_t service, uint32_t owningTask, uint32_t type, uint32_t *connect);
extern int IOServiceClose(uint32_t connect);
extern int IOObjectRelease(uint32_t object);
extern int IOConnectMapMemory(uint32_t connect, uint32_t memoryType, uint32_t intoTask,
                              uintptr_t *atAddress, size_t *ofSize, uint32_t options);
extern int IOConnectCallScalarMethod(uint32_t connection, uint32_t selector,
                                     const uint64_t *input, uint32_t inputCnt,
                                     uint64_t *output, uint32_t *outputCnt);
extern int IOConnectCallStructMethod(uint32_t connection, uint32_t selector,
                                     const void *inputStruct, size_t inputStructCnt,
                                     void *outputStruct, size_t *outputStructCnt);

static uint32_t g_klog_conn = 0;

static void klog_str(const char *s)
{
    if (!g_klog_conn) {
        uint32_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("SGXDriver543"));
        if (svc) {
            IOServiceOpen(svc, mach_task_self_, 0, &g_klog_conn);
            IOObjectRelease(svc);
        }
    }
    if (g_klog_conn) {
        size_t n = strlen(s), z = 0;
        if (n > 0 && n < 160)
            IOConnectCallStructMethod(g_klog_conn, 6u, s, n + 1u, NULL, &z);
    }
    printf("%s", s);
}

static float fast_sin(float x)
{
    const float pi = 3.14159265f, tp = 6.28318531f;
    while (x > pi) x -= tp;
    while (x < -pi) x += tp;
    float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
}
static float fast_cos(float x) { return fast_sin(x + 1.57079633f); }

static void mat4_mul(float *out, const float *a, const float *b)
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            out[c * 4 + r] =
                a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
}

static void mat4_persp(float *m, float aspect, float zn, float zf)
{
    memset(m, 0, 64);
    float f = 1.7320508f; /* cot(30 deg) */
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = (2.0f * zf * zn) / (zn - zf);
}

static void mat4_model(float *m, float tx, float ty, float tz, float ry, float rx, float sc)
{
    float cy = fast_cos(ry), sy = fast_sin(ry);
    float cx = fast_cos(rx), sx = fast_sin(rx);
    m[0] =  cy * sc;       m[4] =  sy * sx * sc; m[8]  =  sy * cx * sc; m[12] = tx;
    m[1] =  0.0f;          m[5] =  cx * sc;      m[9]  = -sx * sc;      m[13] = ty;
    m[2] = -sy * sc;       m[6] =  cy * sx * sc; m[10] =  cy * cx * sc; m[14] = tz;
    m[3] =  0.0f;          m[7] =  0.0f;         m[11] =  0.0f;         m[15] = 1.0f;
}

static int run_bench3d(void)
{
    void *h_cf   = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", 2);
    void *h_ios  = dlopen("/System/Library/PrivateFrameworks/IOSurface.framework/IOSurface", 2);
    void *h_objc = dlopen("/usr/lib/libobjc.A.dylib", 2);
    void *h_gles = dlopen("/System/Library/Frameworks/OpenGLES.framework/OpenGLES", 2);
    if (!h_cf || !h_ios || !h_objc || !h_gles) {
        char eb[128];
        snprintf(eb, sizeof(eb), "[3D] dlopen failed cf=%p ios=%p objc=%p gles=%p\n", h_cf, h_ios, h_objc, h_gles);
        klog_str(eb);
        return 1;
    }

    void *(*objc_getClass)(const char *) = dlsym(h_objc, "objc_getClass");
    void *(*sel_reg)(const char *) = dlsym(h_objc, "sel_registerName");
    void *(*msgSend)(void *, void *, ...) = dlsym(h_objc, "objc_msgSend");
    void *(*CFDictCreate)(void *, const void **, const void **, long, const void *, const void *) =
        dlsym(h_cf, "CFDictionaryCreate");
    void *(*CFNumCreate)(void *, int, const void *) = dlsym(h_cf, "CFNumberCreate");
    void (*CFRel)(void *) = dlsym(h_cf, "CFRelease");
    const void *kCFTypeDictKeyCB = dlsym(h_cf, "kCFTypeDictionaryKeyCallBacks");
    const void *kCFTypeDictValCB = dlsym(h_cf, "kCFTypeDictionaryValueCallBacks");
    void *(*IOSurfCreate)(void *) = dlsym(h_ios, "IOSurfaceCreate");
    int (*IOSurfLock)(void *, uint32_t, uint32_t *) = dlsym(h_ios, "IOSurfaceLock");
    int (*IOSurfUnlock)(void *, uint32_t, uint32_t *) = dlsym(h_ios, "IOSurfaceUnlock");
    void *(*IOSurfBase)(void *) = dlsym(h_ios, "IOSurfaceGetBaseAddress");
    size_t (*IOSurfBPR)(void *) = dlsym(h_ios, "IOSurfaceGetBytesPerRow");
    void **kW   = dlsym(h_ios, "kIOSurfaceWidth");
    void **kH   = dlsym(h_ios, "kIOSurfaceHeight");
    void **kBPE = dlsym(h_ios, "kIOSurfaceBytesPerElement");
    void **kBPR = dlsym(h_ios, "kIOSurfaceBytesPerRow");
    void **kPF  = dlsym(h_ios, "kIOSurfacePixelFormat");

    /* GLES2 entrypoints via OpenGLES.framework (goes through real GLIFunctionDispatch!) */
    typedef uint32_t GLenum, GLuint, GLbitfield;
    typedef int32_t GLint, GLsizei;
    void (*pGenFB)(GLsizei, GLuint *) = dlsym(h_gles, "glGenFramebuffers");
    void (*pBindFB)(GLenum, GLuint) = dlsym(h_gles, "glBindFramebuffer");
    void (*pGenRB)(GLsizei, GLuint *) = dlsym(h_gles, "glGenRenderbuffers");
    void (*pBindRB)(GLenum, GLuint) = dlsym(h_gles, "glBindRenderbuffer");
    void (*pRBStor)(GLenum, GLenum, GLsizei, GLsizei) = dlsym(h_gles, "glRenderbufferStorage");
    void (*pFBRB)(GLenum, GLenum, GLenum, GLuint) = dlsym(h_gles, "glFramebufferRenderbuffer");
    GLenum (*pCheckFB)(GLenum) = dlsym(h_gles, "glCheckFramebufferStatus");
    void (*pDiscardFB)(GLenum, GLsizei, const GLenum *) = dlsym(h_gles, "glDiscardFramebufferEXT");
    void (*pViewport)(GLint, GLint, GLsizei, GLsizei) = dlsym(h_gles, "glViewport");
    void (*pEnable)(GLenum) = dlsym(h_gles, "glEnable");
    void (*pClearColor)(float, float, float, float) = dlsym(h_gles, "glClearColor");
    void (*pClear)(GLbitfield) = dlsym(h_gles, "glClear");
    void (*pFlush)(void) = dlsym(h_gles, "glFlush");
    void (*pFinish)(void) = dlsym(h_gles, "glFinish");
    GLuint (*pCreateSh)(GLenum) = dlsym(h_gles, "glCreateShader");
    void (*pShSrc)(GLuint, GLsizei, const char **, const GLint *) = dlsym(h_gles, "glShaderSource");
    void (*pCompSh)(GLuint) = dlsym(h_gles, "glCompileShader");
    GLuint (*pCreatePr)(void) = dlsym(h_gles, "glCreateProgram");
    void (*pAttSh)(GLuint, GLuint) = dlsym(h_gles, "glAttachShader");
    void (*pBindAttr)(GLuint, GLuint, const char *) = dlsym(h_gles, "glBindAttribLocation");
    void (*pLinkPr)(GLuint) = dlsym(h_gles, "glLinkProgram");
    void (*pUsePr)(GLuint) = dlsym(h_gles, "glUseProgram");
    GLint (*pGetUni)(GLuint, const char *) = dlsym(h_gles, "glGetUniformLocation");
    void (*pUniMat4)(GLint, GLsizei, uint8_t, const float *) = dlsym(h_gles, "glUniformMatrix4fv");
    void (*pUni1i)(GLint, GLint) = dlsym(h_gles, "glUniform1i");
    void (*pGenBuf)(GLsizei, GLuint *) = dlsym(h_gles, "glGenBuffers");
    void (*pBindBuf)(GLenum, GLuint) = dlsym(h_gles, "glBindBuffer");
    void (*pBufData)(GLenum, intptr_t, const void *, GLenum) = dlsym(h_gles, "glBufferData");
    void (*pEnAttr)(GLuint) = dlsym(h_gles, "glEnableVertexAttribArray");
    void (*pAttrPtr)(GLuint, GLint, GLenum, uint8_t, GLsizei, const void *) = dlsym(h_gles, "glVertexAttribPointer");
    void (*pDrawElem)(GLenum, GLsizei, GLenum, const void *) = dlsym(h_gles, "glDrawElements");
    void (*pGenTex)(GLsizei, GLuint *) = dlsym(h_gles, "glGenTextures");
    void (*pBindTex)(GLenum, GLuint) = dlsym(h_gles, "glBindTexture");
    void (*pTexImg2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *) = dlsym(h_gles, "glTexImage2D");
    void (*pTexParam)(GLenum, GLenum, GLint) = dlsym(h_gles, "glTexParameteri");
    void (*pGenMip)(GLenum) = dlsym(h_gles, "glGenerateMipmap");
    const char *(*pGetStr)(GLenum) = dlsym(h_gles, "glGetString");
    GLenum (*pGetErr)(void) = dlsym(h_gles, "glGetError");

    void *cls_eagl = objc_getClass("EAGLContext");
    void *ctx = msgSend(msgSend(cls_eagl, sel_reg("alloc")), sel_reg("initWithAPI:"), 2u);
    if (!ctx) {
        klog_str("[3D] EAGLContext init failed\n");
        return 2;
    }
    msgSend(cls_eagl, sel_reg("setCurrentContext:"), ctx);

    const char *exts = pGetStr ? pGetStr(0x1F03u) : "";
    int has_disc = exts && strstr(exts, "GL_EXT_discard_framebuffer") != NULL;
    int has_msaa = exts && strstr(exts, "GL_APPLE_framebuffer_multisample") != NULL;
    char lbuf[160];
    snprintf(lbuf, sizeof(lbuf), "[3D] EAGL ready disc_ext=%d msaa_ext=%d\n", has_disc, has_msaa);
    klog_str(lbuf);

    /* Create 720x1280 BGRA8888 IOSurface */
    int32_t w = 720, h = 1280, bpe = 4, bpr = 720 * 4, pf = 0x42475241; /* 'BGRA' */
    const void *keys[5] = { *kW, *kH, *kBPE, *kBPR, *kPF };
    const void *vals[5] = {
        CFNumCreate(NULL, 3, &w), CFNumCreate(NULL, 3, &h),
        CFNumCreate(NULL, 3, &bpe), CFNumCreate(NULL, 3, &bpr), CFNumCreate(NULL, 3, &pf)
    };
    void *dict = CFDictCreate(NULL, keys, vals, 5, kCFTypeDictKeyCB, kCFTypeDictValCB);
    void *surf0 = IOSurfCreate(dict);
    void *surf1 = IOSurfCreate(dict);
    CFRel(dict);
    for (int i = 0; i < 5; i++) CFRel((void *)vals[i]);
    if (!surf0 || !surf1) {
        klog_str("[3D] IOSurfaceCreate failed\n");
        return 3;
    }

    /* Compile 3D lit + textured shader */
    const char *vs_src =
        "attribute vec3 aPos;\n"
        "attribute vec3 aNrm;\n"
        "attribute vec2 aUV;\n"
        "uniform mat4 uMVP;\n"
        "uniform mat4 uModel;\n"
        "varying vec2 vUV;\n"
        "varying float vLit;\n"
        "void main() {\n"
        "  vec3 n = normalize((uModel * vec4(aNrm, 0.0)).xyz);\n"
        "  vLit = 0.25 + 0.75 * max(dot(n, normalize(vec3(0.4, 0.7, 0.6))), 0.0);\n"
        "  vUV = aUV;\n"
        "  gl_Position = uMVP * vec4(aPos, 1.0);\n"
        "}\n";
    const char *fs_src =
        "precision mediump float;\n"
        "uniform sampler2D uTex;\n"
        "varying vec2 vUV;\n"
        "varying float vLit;\n"
        "void main() {\n"
        "  vec4 c = texture2D(uTex, vUV);\n"
        "  gl_FragColor = vec4(c.rgb * vLit, 1.0);\n"
        "}\n";
    GLuint vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
    pShSrc(vs, 1, &vs_src, NULL); pCompSh(vs);
    pShSrc(fs, 1, &fs_src, NULL); pCompSh(fs);
    GLuint prog = pCreatePr();
    pAttSh(prog, vs); pAttSh(prog, fs);
    pBindAttr(prog, 0, "aPos"); pBindAttr(prog, 1, "aNrm"); pBindAttr(prog, 2, "aUV");
    pLinkPr(prog); pUsePr(prog);
    GLint uMVP = pGetUni(prog, "uMVP"), uModel = pGetUni(prog, "uModel"), uTex = pGetUni(prog, "uTex");
    pUni1i(uTex, 0);

    /* 256x256 mipmapped checkerboard texture */
    uint32_t *texpix = malloc(256 * 256 * 4);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            texpix[y * 256 + x] = (((x >> 4) ^ (y >> 4)) & 1) ? 0xff40a0f0u : 0xfff08030u;
    GLuint tex = 0;
    pGenTex(1, &tex); pBindTex(0x0DE1u, tex);
    pTexImg2D(0x0DE1u, 0, 0x1908, 256, 256, 0, 0x1908u, 0x1401u, texpix);
    pGenMip(0x0DE1u);
    pTexParam(0x0DE1u, 0x2801u, 0x2703); /* GL_LINEAR_MIPMAP_LINEAR */
    pTexParam(0x0DE1u, 0x2800u, 0x2601); /* GL_LINEAR */
    free(texpix);

    /* 16x16 subdivided 3D sphere mesh: 289 verts, 1536 indices (512 tris) */
    const int SEG = 16;
    const int NV = (SEG + 1) * (SEG + 1);
    const int NI = SEG * SEG * 6;
    float *vdata = malloc((size_t)NV * 8 * sizeof(float));
    uint16_t *idata = malloc((size_t)NI * sizeof(uint16_t));
    for (int j = 0; j <= SEG; j++) {
        float v = (float)j / (float)SEG;
        float phi = v * 3.14159265f;
        float sp = fast_sin(phi), cp = fast_cos(phi);
        for (int i = 0; i <= SEG; i++) {
            float u = (float)i / (float)SEG;
            float th = u * 6.28318531f;
            float nx = sp * fast_cos(th), ny = cp, nz = sp * fast_sin(th);
            float *dst = vdata + (j * (SEG + 1) + i) * 8;
            dst[0] = nx; dst[1] = ny; dst[2] = nz;
            dst[3] = nx; dst[4] = ny; dst[5] = nz;
            dst[6] = u * 4.0f; dst[7] = v * 4.0f;
        }
    }
    int ip = 0;
    for (int j = 0; j < SEG; j++) {
        for (int i = 0; i < SEG; i++) {
            uint16_t a = (uint16_t)(j * (SEG + 1) + i);
            uint16_t b = (uint16_t)(a + 1);
            uint16_t c = (uint16_t)(a + (SEG + 1));
            uint16_t d = (uint16_t)(c + 1);
            idata[ip++] = a; idata[ip++] = c; idata[ip++] = b;
            idata[ip++] = b; idata[ip++] = c; idata[ip++] = d;
        }
    }
    GLuint vbo = 0, ibo = 0;
    pGenBuf(1, &vbo); pBindBuf(0x8892u, vbo);
    pBufData(0x8892u, (intptr_t)(NV * 8 * sizeof(float)), vdata, 0x88E4u);
    pGenBuf(1, &ibo); pBindBuf(0x8893u, ibo);
    pBufData(0x8893u, (intptr_t)(NI * sizeof(uint16_t)), idata, 0x88E4u);

    pEnAttr(0); pEnAttr(1); pEnAttr(2);
    pAttrPtr(0, 3, 0x1406u, 0, 32, (const void *)0);
    pAttrPtr(1, 3, 0x1406u, 0, 32, (const void *)12);
    pAttrPtr(2, 2, 0x1406u, 0, 32, (const void *)24);

    /* Create FBOs: fbo0 (invert=0), fbo1 (invert=1) with shared Z24S8 depth/stencil */
    GLuint fbo[2], crb[2], zsrb = 0;
    pGenFB(2, fbo); pGenRB(2, crb); pGenRB(1, &zsrb);
    pBindRB(0x8D41u, zsrb);
    pRBStor(0x8D41u, 0x88F0u /* GL_DEPTH24_STENCIL8_OES */, w, h);

    void *sel_tex = sel_reg("texImageIOSurface:target:internalFormat:width:height:format:type:plane:invert:");
    void *surfs[2] = { surf0, surf1 };
    for (int k = 0; k < 2; k++) {
        pBindFB(0x8D40u, fbo[k]);
        pBindRB(0x8D41u, crb[k]);
        msgSend(ctx, sel_tex, surfs[k], 0x8D41u, 0x1908u, (uint32_t)w, (uint32_t)h,
                0x80E1u, 0x1401u, 0u, (uint32_t)k);
        pFBRB(0x8D40u, 0x8CE0u, 0x8D41u, crb[k]);
        pFBRB(0x8D40u, 0x8D00u, 0x8D41u, zsrb);
        pFBRB(0x8D40u, 0x8D20u, 0x8D41u, zsrb);
        GLenum st = pCheckFB(0x8D40u);
        if (st != 0x8CD5u) {
            snprintf(lbuf, sizeof(lbuf), "[3D] FBO%d incomplete 0x%x\n", k, (unsigned)st);
            klog_str(lbuf);
        }
    }

    pViewport(0, 0, w, h);
    pEnable(0x0B71u); /* GL_DEPTH_TEST */
    pEnable(0x0B44u); /* GL_CULL_FACE */
    pClearColor(0.08f, 0.12f, 0.18f, 1.0f);

    float proj[16], model[16], mvp[16];
    mat4_persp(proj, (float)w / (float)h, 0.2f, 50.0f);
    const GLenum disc_atts[2] = { 0x8D00u, 0x8D20u }; /* DEPTH, STENCIL */

    const char *names[4] = { "3D_ZC_NODISCARD", "3D_ZC_DISCARD", "3D_INVERT_YFLIP", "3D_DYN_VBO" };
    for (int mode = 0; mode < 4; mode++) {
        int fb_idx = (mode == 2) ? 1 : 0;
        pBindFB(0x8D40u, fbo[fb_idx]);
        /* 3 warmup frames */
        for (int f = 0; f < 3; f++) {
            pClear(0x4500u);
            mat4_model(model, 0.0f, 0.0f, -6.0f, 0.1f * f, 0.2f, 1.0f);
            mat4_mul(mvp, proj, model);
            pUniMat4(uMVP, 1, 0, mvp);
            pUniMat4(uModel, 1, 0, model);
            pDrawElem(0x0004u, NI, 0x1403u, (const void *)0);
            if (mode != 0 && pDiscardFB) pDiscardFB(0x8D40u, 2, disc_atts);
            pFlush();
        }
        pFinish();

        const int NFRAMES = 60;
        uint64_t t0 = mach_absolute_time();
        uint32_t draw_ticks = 0, flush_ticks = 0;
        for (int f = 0; f < NFRAMES; f++) {
            uint64_t fd0 = mach_absolute_time();
            pClear(0x4500u); /* COLOR | DEPTH | STENCIL */
            if (mode == 3) {
                /* Dynamic VBO orphan + upload every frame */
                pBufData(0x8892u, (intptr_t)(NV * 8 * sizeof(float)), vdata, 0x88E8u);
            }
            float ang = (float)f * 0.08f;
            for (int m = 0; m < 20; m++) {
                float tx, ty, tz, sc;
                if (m == 0) {
                    tx = 0.0f; ty = 0.0f; tz = -6.0f; sc = 0.72f;
                } else {
                    int gx = m & 3, gy = m >> 2;
                    tx = ((float)gx - 1.5f) * 1.15f;
                    ty = ((float)gy - 2.0f) * 1.35f;
                    tz = -7.5f - (float)(m & 3) * 0.6f;
                    sc = 0.48f;
                }
                mat4_model(model, tx, ty, tz, ang + (float)m * 0.3f, ang * 0.6f, sc);
                mat4_mul(mvp, proj, model);
                pUniMat4(uMVP, 1, 0, mvp);
                pUniMat4(uModel, 1, 0, model);
                pDrawElem(0x0004u, NI, 0x1403u, (const void *)0);
            }
            if (mode != 0 && pDiscardFB)
                pDiscardFB(0x8D40u, 2, disc_atts);
            uint64_t fd1 = mach_absolute_time();
            pFlush();
            uint64_t fd2 = mach_absolute_time();
            draw_ticks += (uint32_t)(fd1 - fd0);
            flush_ticks += (uint32_t)(fd2 - fd1);
        }
        uint64_t tf0 = mach_absolute_time();
        pFinish();
        uint64_t tf1 = mach_absolute_time();
        uint32_t dt_ticks = (uint32_t)(tf1 - t0);
        uint32_t draw_ms = (uint32_t)((double)draw_ticks / 24000.0);
        uint32_t flush_ms = (uint32_t)((double)flush_ticks / 24000.0);
        uint32_t fin_ms = (uint32_t)((double)(uint32_t)(tf1 - tf0) / 24000.0);
        uint32_t px = 0;
        if (IOSurfLock && IOSurfBase && IOSurfBPR && IOSurfLock(surfs[fb_idx], 1, NULL) == 0) {
            uint8_t *base = (uint8_t *)IOSurfBase(surfs[fb_idx]);
            size_t sbpr = IOSurfBPR(surfs[fb_idx]);
            if (base && sbpr >= (size_t)w * 4u)
                px = *(const uint32_t *)(base + ((size_t)h >> 1) * sbpr + ((size_t)w >> 1) * 4u);
            IOSurfUnlock(surfs[fb_idx], 1, NULL);
        }
        GLenum gerr = pGetErr ? pGetErr() : 0u;
        double ms_f = (double)dt_ticks / 24000.0;
        uint32_t ms = (uint32_t)ms_f;
        uint32_t fps10 = ms_f > 0.1 ? (uint32_t)((double)NFRAMES * 10000.0 / ms_f) : 0u;
        uint32_t msf10 = (uint32_t)(ms_f * 10.0 / (double)NFRAMES);
        uint32_t fps_i = (uint32_t)((double)fps10 * 0.1), fps_d = fps10 - fps_i * 10u;
        uint32_t msf_i = (uint32_t)((double)msf10 * 0.1), msf_d = msf10 - msf_i * 10u;
        snprintf(lbuf, sizeof(lbuf), "[3D] %-16s %df=%ums (%u.%ufps %u.%ums/f) d=%u fl=%u fn=%u px=0x%08x e=0x%x\n",
                 names[mode], NFRAMES, ms, fps_i, fps_d, msf_i, msf_d,
                 draw_ms, flush_ms, fin_ms, px, (unsigned)gerr);
        klog_str(lbuf);
    }

    free(vdata);
    free(idata);
    CFRel(surf0);
    CFRel(surf1);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--bench3d") == 0)
        return run_bench3d();

    uint32_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("SGXDriver543"));
    if (!svc) {
        printf("mali_test: SGXDriver543 not found\n");
        return 1;
    }
    uint32_t conn = 0;
    int kr = IOServiceOpen(svc, mach_task_self_, 0, &conn);
    if (kr != 0) {
        printf("mali_test: IOServiceOpen failed: 0x%x\n", kr);
        IOObjectRelease(svc);
        return 2;
    }

    uint64_t out[4] = {0};
    uint32_t out_cnt = 4;
    kr = IOConnectCallScalarMethod(conn, MALI_CMD_INFO, 0, 0, out, &out_cnt);
    printf("mali_test: INFO kr=0x%x id=0x%x pp=%u work=0x%x size=0x%x\n",
           kr, (unsigned)out[0], (unsigned)out[1], (unsigned)out[2], (unsigned)out[3]);

    uintptr_t work_va = 0;
    size_t work_sz = 0;
    int mr = IOConnectMapMemory(conn, 0, mach_task_self_, &work_va, &work_sz, 1u);
    printf("mali_test: MAP kr=0x%x va=0x%x sz=0x%x\n", mr, (unsigned)work_va, (unsigned)work_sz);

    uint64_t in[5] = { 0x60000000u, 720, 1280, 2880, 0xff2060c0u };
    uint32_t zero = 0;
    int cr = IOConnectCallScalarMethod(conn, MALI_CMD_CLEAR, in, 5, 0, &zero);
    printf("mali_test: CLEAR kr=0x%x\n", cr);

    IOServiceClose(conn);

    void *h = dlopen("/System/Library/Extensions/Mali400GLDriverEX.bundle/Mali400GLDriverEX", 2 /* RTLD_NOW */);
    if (!h) {
        printf("mali_test: dlopen failed: %s\n", dlerror());
    } else {
        int (*create_dev)(void **, uint32_t, void *) = dlsym(h, "gldCreateDevice");
        int (*create_sg)(void *, void **, uint32_t) = dlsym(h, "gldCreateShareGroup");
        int (*create_ctx)(void *, void **, void *, void *, void *, void *) = dlsym(h, "gldCreateContext");
        void (*destroy_ctx)(void *) = dlsym(h, "gldDestroyContext");
        void (*destroy_sg)(void *) = dlsym(h, "gldDestroyShareGroup");
        void *(*get_scr)(void *) = dlsym(h, "mali_gld_get_lima_screen");
        void *(*get_ctx)(void *) = dlsym(h, "mali_gld_get_lima_ctx");
        const char *(*get_str)(void *, uint32_t) = dlsym(h, "gldGetDeviceString");
        void *dev = NULL, *sg = NULL, *ctx = NULL;
        if (create_dev) create_dev(&dev, 1u, NULL);
        if (dev && create_sg) create_sg(dev, &sg, 0u);
        void *scr = (dev && get_scr) ? get_scr(dev) : NULL;
        if (sg && create_ctx) create_ctx(sg, &ctx, NULL, NULL, NULL, NULL);
        void *lctx = (ctx && get_ctx) ? get_ctx(ctx) : NULL;
        const char *ren = (dev && get_str) ? get_str(dev, 0x1F01) : "";
        printf("mali_test: GLD dev=%p lima_screen=%p lima_ctx=%p renderer=%s\n", dev, scr, lctx, ren);
        if (ctx && destroy_ctx) destroy_ctx(ctx);
        if (sg && destroy_sg) destroy_sg(sg);
    }

    IOObjectRelease(svc);
    return (kr || mr || cr) ? 1 : 0;
}
