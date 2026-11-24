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
#define ioservice_attach    ((int   (*)(void *, void *))0x80333c91)
#define ioreg_set_prop_obj  ((int   (*)(void *, const char *, void *))0x8032e261)
#define mach_abs_time       ((uint64_t (*)(void))0x800cb655)
#define iosleep_fn          ((void (*)(uint32_t))0x80328fd1)
#define kthread_start_fn    ((int  (*)(void (*)(void *, int), void *, void **))0x80042c15)

/* IOAudio2Family base methods (Thumb) */
#define ioa2_init           ((int  (*)(void *, void *))0x80b0ed89)
#define ioa2_start          ((int  (*)(void *, void *))0x80b0edfd)
#define ioa2_orig_start_io  ((int  (*)(void *))0x80b0ef21)
#define ioa2_orig_stop_io   ((int  (*)(void *))0x80b0ef6d)

/* Hardware I2S0 / WM1811 primitives from drivers.c */
extern int      exy_drv_set_volume(uint8_t vol);
extern void     exy_i2s0_set_active(int active);
extern uint32_t exy_i2s0_push_frames(const uint32_t *frames, uint32_t count);
extern void     exy_putc(char c);

#define AUDIO_SR            48000u
#define AUDIO_RING_FRAMES   4096u  /* 85.33 ms ring buffer (16 KiB stereo 16-bit PCM) */
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
static void *audio_out_md;
static uint32_t *audio_ring;
static struct ioa2_status *audio_status;
static volatile int audio_running;
static int audio_thread_started;
static uint8_t audio_vol = 75u;
static uint8_t audio_muted = 0u;
static uint64_t audio_seed = 0ull;

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

    for (;;) {
        if (!audio_running || !audio_ring) {
            if (was_running) {
                exy_i2s0_set_active(0);
                was_running = 0;
            }
            iosleep_fn(20u);
            continue;
        }
        if (!was_running) {
            rd_idx = 0u;
            total_frames = 0ull;
            start_host = mach_abs_time();
            if (audio_status) {
                audio_status->seed = audio_seed;
                audio_status->host_time = start_host;
                audio_status->sample_time = 0ull;
                __asm__ volatile("dmb" ::: "memory");
            }
            exy_i2s0_set_active(1);
            was_running = 1;
        }

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
    audio_running = 1;
    (void)exy_drv_set_volume(audio_muted ? 0u : audio_vol);
    return ioa2_orig_start_io(self);
}

int exy_audio_stop_io(void *self)
{
    if (self != audio_dev)
        return ioa2_orig_stop_io(self);
    audio_running = 0;
    exy_i2s0_set_active(0);
    return ioa2_orig_stop_io(self);
}

int exy_audio_set_ctrl(void *self, uint32_t cid, uint32_t val, uint32_t *out_val)
{
    (void)self;
    if (cid == 1u) {
        if (val > 100u) val = 100u;
        audio_vol = (uint8_t)val;
        (void)exy_drv_set_volume(audio_muted ? 0u : audio_vol);
    } else if (cid == 2u) {
        audio_muted = val ? 1u : 0u;
        val = audio_muted;
        (void)exy_drv_set_volume(audio_muted ? 0u : audio_vol);
    }
    if (out_val)
        *out_val = val;
    return 0;
}

int exy_audio_get_out_buf(void *self, uint32_t stream_id, uint32_t *opts, void **out_md)
{
    (void)self;
    if (stream_id != 1u || !audio_out_md || !out_md)
        return (int)0xe00002c2u; /* kIOReturnBadArgument */
    os_ret(audio_out_md);
    if (opts) *opts = 0u;
    *out_md = audio_out_md;
    return 0;
}

