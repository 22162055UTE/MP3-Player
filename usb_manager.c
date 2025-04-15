#include "usb_manager.h"
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "error_handler.h"

#define MAX_RETRIES 10
#define RETRY_DELAY 3

static struct udev* udev = NULL;

static bool check_mount_point(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }
    return false;
}

static bool find_usb_mount_point(char** mount_path) {
    *mount_path = NULL;

    struct udev_enumerate* enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "block");
    udev_enumerate_add_match_property(enumerate, "DEVTYPE", "partition");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry* entry;
    udev_list_entry_foreach(entry, devices) {
        const char* path = udev_list_entry_get_name(entry);
        struct udev_device* dev = udev_device_new_from_syspath(udev, path);

        const char* id_bus = udev_device_get_property_value(dev, "ID_BUS");
        const char* devname = udev_device_get_devnode(dev);
        const char* id_fs_type = udev_device_get_property_value(dev, "ID_FS_TYPE");

        printf("Checking device: %s, ID_BUS: %s, ID_FS_TYPE: %s\n",
               devname ? devname : "unknown",
               id_bus ? id_bus : "none",
               id_fs_type ? id_fs_type : "none");

        if (id_bus && strcmp(id_bus, "usb") == 0 && id_fs_type && strlen(id_fs_type) > 0) {
            const char* mount = udev_device_get_property_value(dev, "ID_MOUNT_POINT");
            const char* udisks_mount = udev_device_get_property_value(dev, "UDISKS_MOUNT_PATH");

            if (mount && strlen(mount) > 0 && check_mount_point(mount)) {
                *mount_path = strdup(mount);
                printf("Found mount point from ID_MOUNT_POINT: %s\n", *mount_path);
                udev_device_unref(dev);
                udev_enumerate_unref(enumerate);
                return true;
            } else if (udisks_mount && strlen(udisks_mount) > 0 && check_mount_point(udisks_mount)) {
                *mount_path = strdup(udisks_mount);
                printf("Found mount point from UDISKS_MOUNT_PATH: %s\n", *mount_path);
                udev_device_unref(dev);
                udev_enumerate_unref(enumerate);
                return true;
            } else if (devname && strlen(devname) > 0) {
                // Nếu không có thông tin mount point, tìm trong /proc/mounts
                FILE* mounts = fopen("/proc/mounts", "r");
                if (mounts) {
                    char line[512];
                    while (fgets(line, sizeof(line), mounts)) {
                        if (strstr(line, devname)) {
                            char device[128], mountpoint[256];
                            sscanf(line, "%127s %255s", device, mountpoint);
                            if (check_mount_point(mountpoint)) {
                                *mount_path = strdup(mountpoint);
                                printf("Found mount point from /proc/mounts: %s\n", *mount_path);
                                fclose(mounts);
                                udev_device_unref(dev);
                                udev_enumerate_unref(enumerate);
                                return true;
                            }
                        }
                    }
                    fclose(mounts);
                }
            }

            printf("USB found but no valid mount point\n");
        }

        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    return false;
}

bool usb_init(char** mount_path) {
    *mount_path = NULL;

    udev = udev_new();
    if (!udev) {
        error_log(ERR_USB, "Cannot create udev context");
        return false;
    }

    for (int i = 0; i < MAX_RETRIES; i++) {
        if (find_usb_mount_point(mount_path)) {
            return true;
        }
        printf("No USB found, retrying in %d seconds... (Attempt %d/%d)\n",
               RETRY_DELAY, i + 1, MAX_RETRIES);
        sleep(RETRY_DELAY);
    }

    struct udev_monitor* mon = udev_monitor_new_from_netlink(udev, "udev");
    if (!mon) {
        error_log(ERR_USB, "Cannot create udev monitor");
        udev_unref(udev);
        return false;
    }

    udev_monitor_filter_add_match_subsystem_devtype(mon, "block", "partition");
    udev_monitor_enable_receiving(mon);

    printf("Waiting for USB device...\n");
    while (true) {
        struct udev_device* dev = udev_monitor_receive_device(mon);
        if (dev) {
            const char* action = udev_device_get_action(dev);
            const char* id_bus = udev_device_get_property_value(dev, "ID_BUS");
            const char* id_fs_type = udev_device_get_property_value(dev, "ID_FS_TYPE");

            printf("Event: %s, ID_BUS: %s, ID_FS_TYPE: %s\n",
                   action ? action : "none",
                   id_bus ? id_bus : "none",
                   id_fs_type ? id_fs_type : "none");

            if (action && strcmp(action, "add") == 0 && id_bus && strcmp(id_bus, "usb") == 0 &&
                id_fs_type && strlen(id_fs_type) > 0) {
                sleep(RETRY_DELAY);
                udev_device_unref(dev);
                udev_monitor_unref(mon);
                return find_usb_mount_point(mount_path);
            }

            udev_device_unref(dev);
        }
    }

    udev_monitor_unref(mon);
    return false;
}

void usb_cleanup(void) {
    if (udev) {
        udev_unref(udev);
        udev = NULL;
    }
}
