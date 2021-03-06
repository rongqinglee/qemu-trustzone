/*
 * QEMU simulated pvpanic device.
 *
 * Copyright Fujitsu, Corp. 2013
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *     Hu Tao <hutao@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qjson.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"

#include "hw/nvram/fw_cfg.h"
#include "hw/i386/pc.h"

/* The bit of supported pv event */
#define PVPANIC_F_PANICKED      0

/* The pv event value */
#define PVPANIC_PANICKED        (1 << PVPANIC_F_PANICKED)

#define TYPE_ISA_PVPANIC_DEVICE    "pvpanic"
#define ISA_PVPANIC_DEVICE(obj)    \
    OBJECT_CHECK(PVPanicState, (obj), TYPE_ISA_PVPANIC_DEVICE)

static void panicked_mon_event(const char *action)
{
    QObject *data;

    data = qobject_from_jsonf("{ 'action': %s }", action);
    monitor_protocol_event(QEVENT_GUEST_PANICKED, data);
    qobject_decref(data);
}

static void handle_event(int event)
{
    static bool logged;

    if (event & ~PVPANIC_PANICKED && !logged) {
        qemu_log_mask(LOG_GUEST_ERROR, "pvpanic: unknown event %#x.\n", event);
        logged = true;
    }

    if (event & PVPANIC_PANICKED) {
        panicked_mon_event("pause");
        vm_stop(RUN_STATE_GUEST_PANICKED);
        return;
    }
}

#include "hw/isa/isa.h"

typedef struct PVPanicState {
    ISADevice parent_obj;

    MemoryRegion io;
    uint16_t ioport;
} PVPanicState;

/* return supported events on read */
static uint64_t pvpanic_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    return PVPANIC_PANICKED;
}

static void pvpanic_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    handle_event(val);
}

static const MemoryRegionOps pvpanic_ops = {
    .read = pvpanic_ioport_read,
    .write = pvpanic_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static int pvpanic_isa_initfn(ISADevice *dev)
{
    PVPanicState *s = ISA_PVPANIC_DEVICE(dev);
    static bool port_configured;
    FWCfgState *fw_cfg;

    memory_region_init_io(&s->io, &pvpanic_ops, s, "pvpanic", 1);
    isa_register_ioport(dev, &s->io, s->ioport);

    if (!port_configured) {
        fw_cfg = fw_cfg_find();
        if (fw_cfg) {
            fw_cfg_add_file(fw_cfg, "etc/pvpanic-port",
                            g_memdup(&s->ioport, sizeof(s->ioport)),
                            sizeof(s->ioport));
            port_configured = true;
        }
    }

    return 0;
}

int pvpanic_init(ISABus *bus)
{
    isa_create_simple(bus, TYPE_ISA_PVPANIC_DEVICE);
    return 0;
}

static Property pvpanic_isa_properties[] = {
    DEFINE_PROP_UINT16("ioport", PVPanicState, ioport, 0x505),
    DEFINE_PROP_END_OF_LIST(),
};

static void pvpanic_isa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISADeviceClass *ic = ISA_DEVICE_CLASS(klass);

    ic->init = pvpanic_isa_initfn;
    dc->no_user = 1;
    dc->props = pvpanic_isa_properties;
}

static TypeInfo pvpanic_isa_info = {
    .name          = TYPE_ISA_PVPANIC_DEVICE,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(PVPanicState),
    .class_init    = pvpanic_isa_class_init,
};

static void pvpanic_register_types(void)
{
    type_register_static(&pvpanic_isa_info);
}

type_init(pvpanic_register_types)
