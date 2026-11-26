/*
 * Native IOAudio2Device provider for iOS 9.3.5 mediaserverd (HALS_IOA2Device),
 * streaming real-time 48 kHz 16-bit stereo PCM to Exynos 4412 I2S0 + WM1811.
 *
 * Built into DRV_AREA alongside disp.c/gpu.c/touch.c/drivers.c.
 */
#include <stdint.h>

/* XNU / IOKit kernelcache.n94.macho Thumb entry points */
#define osmeta_alloc_name   ((void *(*)(const char *))0x8030656d)
#define osdict_with_cap     ((void *(*)(uint32_t))0x8030a5ed)
#define osdict_set_cstr     ((int   (*)(void *, const char *, void *))0x8030ad55)
#define osarr_with_cap      ((void *(*)(uint32_t))0x80307541)
#define osarr_set_obj       ((int   (*)(void *, void *))0x8030781d)
#define osnum_with_u64      ((void *(*)(uint64_t, uint32_t))0x80316e25)
#define osstr_with_cstr     ((void *(*)(const char *))0x8031a9a1)
#define iobuf_with_opts     ((void *(*)(uint32_t, uint32_t, uint32_t))0x803532cd)
#define iobuf_get_bytes     ((void *(*)(void *))0x803537c5)
#define ioservice_attach    ((int   (*)(void *, void *))0x803326dd)
#define ioreg_set_prop_obj  ((int   (*)(void *, const char *, void *))0x8032e26d)
#define iowl_create         ((void *(*)(void))0x8034ce61)
#define mach_abs_time       ((uint64_t (*)(void))0x800cb655)
#define iosleep_fn          ((void (*)(uint32_t))0x80328fd1)
#define kthread_start_fn    ((int  (*)(void (*)(void *, int), void *, void **))0x80042c15)
#define audio_selfpid       ((int  (*)(void))0x802a72c9)

/* IOAudio2Family base methods (Thumb) */
#define ioa2_init           ((int  (*)(void *, void *))0x80b0ed89)
#define ioa2_start          ((int  (*)(void *, void *))0x80b0edfd)
#define ioa2_orig_start_io  ((int  (*)(void *))0x80b0ef21)
#define ioa2_orig_stop_io   ((int  (*)(void *))0x80b0ef6d)
#define ioa2_orig_get_wl    ((void *(*)(void *))0x80333951)

/* Hardware I2S0 / WM1811 primitives from drivers.c */
extern int      exy_drv_set_volume(uint8_t vol);
extern void     exy_i2s0_set_active(int active);
extern uint32_t exy_i2s0_push_frames(const uint32_t *frames, uint32_t count);
extern void     exy_putc(char c);
static void a_puts(const char *s);
static void a_putx(uint32_t v);

#define AUDIO_SR            48000u
#define AUDIO_RING_FRAMES   16384u /* 341.33 ms ring buffer (64 KiB, matches N94 AppleARMDMAAudioDevice) */
#define AUDIO_RING_BYTES    (AUDIO_RING_FRAMES * 4u)
#define TICKS_PER_FRAME     500ull /* 24,000,000 Hz MCT / 48,000 Hz = 500 ticks/frame exact */

/* Matches HALS_IOA2Engine::GetZeroTimeStamp lockless read layout (40 bytes at status MD offset 0) */
struct ioa2_status {
    volatile uint64_t sample_time;
    volatile uint64_t host_time;
    volatile uint64_t seed;
    volatile uint64_t reserved[2];
};

static void *audio_dev;
static void *audio_wl;
static void *audio_out_md;
static void *audio_in_md;
static uint32_t *audio_ring;
static struct ioa2_status *audio_status;
static volatile int audio_running;
static int audio_thread_started;
static uint8_t audio_vol = 75u;
static uint8_t audio_muted = 0u;
static uint64_t audio_seed = 0ull;
static volatile uint64_t audio_start_host = 0ull;
static volatile uint32_t audio_ms_pid = 0u;

static void os_rel(void *obj)
{
    if (!obj) return;
    void (*rel)(void *) = *(void (**)(void *))(*(uintptr_t *)obj + 0x14u);
    rel(obj);
}

static void os_ret(void *obj)
{
    if (!obj) return;
    void (*ret)(void *) = *(void (**)(void *))(*(uintptr_t *)obj + 0x10u);
    ret(obj);
}

static void dict_set_u32(void *d, const char *k, uint32_t v)
{
    void *n = osnum_with_u64((uint64_t)v, 32u);
    if (n) { osdict_set_cstr(d, k, n); os_rel(n); }
}

