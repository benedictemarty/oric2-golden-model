/**
 * @file hostfs.c
 * @brief Host filesystem implementation (stub)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include "hostfs/hostfs.h"
#include <string.h>

bool hostfs_init(hostfs_t* hfs) {
    memset(hfs, 0, sizeof(hostfs_t));
    return true;
}

void hostfs_cleanup(hostfs_t* hfs) {
    if (hfs->mounted) {
        hostfs_unmount(hfs);
    }
}

bool hostfs_mount(hostfs_t* hfs, const char* path, bool read_only) {
    strncpy(hfs->mount_path, path, HOSTFS_MAX_PATH - 1);
    hfs->mounted = true;
    hfs->read_only = read_only;
    return true;
}

void hostfs_unmount(hostfs_t* hfs) {
    /* Close all open handles */
    for (int i = 0; i < HOSTFS_MAX_HANDLES; i++) {
        if (hfs->handles[i].in_use) {
            hostfs_close(hfs, i);
        }
    }
    hfs->mounted = false;
}

bool hostfs_is_mounted(const hostfs_t* hfs) {
    return hfs->mounted;
}

int hostfs_open(hostfs_t* hfs, const char* oric_name, bool writing) {
    /* TODO: Implement file opening */
    (void)hfs;
    (void)oric_name;
    (void)writing;
    return -1;
}

bool hostfs_close(hostfs_t* hfs, int handle) {
    if (handle < 0 || handle >= HOSTFS_MAX_HANDLES) {
        return false;
    }

    if (hfs->handles[handle].in_use && hfs->handles[handle].fp) {
        fclose(hfs->handles[handle].fp);
        hfs->handles[handle].in_use = false;
    }

    return true;
}

int hostfs_read(hostfs_t* hfs, int handle, uint8_t* buffer, size_t size) {
    /* TODO: Implement file reading */
    (void)hfs;
    (void)handle;
    (void)buffer;
    (void)size;
    return -1;
}

int hostfs_write(hostfs_t* hfs, int handle, const uint8_t* buffer, size_t size) {
    /* TODO: Implement file writing */
    (void)hfs;
    (void)handle;
    (void)buffer;
    (void)size;
    return -1;
}

bool hostfs_seek(hostfs_t* hfs, int handle, uint32_t position) {
    /* TODO: Implement file seeking */
    (void)hfs;
    (void)handle;
    (void)position;
    return false;
}

uint32_t hostfs_size(hostfs_t* hfs, int handle) {
    if (handle < 0 || handle >= HOSTFS_MAX_HANDLES) {
        return 0;
    }

    if (hfs->handles[handle].in_use) {
        return hfs->handles[handle].size;
    }

    return 0;
}

int hostfs_list(hostfs_t* hfs, char* buffer, size_t buffer_size) {
    /* TODO: Implement directory listing */
    (void)hfs;
    (void)buffer;
    (void)buffer_size;
    return 0;
}

bool hostfs_delete(hostfs_t* hfs, const char* oric_name) {
    /* TODO: Implement file deletion */
    (void)hfs;
    (void)oric_name;
    return false;
}

bool hostfs_rename(hostfs_t* hfs, const char* old_name, const char* new_name) {
    /* TODO: Implement file renaming */
    (void)hfs;
    (void)old_name;
    (void)new_name;
    return false;
}

bool hostfs_oric_to_host_path(const hostfs_t* hfs, const char* oric_name,
                              char* host_path, size_t path_size) {
    /* TODO: Implement path conversion */
    (void)hfs;
    (void)oric_name;
    (void)host_path;
    (void)path_size;
    return false;
}

bool hostfs_host_to_oric_name(const char* host_name, char* oric_name) {
    /* TODO: Implement name conversion */
    strncpy(oric_name, host_name, HOSTFS_MAX_NAME);
    oric_name[HOSTFS_MAX_NAME] = '\0';
    return true;
}
