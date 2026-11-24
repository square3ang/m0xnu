/*
 * /usr/libexec/HorizonSetup: launchd runs it as the "restore-datapartition" Boot task, the
 * one task that still runs in the restore environment (rd=md0) and that iOS 9 does not ship.
 * It mounts the fstab entries (md1 on /private/var), which launchd skips in that mode.
 */
extern int umask(int mask);
extern void *sem_open(const char *name, int oflag, unsigned int mode, unsigned int value);
extern int fork(void);
extern int waitpid(int pid, int *status, int options);
extern int execv(const char *path, char *const argv[]);
extern void _exit(int status);
extern int mkdir(const char *path, unsigned int mode);
extern int chown(const char *path, unsigned int uid, unsigned int gid);
extern int chmod(const char *path, unsigned int mode);
extern int symlink(const char *target, const char *linkpath);
struct timeval { long tv_sec; int tv_usec; };
extern int gettimeofday(struct timeval *tv, void *tz);
extern int settimeofday(const struct timeval *tv, const void *tz);
extern int open(const char *path, int oflag, ...);
extern long write(int fd, const void *buf, unsigned long n);
extern int close(int fd);
extern int setsid(void);
extern int unlink(const char *path);
extern int snprintf(char *buf, unsigned long sz, const char *fmt, ...);
extern void *dlsym(void *handle, const char *symbol);

static const char *dirent_name(const unsigned char *ent)
{
    /* Darwin 32-bit dirent: d_type@6 (<=15), d_namlen@7, d_name@8 (NUL at 8+namlen). */
    unsigned nl = ent[7];
    if (ent[6] <= 15u && nl > 0u && nl < 255u && ent[8 + nl] == 0u &&
        ent[8] >= 0x20u && ent[8] <= 0x7eu)
        return (const char *)(ent + 8);
    /* Fallback: 64-bit dirent (d_name@21). */
    return (const char *)(ent + 21);
}

static int unlink_ls_dir(const char *dir)
{
    typedef void *(*od_t)(const char *);
    typedef void *(*rd_t)(void *);
    typedef int (*cd_t)(void *);
    od_t fn_od = (od_t)dlsym((void *)-2, "opendir");
    rd_t fn_rd = (rd_t)dlsym((void *)-2, "readdir");
    cd_t fn_cd = (cd_t)dlsym((void *)-2, "closedir");
    if (!fn_od || !fn_rd || !fn_cd) return 0;
    void *dp = fn_od(dir);
    if (!dp) return 0;
    char pbuf[256];
    int cnt = 0;
    for (;;) {
        unsigned char *ent = (unsigned char *)fn_rd(dp);
        if (!ent) break;
        const char *name = dirent_name(ent);
        if (name[0] == '.') continue;
        snprintf(pbuf, sizeof(pbuf), "%s/%s", dir, name);
        if (unlink(pbuf) == 0) cnt++;
    }
    fn_cd(dp);
    return cnt;
}

static void mk_dir(const char *path, unsigned int uid, unsigned int gid)
{
    mkdir(path, 0755);
    chown(path, uid, gid);
}

/* shortcut: disable hardware-missing / cloud-only daemons so boot stays fast on cpus=1,
 * while keeping Setup.app (purplebuddy.budd) and stock app daemons (profiled, calaccessd,
 * assetsd, timed, medialibraryd, healthd, mediaserverd) enabled. */