static void dict_set_u64(void *d, const char *k, uint64_t v)
{
    void *n = osnum_with_u64(v, 64u);
    if (n) { osdict_set_cstr(d, k, n); os_rel(n); }
}

static void dev_set_u32(void *dev, const char *k, uint32_t v)
{
    void *n = osnum_with_u64((uint64_t)v, 32u);
    if (n) { ioreg_set_prop_obj(dev, k, n); os_rel(n); }
}

static void dev_set_str(void *dev, const char *k, const char *s)
{
    void *o = osstr_with_cstr(s);
    if (o) { ioreg_set_prop_obj(dev, k, o); os_rel(o); }
}

static void audio_pump_thread(void *param, int wait_res)
{
    (void)param; (void)wait_res;
    uint32_t rd_idx = 0u;
    uint64_t total_frames = 0ull;
    uint64_t start_host = 0ull;
    int was_running = 0;
    uint64_t last_seed = 0ull;

    for (;;) {
        if (!audio_running || !audio_ring) {
            if (was_running) {
                exy_i2s0_set_active(0);
                was_running = 0;
            }
            iosleep_fn(20u);
            continue;
        }
        uint64_t cur_seed = audio_seed;
        if (!was_running || cur_seed != last_seed) {
            rd_idx = 0u;
            total_frames = 0ull;
            start_host = audio_start_host;
            if (!start_host) start_host = mach_abs_time();
            last_seed = cur_seed;
            exy_i2s0_set_active(1);
            was_running = 1;
        }

        __asm__ volatile("dmb" ::: "memory");
        uint64_t now = mach_abs_time();
        uint64_t target_frames = (now >= start_host) ? ((now - start_host) / TICKS_PER_FRAME) : total_frames;
        /* Keep up to 64 frames buffered in I2S0 TX FIFO */
        uint64_t want = target_frames + 48ull;
        if (total_frames + AUDIO_RING_FRAMES < want) {
            /* Resync if thread was delayed >85ms */
            uint64_t skip = want - total_frames;
            rd_idx = (rd_idx + (uint32_t)(skip % AUDIO_RING_FRAMES)) % AUDIO_RING_FRAMES;
            total_frames = want;
        }
        int pushed_any = 0;
        while (audio_running && total_frames < want) {
            uint32_t todo = (uint32_t)(want - total_frames);
            uint32_t contig = AUDIO_RING_FRAMES - rd_idx;
            if (todo > contig) todo = contig;
            if (todo > 64u) todo = 64u;
            uint32_t n = exy_i2s0_push_frames(&audio_ring[rd_idx], todo);
            if (!n)
                break;
            pushed_any = 1;
            rd_idx += n;
            total_frames += n;
            if (rd_idx >= AUDIO_RING_FRAMES) {
                rd_idx -= AUDIO_RING_FRAMES;
                if (audio_status) {
                    /* Publish hardware zero-timestamp on ring wrap (lockless consistent triple) */
                    uint64_t z_sample = (total_frames / AUDIO_RING_FRAMES) * AUDIO_RING_FRAMES;
                    uint64_t z_host   = start_host + z_sample * TICKS_PER_FRAME;
                    __asm__ volatile("dmb" ::: "memory");
                    audio_status->sample_time = z_sample;
                    audio_status->host_time   = z_host;
                    __asm__ volatile("dmb" ::: "memory");
                }
            }
        }
        if (!pushed_any)
            iosleep_fn(1u);
    }
}

/* --- IOAudio2Device vtable hooks (installed in mkxnu.py) ------------------- */

void *exy_audio_get_wl(void *self)
{
    if (self == audio_dev && audio_wl)
        return audio_wl;
    return ioa2_orig_get_wl(self);
}

int exy_audio_start_io(void *self)
{
    if (self != audio_dev)
        return ioa2_orig_start_io(self);
    if (!audio_thread_started) {
        void *th = 0;
        if (kthread_start_fn(audio_pump_thread, 0, &th) == 0)
            audio_thread_started = 1;
    }
    audio_seed++;
    uint64_t t0 = mach_abs_time();
    audio_start_host = t0;
    /* IOAudio2Device::startIO wrapper (0x80b0efd8) returns the 64-bit start host_time from dev+0x60. */
    *(volatile uint64_t *)((uint8_t *)self + 0x60u) = t0;
    if (audio_status) {
        audio_status->sample_time = 0ull;
        audio_status->host_time = t0;
        audio_status->seed = audio_seed;
        audio_status->reserved[0] = 0ull;
        audio_status->reserved[1] = 0ull;
        __asm__ volatile("dmb" ::: "memory");
    }
    audio_running = 1;
    a_puts("[AUDIO] start vol=0x"); a_putx(audio_muted ? 0u : audio_vol); a_puts("\n");
    (void)exy_drv_set_volume(audio_muted ? 0u : audio_vol);
    return ioa2_orig_start_io(self);
}

