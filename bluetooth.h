#ifndef _BLUETOOTH_H
#define _BLUETOOTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define BLUETOOTH_DEVNAME_MAXLEN (64)

enum bluetooth_error_code {
    BLUETOOTH_ERROR_OPEN  = -1,
};

typedef struct bluetooth_handle bluetooth_t;

/* Primary Functions */
bluetooth_t *bluetooth_new(void);
void bluetooth_free(bluetooth_t *bt);
int bluetooth_open(bluetooth_t *bt, const char *backend);
void bluetooth_close(bluetooth_t *bt);
void bluetooth_scan(bluetooth_t *bt, int timeout);
ssize_t bluetooth_get_devices(bluetooth_t *bt, char devs[][BLUETOOTH_DEVNAME_MAXLEN], int devnum);
/* Return true is connect command is sent. Should use bluetooth_device_is_connected() to check connection status */
bool bluetooth_connect_device(bluetooth_t *bt, const char *device, int timeout);
bool bluetooth_disconnect_device(bluetooth_t *handle, const char *device, int timeout);
bool bluetooth_device_is_connected(bluetooth_t *bt, const char *device);

const char *bluetooth_errmsg(bluetooth_t *bt);

#ifdef __cplusplus
}
#endif

#endif