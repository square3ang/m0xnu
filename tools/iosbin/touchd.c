/*
 * ExyTouchd: userspace HID bridge for Galaxy S3 (Exynos 4412) Melfas MMS114
 * touchscreen + physical GPIO buttons (Home, Power, Vol+, Vol-).
 *
 * Polls kernel touch.c via IOAcceleratorES externalMethod selector 5 and
 * dispatches native multi-touch & hardware button events directly into
 * backboardd via IOHIDEventSystemClientDispatchEvent (plus registers a
 * kernel IOHIDUserDevice in IORegistry).
 */
#include <stdint.h>
#include <stddef.h>

typedef uint32_t mach_port_t;
typedef uint32_t io_service_t;
typedef uint32_t io_connect_t;
typedef int32_t kern_return_t;
typedef const void *CFTypeRef;
typedef void *CFMutableDictionaryRef;

extern mach_port_t mach_task_self_;
extern mach_port_t bootstrap_port;
extern mach_port_t mach_thread_self(void);
extern kern_return_t thread_policy_set(mach_port_t thread, uint32_t flavor, const int32_t *policy, uint32_t count);
extern int setpriority(int which, uint32_t who, int prio);
extern kern_return_t bootstrap_look_up(mach_port_t bp, const char *service_name, mach_port_t *sp);
extern mach_port_t kIOMasterPortDefault;
extern uint64_t mach_absolute_time(void);
extern void *IOServiceMatching(const char *name);
extern io_service_t IOServiceGetMatchingService(mach_port_t master, void *matching);
extern kern_return_t IOObjectRelease(io_service_t obj);
extern kern_return_t IORegistryEntryGetRegistryEntryID(io_service_t entry, uint64_t *entryID);
extern kern_return_t IOServiceOpen(io_service_t svc, mach_port_t task, uint32_t type, io_connect_t *conn);
extern kern_return_t IOConnectCallStructMethod(io_connect_t conn, uint32_t sel,
    const void *in, size_t in_sz, void *out, size_t *out_sz);
extern kern_return_t IOConnectCallScalarMethod(io_connect_t conn, uint32_t sel,
    const uint64_t *in, uint32_t in_cnt, uint64_t *out, uint32_t *out_cnt);

extern void *IOHIDUserDeviceCreate(CFTypeRef allocator, CFTypeRef properties);
extern kern_return_t IOHIDUserDeviceHandleReport(void *dev, const uint8_t *report, long len);

extern void *IOHIDEventSystemClientCreate(CFTypeRef allocator);
extern void *IOHIDEventSystemClientCreateWithType(CFTypeRef allocator, int type, CFTypeRef attrs);
extern void IOHIDEventSystemClientDispatchEvent(void *client, CFTypeRef event);
extern CFTypeRef IOHIDEventSystemClientCopyServices(void *client);
extern CFTypeRef IOHIDServiceClientGetRegistryID(CFTypeRef service);
extern long CFArrayGetCount(CFTypeRef arr);
extern const void *CFArrayGetValueAtIndex(CFTypeRef arr, long idx);
extern int CFNumberGetValue(CFTypeRef num, long type, void *val);
extern CFTypeRef IOHIDEventCreateDigitizerEvent(
    CFTypeRef alloc, uint64_t ts, uint32_t type, uint32_t index, uint32_t identity,
    uint32_t eventMask, uint32_t buttonMask, float x, float y, float z,
    float tipPressure, float barrelPressure, uint32_t range, uint32_t touch, uint32_t options);
extern CFTypeRef IOHIDEventCreateDigitizerFingerEvent(
    CFTypeRef alloc, uint64_t ts, uint32_t index, uint32_t identity,
    uint32_t eventMask, float x, float y, float z,
    float tipPressure, float twist, uint32_t range, uint32_t touch, uint32_t options);
extern CFTypeRef IOHIDEventCreateKeyboardEvent(
    CFTypeRef alloc, uint64_t ts, uint32_t usagePage, uint32_t usage,
    uint32_t down, uint32_t options);
extern void IOHIDEventAppendEvent(CFTypeRef parent, CFTypeRef child, uint32_t options);
extern void IOHIDEventSetIntegerValue(CFTypeRef event, uint32_t field, long value);
extern void IOHIDEventSetSenderID(CFTypeRef event, uint64_t senderID);

extern const CFTypeRef kCFBooleanTrue;
extern const void *kCFTypeDictionaryKeyCallBacks;
extern const void *kCFTypeDictionaryValueCallBacks;
extern CFMutableDictionaryRef CFDictionaryCreateMutable(CFTypeRef alloc, long cap, const void *kcb, const void *vcb);
extern void CFDictionarySetValue(CFMutableDictionaryRef dict, const void *k, const void *v);
extern CFTypeRef CFDataCreate(CFTypeRef alloc, const uint8_t *bytes, long len);
extern CFTypeRef CFStringCreateWithCString(CFTypeRef alloc, const char *cstr, uint32_t enc);
extern CFTypeRef CFNumberCreate(CFTypeRef alloc, long type, const void *val);
extern void CFRelease(CFTypeRef cf);

extern int open(const char *p, int flags, ...);
extern long write(int fd, const void *buf, size_t n);
extern int usleep(uint32_t us);
extern int snprintf(char *buf, size_t sz, const char *fmt, ...);
extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *symbol);

#define kCFStringEncodingUTF8 0x08000100u
#define kCFNumberSInt32Type   3L
#define MALI_CMD_POLL_TOUCH   5u
#define TOUCH_MAX_FINGERS     5u

#define kIOHIDDigitizerTransducerTypeHand          3u
#define kIOHIDDigitizerEventRange                  (1u << 0)
#define kIOHIDDigitizerEventTouch                  (1u << 1)
#define kIOHIDDigitizerEventPosition               (1u << 2)
#define kIOHIDDigitizerEventIdentity               (1u << 5)
#define kIOHIDEventFieldDigitizerCollection        0x000b0016u
#define kIOHIDEventFieldDigitizerChildEventMask    0x000b0018u
#define kIOHIDEventFieldDigitizerIsDisplayIntegrated 0x000b0019u
#define kExyTouchSenderID                          0x8000000817319372ULL

struct exy_finger {
    uint8_t id;
    uint8_t down;
    uint16_t x;
    uint16_t y;
    uint16_t pad;
};

struct exy_touch_state {
    uint32_t seq;
    uint32_t chip;
    uint32_t buttons;
    uint32_t count;
    struct exy_finger f[TOUCH_MAX_FINGERS];
};

#define FINGER_DESC \
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, \
    0x09, 0x42, 0x09, 0x32, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, \
    0x75, 0x06, 0x95, 0x01, 0x81, 0x03, \
    0x09, 0x51, 0x75, 0x08, 0x95, 0x01, 0x25, 0x7F, 0x81, 0x02, \
    0x05, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26, 0xCF, 0x02, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, \
    0x09, 0x31, 0x26, 0xFF, 0x04, 0x81, 0x02, \
    0xC0

static const uint8_t kHidDesc[] = {
    0x05, 0x0D, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x01,
    FINGER_DESC, FINGER_DESC, FINGER_DESC, FINGER_DESC, FINGER_DESC,
    0x05, 0x0D, 0x09, 0x54, 0x15, 0x00, 0x25, 0x05, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04,
    0x09, 0x40, 0x09, 0x30, 0x09, 0xE9, 0x09, 0xEA, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
    0xC0
};

static int log_fd = -1;
static io_connect_t klog_conn = 0;

static void dlog(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    if (klog_conn && n > 0 && n < 160) {
        size_t z = 0;
        IOConnectCallStructMethod(klog_conn, 6u, s, n + 1u, NULL, &z);
    }
    if (log_fd < 0)
        log_fd = open("/dev/console", 1 /* O_WRONLY */);
    if (log_fd >= 0)
        (void)write(log_fd, s, n);
}

static void dict_set_i32(CFMutableDictionaryRef d, const char *key, int32_t v)
{
    CFTypeRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFTypeRef n = CFNumberCreate(NULL, kCFNumberSInt32Type, &v);
    CFDictionarySetValue(d, k, n);
    CFRelease(n);
    CFRelease(k);
}

