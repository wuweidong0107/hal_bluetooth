#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "bluetooth_internal.h"

#define min(x, y) (((x) < (y)) ? (x) : (y))

typedef struct bluetooth_device {
    char ident[128];
    char macaddr[32];
    char name[BLUETOOTH_DEVNAME_MAXLEN];
	struct list_head list;
} bluetooth_device_t;

typedef struct bluetoothctl_handle {
    struct list_head devices;
} bluetoothctl_t;

static void* bluetoothctl_init(void)
{
    bluetoothctl_t *btctl;
    int ret;
    
    ret = pclose(popen("bluetoothctl -v >/dev/null 2>&1", "r"));
    if (WIFEXITED(ret) != 1 || WEXITSTATUS(ret) !=0 )
        return NULL;
        
    btctl = calloc(1, sizeof(bluetoothctl_t));
    INIT_LIST_HEAD(&btctl->devices);
    return btctl;
}

static void free_devices(bluetoothctl_t *btctl)
{
    bluetooth_device_t *dev;

    while(!list_empty(&btctl->devices)) {
        dev = list_first_entry(&btctl->devices, bluetooth_device_t, list);
        list_del(&dev->list);
        free(dev);
    }
}

static void __attribute__((unused)) dump_devices(bluetoothctl_t *btctl)
{
    bluetooth_device_t *dev;

	list_for_each_entry(dev, &btctl->devices, list) {
        printf("name: %s, address: %s\n", dev->name, dev->macaddr);
	}
}

static void bluetoothctl_free(void *handle)
{
    bluetoothctl_t *btctl = (bluetoothctl_t *)handle;

    free_devices(btctl);
    if (handle)
        free(handle);
}

static void bluetoothctl_scan(void *handle, int timeout)
{
    bluetoothctl_t *btctl = (bluetoothctl_t *)handle;
    FILE *stream;
    char line[128] = {0};
    char command[128] = {0};

    if(timeout < 1)
        timeout = 1;

    /* Clean up */
    free_devices(btctl);

    /* Fill up */
    pclose(popen("bluetoothctl -- power on", "r"));
    snprintf(command, sizeof(command), "bluetoothctl --timeout %d scan on", timeout);
    pclose(popen(command, "r"));
    stream = popen("bluetoothctl -- devices", "r");
    while(fgets(line, sizeof(line), stream)) {
        if (!strncmp(line, "Device ", strlen("Device "))) {
            char *n = strchr(line, '\n');
            *n = '\0';                       // strip '\n'

            bluetooth_device_t *dev = calloc(1, sizeof(bluetooth_device_t));
            strncpy(dev->ident, line, sizeof(dev->ident));
            dev->ident[sizeof(dev->ident)-1] = '\0'; 

            char *p = NULL;
            char *sbuf = NULL;                  // split ident, format: Device ${MAC} ${name} 
            strtok_r(line, " ", &sbuf);
            p = strtok_r(NULL, " ", &sbuf);
            strncpy(dev->macaddr, p, sizeof(dev->macaddr));
            dev->macaddr[sizeof(dev->macaddr)-1] = '\0'; 
            p = strtok_r(NULL, "\n", &sbuf);
            strncpy(dev->name, p, sizeof(dev->name));
            dev->name[sizeof(dev->name)-1] = '\0'; 
            list_add_tail(&dev->list, &btctl->devices);
            printf("add: %s\n", dev->name);
        }
    }
}

static ssize_t bluetoothctl_get_devices(void *handle, char devs[][BLUETOOTH_DEVNAME_MAXLEN], int devnum)
{
    bluetoothctl_t *btctl = (bluetoothctl_t *)handle;
    bluetooth_device_t *dev;
    ssize_t num = 0;

	list_for_each_entry(dev, &btctl->devices, list) {
        strncpy(devs[num], dev->name, sizeof(devs[num]));
        num++;
        if (num == devnum)
            break;
	}
    return num;
}

static bool bluetoothctl_device_is_connected(void *handle, const char *device)
{
    bluetoothctl_t *btctl = (bluetoothctl_t *)handle;
    bluetooth_device_t *dev;
    char command[128] = {0};
    FILE *stream;
    char line[512] = {0};

    list_for_each_entry(dev, &btctl->devices, list) {
        if(!strncmp(dev->name, device, strlen(device))) {
            snprintf(command, sizeof(command), "bluetoothctl info %s",
            dev->macaddr);
            stream = popen(command, "r");
            while(fgets(line, 512, stream)) {
                if (strstr(line, "Connected: yes"))
                    return true;
            }
        }
    }
    return false;
}

static bool bluetoothctl_connect_device(void *handle, const char *device, int timeout)
{
    bluetoothctl_t *btctl = (bluetoothctl_t *)handle;
    bluetooth_device_t *dev;
    char command[128] = {0};
    int ret;

    if(bluetoothctl_device_is_connected(btctl, device))
        return true;

	list_for_each_entry(dev, &btctl->devices, list) {
        if(!strncmp(dev->name, device, strlen(device))) {
            snprintf(command, sizeof(command), "bluetoothctl -- pairable on");
            pclose(popen(command, "r"));

            snprintf(command, sizeof(command), "bluetoothctl -- pair %s",
                    dev->macaddr);
            pclose(popen(command, "r"));

            snprintf(command, sizeof(command), "bluetoothctl -- trust %s",
                    dev->macaddr);
            pclose(popen(command, "r"));

            snprintf(command, sizeof(command), "bluetoothctl --timeout %d connect %s",
                    timeout,
                    dev->macaddr);
            ret = pclose(popen(command, "r"));
            return WIFEXITED(ret);
        }
	}

    /* connect command not executed */
    return false;
}

static bool bluetoothctl_disconnect_device(void *handle, const char *device, int timeout)
{
    bluetoothctl_t *btctl = (bluetoothctl_t *)handle;
    bluetooth_device_t *dev;
    char command[128] = {0};
    int ret;

    if (!bluetoothctl_device_is_connected(handle, device))
        return true;

    list_for_each_entry(dev, &btctl->devices, list) {
        if(!strncmp(dev->name, device, strlen(device))) {
            snprintf(command, sizeof(command), "bluetoothctl --timeout %d disconnect %s",
                    timeout,
                    dev->macaddr);
            ret = pclose(popen(command, "r"));
            return WIFEXITED(ret);
        }
    }

    /* disconnect command not executed */
    return false;
}

bluetooth_backend_t bluetooth_bluetoothctl = {
    bluetoothctl_init,
    bluetoothctl_free,
    bluetoothctl_scan,
    bluetoothctl_get_devices,
    bluetoothctl_device_is_connected,
    bluetoothctl_connect_device,
    bluetoothctl_disconnect_device,
    "bluetoothctl"
};