static const char disabled_plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\"><dict>\n"
    "  <key>com.apple.CommCenter</key><true/>\n"
    "  <key>com.apple.telephonyutilities.callservicesd</key><true/>\n"
    "  <key>com.apple.backupd</key><true/>\n"
    "  <key>com.apple.BTServer</key><true/>\n"
    "  <key>com.apple.apsd</key><true/>\n"
    "  <key>com.apple.itunesstored</key><true/>\n"
    "  <key>com.apple.fairplayd.H1</key><true/>\n"
    "  <key>com.apple.mobileactivationd</key><true/>\n"
    "  <key>com.apple.geod</key><true/>\n"
    "  <key>com.apple.atc</key><true/>\n"
    "  <key>com.apple.imagent</key><true/>\n"
    "  <key>com.apple.identityservicesd</key><true/>\n"
    "  <key>com.apple.locationd</key><true/>\n"
    "  <key>com.apple.wifid</key><true/>\n"
    "  <key>com.apple.wifiFirmwareLoader</key><true/>\n"
    "  <key>com.apple.assistantd</key><true/>\n"
    "  <key>com.apple.assistant_service</key><true/>\n"
    "  <key>com.apple.awdd</key><true/>\n"
    "  <key>com.apple.symptomsd</key><true/>\n"
    "  <key>com.apple.hangtracerd</key><true/>\n"
    "  <key>com.apple.passd</key><true/>\n"
    "  <key>com.apple.searchd</key><true/>\n"
    "  <key>com.apple.itunescloudd</key><true/>\n"
    "  <key>com.apple.voiced</key><true/>\n"
    "  <key>com.apple.usbptpd</key><true/>\n"
    "  <key>com.apple.misd</key><true/>\n"
    "  <key>com.apple.iaptransportd</key><true/>\n"
    "  <key>com.apple.mobileassetd</key><true/>\n"
    "  <key>com.apple.lskdd</key><true/>\n"
    "  <key>com.apple.lskdmsed</key><true/>\n"
    "  <key>com.apple.aggregated</key><true/>\n"
    "  <key>com.apple.coreduetd</key><true/>\n"
    "  <key>com.apple.duetexpertd</key><true/>\n"
    "  <key>com.apple.sharingd</key><true/>\n"
    "  <key>com.apple.routined</key><true/>\n"
    "  <key>com.apple.cloudd</key><true/>\n"
    "  <key>com.apple.bird</key><true/>\n"
    "  <key>com.apple.suggestd</key><true/>\n"
    "  <key>com.apple.gamed</key><true/>\n"
    "  <key>com.apple.homed</key><true/>\n"
    "  <key>com.apple.adid</key><true/>\n"
    "  <key>com.apple.askpermissiond</key><true/>\n"
    "  <key>com.apple.EscrowSecurityAlert</key><true/>\n"
    "  <key>com.apple.familycircled</key><true/>\n"
    "  <key>com.apple.datamigrator</key><true/>\n"
    "</dict></plist>\n";

static const char data_ark_plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\"><dict>\n"
    "  <key>-ActivationState</key><string>FactoryActivated</string>\n"
    "  <key>-ActivationStateAcknowledged</key><true/>\n"
    "  <key>-BrickState</key><false/>\n"
    "</dict></plist>\n";

static const char global_prefs_plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\"><dict>\n"
    "  <key>AppleLocale</key><string>en_US</string>\n"
    "  <key>AppleLanguages</key><array><string>en-US</string></array>\n"
    "</dict></plist>\n";

static const char sb_prefs_plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\"><dict>\n"
    "  <key>SBDontDimOrLockOnAC</key><true/>\n"
    "  <key>SBAutoDimTime</key><integer>-1</integer>\n"
    "  <key>SBAutoLockTime</key><integer>-1</integer>\n"
    "  <key>defaultLockScreenDimInterval</key><real>3600</real>\n"
    "</dict></plist>\n";

/* Mark 9.3.5 (13G36) data migration complete so backboardd/DataMigrator doesn't block UI with 0% progress bar. */
static const char migration_plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\"><dict>\n"
    "  <key>LastSystemVersion</key><string>13G36</string>\n"
    "  <key>LastBuildVersion</key><string>13G36</string>\n"
    "</dict></plist>\n";

