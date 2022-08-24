#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dbus/dbus.h>

#include "list.h"
#include "bluetooth_internal.h"

typedef struct bluetooth_device {
   /* object path. usually looks like /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF */
   char path[128];

   /* for display purposes */
   char name[BLUETOOTH_DEVNAME_MAXLEN];

   /* MAC address, 17 bytes */
   char macaddr[32];

   /* freedesktop.org icon name
    * See bluez/src/dbus-common.c
    * Can be NULL */
   char icon[64];

   int connected;
   int paired;
   int trusted;

	struct list_head list;
} bluetooth_device_t;

typedef struct bluez_handle {
    DBusConnection *dbus_connection;
    char adapter[256];
    struct list_head devices;
} bluez_t;

static void* bluez_init(void)
{
    bluez_t *bluez;

    bluez = calloc(1, sizeof(bluez_t));
    INIT_LIST_HEAD(&bluez->devices);
    return bluez;
}

static void bluez_free(void *handle)
{
    bluez_t *bluez = (bluez_t *)handle;
    bluetooth_device_t *dev;

    while(!list_empty(&bluez->devices)) {
        dev = list_first_entry(&bluez->devices, bluetooth_device_t, list);
        list_del(&dev->list);
        free(dev);
    }

    if (handle)
        free(handle);
}

static int set_bool_property(
                bluez_t * bluez, 
                const char *path, 
                const char *arg_adapter, 
                const char *arg_property,
                int value,
                int timeout)
{
    DBusError err;
    DBusMessage *message, *reply;
    DBusMessageIter req_iter, req_subiter;

    dbus_error_init(&err);

    message = dbus_message_new_method_call(
        "org.bluez",
        path,
        "org.freedesktop.DBus.Properties",
        "Set"
    );
    if (!message)
        return 1;
    
    dbus_message_iter_init_append(message, &req_iter);
    if (!dbus_message_iter_append_basic(
            &req_iter, DBUS_TYPE_STRING, &arg_adapter))
        goto fault;
    if (!dbus_message_iter_append_basic(
            &req_iter, DBUS_TYPE_STRING, &arg_property))
        goto fault;
    if (!dbus_message_iter_open_container(
            &req_iter, DBUS_TYPE_VARIANT,
            DBUS_TYPE_BOOLEAN_AS_STRING, &req_subiter))
        goto fault;
    if (!dbus_message_iter_append_basic(
            &req_subiter, DBUS_TYPE_BOOLEAN, &value))
        goto fault;
    if (!dbus_message_iter_close_container(
            &req_iter, &req_subiter))
        goto fault;

    reply = dbus_connection_send_with_reply_and_block(bluez->dbus_connection,
        message, 1000 * timeout, &err);
    if (!reply)
        goto fault;
    dbus_message_unref(reply);
    dbus_message_unref(message);
    return 0;

fault:
    dbus_message_iter_abandon_container_if_open(&req_iter, &req_subiter);
    dbus_message_unref(message);
    return 1;
}

static int get_bool_property(
                bluez_t *bluez,
                const char *path,
                const char *arg_adapter,
                const char *arg_property,
                int *value,
                int timeout)
{
    DBusMessage *message, *reply;
    DBusError err;
    DBusMessageIter root_iter, variant_iter;

    dbus_error_init(&err);

    message = dbus_message_new_method_call( "org.bluez", path,
        "org.freedesktop.DBus.Properties", "Get");
    if (!message)
        return 1;

    if (!dbus_message_append_args(message,
        DBUS_TYPE_STRING, &arg_adapter,
        DBUS_TYPE_STRING, &arg_property,
        DBUS_TYPE_INVALID))
        return 1;

    reply = dbus_connection_send_with_reply_and_block(bluez->dbus_connection,
        message, 1000 * timeout, &err);

    dbus_message_unref(message);

    if (!reply)
        return 1;

    if (!dbus_message_iter_init(reply, &root_iter))
        return 1;

    if (DBUS_TYPE_VARIANT != dbus_message_iter_get_arg_type(&root_iter))
        return 1;

    dbus_message_iter_recurse(&root_iter, &variant_iter);
    dbus_message_iter_get_basic(&variant_iter, value);

    dbus_message_unref(reply);
    return 0;
}