static void *make_pcm_format_dict(int with_range)
{
    void *f = osdict_with_cap(12u);
    if (!f) return 0;
    dict_set_u32(f, "sample rate", AUDIO_SR);
    dict_set_u32(f, "format ID", 0x6c70636du);      /* 'lpcm' */
    dict_set_u32(f, "format flags", 0x0000000cu);   /* SignedInteger | Packed */
    dict_set_u32(f, "bytes per packet", 4u);
    dict_set_u32(f, "frames per packet", 1u);
    dict_set_u32(f, "bytes per frame", 4u);
    dict_set_u32(f, "channels per frame", 2u);
    dict_set_u32(f, "bits per channel", 16u);
    if (with_range) {
        dict_set_u32(f, "min sample rate", AUDIO_SR);
        dict_set_u32(f, "max sample rate", AUDIO_SR);
    }
    return f;
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
    /* Allocate 16 KiB shared output PCM ring buffer (4096 stereo 16-bit frames) */
    audio_out_md = iobuf_with_opts(0x00000003u /* kIODirectionOutIn */, AUDIO_RING_BYTES, 4096u);
    if (!audio_out_md) {
        os_rel(dev);
        return;
    }
    audio_ring = (uint32_t *)iobuf_get_bytes(audio_out_md);
    if (audio_ring) {
        for (uint32_t i = 0; i < AUDIO_RING_FRAMES; i++)
            audio_ring[i] = 0u;
    }

    dev_set_str(dev, "device UID", "Exynos4412Audio");
    dev_set_str(dev, "device name", "Built-in Speaker");
    dev_set_str(dev, "device manufacturer", "Samsung");
    dev_set_u32(dev, "transport type", 0x626c746eu); /* 'bltn' */
    dev_set_u32(dev, "clock domain", 1u);
    dev_set_u32(dev, "sample rate", AUDIO_SR);
    dev_set_u32(dev, "io buffer frame size", AUDIO_RING_FRAMES);
    dev_set_u32(dev, "output latency", 256u);
    dev_set_u32(dev, "output safety offset", 128u);

    /* Output stream #1: 48 kHz 16-bit stereo LPCM */
    void *ostreams = osarr_with_cap(1u);
    void *st = osdict_with_cap(8u);
    void *cf = make_pcm_format_dict(0);
    void *af_arr = osarr_with_cap(1u);
    void *af = make_pcm_format_dict(1);
    if (ostreams && st && cf && af_arr && af) {
        dict_set_u32(st, "stream ID", 1u);
        dict_set_u32(st, "starting channel", 1u);
        dict_set_u32(st, "terminal type", 0x73706b72u); /* 'spkr' */
        osdict_set_cstr(st, "current format", cf);
        osarr_set_obj(af_arr, af);
        osdict_set_cstr(st, "available formats", af_arr);
        osarr_set_obj(ostreams, st);
        ioreg_set_prop_obj(dev, "output streams", ostreams);
    }
    os_rel(af); os_rel(af_arr); os_rel(cf); os_rel(st); os_rel(ostreams);

    /* Controls: #1 Master Output Volume (0..100), #2 Master Output Mute (0/1) */
    void *ctrls = osarr_with_cap(2u);
    void *cvol = osdict_with_cap(12u);
    void *cmut = osdict_with_cap(8u);
    if (ctrls && cvol && cmut) {
        dict_set_u32(cvol, "control ID", 1u);
        dict_set_u32(cvol, "base class", 0x6c65766cu); /* 'levl' */
        dict_set_u32(cvol, "class",      0x766c6d65u); /* 'vlme' */
        dict_set_u32(cvol, "scope",      0x6f757470u); /* 'outp' */
        dict_set_u32(cvol, "element",    0u);
        dict_set_u32(cvol, "value",      audio_vol);
        dict_set_u32(cvol, "minimum value", 0u);
        dict_set_u32(cvol, "maximum value", 100u);
        dict_set_u32(cvol, "start int value", 0u);
        dict_set_u32(cvol, "start db value", (uint32_t)(-57 << 16));
        dict_set_u32(cvol, "integer steps", 100u);
        dict_set_u32(cvol, "db per step", (57u << 16) / 100u);
        osarr_set_obj(ctrls, cvol);

        dict_set_u32(cmut, "control ID", 2u);
        dict_set_u32(cmut, "base class", 0x746f676cu); /* 'togl' */
        dict_set_u32(cmut, "class",      0x6d757465u); /* 'mute' */
        dict_set_u32(cmut, "scope",      0x6f757470u); /* 'outp' */
        dict_set_u32(cmut, "element",    0u);
        dict_set_u32(cmut, "value",      0u);
        osarr_set_obj(ctrls, cmut);

        ioreg_set_prop_obj(dev, "controls", ctrls);
    }
    os_rel(cmut); os_rel(cvol); os_rel(ctrls);

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
        }
    }
}