static void write_disabled(void)
{
    int fd = open("/private/var/db/com.apple.xpc.launchd/disabled.plist",
                  0x0601 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644);
    if (fd >= 0) {
        (void)write(fd, disabled_plist, sizeof(disabled_plist) - 1);
        close(fd);
    }
    fd = open("/private/var/mobile/Library/Preferences/.GlobalPreferences.plist",
              0x0a01 /* O_WRONLY|O_CREAT|O_EXCL */, 0644);
    if (fd >= 0) {
        (void)write(fd, global_prefs_plist, sizeof(global_prefs_plist) - 1);
        close(fd);
        chown("/private/var/mobile/Library/Preferences/.GlobalPreferences.plist", 501, 501);
    }
    fd = open("/private/var/mobile/Library/Preferences/com.apple.springboard.plist",
              0x0601 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644);
    if (fd >= 0) {
        (void)write(fd, sb_prefs_plist, sizeof(sb_prefs_plist) - 1);
        close(fd);
        chown("/private/var/mobile/Library/Preferences/com.apple.springboard.plist", 501, 501);
    }
    fd = open("/private/var/mobile/Library/Preferences/com.apple.migration.plist",
              0x0601 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644);
    if (fd >= 0) {
        (void)write(fd, migration_plist, sizeof(migration_plist) - 1);
        close(fd);
        chown("/private/var/mobile/Library/Preferences/com.apple.migration.plist", 501, 501);
    }
    /* com.apple.datamigrator reads LastSystemVersion via kCFPreferencesCurrentHost -> ByHost/<domain>.<IOPlatformUUID>.plist */
    mk_dir("/private/var/mobile/Library/Preferences/ByHost", 501, 501);
    {
        const char *bh[2] = {
            "/private/var/mobile/Library/Preferences/ByHost/com.apple.migration.5333494F-5339-5335-8558-593434313221.plist",
            "/private/var/mobile/Library/Preferences/ByHost/com.apple.migration.00000000-0000-0000-0000-000000000000.plist"
        };
        for (int i = 0; i < 2; i++) {
            fd = open(bh[i], 0x0601, 0644);
            if (fd >= 0) {
                (void)write(fd, migration_plist, sizeof(migration_plist) - 1);
                close(fd);
                chown(bh[i], 501, 501);
            }
        }
    }
    /* Mirror migration + purplebuddy prefs into /private/var/root/Library/Preferences (matching var-j4.raw). */
    mk_dir("/private/var/root", 0, 0);
    mk_dir("/private/var/root/Library", 0, 0);
    mk_dir("/private/var/root/Library/Preferences", 0, 0);
    mk_dir("/private/var/root/Library/Preferences/ByHost", 0, 0);
    fd = open("/private/var/root/Library/Preferences/com.apple.migration.plist",
              0x0601 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644);
    if (fd >= 0) { (void)write(fd, migration_plist, sizeof(migration_plist) - 1); close(fd); }
    fd = open("/private/var/root/Library/Preferences/ByHost/com.apple.migration.5333494F-5339-5335-8558-593434313221.plist",
              0x0601, 0644);
    if (fd >= 0) { (void)write(fd, migration_plist, sizeof(migration_plist) - 1); close(fd); }
    /* Provide FactoryActivated state for lockdownd & Setup.app (no Wi-Fi/SIM hardware). */
    mk_dir("/private/var/root/Library/Lockdown", 0, 0);
    fd = open("/private/var/root/Library/Lockdown/data_ark.plist",
              0x0601 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644);
    if (fd >= 0) {
        (void)write(fd, data_ark_plist, sizeof(data_ark_plist) - 1);
        close(fd);
    }
    /* One-time Setup.app reset: clear previous SetupDone flags and LaunchServices/installd caches ONCE so
     * iOS rescans /Applications/Setup.app and boots into the genuine "Hello" Setup Assistant. */
    int mfd = open("/private/var/mobile/Library/Preferences/.exy_setup_reset_v3",
                   0x0a01 /* O_WRONLY|O_CREAT|O_EXCL */, 0644);
    int wiped = 0;
    if (mfd >= 0) {
        close(mfd);
        unlink("/private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist");
        unlink("/private/var/mobile/Library/Preferences/com.apple.purplebuddy.notbackedup.plist");
        unlink("/private/var/root/Library/Preferences/com.apple.purplebuddy.plist");
        if (unlink("/private/var/mobile/Library/MobileInstallation/LastBuildInfo.plist") == 0) wiped++;
        if (unlink("/private/var/installd/Library/MobileInstallation/LastBuildInfo.plist") == 0) wiped++;
        if (unlink("/private/var/mobile/Library/MobileInstallation/LastLaunchServicesMap.plist") == 0) wiped++;
        if (unlink("/private/var/installd/Library/MobileInstallation/LastLaunchServicesMap.plist") == 0) wiped++;
        if (unlink("/private/var/mobile/Library/Caches/com.apple.mobile.installation.plist") == 0) wiped++;
        if (unlink("/private/var/installd/Library/Caches/com.apple.mobile.installation.plist") == 0) wiped++;
        if (unlink("/private/var/mobile/Library/Caches/com.apple.lsdidentifiers.plist") == 0) wiped++;
        if (unlink("/private/var/mobile/Library/FrontBoard/applicationState.plist") == 0) wiped++;
        wiped += unlink_ls_dir("/private/var/mobile/Library/Caches");
        wiped += unlink_ls_dir("/private/var/installd/Library/Caches");
        wiped += unlink_ls_dir("/private/var/installd/Library/MobileInstallation");
        wiped += unlink_ls_dir("/private/var/mobile/Library/MobileInstallation");
        /* iOS 9.3.5 lsd & installd store caches inside Shared/SystemGroup & Data/System & Data/InternalDaemon */
        typedef void *(*od_t)(const char *);
        typedef void *(*rd_t)(void *);
        typedef int (*cd_t)(void *);
        od_t fn_od = (od_t)dlsym((void *)-2, "opendir");
        rd_t fn_rd = (rd_t)dlsym((void *)-2, "readdir");
        cd_t fn_cd = (cd_t)dlsym((void *)-2, "closedir");
        if (fn_od && fn_rd && fn_cd) {
            const char *roots[4] = {
                "/private/var/containers/Shared/SystemGroup",
                "/private/var/mobile/Containers/Shared/SystemGroup",
                "/private/var/containers/Data/System",
                "/private/var/containers/Data/InternalDaemon"
            };
            for (int r = 0; r < 4; r++) {
                void *dp = fn_od(roots[r]);
                if (!dp) continue;
                char cdir[256];
                for (;;) {
                    unsigned char *ent = (unsigned char *)fn_rd(dp);
                    if (!ent) break;
                    const char *name = dirent_name(ent);
                    if (name[0] == '.') continue;
                    snprintf(cdir, sizeof(cdir), "%s/%s/Library/Caches", roots[r], name);
                    wiped += unlink_ls_dir(cdir);
                    snprintf(cdir, sizeof(cdir), "%s/%s/Library/Caches/com.apple.lsd", roots[r], name);
                    wiped += unlink_ls_dir(cdir);
                    snprintf(cdir, sizeof(cdir), "%s/%s/Library/MobileInstallation", roots[r], name);
                    wiped += unlink_ls_dir(cdir);
                }
                fn_cd(dp);
            }
        }
    }
    {
        int cfd = open("/dev/console", 1);
        if (cfd >= 0) {
            char msg[96];
            int n = snprintf(msg, sizeof(msg), "[HORIZON] setup_reset_v3 mfd=%d wiped=%d\n", mfd, wiped);
            if (n > 0) (void)write(cfd, msg, (unsigned long)n);
            close(cfd);
        }
    }
}