int exy_audio_stop_io(void *self)
{
    if (self != audio_dev)
        return ioa2_orig_stop_io(self);
    audio_running = 0;
    exy_i2s0_set_active(0);
    a_puts("[AUDIO] stop\n");
    return ioa2_orig_stop_io(self);
}

int exy_audio_set_ctrl(void *self, uint32_t cid, uint32_t val, uint32_t *out_val)
{
    (void)self;
    a_puts("[AUDIO] ctrl "); a_putx(cid); a_puts(" "); a_putx(val); a_puts("\n");
    if (cid == 313u || cid == 305u) {
        if (val > 100u) val = 100u;
        audio_vol = (uint8_t)val;
        (void)exy_drv_set_volume(audio_muted ? 0u : audio_vol);
    } else if (cid == 314u || cid == 303u || cid == 304u) {
        audio_muted = val ? 1u : 0u;
        val = audio_muted;
        (void)exy_drv_set_volume(audio_muted ? 0u : audio_vol);
    }
    if (out_val)
        *out_val = val;
    return 0;
}

int exy_audio_set_mctrl(void *self, const uint32_t *in, uint32_t *out, uint32_t in_sz, uint32_t *out_sz)
{
    (void)self;
    a_puts("[AUDIO] mctrl sz="); a_putx(in_sz);
    if (in && in_sz >= 8u) { a_puts(" id="); a_putx(in[0]); a_puts(" v="); a_putx(in[1]); }
    a_puts("\n");
    if (out && out_sz && in && in_sz <= *out_sz) {
        const uint8_t *s = (const uint8_t *)in;
        uint8_t *d = (uint8_t *)out;
        for (uint32_t i = 0; i < in_sz; i++) d[i] = s[i];
        *out_sz = in_sz;
    }
    return 0;
}

int exy_audio_change_sr(void *self, const uint32_t *sr, uint32_t sz)
{
    (void)self;
    a_puts("[AUDIO] chg_sr sz="); a_putx(sz);
    if (sr && sz >= 8u) { a_puts(" lo="); a_putx(sr[0]); a_puts(" hi="); a_putx(sr[1]); }
    a_puts("\n");
    return 0;
}

int exy_audio_change_fmt(void *self, uint32_t st, const uint32_t *fmt, uint32_t sz)
{
    (void)self; (void)fmt;
    a_puts("[AUDIO] chg_fmt st="); a_putx(st); a_puts(" sz="); a_putx(sz); a_puts("\n");
    return 0;
}

int exy_audio_set_active(void *self, uint32_t st, uint32_t active)
{
    (void)self;
    a_puts("[AUDIO] active st="); a_putx(st); a_puts(" on="); a_putx(active); a_puts("\n");
    return 0;
}

int exy_audio_m7(void *self, uint32_t a, uint32_t b, uint32_t c)
{
    (void)self;
    a_puts("[AUDIO] m7 "); a_putx(a); a_puts(" "); a_putx(b); a_puts(" "); a_putx(c); a_puts("\n");
    return 0;
}

int exy_audio_m12(void *self, uint32_t a, uint32_t b)
{
    (void)self;
    a_puts("[AUDIO] m12 "); a_putx(a); a_puts(" "); a_putx(b); a_puts("\n");
    return 0;
}

int exy_audio_get_out_buf(void *self, uint32_t stream_id, uint32_t *opts, void **out_md)
{
    (void)self;
    audio_ms_pid = (uint32_t)audio_selfpid();
    a_puts("[AUDIO] get_buf st="); a_putx(stream_id);
    a_puts(" pid="); a_putx(audio_ms_pid); a_puts("\n");
    void *md = (stream_id == 1u) ? audio_out_md : (stream_id == 2u ? audio_in_md : 0);
    if (!md || !out_md)
        return (int)0xe00002c2u; /* kIOReturnBadArgument */
    os_ret(md);
    if (opts) *opts = 0u;
    *out_md = md;
    return 0;
}

