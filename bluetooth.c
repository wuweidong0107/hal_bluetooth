#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "bluetooth_internal.h"
#include "bluetooth.h"

struct bluetooth_handle {
    const bluetooth_backend_t *backend;
    void *backend_handle;

    struct {
        int c_errno;
        char errmsg[128];
    } error;
};

const bluetooth_backend_t *bluetooth_backends[] = {
    &bluetooth_bluetoothctl,
    NULL,
};

static int _bluetooth_error(bluetooth_t *bt, int code, int c_errno, const char *fmt, ...)
{
    va_list ap;
    
    bt->error.c_errno = c_errno;
    va_start(ap, fmt);
    vsnprintf(bt->error.errmsg, sizeof(bt->error.errmsg), fmt, ap);
    va_end(ap);

    if (c_errno) {
        char buf[64];
        strerror_r(c_errno, buf, sizeof(buf));
        snprintf(bt->error.errmsg + strlen(bt->error.errmsg),
                 sizeof(bt->error.errmsg) - strlen(bt->error.errmsg),
                 ": %s [errno %d]", buf, c_errno);
    }

    return code;
}

bool bluetooth_device_is_connected(bluetooth_t *bt, const char *device)
{
    if (bt && bt->backend && bt->backend->device_is_connected)
        return bt->backend->device_is_connected(bt->backend_handle, device);
    
    return false;
}

bool bluetooth_disconnect_device(bluetooth_t *bt, const char *device, int timeout)
{
    if (bt && bt->backend && bt->backend->disconnect_device)
        return bt->backend->disconnect_device(bt->backend_handle, device, timeout);
    
    return false;
}

bool bluetooth_connect_device(bluetooth_t *bt, const char *device, int timeout)
{
    if (bt && bt->backend && bt->backend->connect_device)
        return bt->backend->connect_device(bt->backend_handle, device, timeout);
    
    return false;
}

ssize_t bluetooth_get_devices(bluetooth_t *bt, char devs[][BLUETOOTH_DEVNAME_MAXLEN], int devnum)
{
    if (bt && bt->backend && bt->backend->get_devices)
        return bt->backend->get_devices(bt->backend_handle, devs, devnum);
    
    return false;
}

void bluetooth_scan(bluetooth_t *bt, int timeout)
{
    if (bt && bt->backend && bt->backend->scan)
        bt->backend->scan(bt->backend_handle, timeout);
}

int bluetooth_open(bluetooth_t *bt, const char *backend)
{
    int i;

    if (backend == NULL)
        return _bluetooth_error(bt, BLUETOOTH_ERROR_OPEN, 0, "Bluetooth backend param invalid");

    for(i=0; bluetooth_backends[i]; i++) {
        if (!strncmp(backend, bluetooth_backends[i]->ident, strlen(backend))) {
            bt->backend = bluetooth_backends[i];
            break;
        }
    }
    if (bt->backend == NULL)
        return _bluetooth_error(bt, BLUETOOTH_ERROR_OPEN, 0, "Bluetooth backend %s not found", backend);

    if(bt->backend->init) {
        bt->backend_handle = bt->backend->init();
        if (bt->backend_handle == NULL)
            return _bluetooth_error(bt, BLUETOOTH_ERROR_OPEN, 0, "Bluetooth init fail", backend);
    } else {
        return _bluetooth_error(bt, BLUETOOTH_ERROR_OPEN, 0, "Bluetooth backend %s not implemented yet", backend);
    }

    return 0;
}

void bluetooth_close(bluetooth_t *bt)
{
    if (bt == NULL || bt->backend == NULL)
        return;
    
    if (bt->backend->free)
        bt->backend->free(bt->backend_handle);
}

bluetooth_t *bluetooth_new(void)
{
    bluetooth_t *bt = calloc(1, sizeof(bluetooth_t));
    if (bt == NULL)
        return NULL;
    
    return bt;
}

void bluetooth_free(bluetooth_t *bt)
{
    free(bt);
}

const char *bluetooth_errmsg(bluetooth_t *bt)
{
    return bt->error.errmsg;
}