static int adapter_discovery(bluez_t *bluez, const char *method)
{
    DBusMessage *message = dbus_message_new_method_call(
            "org.bluez", bluez->adapter,
            "org.bluez.Adapter1", method);
    if (!message)
        return 1;

    if (!dbus_connection_send(bluez->dbus_connection, message, NULL))
        return 1;

    dbus_connection_flush(bluez->dbus_connection);
    dbus_message_unref(message);

    return 0;
}

static void bluez_dbus_connect(bluez_t *bluez)
{
    DBusError err;
    dbus_error_init(&err);
    bluez->dbus_connection = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
}

static void bluez_dbus_disconnect(bluez_t *bluez)
{
    if (!bluez->dbus_connection)
        return;
    
    dbus_connection_close(bluez->dbus_connection);
    dbus_connection_unref(bluez->dbus_connection);
    bluez->dbus_connection = NULL;
}

static int get_managed_objects(bluez_t *bluez, DBusMessage **reply)
{
    DBusMessage *message;
    DBusError err;

    dbus_error_init(&err);

    message = dbus_message_new_method_call("org.bluez", "/",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    if (!message)
        return 1;

    *reply = dbus_connection_send_with_reply_and_block(bluez->dbus_connection,
            message, -1, &err);
    /* if (!reply) is done by the caller in this one */

    dbus_message_unref(message);
    return 0;
}

static int get_default_adapter(bluez_t *bluez, DBusMessage *reply)
{
    /* "...an application would discover the available adapters by
    * performing a ObjectManager.GetManagedObjects call and look for any
    * returned objects with an â€œorg.bluez.Adapter1â€³ interface.
    * The concept of a default adapter was always a bit fuzzy and the
    * value couldâ€™t be changed, so if applications need something like it
    * they could e.g. just pick the first adapter they encounter in the
    * GetManagedObjects reply."
    * -- http://www.bluez.org/bluez-5-api-introduction-and-porting-guide/
    */

    DBusMessageIter root_iter;
    DBusMessageIter dict_1_iter, dict_2_iter;
    DBusMessageIter array_1_iter, array_2_iter;

    char *obj_path, *interface_name;

    /* a{oa{sa{sv}}} */
    if (!dbus_message_iter_init(reply, &root_iter))
        return 1;

    /* a */
    if (DBUS_TYPE_ARRAY != dbus_message_iter_get_arg_type(&root_iter))
        return 1;
    dbus_message_iter_recurse(&root_iter, &array_1_iter);
    do {
        /* a{...} */
        if (DBUS_TYPE_DICT_ENTRY != dbus_message_iter_get_arg_type(&array_1_iter))
            return 1;
        dbus_message_iter_recurse(&array_1_iter, &dict_1_iter);

        /* a{o...} */
        if (DBUS_TYPE_OBJECT_PATH != dbus_message_iter_get_arg_type(&dict_1_iter))
            return 1;
        dbus_message_iter_get_basic(&dict_1_iter, &obj_path);

        if (!dbus_message_iter_next(&dict_1_iter))
            return 1;
        /* a{oa} */
        if (DBUS_TYPE_ARRAY != dbus_message_iter_get_arg_type(&dict_1_iter))
            return 1;
        dbus_message_iter_recurse(&dict_1_iter, &array_2_iter);
        do
        {
            /* empty array? */
            if (DBUS_TYPE_INVALID == 
                dbus_message_iter_get_arg_type(&array_2_iter))
            continue;

            /* a{oa{...}} */
            if (DBUS_TYPE_DICT_ENTRY != 
                dbus_message_iter_get_arg_type(&array_2_iter))
                return 1;
            dbus_message_iter_recurse(&array_2_iter, &dict_2_iter);

            /* a{oa{s...}} */
            if (DBUS_TYPE_STRING != 
                dbus_message_iter_get_arg_type(&dict_2_iter))
            return 1;
            dbus_message_iter_get_basic(&dict_2_iter, &interface_name);

            if (!strcmp(interface_name, "org.bluez.Adapter1")) {
                strncpy(bluez->adapter, obj_path, sizeof(bluez->adapter));
                return 0;
            }
        } while (dbus_message_iter_next(&array_2_iter));
    } while (dbus_message_iter_next(&array_1_iter));

    /* Couldn't find an adapter */
    return 1;
}

static void free_devices(bluez_t *bluez)
{
    bluetooth_device_t *dev;

    while(!list_empty(&bluez->devices)) {
        dev = list_first_entry(&bluez->devices, bluetooth_device_t, list);
        list_del(&dev->list);
        free(dev);
    }
}

static void __attribute__((unused)) dump_devices(bluez_t *bluez)
{
    bluetooth_device_t *dev;

	list_for_each_entry(dev, &bluez->devices, list) {
        printf("name: %s, address: %s\n", dev->name, dev->macaddr);
	}
}

static int read_scanned_devices(bluez_t *bluez, DBusMessage *reply)
{
    DBusMessageIter root_iter;
    DBusMessageIter dict_1_iter, dict_2_iter, dict_3_iter;
    DBusMessageIter array_1_iter, array_2_iter, array_3_iter;
    DBusMessageIter variant_iter;
    char *obj_path, *interface_name, *interface_property_name;
    char *found_device_address, *found_device_name, *found_device_icon;

    /* a{oa{sa{sv}}} */
    if (!dbus_message_iter_init(reply, &root_iter))
        return 1;

    /* a */
    if (DBUS_TYPE_ARRAY != dbus_message_iter_get_arg_type(&root_iter))
        return 1;

    dbus_message_iter_recurse(&root_iter, &array_1_iter);

    do {
        /* a{...} */
        if (DBUS_TYPE_DICT_ENTRY != 
            dbus_message_iter_get_arg_type(&array_1_iter))
            return 1;

        dbus_message_iter_recurse(&array_1_iter, &dict_1_iter);

        /* a{o...} */
        if (DBUS_TYPE_OBJECT_PATH != 
            dbus_message_iter_get_arg_type(&dict_1_iter))
            return 1;

        dbus_message_iter_get_basic(&dict_1_iter, &obj_path);

        if (!dbus_message_iter_next(&dict_1_iter))
            return 1;

        /* a{oa} */
        if (DBUS_TYPE_ARRAY != 
            dbus_message_iter_get_arg_type(&dict_1_iter))
            return 1;

        free_devices(bluez);
        dump_devices(bluez);
        dbus_message_iter_recurse(&dict_1_iter, &array_2_iter);
        do {
            /* empty array? */
            if (DBUS_TYPE_INVALID == 
                dbus_message_iter_get_arg_type(&array_2_iter))
                continue;

            /* a{oa{...}} */
            if (DBUS_TYPE_DICT_ENTRY != 
                dbus_message_iter_get_arg_type(&array_2_iter))
                return 1;
            dbus_message_iter_recurse(&array_2_iter, &dict_2_iter);

            /* a{oa{s...}} */
            if (DBUS_TYPE_STRING != 
                dbus_message_iter_get_arg_type(&dict_2_iter))
                return 1;
            dbus_message_iter_get_basic(&dict_2_iter, &interface_name);

            if (strcmp(interface_name, "org.bluez.Device1"))
                continue;

            bluetooth_device_t *dev = calloc(1, sizeof(bluetooth_device_t));
            strncpy(dev->path, obj_path, sizeof(dev->path));

            if (!dbus_message_iter_next(&dict_2_iter))
                return 1;

            /* a{oa{sa}} */
            if (DBUS_TYPE_ARRAY != dbus_message_iter_get_arg_type(&dict_2_iter))
                return 1;

            dbus_message_iter_recurse(&dict_2_iter, &array_3_iter);

            do {
                /* empty array? */
                if (DBUS_TYPE_INVALID ==
                        dbus_message_iter_get_arg_type(&array_3_iter))
                    continue;

                /* a{oa{sa{...}}} */
                if (DBUS_TYPE_DICT_ENTRY != 
                        dbus_message_iter_get_arg_type(&array_3_iter))
                    return 1;
                dbus_message_iter_recurse(&array_3_iter, &dict_3_iter);

                /* a{oa{sa{s...}}} */
                if (DBUS_TYPE_STRING != 
                        dbus_message_iter_get_arg_type(&dict_3_iter))
                    return 1;

                dbus_message_iter_get_basic(&dict_3_iter,
                        &interface_property_name);

                if (!dbus_message_iter_next(&dict_3_iter))
                    return 1;
                /* a{oa{sa{sv}}} */
                if (DBUS_TYPE_VARIANT != 
                        dbus_message_iter_get_arg_type(&dict_3_iter))
                    return 1;

                /* Below, "Alias" property is used instead of "Name".
                    * "This value ("Name") is only present for
                    * completeness.  It is better to always use
                    * the Alias property when displaying the
                    * devices name."
                    * -- bluez/doc/device-api.txt
                    */

                /* DBUS_TYPE_VARIANT is a container type */
                dbus_message_iter_recurse(&dict_3_iter, &variant_iter);
                if (!strcmp(interface_property_name, "Address")) {
                    dbus_message_iter_get_basic(&variant_iter,
                            &found_device_address);
                    strncpy(dev->macaddr, found_device_address, sizeof(dev->macaddr));
                } else if (!strcmp(interface_property_name, "Alias")) {
                    dbus_message_iter_get_basic(&variant_iter,
                            &found_device_name);
                    strncpy(dev->name, found_device_name, sizeof(dev->name));
                } else if (!strcmp(interface_property_name, "Icon")) {
                    dbus_message_iter_get_basic(&variant_iter,
                            &found_device_icon);
                    strncpy(dev->icon, found_device_icon, sizeof(dev->icon));
                } else if (!strcmp(interface_property_name, "Connected")) {
                    dbus_message_iter_get_basic(&variant_iter,
                            &dev->connected);
                } else if (!strcmp(interface_property_name, "Paired")) {
                    dbus_message_iter_get_basic(&variant_iter,
                            &dev->paired);
                } else if (!strcmp(interface_property_name, "Trusted")) {
                    dbus_message_iter_get_basic(&variant_iter,
                            &dev->trusted);
                }
            } while (dbus_message_iter_next(&array_3_iter));
            printf("name: %s, address: %s\n", dev->name, dev->macaddr);
            list_add_tail(&dev->list, &bluez->devices);
        } while (dbus_message_iter_next(&array_2_iter));
    } while (dbus_message_iter_next(&array_1_iter));

    dump_devices(bluez);
    return 0;
}

static void bluez_scan(void *handle, int timeout)
{
    DBusError err;
    DBusMessage *reply;
    bluez_t *bluez = (bluez_t *)handle;

    bluez_dbus_connect(bluez);
    if (get_managed_objects(bluez, &reply))
        return;
    if (!reply)
        return;

    /* Get default adapter */
    if (get_default_adapter(bluez, reply))
        return;
    dbus_message_unref(reply);

    /* Power device on */
    if (set_bool_property(bluez, bluez->adapter,
            "org.bluez.Adapter1", "Powered", 1, 1))
        return;

    /* Start discovery */
    if (adapter_discovery(bluez, "StartDiscovery"))
        return;

    sleep(timeout);

    /* Stop discovery */
    if (adapter_discovery(bluez, "StopDiscovery"))
        return;

    /* Get scanned devices */
    if (get_managed_objects(bluez, &reply))
        return;
    if (!reply)
        return;

    read_scanned_devices(bluez, reply);
    dbus_message_unref(reply);

    bluez_dbus_disconnect(bluez);
}

static size_t bluez_get_devices(void *handle, char devs[][BLUETOOTH_DEVNAME_MAXLEN], int devnum)
{
    bluez_t *bluez = (bluez_t *)handle;

    return 0;
}

static bool bluez_device_is_connected(void *handle, const char *device)
{
    bluez_t *bluez = (bluez_t *)handle;

    return false;
}

static bool bluez_connect_device(void *handle, const char *device, int timeout)
{
    bluez_t *bluez = (bluez_t *)handle;

    return false;
}

static bool bluez_disconnect_device(void *handle, const char *device, int timeout)
{
    bluez_t *bluez = (bluez_t *)handle;
    
    return false;
}

bluetooth_backend_t bluetooth_bluez = {
    bluez_init,
    bluez_free,
    bluez_scan,
    bluez_get_devices,
    bluez_device_is_connected,
    bluez_connect_device,
    bluez_disconnect_device,
    "bluez"
};