int exy_audio_get_in_buf(void *self, uint32_t stream_id, uint32_t *opts, void **out_md)
{
    (void)self;
    a_puts("[AUDIO] get_in st="); a_putx(stream_id); a_puts("\n");
    if (stream_id != 2u || !audio_in_md || !out_md)
        return (int)0xe00002c2u;
    os_ret(audio_in_md);
    if (opts) *opts = 0u;
    *out_md = audio_in_md;
    return 0;
}

int exy_audio_get_ctrl_buf(void *self, uint32_t cid, uint32_t *opts, void **out_md)
{
    (void)self; (void)opts; (void)out_md;
    a_puts("[AUDIO] get_cbuf id="); a_putx(cid); a_puts("\n");
    return (int)0xe00002f0u;
}

static void *make_pcm_format_dict(uint32_t sr, int with_range)
{
    void *f = osdict_with_cap(12u);
    if (!f) return 0;
    /* IOAudio2Family / CoreAudio HALS_IOA2Stream encode all sample rates as 64-bit Q32.32 fixed-point. */
    dict_set_u64(f, "sample rate", (uint64_t)sr << 32);
    dict_set_u32(f, "format ID", 0x6c70636du);      /* 'lpcm' */
    dict_set_u32(f, "format flags", 0x0000000cu);   /* SignedInteger | Packed */
    dict_set_u32(f, "bytes per packet", 4u);
    dict_set_u32(f, "frames per packet", 1u);
    dict_set_u32(f, "bytes per frame", 4u);
    dict_set_u32(f, "channels per frame", 2u);
    dict_set_u32(f, "bits per channel", 16u);
    if (with_range) {
        dict_set_u64(f, "min sample rate", (uint64_t)sr << 32);
        dict_set_u64(f, "max sample rate", (uint64_t)sr << 32);
    }
    return f;
}

static void add_vol_ctrl(void *ctrls, uint32_t cid, uint32_t scope, uint32_t elem, uint32_t val)
{
    void *c = osdict_with_cap(12u);
    void *rmap = osarr_with_cap(1u);
    void *rent = osdict_with_cap(4u);
    if (c && rmap && rent) {
        dict_set_u32(c, "control ID", cid);
        dict_set_u32(c, "base class", 0x6c65766cu); /* 'levl' */
        dict_set_u32(c, "class",      0x766c6d65u); /* 'vlme' */
        dict_set_u32(c, "scope",      scope);
        dict_set_u32(c, "element",    elem);
        dict_set_u32(c, "value",      val);
        dict_set_u32(c, "minimum value", 0u);
        dict_set_u32(c, "maximum value", 100u);
        dict_set_u32(rent, "start int value", 0u);
        dict_set_u64(rent, "start db value",  (uint64_t)((int64_t)-57 << 32));
        dict_set_u32(rent, "integer steps",   100u);
        dict_set_u64(rent, "db per step",     (57ull << 32) / 100ull);
        osarr_set_obj(rmap, rent);
        osdict_set_cstr(c, "range map", rmap);
        osarr_set_obj(ctrls, c);
    }
    os_rel(rent); os_rel(rmap); os_rel(c);
}

static void add_bool_ctrl(void *ctrls, uint32_t cid, uint32_t cls, uint32_t scope, uint32_t elem, uint32_t val)
{
    void *c = osdict_with_cap(8u);
    if (c) {
        dict_set_u32(c, "control ID", cid);
        dict_set_u32(c, "base class", 0x746f676cu); /* 'togl' */
        dict_set_u32(c, "class",      cls);
        dict_set_u32(c, "scope",      scope);
        dict_set_u32(c, "element",    elem);
        dict_set_u32(c, "value",      val);
        osarr_set_obj(ctrls, c);
    }
    os_rel(c);
}

static void add_sel_item(void *sels, uint32_t val, const char *name)
{
    void *it = osdict_with_cap(4u);
    void *ns = osstr_with_cstr(name);
    if (it && ns) {
        dict_set_u32(it, "value", val);
        osdict_set_cstr(it, "name", ns);
        dict_set_u32(it, "kind", 0u);
        osarr_set_obj(sels, it);
    }
    os_rel(ns); os_rel(it);
}

