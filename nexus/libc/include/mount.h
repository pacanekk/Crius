#ifndef NEXUS_MOUNT_H
#define NEXUS_MOUNT_H

#include <sys/types.h>

/* mount(device, path) → 0 on success, -1 on error */
int mount(const char *device, const char *path);

/* umount(path) → 0 on success, -1 on error */
int umount(const char *path);

/* mount_count() → number of active mounts */
int mount_count(void);

/* mount_point(index) → mount path string, or NULL */
char *mount_point(int index);

/* mount_device(index) → device path string, or NULL */
char *mount_device(int index);

#endif
