#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bluetooth.h"

typedef enum device_status {
    DEVICE_CONNECTED,
    DEVICE_DISCONNECTED,
} device_status_t;

bool wait_status(bluetooth_t *bt, const char *device, device_status_t status, int timeout)
{
    do {
        if (status == DEVICE_CONNECTED && bluetooth_device_is_connected(bt, device))
            return true;
        if (status == DEVICE_DISCONNECTED && !bluetooth_device_is_connected(bt, device))
            return true;
        if (timeout > 0)
            sleep(1);
    } while(timeout > 0);
    return false;
}

int main(void)
{
    bluetooth_t *bt = bluetooth_new();
    if (bluetooth_open(bt, "bluetoothctl")) {
        fprintf(stderr, "%s\n", bluetooth_errmsg(bt));
        exit(1);
    }
    int timeout = 0;
    bluetooth_scan(bt, timeout);

    char devs[64][BLUETOOTH_DEVNAME_MAXLEN];
    int i = 0, len;
    len =bluetooth_get_devices(bt, devs, sizeof(devs)/sizeof(devs[0]));
    for(i=0; i<len; i++)
        printf("%2d: %s\n", i, devs[i]);

    const char *device = "WI-XB400";
    printf("%s connecting\n", device);
    bluetooth_connect_device(bt, device, 0);
    wait_status(bt, device, DEVICE_CONNECTED, 5);
    printf("%s connected %s\n", device, wait_status(bt, device, DEVICE_CONNECTED, 5) ? "OK":"fail");

    printf("%s disconnecting\n", device);
    bluetooth_disconnect_device(bt, device, 0);
    printf("%s disconnected %s\n", device, wait_status(bt, device, DEVICE_DISCONNECTED, 5) ? "OK":"fail");

    bluetooth_close(bt);
    bluetooth_free(bt);
}