static void add_sel_ctrl(void *ctrls, uint32_t cid, uint32_t cls, uint32_t scope, uint32_t elem, uint32_t val, void *sels)
{
    void *c = osdict_with_cap(8u);
    if (c && sels) {
        dict_set_u32(c, "control ID", cid);
        dict_set_u32(c, "base class", 0x736c6374u); /* 'slct' */
        dict_set_u32(c, "class",      cls);
        dict_set_u32(c, "scope",      scope);
        dict_set_u32(c, "element",    elem);
        dict_set_u32(c, "value",      val);
        osdict_set_cstr(c, "selectors", sels);
        osarr_set_obj(ctrls, c);
    }
    os_rel(c);
}

static void set_stream_prop(void *dev, const char *prop_key, uint32_t sid, uint32_t term_type)
{
    static const uint32_t kRates[] = { 48000u };
    void *arr = osarr_with_cap(1u);
    void *st = osdict_with_cap(8u);
    void *cf = make_pcm_format_dict(AUDIO_SR, 0);
    void *af_arr = osarr_with_cap(1u);
    if (arr && st && cf && af_arr) {
        dict_set_u32(st, "stream ID", sid);
        dict_set_u32(st, "starting channel", 1u);
        dict_set_u32(st, "terminal type", term_type);
        osdict_set_cstr(st, "current format", cf);
        for (unsigned i = 0; i < sizeof(kRates) / sizeof(kRates[0]); i++) {
            void *af = make_pcm_format_dict(kRates[i], 1);
            if (af) { osarr_set_obj(af_arr, af); os_rel(af); }
        }
        osdict_set_cstr(st, "available formats", af_arr);
        osarr_set_obj(arr, st);
        ioreg_set_prop_obj(dev, prop_key, arr);
    }
    os_rel(af_arr); os_rel(cf); os_rel(st); os_rel(arr);
}

static void a_puts(const char *s)
{
    while (*s) exy_putc(*s++);
}

static void a_putx(uint32_t v)
{
    static const char h[] = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4)
        exy_putc(h[(v >> i) & 0xfu]);
}