static void dict_set_str(CFMutableDictionaryRef d, const char *key, const char *val)
{
    CFTypeRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFTypeRef v = CFStringCreateWithCString(NULL, val, kCFStringEncodingUTF8);
    CFDictionarySetValue(d, k, v);
    CFRelease(v);
    CFRelease(k);
}

static void dict_set_bool(CFMutableDictionaryRef d, const char *key, CFTypeRef b)
{
    CFTypeRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFDictionarySetValue(d, k, b);
    CFRelease(k);
}

static void *create_hid_device(void)
{
    CFMutableDictionaryRef props = CFDictionaryCreateMutable(
        NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFTypeRef desc = CFDataCreate(NULL, kHidDesc, (long)sizeof(kHidDesc));
    CFTypeRef kdesc = CFStringCreateWithCString(NULL, "ReportDescriptor", kCFStringEncodingUTF8);
    CFDictionarySetValue(props, kdesc, desc);
    CFRelease(desc);
    CFRelease(kdesc);

    dict_set_str(props, "Product", "Exynos MultiTouch");
    dict_set_str(props, "Transport", "I2C");
    dict_set_i32(props, "VendorID", 0x05AC);
    dict_set_i32(props, "ProductID", 0x0201);
    dict_set_i32(props, "PrimaryUsagePage", 0x0D);
    dict_set_i32(props, "PrimaryUsage", 0x04);
    dict_set_i32(props, "ReportInterval", 16666);
    dict_set_bool(props, "Built-In", kCFBooleanTrue);
    dict_set_bool(props, "DisplayIntegrated", kCFBooleanTrue);

    void *dev = IOHIDUserDeviceCreate(NULL, props);
    CFRelease(props);
    return dev;
}

static uint64_t g_sender_id = kExyTouchSenderID;
static int g_sender_resolved = 0;

static void resolve_sender_id(void *hid_client)
{
    uint64_t rid = 0, drv_id = 0;
    io_service_t svc = IOServiceGetMatchingService(
        kIOMasterPortDefault, IOServiceMatching("IOHIDUserDevice"));
    if (svc) {
        IORegistryEntryGetRegistryEntryID(svc, &rid);
        IOObjectRelease(svc);
    }
    /* Query child IOHIDEventDriver directly in IORegistry (backboardd matches senderID against it, not IOHIDUserDevice). */
    io_service_t dsvc = IOServiceGetMatchingService(
        kIOMasterPortDefault, IOServiceMatching("IOHIDEventDriver"));
    if (dsvc) {
        IORegistryEntryGetRegistryEntryID(dsvc, &drv_id);
        IOObjectRelease(dsvc);
    }
    long nsvc = 0;
    uint64_t first_id = 0;
    if (hid_client) {
        CFTypeRef arr = IOHIDEventSystemClientCopyServices(hid_client);
        if (arr) {
            nsvc = CFArrayGetCount(arr);
            if (nsvc > 0) {
                CFTypeRef s0 = CFArrayGetValueAtIndex(arr, 0);
                CFTypeRef num = s0 ? IOHIDServiceClientGetRegistryID(s0) : NULL;
                if (num)
                    CFNumberGetValue(num, 4L /* kCFNumberSInt64Type */, &first_id);
            }
            CFRelease(arr);
        }
    }
    if (first_id) {
        g_sender_id = first_id;
        g_sender_resolved = 1;
    } else if (drv_id) {
        g_sender_id = drv_id;
        g_sender_resolved = 1;
    } else if (rid) {
        g_sender_id = rid;
    }
    char buf[152];
    snprintf(buf, sizeof(buf), "[TOUCHD] sender rid=0x%llx drv=0x%llx nsvc=%ld s0=0x%llx use=0x%llx\n",
             (unsigned long long)rid, (unsigned long long)drv_id, nsvc,
             (unsigned long long)first_id, (unsigned long long)g_sender_id);
    dlog(buf);
}

static kern_return_t send_hid_touch_report(void *dev, const struct exy_touch_state *prev,
                                           const struct exy_touch_state *cur)
{
    if (!dev || !cur) return -1;
    uint8_t trep[32];
    for (size_t i = 0; i < sizeof(trep); i++) trep[i] = 0;
    trep[0] = 0x01;
    uint8_t n = 0;
    /* HID Multitouch parallel mode: Contact Count MUST include fingers lifting up (Tip=0, InRange=0)
     * so IOHIDEventDriver processes finger-up instead of dropping the release! */
    for (unsigned i = 0; i < TOUCH_MAX_FINGERS && n < TOUCH_MAX_FINGERS; i++) {
        const struct exy_finger *cf = &cur->f[i];
        const struct exy_finger *pf = prev ? &prev->f[i] : NULL;
        int was_down = pf ? pf->down : 0;
        if (!cf->down && !was_down)
            continue;
        uint8_t *p = trep + 1 + n * 6;
        uint16_t rx = cf->down ? cf->x : (cf->x ? cf->x : (pf ? pf->x : 0u));
        uint16_t ry = cf->down ? cf->y : (cf->y ? cf->y : (pf ? pf->y : 0u));
        uint8_t fid = cf->id ? cf->id : ((pf && pf->id) ? pf->id : (uint8_t)(i + 1u));
        p[0] = cf->down ? 0x03u : 0x00u; /* bit0=TipSwitch, bit1=InRange */
        p[1] = fid;
        p[2] = (uint8_t)(rx & 0xffu);
        p[3] = (uint8_t)(rx >> 8);
        p[4] = (uint8_t)(ry & 0xffu);
        p[5] = (uint8_t)(ry >> 8);
        n++;
    }
    trep[31] = n;
    return IOHIDUserDeviceHandleReport(dev, trep, sizeof(trep));
}

static void dispatch_buttons(void *client, uint32_t prev, uint32_t cur)
{
    static const uint32_t kUsages[4] = { 0x40u, 0x30u, 0xE9u, 0xEAu }; /* Home, Power, Vol+, Vol- */
    uint32_t diff = prev ^ cur;
    if (!client || !diff)
        return;
    uint64_t ts = mach_absolute_time();
    for (unsigned i = 0; i < 4; i++) {
        if (!(diff & (1u << i)))
            continue;
        uint32_t down = (cur >> i) & 1u;
        CFTypeRef ev = IOHIDEventCreateKeyboardEvent(NULL, ts, 0x0Cu, kUsages[i], down, 0);
        if (ev) {
            IOHIDEventSetSenderID(ev, g_sender_id);
            IOHIDEventSystemClientDispatchEvent(client, ev);
            CFRelease(ev);
        }
    }
}

static void dispatch_touch(void *client, const struct exy_touch_state *prev,
                           const struct exy_touch_state *cur)
{
    if (!client)
        return;
    uint32_t any_down = 0, any_changed = 0, have_pos = 0;
    uint32_t hand_mask = 0;
    float hx = 0.0f, hy = 0.0f;

    for (unsigned i = 0; i < TOUCH_MAX_FINGERS; i++) {
        const struct exy_finger *pf = &prev->f[i];
        const struct exy_finger *cf = &cur->f[i];
        if (cf->down) {
            if (!any_down) {
                hx = (float)cf->x / 720.0f;
                hy = (float)cf->y / 1280.0f;
                have_pos = 1;
            }
            any_down = 1;
        } else if (pf->down && !have_pos) {
            /* Keep release position on hand event so slide-to-unlock doesn't snap back to (0,0). */
            uint16_t rx = (cf->x || cf->y) ? cf->x : pf->x;
            uint16_t ry = (cf->x || cf->y) ? cf->y : pf->y;
            hx = (float)rx / 720.0f;
            hy = (float)ry / 1280.0f;
            have_pos = 1;
        }
        if (cf->down != pf->down) {
            hand_mask |= kIOHIDDigitizerEventRange | kIOHIDDigitizerEventTouch |
                         (cf->down ? (kIOHIDDigitizerEventPosition | kIOHIDDigitizerEventIdentity) : 0u);
            any_changed = 1;
        } else if (cf->down && (cf->x != pf->x || cf->y != pf->y)) {
            hand_mask |= kIOHIDDigitizerEventPosition;
            any_changed = 1;
        }
    }
    if (!any_changed)
        return;

    uint64_t ts = mach_absolute_time();
    CFTypeRef hand = IOHIDEventCreateDigitizerEvent(
        NULL, ts, kIOHIDDigitizerTransducerTypeHand,
        0, 0, hand_mask, 0, hx, hy, 0.0f, 0.0f, 0.0f,
        any_down, any_down, 0);
    if (!hand)
        return;
    IOHIDEventSetIntegerValue(hand, kIOHIDEventFieldDigitizerCollection, 1);
    IOHIDEventSetIntegerValue(hand, kIOHIDEventFieldDigitizerChildEventMask, (long)hand_mask);
    IOHIDEventSetIntegerValue(hand, kIOHIDEventFieldDigitizerIsDisplayIntegrated, 1);
    IOHIDEventSetSenderID(hand, g_sender_id);

    for (unsigned i = 0; i < TOUCH_MAX_FINGERS; i++) {
        const struct exy_finger *pf = &prev->f[i];
        const struct exy_finger *cf = &cur->f[i];
        if (!cf->down && !pf->down)
            continue;
        uint32_t fmask = (cf->down != pf->down)
            ? (kIOHIDDigitizerEventRange | kIOHIDDigitizerEventTouch |
               (cf->down ? (kIOHIDDigitizerEventPosition | kIOHIDDigitizerEventIdentity) : 0u))
            : kIOHIDDigitizerEventPosition;
        uint16_t fx_i = (!cf->down && !cf->x && !cf->y) ? pf->x : cf->x;
        uint16_t fy_i = (!cf->down && !cf->x && !cf->y) ? pf->y : cf->y;
        float fx = (float)fx_i / 720.0f;
        float fy = (float)fy_i / 1280.0f;
        uint32_t fid = cf->id ? cf->id : (i + 1u);
        CFTypeRef finger = IOHIDEventCreateDigitizerFingerEvent(
            NULL, ts, fid, fid, fmask, fx, fy, 0.0f,
            cf->down ? 1.0f : 0.0f, 0.0f, cf->down, cf->down, 0);
        if (finger) {
            IOHIDEventSetIntegerValue(finger, kIOHIDEventFieldDigitizerIsDisplayIntegrated, 1);
            IOHIDEventSetSenderID(finger, g_sender_id);
            IOHIDEventAppendEvent(hand, finger, 0);
            CFRelease(finger);
        }
    }

    IOHIDEventSystemClientDispatchEvent(client, hand);
    CFRelease(hand);
}

static int bootarg_has_bench(void)
{
    typedef int (*sysctlbyname_t)(const char *, void *, size_t *, void *, size_t);
    sysctlbyname_t fn = (sysctlbyname_t)dlsym((void *)-2, "sysctlbyname");
    if (!fn)
        return 0;
    char buf[256];
    size_t len = sizeof(buf) - 1;
    if (fn("kern.bootargs", buf, &len, NULL, 0) != 0)
        return 0;
    buf[len] = '\0';
    for (size_t i = 0; i + 7 <= len; i++) {
        if (buf[i] == 'b' && buf[i+1] == 'e' && buf[i+2] == 'n' && buf[i+3] == 'c' &&
            buf[i+4] == 'h' && buf[i+5] == '=' && (buf[i+6] >= '1' && buf[i+6] <= '5'))
            return (int)(buf[i+6] - '0');
    }
    return 0;
}

static void bench_swipe(void *client, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, int shift, uint32_t step_us)
{
    int steps = 1 << shift;
    struct exy_touch_state prev, cur;
    for (size_t i = 0; i < sizeof(prev); i++) ((uint8_t *)&prev)[i] = 0;
    for (int s = 0; s <= steps; s++) {
        cur = prev;
        cur.count = 1;
        cur.f[0].id = 1;
        cur.f[0].down = 1;
        cur.f[0].x = (uint16_t)((int)x0 + ((((int)x1 - (int)x0) * s) >> shift));
        cur.f[0].y = (uint16_t)((int)y0 + ((((int)y1 - (int)y0) * s) >> shift));
        dispatch_touch(client, &prev, &cur);
        prev = cur;
        usleep(step_us);
    }
    cur = prev;
    cur.count = 0;
    cur.f[0].down = 0;
    dispatch_touch(client, &prev, &cur);
}

static int str_has(const char *s, const char *sub)
{
    for (int i = 0; s[i]; i++) {
        int j = 0;
        while (sub[j] && s[i + j] == sub[j]) j++;
        if (!sub[j]) return 1;
    }
    return 0;
}

static void inspect_procs(void)
{
    typedef int (*proc_name_t)(int, void *, uint32_t);
    typedef int (*tfp_t)(uint32_t, int, uint32_t *);
    typedef int (*tt_t)(uint32_t, uint32_t **, uint32_t *);
    typedef int (*tgs_t)(uint32_t, int, uint32_t *, uint32_t *);
    typedef int (*vmr_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *);
    proc_name_t fn_pn = (proc_name_t)dlsym((void *)-2, "proc_name");
    tfp_t fn_tfp = (tfp_t)dlsym((void *)-2, "task_for_pid");
    tt_t fn_tt = (tt_t)dlsym((void *)-2, "task_threads");
    tgs_t fn_tgs = (tgs_t)dlsym((void *)-2, "thread_get_state");
    vmr_t fn_vmr = (vmr_t)dlsym((void *)-2, "vm_read_overwrite");
    if (!fn_pn) return;
    char name[64], msg[220];
    for (int pid = 1; pid < 160; pid++) {
        name[0] = '\0';
        if (fn_pn(pid, name, sizeof(name)) <= 0 || !name[0]) continue;
        int is_tgt = str_has(name, "Safari") || str_has(name, "Web") || str_has(name, "temple") ||
                     str_has(name, "Spring") || str_has(name, "backboard");
        int is_sys = str_has(name, "Report") || str_has(name, "Migrator") || str_has(name, "configd");
        if (!is_tgt && !is_sys && pid < 60) continue;
        uint32_t task = 0, nthr = 0, *thrs = NULL;
        if (fn_tfp && fn_tt && fn_tgs && fn_tfp(mach_task_self_, pid, &task) == 0 && task)
            (void)fn_tt(task, &thrs, &nthr);
        if (!is_tgt) {
            uint32_t st[17] = {0}, cnt = 17;
            if (thrs && nthr > 0) (void)fn_tgs(thrs[0], 1, st, &cnt);
            snprintf(msg, sizeof(msg), "[PS] pid=%d %s nthr=%u pc=0x%x lr=0x%x\n",
                     pid, name, nthr, st[15], st[14]);
            dlog(msg);
            continue;
        }
        snprintf(msg, sizeof(msg), "[PS] pid=%d %s nthr=%u\n", pid, name, nthr);
        dlog(msg);
        uint32_t max_t = str_has(name, "backboard") ? 16u : 6u;
        for (uint32_t ti = 0; ti < nthr && ti < max_t; ti++) {
            uint32_t st[17] = {0}, cnt = 17, fp = 0, f[2] = {0}, outsz = 8;
            if (fn_tgs(thrs[ti], 1, st, &cnt) != 0) continue;
            uint32_t bt[4] = {0};
            fp = st[7];
            for (int k = 0; k < 4 && fn_vmr && fp > 0x10000 && (fp & 3u) == 0; k++) {
                outsz = 8;
                if (fn_vmr(task, fp, 8, (uint32_t)(uintptr_t)f, &outsz) != 0 || outsz < 8) break;
                bt[k] = f[1];
                if (f[0] <= fp) break;
                fp = f[0];
            }
            snprintf(msg, sizeof(msg), "[PS]   t%u pc=0x%x lr=0x%x bt=0x%x,0x%x,0x%x,0x%x\n",
                     ti, st[15], st[14], bt[0], bt[1], bt[2], bt[3]);
            dlog(msg);
        }
    }
}

static void dump_crash_dir(const char *dir)
{
    typedef void *(*opendir_t)(const char *);
    typedef void *(*readdir_t)(void *);
    typedef int (*closedir_t)(void *);
    typedef long (*read_t)(int, void *, size_t);
    typedef int (*close_t)(int);
    opendir_t fn_od = (opendir_t)dlsym((void *)-2, "opendir");
    readdir_t fn_rd = (readdir_t)dlsym((void *)-2, "readdir");
    closedir_t fn_cd = (closedir_t)dlsym((void *)-2, "closedir");
    read_t fn_read = (read_t)dlsym((void *)-2, "read");
    close_t fn_close = (close_t)dlsym((void *)-2, "close");
    if (!fn_od || !fn_rd || !fn_cd) return;
    void *dp = fn_od(dir);
    if (!dp) return;
    char msg[160], path[256];
    int printed = 0;
    for (;;) {
        uint8_t *ent = (uint8_t *)fn_rd(dp);
        if (!ent) break;
        const char *name = (const char *)(ent + 21);
        if (name[0] < 0x20 || name[0] > 0x7e) name = (const char *)(ent + 8);
        if (name[0] == '.') continue;
        if (str_has(name, "Jetsam") || str_has(name, "purplebuddy") || str_has(name, "touchd") || str_has(name, "panic-2016"))
            continue;
        snprintf(msg, sizeof(msg), "[CRASH] %s/%s\n", dir, name);
        dlog(msg);
        if (printed < 3 && fn_read && fn_close) {
            snprintf(path, sizeof(path), "%s/%s", dir, name);
            int fd = open(path, 0 /* O_RDONLY */);
            if (fd >= 0) {
                char buf[2048];
                long n = fn_read(fd, buf, sizeof(buf) - 1);
                fn_close(fd);
                if (n > 0) {
                    buf[n] = '\0';
                    for (long p = 0; p < n; ) {
                        long q = p;
                        while (q < n && buf[q] != '\n' && q - p < 140) q++;
                        char line[150];
                        long len = q - p;
                        for (long i = 0; i < len; i++) line[i] = buf[p + i];
                        line[len] = '\n'; line[len + 1] = '\0';
                        dlog(line);
                        p = (q < n && buf[q] == '\n') ? q + 1 : q;
                    }
                    printed++;
                }
            }
        }
    }
    fn_cd(dp);
}

static void run_safari_repro(io_connect_t conn, void *client)
{
    dlog("[BENCH] SAFARI: unlock\n");
    usleep(3000000u);
    bench_swipe(client, 80u, 640u, 640u, 640u, 4, 16000u);
    usleep(5000000u);
    dlog("[BENCH] SAFARI: pre-tap\n");
    inspect_procs();
    dlog("[BENCH] SAFARI: tap dock (275,1185)\n");
    bench_swipe(client, 275u, 1185u, 275u, 1185u, 2, 30000u);
    usleep(6000000u);
    dlog("[BENCH] SAFARI: +6s post-tap\n");
    inspect_procs();
    dump_crash_dir("/var/mobile/Library/Logs/CrashReporter");
    dlog("[BENCH] SAFARI: end -> warm reboot\n");
    size_t z = 0;
    (void)IOConnectCallStructMethod(conn, 11u /* MALI_CMD_BENCH_REBOOT */, NULL, 0, NULL, &z);
}

#define MALI_CMD_DRV_POLL       12u
#define MALI_CMD_DRV_BRIGHTNESS 13u
#define MALI_CMD_DRV_LED        14u
#define MALI_CMD_DRV_VIBRATE    15u
#define MALI_CMD_DRV_POWER_SYNC 16u
#define MALI_CMD_DRV_AUDIO_VOL  17u
#define MALI_CMD_DRV_AUDIO_TONE 18u
#define MALI_CMD_DRV_TORCH      19u

struct exy_drv_status {
    uint32_t probe_mask;
    uint16_t batt_soc;
    uint16_t batt_mv;
    int16_t  batt_ma;
    int16_t  batt_temp;
    uint8_t  chg_state;
    uint8_t  cable_type;
    uint8_t  brightness;
    uint8_t  touchkeys;
    int16_t  accel[3];
    int16_t  gyro[3];
    int16_t  mag[3];
    uint16_t als_lux;
    uint16_t prox_raw;
    uint32_t rtc_epoch;
    uint16_t wm1811_id;
    uint8_t  sd_present;
    uint8_t  pad0;
    uint32_t vpll_con0;
    uint32_t otg_id;
    uint32_t sd3_present;
    uint32_t led_rgb;
    int16_t  cpu_temp_c;
    uint16_t adc_ap;
    uint8_t  hp_jack;
    uint8_t  volume;
    uint8_t  torch_lvl;
    uint8_t  bt_hci_ok;
    uint32_t sd3_ocr;
    uint32_t otg_resets;
};

static uint8_t g_audio_vol = 75u;

static void dispatch_sensors(io_connect_t conn, void *client, uint32_t tick)
{
    typedef CFTypeRef (*hid_vec3_t)(CFTypeRef, uint64_t, float, float, float, uint32_t);
    typedef CFTypeRef (*hid_als_t)(CFTypeRef, uint64_t, float, uint32_t);
    typedef CFTypeRef (*hid_prox_t)(CFTypeRef, uint64_t, uint32_t, uint32_t);
    static int resolved = 0;
    static hid_vec3_t fn_accel, fn_gyro, fn_compass;
    static hid_als_t fn_als;
    static hid_prox_t fn_prox;
    static uint8_t prev_tkey = 0;
    static uint8_t prev_jack = 0xffu;

    if (!client)
        return;
    if (!resolved) {
        resolved = 1;
        fn_accel   = (hid_vec3_t)dlsym((void *)-2, "IOHIDEventCreateAccelerometerEvent");
        fn_gyro    = (hid_vec3_t)dlsym((void *)-2, "IOHIDEventCreateGyroEvent");
        fn_compass = (hid_vec3_t)dlsym((void *)-2, "IOHIDEventCreateCompassEvent");
        fn_als     = (hid_als_t)dlsym((void *)-2, "IOHIDEventCreateAmbientLightSensorEvent");
        fn_prox    = (hid_prox_t)dlsym((void *)-2, "IOHIDEventCreateProximtyEvent");
    }
    struct exy_drv_status ds;
    size_t sz = sizeof(ds);
    if (IOConnectCallStructMethod(conn, MALI_CMD_DRV_POLL, NULL, 0, &ds, &sz) != 0 || sz < 64u)
        return;

    uint64_t ts = mach_absolute_time();
    /* Skip injecting synthetic accel/gyro/compass/ALS/prox HID events with digitizer g_sender_id:
     * iOS 9 IOHIDEventCreateAmbientLightSensorEvent takes 8 args (not 4) and reusing the digitizer
     * sender ID triggers backboardd HID re-probing every ~838ms and stalls SpringBoard's Lock Screen transition. */
    (void)fn_accel; (void)fn_gyro; (void)fn_compass; (void)fn_als; (void)fn_prox; (void)tick;
    /* Capacitive touchkeys (bit0=Menu, bit1=Back) -> haptic click + Home event */
    if ((ds.probe_mask & (1u << 9)) && ds.touchkeys != prev_tkey) {
        uint8_t rise = (uint8_t)(ds.touchkeys & ~prev_tkey);
        if (rise) {
            uint64_t vib = 15u; uint32_t oc = 0;
            IOConnectCallScalarMethod(conn, MALI_CMD_DRV_VIBRATE, &vib, 1, NULL, &oc);
        }
        uint32_t down = (ds.touchkeys & 3u) ? 1u : 0u;
        CFTypeRef ev = IOHIDEventCreateKeyboardEvent(NULL, ts, 0x0Cu, 0x40u /* Home */, down, 0u);
        if (ev) {
            IOHIDEventSetSenderID(ev, g_sender_id);
            IOHIDEventSystemClientDispatchEvent(client, ev);
            CFRelease(ev);
        }
        prev_tkey = ds.touchkeys;
    }
    /* Auto-reroute WM1811 speaker <-> headphone on 3.5mm jack state change */
    if (sz >= 80u && ds.hp_jack != prev_jack) {
        prev_jack = ds.hp_jack;
        uint64_t v = g_audio_vol; uint32_t oc = 0;
        IOConnectCallScalarMethod(conn, MALI_CMD_DRV_AUDIO_VOL, &v, 1, NULL, &oc);
    }
}

static void drv_log_status(io_connect_t conn)
{
    struct exy_drv_status ds;
    size_t sz = sizeof(ds);
    if (IOConnectCallStructMethod(conn, MALI_CMD_DRV_POLL, NULL, 0, &ds, &sz) != 0)
        return;
    char msg[136];
    snprintf(msg, sizeof(msg), "[DRV] m=0x%x bat=%u%%/%umV/%dmA chg=%u cab=%u br=%u\n",
             (unsigned)ds.probe_mask, (unsigned)ds.batt_soc, (unsigned)ds.batt_mv,
             (int)ds.batt_ma, (unsigned)ds.chg_state, (unsigned)ds.cable_type, (unsigned)ds.brightness);
    dlog(msg);
    snprintf(msg, sizeof(msg), "[DRV] vpll=0x%x wm=0x%x sd=%u otg=0x%x rtc=%u\n",
             (unsigned)ds.vpll_con0, (unsigned)ds.wm1811_id, (unsigned)ds.sd_present,
             (unsigned)ds.otg_id, (unsigned)ds.rtc_epoch);
    dlog(msg);
    snprintf(msg, sizeof(msg), "[DRV] acc=(%d,%d,%d) gyr=(%d,%d,%d) als=%u prox=%u\n",
             (int)ds.accel[0], (int)ds.accel[1], (int)ds.accel[2],
             (int)ds.gyro[0], (int)ds.gyro[1], (int)ds.gyro[2],
             (unsigned)ds.als_lux, (unsigned)ds.prox_raw);
    dlog(msg);
    if (sz >= 80u) {
        snprintf(msg, sizeof(msg), "[DRV] cpu=%dC adc=%u jack=%u vol=%u bt=%u ocr=0x%x rst=%u\n",
                 (int)ds.cpu_temp_c, (unsigned)ds.adc_ap, (unsigned)ds.hp_jack,
                 (unsigned)ds.volume, (unsigned)ds.bt_hci_ok, (unsigned)ds.sd3_ocr,
                 (unsigned)ds.otg_resets);
        dlog(msg);
    }
}

static int find_or_kill_proc(const char *sub, int do_kill)
{
    typedef int (*proc_name_t)(int, void *, uint32_t);
    typedef int (*kill_t)(int, int);
    proc_name_t fn_pn = (proc_name_t)dlsym((void *)-2, "proc_name");
    kill_t fn_kill = do_kill ? (kill_t)dlsym((void *)-2, "kill") : NULL;
    if (!fn_pn) return 0;
    char name[64];
    int found = 0;
    for (int pid = 2; pid < 200; pid++) {
        name[0] = '\0';
        if (fn_pn(pid, name, sizeof(name)) <= 0 || !name[0]) continue;
        if (str_has(name, sub)) {
            found = pid;
            if (do_kill && fn_kill) fn_kill(pid, 9);
        }
    }
    return found;
}

static void run_setup_bench(io_connect_t conn, void *client)
{
    dlog("[BENCH] SETUP: waiting for Setup.app\n");
    {
        typedef void *(*dlopen_t)(const char *, int);
        typedef uint8_t (*need_t)(void);
        typedef int (*access_t)(const char *, int);
        dlopen_t fn_dlopen = (dlopen_t)dlsym((void *)-2, "dlopen");
        access_t fn_acc = (access_t)dlsym((void *)-2, "access");
        void *h_sa = fn_dlopen ? fn_dlopen("/System/Library/PrivateFrameworks/SetupAssistant.framework/SetupAssistant", 2) : NULL;
        need_t fn_need = h_sa ? (need_t)dlsym(h_sa, "BYSetupAssistantNeedsToRun") : NULL;
        need_t fn_init = h_sa ? (need_t)dlsym(h_sa, "BYSetupAssistantHasCompletedInitialRun") : NULL;
        int need = fn_need ? (int)fn_need() : -1;
        int initdone = fn_init ? (int)fn_init() : -1;
        int a_sent = fn_acc ? fn_acc("/tmp/com.apple.purplebuddy.sentinel", 0) : -9;
        int a_app = fn_acc ? fn_acc("/Applications/Setup.app/Setup", 0) : -9;
        int a_pl = fn_acc ? fn_acc("/private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist", 0) : -9;
        char db[160];
        snprintf(db, sizeof(db), "[BENCH] SETUP: need=%d initdone=%d sent=%d app=%d plist=%d\n",
                 need, initdone, a_sent, a_app, a_pl);
        dlog(db);
        if (a_pl == 0) {
            typedef long (*read_t)(int, void *, unsigned long);
            typedef int (*close_t)(int);
            read_t fn_read = (read_t)dlsym((void *)-2, "read");
            close_t fn_close = (close_t)dlsym((void *)-2, "close");
            int pfd = open("/private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist", 0);
            if (pfd >= 0 && fn_read) {
                char pb[512];
                long nr = fn_read(pfd, pb, sizeof(pb) - 1);
                if (fn_close) fn_close(pfd);
                if (nr > 0) { pb[nr] = 0; dlog("[BENCH] SETUP: plist:\n"); dlog(pb); dlog("\n"); }
            }
        }
    }
    /* On iOS 9.3.5, SpringBoard renders the white "Hello / slide to set up" screen first and
     * launches Setup.app when the user slides to set up. Wait until SpringBoard finishes startup! */
    usleep(12000000u);
    dlog("[BENCH] SETUP: slide to set up\n");
    bench_swipe(client, 80u, 1140u, 640u, 1140u, 6, 15000u);
    int spid = 0;
    for (int i = 0; i < 12; i++) {
        spid = find_or_kill_proc("Setup", 0);
        if (spid > 0) {
            char mb[64];
            snprintf(mb, sizeof(mb), "[BENCH] SETUP: alive pid=%d (t=%ds)\n", spid, i);
            dlog(mb);
            break;
        }
        if (i == 4) {
            /* Retry slide slightly higher in case slide-to-setup text is around y=1080 */
            dlog("[BENCH] SETUP: retry slide (y=1080)\n");
            bench_swipe(client, 80u, 1080u, 640u, 1080u, 6, 15000u);
        }
        usleep(1000000u);
    }
    if (spid > 0) {
        usleep(3500000u);
        dlog("[BENCH] SETUP: tap English (360,340)\n");
        bench_swipe(client, 360u, 340u, 360u, 340u, 1, 25000u);
        usleep(3500000u);
        dlog("[BENCH] SETUP: tap United States (360,680)\n");
        bench_swipe(client, 360u, 680u, 360u, 680u, 1, 25000u);
        /* Wait for SetupLanguageChange relaunch + next Setup pane (Wi-Fi/Location/Passcode) */
        usleep(14000000u);
    }
    int spid2 = find_or_kill_proc("Setup", 0);
    char mb2[64];
    snprintf(mb2, sizeof(mb2), "[BENCH] SETUP: post-nav Setup pid=%d\n", spid2);
    dlog(mb2);
    inspect_procs();
    dump_crash_dir("/private/var/mobile/Library/Logs/CrashReporter");
    dlog("[BENCH] SETUP: done -> warm reboot\n");
    usleep(500000u);
    size_t z = 0;
    (void)IOConnectCallStructMethod(conn, 11u /* MALI_CMD_BENCH_REBOOT */, NULL, 0, NULL, &z);
}

static void run_ipa_bench(io_connect_t conn, void *client)
{
    dlog("[BENCH] IPA_3D: unlock + launch\n");
    drv_log_status(conn);
    usleep(3000000u);
    for (int k = 0; k < 4; k++) {
        int spid = find_or_kill_proc("Setup", 1);
        if (spid) {
            char km[64];
            snprintf(km, sizeof(km), "[BENCH] IPA_3D: killed Setup pid=%d\n", spid);
            dlog(km);
            usleep(2500000u);
        } else {
            break;
        }
    }
    /* Dismiss any first-boot alert dialog on Lock Screen, then slide to unlock cleanly. */
    bench_swipe(client, 360u, 740u, 360u, 740u, 1, 25000u);
    usleep(600000u);
    bench_swipe(client, 60u, 960u, 660u, 960u, 5, 14000u);
    usleep(1200000u);
    bench_swipe(client, 80u, 640u, 640u, 640u, 4, 16000u);
    usleep(2500000u);

    typedef void *(*dlopen_t)(const char *, int);
    typedef int (*sbs_launch_t)(CFTypeRef, uint8_t);
    typedef int (*sbs_launch_opt_t)(CFTypeRef, CFTypeRef, uint8_t);
    dlopen_t fn_dlopen = (dlopen_t)dlsym((void *)-2, "dlopen");
    int sbs_rc = -99, ws_rc = -99, tr_pid = 0;
    CFTypeRef bid = CFStringCreateWithCString(NULL, "com.imangi.templerun2", 0x08000100u);
    CFMutableDictionaryRef lopts = CFDictionaryCreateMutable(NULL, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFTypeRef k_unlock = CFStringCreateWithCString(NULL, "__UnlockDevice", 0x08000100u);
    if (lopts && k_unlock) CFDictionarySetValue(lopts, k_unlock, kCFBooleanTrue);
    if (fn_dlopen && bid) {
        void *h_sbs = fn_dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", 2);
        sbs_launch_opt_t fn_launch_opt = h_sbs ? (sbs_launch_opt_t)dlsym(h_sbs, "SBSLaunchApplicationWithIdentifierAndLaunchOptions") : NULL;
        sbs_launch_t fn_launch = h_sbs ? (sbs_launch_t)dlsym(h_sbs, "SBSLaunchApplicationWithIdentifier") : NULL;
        void *h_objc = fn_dlopen("/usr/lib/libobjc.A.dylib", 2);
        (void)fn_dlopen("/System/Library/Frameworks/MobileCoreServices.framework/MobileCoreServices", 2);
        void *(*get_cls)(const char *) = h_objc ? (void *(*)(const char *))dlsym(h_objc, "objc_getClass") : NULL;
        void *(*sel_reg)(const char *) = h_objc ? (void *(*)(const char *))dlsym(h_objc, "sel_registerName") : NULL;
        void *(*msgSend)(void *, void *, ...) = h_objc ? (void *(*)(void *, void *, ...))dlsym(h_objc, "objc_msgSend") : NULL;
        void *ws = (get_cls && sel_reg && msgSend) ? msgSend(get_cls("LSApplicationWorkspace"), sel_reg("defaultWorkspace")) : NULL;

        for (int attempt = 0; attempt < 3; attempt++) {
            if (fn_launch_opt && lopts) sbs_rc = fn_launch_opt(bid, lopts, 0);
            else if (fn_launch) sbs_rc = fn_launch(bid, 0);
            if (sbs_rc != 0 && ws) ws_rc = (int)(uintptr_t)msgSend(ws, sel_reg("openApplicationWithBundleID:"), bid) & 0xff;
            usleep(3000000u);
            tr_pid = find_or_kill_proc("temple", 0);
            char mb[128];
            snprintf(mb, sizeof(mb), "[BENCH] IPA_3D: try=%d sbs=%d ws=%d tr_pid=%d\n", attempt, sbs_rc, ws_rc, tr_pid);
            dlog(mb);
            if (tr_pid > 0) break;
            bench_swipe(client, 60u, 960u, 660u, 960u, 5, 14000u);
            usleep(2500000u);
        }
    }
    if (k_unlock) CFRelease(k_unlock);
    if (lopts) CFRelease(lopts);
    if (bid) CFRelease(bid);
    /* Keep idle timer alive during Temple Run 2's ~26s initial Unity asset load, then tap PLAY. */
    usleep(12000000u);
    bench_swipe(client, 16u, 16u, 16u, 16u, 1, 20000u);
    usleep(12000000u);
    bench_swipe(client, 16u, 16u, 16u, 16u, 1, 20000u);
    usleep(4000000u);
    dlog("[BENCH] IPA_3D: tap PLAY (360,880)\n");
    bench_swipe(client, 360u, 880u, 360u, 880u, 1, 30000u);
    usleep(6000000u);
    dlog("[BENCH] IPA_3D: NC swipe test\n");
    bench_swipe(client, 360u, 10u, 360u, 600u, 8, 16000u);
    usleep(6000000u);
    inspect_procs();
    dump_crash_dir("/private/var/mobile/Library/Logs/CrashReporter");
    dlog("[BENCH] IPA_3D: done -> warm reboot\n");
    {
        typedef void (*sync_t)(void);
        sync_t fn_sync = (sync_t)dlsym((void *)-2, "sync");
        if (fn_sync) fn_sync();
    }
    usleep(500000u);
    size_t z = 0;
    (void)IOConnectCallStructMethod(conn, 11u /* MALI_CMD_BENCH_REBOOT */, NULL, 0, NULL, &z);
}

static void run_auto_bench(io_connect_t conn, void *client)
{
    dlog("[BENCH] START: unlock Lock Screen\n");
    drv_log_status(conn);
    usleep(3000000u);
    /* Slide to unlock (16 steps x 16ms across mid-screen) */
    bench_swipe(client, 80u, 640u, 640u, 640u, 4, 16000u);
    usleep(3000000u);

    /* Phase 1: Home Screen continuous single-finger drag (~5s, zero gesture-slop pauses) */
    dlog("[BENCH] HOME_START\n");
    {
        struct exy_touch_state prev, cur;
        for (size_t i = 0; i < sizeof(prev); i++) ((uint8_t *)&prev)[i] = 0;
        for (int r = 0; r < 5; r++) {
            for (int s = 0; s < 32; s++) {
                cur = prev; cur.count = 1; cur.f[0].id = 1; cur.f[0].down = 1;
                cur.f[0].x = (uint16_t)(580 - ((460 * s) >> 5)); cur.f[0].y = 640u;
                dispatch_touch(client, &prev, &cur); prev = cur; usleep(14500u);
            }
            for (int s = 0; s < 32; s++) {
                cur = prev; cur.count = 1; cur.f[0].id = 1; cur.f[0].down = 1;
                cur.f[0].x = (uint16_t)(120 + ((460 * s) >> 5)); cur.f[0].y = 640u;
                dispatch_touch(client, &prev, &cur); prev = cur; usleep(14500u);
            }
        }
        cur = prev; cur.count = 0; cur.f[0].down = 0;
        dispatch_touch(client, &prev, &cur);
    }
    dlog("[BENCH] HOME_END\n");
    usleep(300000u);

    /* Phase 1.5: Pull down Spotlight for 3s visual glitch audit, then dismiss */
    dlog("[BENCH] SPOTLIGHT_START\n");
    bench_swipe(client, 360u, 380u, 360u, 880u, 4, 16000u);
    usleep(3000000u);
    bench_swipe(client, 635u, 90u, 635u, 90u, 1, 20000u); /* tap Cancel */
    usleep(1200000u);
    dlog("[BENCH] SPOTLIGHT_END\n");

    /* Phase 2: Swipe right to ensure Page 1, tap Settings (595, 620), scroll, General -> Back */
    dlog("[BENCH] APP_START\n");
    bench_swipe(client, 120u, 640u, 580u, 640u, 4, 16000u);
    usleep(600000u);
    bench_swipe(client, 595u, 620u, 595u, 620u, 1, 20000u);
    usleep(2500000u);
    for (int r = 0; r < 3; r++) {
        bench_swipe(client, 360u, 1000u, 360u, 260u, 5, 16000u);
        bench_swipe(client, 360u, 260u, 360u, 1000u, 5, 16000u);
    }
    /* Tap General (~360, 920), then Back (70, 90) to verify transition smoothness & zero tear */
    bench_swipe(client, 360u, 920u, 360u, 920u, 1, 20000u);
    usleep(1200000u);
    bench_swipe(client, 70u, 90u, 70u, 90u, 1, 20000u);
    usleep(1000000u);

    /* Phase 3: Haptic + power sync telemetry + 3D EAGL/GLES2 benchmark */
    {
        uint64_t vib = 35u; uint32_t oc = 0;
        IOConnectCallScalarMethod(conn, MALI_CMD_DRV_VIBRATE, &vib, 1, NULL, &oc);
        IOConnectCallScalarMethod(conn, MALI_CMD_DRV_POWER_SYNC, NULL, 0, NULL, &oc);
    }
    drv_log_status(conn);
    dlog("[BENCH] 3D_START\n");
    {
        extern int fork(void);
        extern int execv(const char *, char *const[]);
        extern int waitpid(int, int *, int);
        extern void _exit(int);
        int pid = fork();
        if (pid == 0) {
            char *argv[] = { "mali_test", "--bench3d", NULL };
            execv("/usr/libexec/mali_test", argv);
            _exit(127);
        } else if (pid > 0) {
            int st = 0;
            waitpid(pid, &st, 0);
        }
    }
    dlog("[BENCH] 3D_END\n");
    dlog("[BENCH] APP_END -> warm reboot to xnu-mem\n");
    usleep(500000u);
    size_t z = 0;
    (void)IOConnectCallStructMethod(conn, 11u /* MALI_CMD_BENCH_REBOOT */, NULL, 0, NULL, &z);
}

#define MALI_CMD_WIFI_OP 22u
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);

struct wifi_status_u {
    uint32_t probed, fw_loaded, f2_ready, link_up, chip_id, ocr, rca, tx_pkts, rx_pkts, en0_attached;
    uint8_t  mac[6], pad[2];
};
struct wifi_req_u {
    uint32_t op, arg0, arg1, arg2;
    uint8_t  data[1024];
};
struct wifi_rsp_u {
    int32_t  err;
    uint32_t out_len;
    uint8_t  data[1024];
};

static int wifi_call(io_connect_t conn, const struct wifi_req_u *req, struct wifi_rsp_u *rsp)
{
    size_t osz = sizeof(*rsp);
    return IOConnectCallStructMethod(conn, MALI_CMD_WIFI_OP, req, sizeof(*req), rsp, &osz);
}

static void wifi_boot_firmware(io_connect_t conn)
{
    static int wifi_tried = 0;
    if (wifi_tried) return;
    wifi_tried = 1;

    int  (*fn_open)(const char *, int, ...) = (int (*)(const char *, int, ...))dlsym((void *)-2, "open");
    long (*fn_read)(int, void *, size_t)    = (long (*)(int, void *, size_t))dlsym((void *)-2, "read");
    int  (*fn_close)(int)                   = (int (*)(int))dlsym((void *)-2, "close");
    if (!fn_open || !fn_read || !fn_close) return;

    struct wifi_req_u req;
    struct wifi_rsp_u rsp;
    char msg[192];
    memset(&req, 0, sizeof(req));
    req.op = 0u; /* STATUS */
    if (wifi_call(conn, &req, &rsp) != 0 || rsp.out_len < sizeof(struct wifi_status_u))
        return;
    struct wifi_status_u wst;
    memcpy(&wst, rsp.data, sizeof(wst));
    if (!wst.probed) {
        req.op = 1u; /* INIT */
        (void)wifi_call(conn, &req, &rsp);
        req.op = 0u;
        if (wifi_call(conn, &req, &rsp) == 0 && rsp.out_len >= sizeof(wst))
            memcpy(&wst, rsp.data, sizeof(wst));
    }
    snprintf(msg, sizeof(msg), "[WIFI] probe=%u chip=0x%x ocr=0x%x rca=0x%x f2=%u en0=%u\n",
             (unsigned)wst.probed, (unsigned)wst.chip_id, (unsigned)wst.ocr,
             (unsigned)wst.rca, (unsigned)wst.f2_ready, (unsigned)wst.en0_attached);
    dlog(msg);
    if (!wst.probed || wst.fw_loaded)
        return;

    int fd = fn_open("/usr/share/firmware/wifi/brcmfmac4334-sdio.bin", 0 /* O_RDONLY */);
    if (fd < 0) {
        dlog("[WIFI] missing /usr/share/firmware/wifi/brcmfmac4334-sdio.bin\n");
        return;
    }
    uint32_t off = 0u;
    int ok = 1;
    for (;;) {
        long n = fn_read(fd, req.data, 1024u);
        if (n <= 0) break;
        req.op = 2u; /* FW_WRITE */
        req.arg0 = off;
        req.arg1 = (uint32_t)n;
        if (wifi_call(conn, &req, &rsp) != 0 || rsp.err != 0) {
            snprintf(msg, sizeof(msg), "[WIFI] fw_write err=%d at off=0x%x\n", (int)rsp.err, (unsigned)off);
            dlog(msg);
            ok = 0;
            break;
        }
        off += (uint32_t)n;
    }
    fn_close(fd);
    if (!ok || off == 0u) return;

    /* Load & format NVRAM text (strip comments/blank lines -> null-separated key=value pairs) */
    int nfd = fn_open("/usr/share/firmware/wifi/brcmfmac4334-sdio.txt", 0);
    if (nfd < 0) {
        dlog("[WIFI] missing /usr/share/firmware/wifi/brcmfmac4334-sdio.txt\n");
        return;
    }
    char raw[2048];
    long rn = fn_read(nfd, raw, sizeof(raw) - 1u);
    fn_close(nfd);
    if (rn <= 0) return;
    raw[rn] = '\0';

    memset(&req, 0, sizeof(req));
    uint32_t nlen = 0u;
    const char *p = raw;
    while (*p && nlen < 1016u) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        const char *ls = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        uint32_t ll = (uint32_t)(p - ls);
        if (ll > 0u && nlen + ll + 2u < 1020u) {
            memcpy(req.data + nlen, ls, ll);
            nlen += ll;
            req.data[nlen++] = '\0';
        }
    }
    req.data[nlen++] = '\0';
    req.op = 3u; /* NVRAM_BOOT */
    req.arg1 = nlen;
    (void)wifi_call(conn, &req, &rsp);
    req.op = 0u;
    if (wifi_call(conn, &req, &rsp) == 0 && rsp.out_len >= sizeof(wst)) {
        memcpy(&wst, rsp.data, sizeof(wst));
        snprintf(msg, sizeof(msg), "[WIFI] boot err=%d fw=%uB nv=%uB f2=%u en0=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
                 (int)rsp.err, (unsigned)off, (unsigned)nlen, (unsigned)wst.f2_ready,
                 (unsigned)wst.en0_attached, wst.mac[0], wst.mac[1], wst.mac[2],
                 wst.mac[3], wst.mac[4], wst.mac[5]);
        dlog(msg);
    }
}