int main(void)
{
    /* Work around iOS 9.3.5 lsd first-boot NULL deref (MobileCoreServices+0x49cdc) when
     * _LSDatabaseGetSession returns kLSNoDatabaseErr and sem_open("lsd.firstboot_check", 0) fails. */
    umask(0);
    chmod("/dev/console", 0666);
    (void)sem_open("lsd.firstboot_check", 0x0200 /* O_CREAT */, 0666, 1);

    int pid = fork();
    if (pid == 0) {
        char *argv[] = { "mount", "-vat", "nonfs", 0 };
        execv("/sbin/mount", argv);
        _exit(1);
    }
    int st = 0;
    if (pid > 0)
        waitpid(pid, &st, 0);

    /* Create iOS 9 container roots on /private/var (owned by mobile 501:501) so
     * containermanagerd succeeds for backboardd, BTServer, apsd, and installd. */
    mk_dir("/private/var/containers", 0, 0);
    mk_dir("/private/var/containers/Bundle", 501, 501);
    mk_dir("/private/var/containers/Bundle/Application", 501, 501);
    mk_dir("/private/var/containers/Data", 501, 501);
    mk_dir("/private/var/containers/Data/Application", 501, 501);
    mk_dir("/private/var/containers/Data/InternalDaemon", 501, 501);
    mk_dir("/private/var/containers/Data/PluginKitPlugin", 501, 501);
    mk_dir("/private/var/containers/Data/System", 501, 501);
    mk_dir("/private/var/containers/Data/VPNPlugin", 501, 501);
    mk_dir("/private/var/containers/Shared", 501, 501);
    mk_dir("/private/var/containers/Shared/SystemGroup", 501, 501);
    mk_dir("/private/var/containers/Temp", 501, 501);
    mk_dir("/private/var/mobile/Containers", 501, 501);
    mk_dir("/private/var/mobile/Containers/Data", 501, 501);
    mk_dir("/private/var/mobile/Containers/Shared", 501, 501);
    /* Stock app data directories (owned by mobile 501:501) */
    mk_dir("/private/var/mobile/Library/Notes", 501, 501);
    mk_dir("/private/var/mobile/Library/Calendar", 501, 501);
    mk_dir("/private/var/mobile/Library/AddressBook", 501, 501);
    mk_dir("/private/var/mobile/Library/Caches", 501, 501);
    mk_dir("/private/var/mobile/Library/SpringBoard", 501, 501);
    mk_dir("/private/var/mobile/Media", 501, 501);
    mk_dir("/private/var/mobile/Media/DCIM", 501, 501);
    mk_dir("/private/var/mobile/Media/PhotoData", 501, 501);
    mk_dir("/private/var/mobile/Documents", 501, 501);
    /* Fix dangling /usr/share/zoneinfo, /var/db/timezone/localtime, and /var/db/icu so ICU/NSDateFormatter work. */
    symlink("/usr/share/zoneinfo.default", "/private/var/db/timezone/zoneinfo");
    symlink("/usr/share/icu", "/private/var/db/icu");
    /* Preserve valid hardware RTC clock; only seed 2016-09-01 if RTC is unset. */
    struct timeval cur = { 0, 0 };
    if (gettimeofday(&cur, 0) != 0 || cur.tv_sec < 1451606400L) {
        struct timeval tv = { 1472688000L, 0 }; /* 2016-09-01 00:00:00 UTC */
        settimeofday(&tv, 0);
    }
    write_disabled();
    {
        typedef void *(*od_t)(const char *);
        typedef void *(*rd_t)(void *);
        typedef int (*cd_t)(void *);
        typedef long (*read_t)(int, void *, unsigned long);
        od_t fn_od = (od_t)dlsym((void *)-2, "opendir");
        rd_t fn_rd = (rd_t)dlsym((void *)-2, "readdir");
        cd_t fn_cd = (cd_t)dlsym((void *)-2, "closedir");
        read_t fn_read = (read_t)dlsym((void *)-2, "read");
        int cfd = open("/dev/console", 1);
        if (cfd >= 0 && fn_od && fn_rd && fn_cd && fn_read) {
            const char *dirs[2] = {
                "/private/var/mobile/Library/Preferences/ByHost",
                "/private/var/mobile/Library/Logs/CrashReporter/DiagnosticLogs/DataMigration"
            };
            char pbuf[256], buf[2048];
            for (int d = 0; d < 2; d++) {
                void *dp = fn_od(dirs[d]);
                if (!dp) continue;
                int dumped = 0;
                for (;;) {
                    unsigned char *ent = (unsigned char *)fn_rd(dp);
                    if (!ent) break;
                    const char *nm = dirent_name(ent);
                    if (nm[0] == '.') continue;
                    int n = snprintf(pbuf, sizeof(pbuf), "[DM%d] %s\n", d, nm);
                    if (n > 0) (void)write(cfd, pbuf, (unsigned long)n);
                    if (d == 1 && dumped < 2) {
                        snprintf(pbuf, sizeof(pbuf), "%s/%s", dirs[d], nm);
                        int fd = open(pbuf, 0);
                        if (fd >= 0) {
                            long r = fn_read(fd, buf, sizeof(buf) - 1);
                            close(fd);
                            if (r > 0) { (void)write(cfd, buf, (unsigned long)r); (void)write(cfd, "\n", 1); dumped++; }
                        }
                    }
                }
                fn_cd(dp);
            }
            close(cfd);
        }
    }
    /* ExyTouchd is now spawned from backboardd (Mali400GLDriverEX) so it inherits a live System bootstrap_port. */
    return 0;
}