void exy_audio_register(void *parent_service)
{
    if (audio_dev || !parent_service)
        return;
    void *dev = osmeta_alloc_name("IOAudio2Device");
    if (!dev)
        return;
    if (!ioa2_init(dev, 0)) {
        os_rel(dev);
        return;
    }
    /* Allocate 64 KiB kernel-user shared output and silent input PCM ring buffers */
    audio_out_md = iobuf_with_opts(0x00010003u /* kIOMemoryKernelUserShared | OutIn */, AUDIO_RING_BYTES, 4096u);
    audio_in_md  = iobuf_with_opts(0x00010003u /* kIOMemoryKernelUserShared | OutIn */, AUDIO_RING_BYTES, 4096u);
    if (!audio_out_md || !audio_in_md) {
        os_rel(dev);
        return;
    }
    audio_ring = (uint32_t *)iobuf_get_bytes(audio_out_md);
    if (audio_ring) {
        for (uint32_t i = 0; i < AUDIO_RING_FRAMES; i++)
            audio_ring[i] = 0u;
    }
    uint32_t *in_ring = (uint32_t *)iobuf_get_bytes(audio_in_md);
    if (in_ring) {
        for (uint32_t i = 0; i < AUDIO_RING_FRAMES; i++)
            in_ring[i] = 0u;
    }

    dev_set_str(dev, "device UID", "Codec");
    dev_set_str(dev, "device name", "Built-In Speaker");
    dev_set_str(dev, "device manufacturer", "Samsung");
    dev_set_u32(dev, "transport type", 0x626c746eu); /* 'bltn' */
    dev_set_u32(dev, "clock domain", 1u);
    {
        void *n = osnum_with_u64((uint64_t)AUDIO_SR << 32, 64u);
        if (n) { ioreg_set_prop_obj(dev, "sample rate", n); os_rel(n); }
    }
    dev_set_u32(dev, "io buffer frame size", AUDIO_RING_FRAMES);
    dev_set_u32(dev, "output latency", 256u);
    dev_set_u32(dev, "output safety offset", 128u);
    dev_set_u32(dev, "input latency", 256u);
    dev_set_u32(dev, "input safety offset", 128u);
    dev_set_u32(dev, "MaxOutputChannelCount", 2u);
    dev_set_u32(dev, "MaxInputChannelCount", 2u);

    set_stream_prop(dev, "output streams", 1u, 0x73706b72u /* 'spkr' */);
    set_stream_prop(dev, "input streams",  2u, 0x696d6963u /* 'imic' */);

    /* Controls matching AppleEmbeddedAudioDevice (N94 Codec) layout */
    void *ctrls = osarr_with_cap(24u);
    if (ctrls) {
        const uint32_t OUTP = 0x6f757470u, INPT = 0x696e7074u, GLOB = 0x676c6f62u;
        const uint32_t MUTE = 0x6d757465u, DMUT = 0x646d7574u, JACK = 0x6a61636bu;
        const uint32_t DSRC = 0x64737263u, CLCK = 0x636c636bu;
        /* Master output + per-port elements (1=Receiver, 2=Speaker, 3=Headphones, 4=LineOut) */
        add_vol_ctrl(ctrls, 305u, OUTP, 0u, audio_vol);
        add_bool_ctrl(ctrls, 303u, MUTE, OUTP, 0u, 0u);
        add_bool_ctrl(ctrls, 304u, DMUT, OUTP, 0u, 0u);
        add_vol_ctrl(ctrls, 306u, OUTP, 1u, audio_vol);
        add_bool_ctrl(ctrls, 307u, MUTE, OUTP, 1u, 0u);
        add_vol_ctrl(ctrls, 308u, OUTP, 2u, audio_vol);
        add_bool_ctrl(ctrls, 309u, MUTE, OUTP, 2u, 0u);
        add_vol_ctrl(ctrls, 311u, OUTP, 3u, audio_vol);
        add_bool_ctrl(ctrls, 312u, MUTE, OUTP, 3u, 0u);
        add_vol_ctrl(ctrls, 313u, OUTP, 4u, audio_vol);
        add_bool_ctrl(ctrls, 314u, MUTE, OUTP, 4u, 0u);

        void *osels = osarr_with_cap(2u);
        if (osels) {
            add_sel_item(osels, 0x73706b72u /* 'spkr' */, "Speaker");
            add_sel_item(osels, 0x72637672u /* 'rcvr' */, "Receiver");
            add_sel_ctrl(ctrls, 316u, DSRC, OUTP, 0u, 0x73706b72u, osels);
            os_rel(osels);
        }
        void *ssels = osarr_with_cap(1u);
        if (ssels) {
            add_sel_item(ssels, 0x73706b72u /* 'spkr' */, "Speaker");
            add_sel_ctrl(ctrls, 310u, DSRC, OUTP, 2u, 0x73706b72u, ssels);
            os_rel(ssels);
        }

        /* Input controls (Built-In Microphone + Jack=0) */
        add_vol_ctrl(ctrls, 368u, INPT, 0u, 80u);
        add_bool_ctrl(ctrls, 367u, MUTE, INPT, 0u, 0u);
        add_bool_ctrl(ctrls, 301u, JACK, INPT, 0u, 0u);
        void *isels = osarr_with_cap(1u);
        if (isels) {
            add_sel_item(isels, 0x696d6963u /* 'imic' */, "Internal microphone");
            add_sel_ctrl(ctrls, 369u, DSRC, INPT, 0u, 0x696d6963u, isels);
            os_rel(isels);
        }

        /* Global clock source (I2S Master / Slave) */
        void *csels = osarr_with_cap(2u);
        if (csels) {
            add_sel_item(csels, 0x6932734du /* 'i2sM' */, "I2S Master");
            add_sel_item(csels, 0x69327353u /* 'i2sS' */, "I2S Slave");
            add_sel_ctrl(ctrls, 302u, CLCK, GLOB, 0u, 0x6932734du, csels);
            os_rel(csels);
        }

        ioreg_set_prop_obj(dev, "controls", ctrls);
        os_rel(ctrls);
    }

    audio_wl = iowl_create();
    audio_dev = dev;
    if (ioservice_attach(dev, parent_service)) {
        if (ioa2_start(dev, parent_service)) {
            /* Grab status IOBufferMemoryDescriptor allocated by IOAudio2Device::start at +0x58 */
            void *smd = *(void **)((uint8_t *)dev + 0x58u);
            if (smd) {
                audio_status = (struct ioa2_status *)iobuf_get_bytes(smd);
                if (audio_status) {
                    audio_status->sample_time = 0ull;
                    audio_status->host_time = mach_abs_time();
                    audio_status->seed = 1ull;
                }
            }
            a_puts("[AUDIO] IOAudio2Device dev=0x"); a_putx((uint32_t)(uintptr_t)dev);
            a_puts(" st=0x"); a_putx((uint32_t)(uintptr_t)audio_status);
            a_puts(" ring=0x"); a_putx((uint32_t)(uintptr_t)audio_ring);
            a_puts("\n");
        }
    }
}