int main(void)
{
    /* Elevate above SpringBoard render loops so single-core CPU load never starves touch input. */
    int pr_rc = setpriority(0 /* PRIO_PROCESS */, 0u, -15);
    int32_t imp = 47;
    kern_return_t tp_rc = thread_policy_set(mach_thread_self(), 3u /* THREAD_PRECEDENCE_POLICY */, &imp, 1u);

    char msg[160];
    io_connect_t conn = 0;
    for (int tries = 0; tries < 200; tries++) {
        io_service_t svc = IOServiceGetMatchingService(
            kIOMasterPortDefault, IOServiceMatching("IOAcceleratorES"));
        if (svc && IOServiceOpen(svc, mach_task_self_, 0, &conn) == 0)
            break;
        usleep(100000);
    }
    if (!conn) {
        dlog("[TOUCHD] no IOAcceleratorES\n");
        return 1;
    }
    klog_conn = conn;

    void *dev = create_hid_device();
    void *hid_client = IOHIDEventSystemClientCreateWithType(NULL, 0 /* Admin */, NULL);
    if (!hid_client)
        hid_client = IOHIDEventSystemClientCreate(NULL);
    uint32_t status_ptr = hid_client ? (uint32_t)(uintptr_t)hid_client : (uint32_t)(uintptr_t)dev;

    struct exy_touch_state prev, st;
    size_t sz = sizeof(st);
    (void)IOConnectCallStructMethod(conn, MALI_CMD_POLL_TOUCH,
                                    &status_ptr, sizeof(status_ptr), &st, &sz);
    prev = st;
    uint32_t init_srv = hid_client ? *(const uint32_t *)((const uint8_t *)hid_client + 8) : 0u;
    snprintf(msg, sizeof(msg), "[TOUCHD] bp=0x%x client=%p srv=0x%x dev=%p prio=(%d,0x%x)\n",
             (unsigned)bootstrap_port, hid_client, (unsigned)init_srv, dev, pr_rc, (unsigned)tp_rc);
    dlog(msg);

    uint32_t logged = 0;
    uint32_t client_retry = 0;
    uint64_t t_boot = mach_absolute_time();
    int bs_probed = 0;

    usleep(300000u);
    resolve_sender_id(hid_client);

    typedef int (*getppid_t)(void);
    getppid_t fn_getppid = (getppid_t)dlsym((void *)-2, "getppid");
    int bench_mode = bootarg_has_bench();
    int bench_done = 0;
    if (bench_mode)
        dlog("[BENCH] enabled (waiting for Lock Screen)\n");
    /* Adaptive polled loop: 250 Hz burst, 125 Hz finger-down, 100 Hz recent, ~33 Hz idle (saves CPU/heat). */
    int had_ev = 0;
    uint32_t idle_polls = 0;
    for (;;) {
        if (fn_getppid && fn_getppid() == 1)
            return 0; /* backboardd exited; let new backboardd spawn fresh ExyTouchd */
        usleep(had_ev ? 4000u : (st.count ? 8000u : (idle_polls > 120u ? 30000u : 10000u)));
        had_ev = 0;
        uint64_t now = mach_absolute_time();
        uint32_t el_sec = (uint32_t)((now - t_boot) >> 20) * 1000u / 22888u;
        uint32_t cur_srv = hid_client ? *(const uint32_t *)((const uint8_t *)hid_client + 8) : 0u;
        if (cur_srv && !g_sender_resolved && (++client_retry % 64u == 0u))
            resolve_sender_id(hid_client);
        if (!cur_srv && (++client_retry % 80u == 0u)) {
            if (hid_client) {
                CFRelease(hid_client);
                hid_client = NULL;
            }
            hid_client = IOHIDEventSystemClientCreateWithType(NULL, 0 /* Admin */, NULL);
            cur_srv = hid_client ? *(const uint32_t *)((const uint8_t *)hid_client + 8) : 0u;
            if (!cur_srv) {
                if (hid_client) CFRelease(hid_client);
                hid_client = IOHIDEventSystemClientCreate(NULL);
                cur_srv = hid_client ? *(const uint32_t *)((const uint8_t *)hid_client + 8) : 0u;
            }
            if (cur_srv) {
                uint32_t cp = (uint32_t)(uintptr_t)hid_client;
                sz = sizeof(st);
                IOConnectCallStructMethod(conn, MALI_CMD_POLL_TOUCH, &cp, sizeof(cp), &st, &sz);
                snprintf(msg, sizeof(msg), "[TOUCHD] connected t=%us client=%p srv=0x%x\n",
                         (unsigned)el_sec, hid_client, (unsigned)cur_srv);
                dlog(msg);
                resolve_sender_id(hid_client);
                drv_log_status(conn);
                wifi_boot_firmware(conn);
            } else if (!bs_probed && el_sec >= 20u) {
                bs_probed = 1;
                mach_port_t bsp = 0;
                kern_return_t bkr = bootstrap_look_up(bootstrap_port, "com.apple.iohideventsystem", &bsp);
                snprintf(msg, sizeof(msg), "[TOUCHD] bs_lookup t=%us bp=0x%x kr=0x%x sp=0x%x\n",
                         (unsigned)el_sec, (unsigned)bootstrap_port, (unsigned)bkr, (unsigned)bsp);
                dlog(msg);
            }
        }
        sz = sizeof(st);
        if (IOConnectCallStructMethod(conn, MALI_CMD_POLL_TOUCH, NULL, 0, &st, &sz) != 0)
            continue;
        if (bench_mode && !bench_done && hid_client && cur_srv &&
            ((st.chip >> 16) >= 17u || el_sec >= 48u)) {
            bench_done = 1;
            if (bench_mode == 2) {
                run_safari_repro(conn, hid_client);
            } else if (bench_mode == 4) {
                run_ipa_bench(conn, hid_client);
            } else if (bench_mode == 5) {
                run_setup_bench(conn, hid_client);
            } else if (bench_mode == 3) {
                dlog("[BENCH] 3D_START\n");
                extern int fork(void);
                extern int execv(const char *, char *const[]);
                extern int waitpid(int, int *, int);
                extern void _exit(int);
                int pid = fork();
                if (pid == 0) {
                    char *argv[] = { "mali_test", "--bench3d", NULL };
                    execv("/usr/libexec/mali_test", argv);
                    _exit(127);
                } else if (pid > 0) {
                    int st3 = 0;
                    waitpid(pid, &st3, 0);
                }
                dlog("[BENCH] 3D_END\n");
                usleep(300000u);
                size_t z = 0;
                (void)IOConnectCallStructMethod(conn, 11u /* MALI_CMD_BENCH_REBOOT */, NULL, 0, NULL, &z);
            } else {
                run_auto_bench(conn, hid_client);
            }
        }
        static uint32_t sensor_tick = 0;
        uint32_t sensor_mask = (idle_polls > 120u) ? 7u : 3u;
        if (hid_client && cur_srv && (++sensor_tick & sensor_mask) == 0u)
            dispatch_sensors(conn, hid_client, sensor_tick >> 2);
        if (st.seq == prev.seq) {
            if (!st.count && idle_polls < 1000u)
                idle_polls++;
            continue;
        }
        idle_polls = 0;

        if (st.buttons != prev.buttons) {
            /* Tactile 20ms haptic click + hardware WM1811 volume sync on Vol+/Vol- */
            uint32_t rise = st.buttons & ~prev.buttons;
            if (rise) {
                uint64_t vib = 20u;
                uint32_t oc = 0;
                IOConnectCallScalarMethod(conn, MALI_CMD_DRV_VIBRATE, &vib, 1, NULL, &oc);
                if (rise & (1u << 2)) { /* Vol+ */
                    g_audio_vol = (g_audio_vol <= 90u) ? (uint8_t)(g_audio_vol + 10u) : 100u;
                    uint64_t v = g_audio_vol;
                    IOConnectCallScalarMethod(conn, MALI_CMD_DRV_AUDIO_VOL, &v, 1, NULL, &oc);
                } else if (rise & (1u << 3)) { /* Vol- */
                    g_audio_vol = (g_audio_vol >= 10u) ? (uint8_t)(g_audio_vol - 10u) : 0u;
                    uint64_t v = g_audio_vol;
                    IOConnectCallScalarMethod(conn, MALI_CMD_DRV_AUDIO_VOL, &v, 1, NULL, &oc);
                }
            }
            /* Always inject Consumer Page keyboard events (Home/Power/Vol+/Vol-) into backboardd
             * so iOS native Volume HUD & SpringBoard respond even when IOHIDUserDevice exists. */
            if (hid_client && cur_srv) {
                dispatch_buttons(hid_client, prev.buttons, st.buttons);
            } else if (dev) {
                uint8_t brep[2] = { 0x02, (uint8_t)(st.buttons & 0x0fu) };
                IOHIDUserDeviceHandleReport(dev, brep, sizeof(brep));
            }
        }

        if (hid_client && cur_srv)
            dispatch_touch(hid_client, &prev, &st);
        else if (dev)
            send_hid_touch_report(dev, &prev, &st);

        if (logged < 8) {
            logged++;
            uint32_t srv = hid_client ? *(const uint32_t *)((const uint8_t *)hid_client + 8) : 0u;
            snprintf(msg, sizeof(msg), "[TOUCHD] ev#%u srv=0x%x cnt=%u btn=0x%x f0=(%u,%u,%u)\n",
                     logged, (unsigned)srv, (unsigned)st.count, (unsigned)st.buttons,
                     (unsigned)st.f[0].down, (unsigned)st.f[0].x, (unsigned)st.f[0].y);
            dlog(msg);
        }
        prev = st;
        had_ev = 1;
    }
}
