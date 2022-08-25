#ifndef __BLUETOOTH_INTERNAL_H__
#define __BLUETOOTH_INTERNAL_H__

#include <stdbool.h>

#ifndef BLUETOOTH_DEVNAME_MAXLEN
#define BLUETOOTH_DEVNAME_MAXLEN    (64)
#endif

typedef struct bluetooth_backend
{
    void* (*init)(void);
    void (*free)(void *handle);
    void (*scan)(void *handle, int timeout);
    int (*get_devices)(void *handle, char devs[][BLUETOOTH_DEVNAME_MAXLEN], int devnum);
    bool (*device_is_connected)(void *handle, const char *device);
    bool (*connect_device)(void *handle, const char *device, int timeout);
    bool (*disconnect_device)(void *handle, const char *device, int timeout);

    const char *ident;
} bluetooth_backend_t;

extern bluetooth_backend_t bluetooth_bluetoothctl;
extern bluetooth_backend_t bluetooth_bluez;

#endif