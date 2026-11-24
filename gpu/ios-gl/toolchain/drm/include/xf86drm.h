#ifndef XF86DRM_H
#define XF86DRM_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "drm-uapi/drm.h"

#define DRM_NODE_PRIMARY 0
#define DRM_NODE_CONTROL 1
#define DRM_NODE_RENDER  2
#define DRM_BUS_PLATFORM 2
#define DRM_CAP_SYNCOBJ  0x13

typedef struct _drmVersion {
    int version_major, version_minor, version_patchlevel;
    int name_len; char *name;
    int date_len; char *date;
    int desc_len; char *desc;
} drmVersion, *drmVersionPtr;

typedef struct _drmPlatformDeviceInfo {
    char *fullname;
    char **compatible;
} drmPlatformDeviceInfo, *drmPlatformDeviceInfoPtr;

typedef struct _drmDevice {
    char **nodes;
    int available_nodes;
    int bustype;
    union { drmPlatformDeviceInfoPtr platform; } deviceinfo;
} drmDevice, *drmDevicePtr;

int drmIoctl(int fd, unsigned long request, void *arg);
drmVersionPtr drmGetVersion(int fd);
void drmFreeVersion(drmVersionPtr v);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);
int drmCloseBufferHandle(int fd, uint32_t handle);
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec, unsigned flags, uint32_t *first_signaled);
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t handle_count);
int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle);
int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd);
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);
char *drmGetRenderDeviceNameFromFd(int fd);
char *drmGetPrimaryDeviceNameFromFd(int fd);
int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device);
void drmFreeDevice(drmDevicePtr *device);
#endif
