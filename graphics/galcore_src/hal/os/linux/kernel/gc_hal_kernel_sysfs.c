/****************************************************************************
*
* gc_hal_kernel_sysfs.c
*
* Author: Watson Wang <zswang@marvell.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version. *
*
*****************************************************************************/

#include "gc_hal_kernel_linux.h"
#include "gc_hal_kernel_sysfs.h"

#if MRVL_CONFIG_SYSFS
#include <linux/sysfs.h>

static gckGALDEVICE galDevice = NULL;

static inline int __create_sysfs_file_debug(void);
static inline void __remove_sysfs_file_debug(void);

static struct kset *kset_gpu = NULL;
static int registered_debug = 0;
static int registered_common = 0;
static int registered_gpufreq = 0;
static int registered_pm_test = 0;

static const char* const _core_desc[] = {
    [gcvCORE_MAJOR]     = "3D",
    [gcvCORE_2D]        = "2D",
    [gcvCORE_VG]        = "VG",
};

static const char* const _power_states[] = {
    [gcvPOWER_ON]       = "ON",
    [gcvPOWER_OFF]      = "OFF",
    [gcvPOWER_IDLE]     = "IDLE",
    [gcvPOWER_SUSPEND]  = "SUSPEND",
};

/* *********************************************************** */
static ssize_t show_pm_state (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    gceSTATUS status;
    gceCHIPPOWERSTATE states;
    int i, len = 0;

    for (i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        if (galDevice->kernels[i] != gcvNULL)
        {
            status = gckHARDWARE_QueryPowerManagementState(
                            galDevice->kernels[i]->hardware,
                            &states);

            if (gcmIS_ERROR(status))
            {
                len += sprintf(buf+len, "[%s] querying chip power state failed\n", _core_desc[i]);
                continue;
            }

            len += sprintf(buf+len, "[%s] current chipPowerState = %s\n", _core_desc[i], _power_states[states]);
        }
    }

    len += sprintf(buf+len, "\n* Usage:\n"
                            " $ cat /sys/devices/.../pm_state\n"
                            " $ echo [core],[state] > /sys/devices/.../pm_state\n"
                            "   e.g. core[3D] power [ON]\n"
                            " $ echo 0,0 > /sys/devices/.../pm_state\n"
                            );

    len += sprintf(buf+len, "* Core options:\n");
    if (galDevice->kernels[gcvCORE_MAJOR] != gcvNULL)
        len += sprintf(buf+len, " - %d   core [%s]\n", gcvCORE_MAJOR, _core_desc[gcvCORE_MAJOR]);
    if (galDevice->kernels[gcvCORE_2D] != gcvNULL)
        len += sprintf(buf+len, " - %d   core [%s]\n", gcvCORE_2D, _core_desc[gcvCORE_2D]);
    if (galDevice->kernels[gcvCORE_VG] != gcvNULL)
        len += sprintf(buf+len, " - %d   core [%s]\n", gcvCORE_VG, _core_desc[gcvCORE_VG]);

    len += sprintf(buf+len, "* Power state options:\n"
                            " - 0   to power [   ON  ]\n"
                            " - 1   to power [  OFF  ]\n"
                            " - 2   to power [  IDLE ]\n"
                            " - 3   to power [SUSPEND]\n");

    return len;
}

static ssize_t store_pm_state (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    int core, state, i, gpu_count;
    gceSTATUS status;

    /* count core numbers */
    for (i = 0, gpu_count = 0; i < gcdMAX_GPU_COUNT; i++)
        if (galDevice->kernels[i] != gcvNULL)
            gpu_count++;

    /* read input and verify */
    SYSFS_VERIFY_INPUT(sscanf(buf, "%d,%d", &core, &state), 2);
    SYSFS_VERIFY_INPUT_RANGE(core, 0, (gpu_count-1));
    SYSFS_VERIFY_INPUT_RANGE(state, 0, 3);

    /* power state transition */
    status = gckHARDWARE_SetPowerManagementState(galDevice->kernels[core]->hardware, state);
    if (gcmIS_ERROR(status))
    {
        printk("[%d] failed in transfering power state to %d\n", core, state);
    }

    return count;
}

static ssize_t show_profiler_debug (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    return sprintf(buf, "%d\n", galDevice->profilerDebug);
}

static ssize_t store_profiler_debug (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    int value;

    SYSFS_VERIFY_INPUT(sscanf(buf, "%d", &value), 1);
    SYSFS_VERIFY_INPUT_RANGE(value, 0, 1);

    galDevice->profilerDebug = value;

    return count;
}

static ssize_t show_power_debug (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    return sprintf(buf, "%d\n", galDevice->powerDebug);
}

static ssize_t store_power_debug (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    int value;

    SYSFS_VERIFY_INPUT(sscanf(buf, "%d", &value), 1);
    SYSFS_VERIFY_INPUT_RANGE(value, 0, 1);

    galDevice->powerDebug = value;

    return count;
}

static ssize_t show_runtime_debug (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    return sprintf(buf, "%d\n", galDevice->pmrtDebug);
}

static ssize_t store_runtime_debug (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    int value;

    SYSFS_VERIFY_INPUT(sscanf(buf, "%d", &value), 1);
    SYSFS_VERIFY_INPUT_RANGE(value, 0, 2);

    galDevice->pmrtDebug = value;

    return count;
}

static ssize_t show_show_commands (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    return sprintf(buf, "Current value: %d\n"
                        "options:\n"
                        " 0    disable show commands\n"
                        " 1    show 3D commands\n"
                        " 2    show 2D commands\n"
                        " 3    show 2D&3D commands\n"
#if gcdENABLE_VG
                        " 4    show VG commands\n"
#endif
                        ,galDevice->printPID);
}

static ssize_t store_show_commands (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    int value;

    SYSFS_VERIFY_INPUT(sscanf(buf, "%d", &value), 1);
#if gcdENABLE_VG
    /* 3D, 2D, 2D&3D, VG. */
    SYSFS_VERIFY_INPUT_RANGE(value, 0, 4);
#else
    /* 3D, 2D, 2D&3D. */
    SYSFS_VERIFY_INPUT_RANGE(value, 0, 3);
#endif

    galDevice->printPID = value;

    return count;
}

static ssize_t show_fscale (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    int len = 0, i;
    gctUINT32 fscale = 64, minscale = 1, maxscale = 64;

    for (i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        if (galDevice->kernels[i] != gcvNULL)
        {
#if gcdENABLE_FSCALE_VAL_ADJUST
            gcmkVERIFY_OK(gckHARDWARE_GetFscaleValue(
                                    galDevice->kernels[i]->hardware,
                                    &fscale, &minscale, &maxscale));
#endif
        }

        len += sprintf(buf+len, "[%s] internal fscale value = %d\n", _core_desc[i], fscale);
    }

    return len;
}

static ssize_t store_fscale (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    int core, fscale, i, gpu_count;

    for (i = 0, gpu_count = 0; i < gcdMAX_GPU_COUNT; i++)
        if (galDevice->kernels[i] != gcvNULL)
            gpu_count++;

    /* read input and verify */
    SYSFS_VERIFY_INPUT(sscanf(buf, "%d,%d", &core, &fscale), 2);
    SYSFS_VERIFY_INPUT_RANGE(core, 0, (gpu_count-1));
    SYSFS_VERIFY_INPUT_RANGE(fscale, 1, 64);

#if gcdENABLE_FSCALE_VAL_ADJUST
    if(galDevice->kernels[core] != gcvNULL)
        gcmkVERIFY_OK(gckHARDWARE_SetFscaleValue(
                                galDevice->kernels[core]->hardware,
                                fscale));
#endif

    return count;
}

static ssize_t show_register_stats (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    return sprintf(buf, "options:\n"
                        " default 0x0            idle & clock & current-cmd\n"
                        " offset  0xaddr         register's offset\n");
}

static ssize_t store_register_stats (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    gctCHAR type[10];
    gctUINT32 offset, clkState = 0;
    gctINT t = ~0;

    SYSFS_VERIFY_INPUT(sscanf(buf, "%s 0x%x", type, &offset), 2);
    SYSFS_VERIFY_INPUT_RANGE(offset, 0, 0x30001);

    if(strstr(type, "default"))
    {
        t = 0;
    }
    else if(strstr(type, "offset"))
    {
        t = 1;
    }
    else
    {
        gcmkPRINT("Invalid Command~");
        return count;
    }

    gcmkVERIFY_OK(gckOS_QueryRegisterStats( galDevice->os, t, offset, &clkState));

    if(t && clkState)
    {
        gcmkPRINT("Some Registers can't be read because of %s disabled",
            (clkState&0x11)?("External/Internal clk"):((clkState&0x01)?"External":"Internal"));
    }

    return count;
}

static ssize_t show_clk_rate (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    gceSTATUS status;
    unsigned int clockRate = 0;
    int i = 0, len = 0;

    for(i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        if(galDevice->kernels[i] != gcvNULL)
        {
            status = gckOS_QueryClkRate(galDevice->os, i, &clockRate);
            len += sprintf(buf+len, "[%s] current frequency: %u MHZ\n", _core_desc[i], clockRate/1000/1000);
        }
    }

    return len;
}

static ssize_t store_clk_rate (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    gceSTATUS status;
    int core, frequency, i, gpu_count;

    for (i = 0, gpu_count = 0; i < gcdMAX_GPU_COUNT; i++)
        if (galDevice->kernels[i] != gcvNULL)
            gpu_count++;

    /* read input and verify */
    SYSFS_VERIFY_INPUT(sscanf(buf, "%d,%d", &core, &frequency), 2);
    SYSFS_VERIFY_INPUT_RANGE(core, 0, (gpu_count-1));
    SYSFS_VERIFY_INPUT_RANGE(frequency, 156, 624);

    status = gckOS_SetClkRate(galDevice->os, core, frequency*1000*1000);

    if(gcmIS_ERROR(status))
    {
        printk("fail to set core[%d] frequency to %d MHZ\n", core, frequency);
    }

    return count;
}

#if gcdPOWEROFF_TIMEOUT
static ssize_t show_poweroff_timeout (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    gctUINT32 timeout, i;
    ssize_t len = 0;

    for (i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        if (galDevice->kernels[i] != gcvNULL)
        {
            gcmkVERIFY_OK(gckHARDWARE_QueryPowerOffTimeout(
                                    galDevice->kernels[i]->hardware,
                                    &timeout));

            len += sprintf(buf+len, "[%s] power_off_timeout = %d ms\n", _core_desc[i], timeout);
        }
    }

    len += sprintf(buf+len, "\n* Usage:\n"
                            "  $ cat /sys/devices/.../poweroff_timeout\n"
                            "  $ echo [core],[timeout] > /sys/devices/.../poweroff_timeout\n"
                            );
    return len;
}

static ssize_t store_poweroff_timeout (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    int core, timeout, i, gpu_count;

    /* count core numbers */
    for (i = 0, gpu_count = 0; i < gcdMAX_GPU_COUNT; i++)
        if (galDevice->kernels[i] != gcvNULL)
            gpu_count++;

    /* read input and verify */
    SYSFS_VERIFY_INPUT(sscanf(buf, "%d,%d", &core, &timeout), 2);
    SYSFS_VERIFY_INPUT_RANGE(core, 0, (gpu_count-1));
    SYSFS_VERIFY_INPUT_RANGE(timeout, 0, 3600000);

    gcmkVERIFY_OK(gckHARDWARE_SetPowerOffTimeout(
                            galDevice->kernels[core]->hardware,
                            timeout));

    return count;
}

gc_sysfs_attr_rw(poweroff_timeout);
#endif

gc_sysfs_attr_rw(pm_state);
gc_sysfs_attr_rw(profiler_debug);
gc_sysfs_attr_rw(power_debug);
gc_sysfs_attr_rw(runtime_debug);
gc_sysfs_attr_rw(show_commands);
gc_sysfs_attr_rw(fscale);
gc_sysfs_attr_rw(register_stats);
gc_sysfs_attr_rw(clk_rate);

static struct attribute *gc_debug_attrs[] = {
    &gc_attr_pm_state.attr,
    &gc_attr_profiler_debug.attr,
    &gc_attr_power_debug.attr,
    &gc_attr_runtime_debug.attr,
    &gc_attr_show_commands.attr,
    &gc_attr_fscale.attr,
    &gc_attr_register_stats.attr,
    &gc_attr_clk_rate.attr,
#if gcdPOWEROFF_TIMEOUT
    &gc_attr_poweroff_timeout.attr,
#endif
    NULL
};

static struct attribute_group gc_debug_attr_group = {
    .attrs = gc_debug_attrs,
    .name = "debug"
};

// -------------------------------------------------
static ssize_t show_mem_stats (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    gctUINT32 len = 0;

    gcmkVERIFY_OK(gckOS_ShowVidMemUsage(galDevice->os, buf, &len));

    return (ssize_t)len;
}

static ssize_t store_mem_stats (struct device *dev,
    struct device_attribute *attr,
    const char *buf, size_t count)
{
    gctCHAR value1[16];
    gctINT value2 = -1;
    int i;

    sscanf(buf, "%s %d", value1, &value2);

    if(strstr(value1, "type"))
    {
        for (i = 0; i < gcdMAX_GPU_COUNT; i++)
        {
            if (galDevice->kernels[i] != gcvNULL)
            {
                gckKERNEL_ShowVidMemUsageDetails(galDevice->kernels[i], 0, value2);
            }
        }
    }
    else if(strstr(value1, "pid"))
    {
        for (i = 0; i < gcdMAX_GPU_COUNT; i++)
        {
            if (galDevice->kernels[i] != gcvNULL)
            {
                gckKERNEL_ShowVidMemUsageDetails(galDevice->kernels[i], 1, value2);
            }
        }
    }
    else if(strstr(value1, "database"))
    {
        gctCHAR filename[] = "/data/DB/vidmem";
        printk("Video memory details in Database saved into file %s\n", filename);
        for (i = 0; i < gcdMAX_GPU_COUNT; i++)
        {
            if (galDevice->kernels[i] != gcvNULL)
            {
                gckKERNEL_PrintVIDMEMProcessDB(galDevice->kernels[i], value2, filename);
            }
        }
    }
    else if(strstr(value1, "flag"))
    {
        gctCHAR filename[] = "/data/DB/flagRecord";
        for (i = 0; i < gcdMAX_GPU_COUNT; i++)
        {
            if (galDevice->kernels[i] != gcvNULL)
            {
                if(galDevice->kernels[i]->dbflag == gcvFALSE)
                {
                    printk("Data base flag is set \n");
                }
                else
                {
                    printk("Data base flag is clean. New records are saved in %s. \n", filename);
                    gckKERNEL_PrintFlagedProcessDB(galDevice->kernels[i], filename);
                }
                galDevice->kernels[i]->dbflag = !galDevice->kernels[i]->dbflag;
            }
        }
    }
    else if(strstr(value1, "period"))
    {
        for (i = 0; i < gcdMAX_GPU_COUNT; i++)
        {
            if (galDevice->kernels[i] != gcvNULL)
            {
                gckKERNEL_ShowVidMemPeriodUsage(galDevice->kernels[i], value2);
            }
        }
    }

    return count;
}

// TODO: finish *ticks* and *dutycycle* implementation
static ssize_t show_ticks (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    return sprintf(buf, "Oops, %s is under working\n", __func__);
}

static ssize_t store_ticks (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    printk("Oops, %s is under working\n", __func__);
    return count;
}

static ssize_t show_dutycycle (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    return sprintf(buf, "Oops, %s is under working\n", __func__);
}

static ssize_t store_dutycycle (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    printk("Oops, %s is under working\n", __func__);
    return count;
}

static ssize_t show_current_freq (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    gceSTATUS status;
    unsigned int clockRate = 0;
    int i = 0, len = 0;

    for(i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        if(galDevice->kernels[i] != gcvNULL)
        {
            status = gckOS_QueryClkRate(galDevice->os, i, &clockRate);
            len += sprintf(buf+len, "[%s] current frequency: %u KHZ\n", _core_desc[i], clockRate/1000);
        }
    }

    return len;
}

static ssize_t show_control (struct device *dev,
                    struct device_attribute *attr,
                    char * buf)
{
    return sprintf(buf, "options:\n"
                        " 0,(0|1)      debug, disable/enable\n"
                        " 1,(0|1)    gpufreq, disable/enable\n");
}

static ssize_t store_control (struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    unsigned int option, value;

    SYSFS_VERIFY_INPUT(sscanf(buf, "%u,%u", &option, &value), 2);
    SYSFS_VERIFY_INPUT_RANGE(option, 0, 1);
    SYSFS_VERIFY_INPUT_RANGE(value, 0, 1);

    switch(option) {
    case 0:
        if(value == 0)
        {
            if(registered_debug)
            {
                __remove_sysfs_file_debug();
                registered_debug = 0;
            }
        }
        else if(value == 1)
        {
            if(registered_debug == 0)
            {
                __create_sysfs_file_debug();
                registered_debug = 1;
            }
        }
        break;
/*
    case 1:
        if(value == 0)
            __disable_gpufreq(galDevice);
        else if(value == 1)
            __enable_gpufreq(galDevice);
        break;
*/
    }

    return count;
}

gc_sysfs_attr_rw(mem_stats);
gc_sysfs_attr_rw(ticks);
gc_sysfs_attr_rw(dutycycle);
gc_sysfs_attr_ro(current_freq);
gc_sysfs_attr_rw(control);

static struct attribute *gc_default_attrs[] = {
    &gc_attr_control.attr,
    &gc_attr_mem_stats.attr,
    &gc_attr_ticks.attr,
    &gc_attr_dutycycle.attr,
    &gc_attr_current_freq.attr,
    NULL
};

/* *********************************************************** */
static inline int __create_sysfs_file_debug(void)
{
    int ret = 0;

    if((ret = sysfs_create_group(&kset_gpu->kobj, &gc_debug_attr_group)))
        printk("fail to create sysfs group %s\n", gc_debug_attr_group.name);

    return ret;
}

static inline void __remove_sysfs_file_debug(void)
{
    sysfs_remove_group(&kset_gpu->kobj, &gc_debug_attr_group);
}

static inline int __create_sysfs_file_common(void)
{
    int ret = 0;

    if((ret = sysfs_create_files(&kset_gpu->kobj, (const struct attribute **)gc_default_attrs)))
        printk("fail to create sysfs files gc_default_attrs\n");

    return ret;
}

static inline void __remove_sysfs_file_common(void)
{
    sysfs_remove_files(&kset_gpu->kobj, (const struct attribute **)gc_default_attrs);
}

#if MRVL_CONFIG_ENABLE_GPUFREQ
static inline int __create_sysfs_file_gpufreq(void)
{
    int i, len = 0;
    char buf[8] = {0};
    struct kobject *kobj;

    for(i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        if(galDevice->kernels[i] != gcvNULL)
        {
            len = sprintf(buf, "gpu%d", i);
            buf[len] = '\0';

            kobj = kobject_create_and_add(buf,&kset_gpu->kobj);
            if (!kobj)
            {
                printk("error: allocate kobj for core %d\n", i);
                continue;
            }

            galDevice->kernels[i]->hardware->devObj.kobj = kobj;
        }
    }

    return 0;
}

static inline void __remove_sysfs_file_gpufreq(void)
{
    int i;

    for(i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        if(galDevice->kernels[i] != gcvNULL)
        {
            if(!galDevice->kernels[i]->hardware->devObj.kobj)
                continue ;

            kobject_put((struct kobject *)galDevice->kernels[i]->hardware->devObj.kobj);
        }
    }
}
#else
static inline int __create_sysfs_file_gpufreq(void)
{
    return 0;
}
static inline void __remove_sysfs_file_gpufreq(void)
{
    return;
}
#endif

void create_gc_sysfs_file(struct platform_device *pdev)
{
    int ret = 0;

    galDevice = (gckGALDEVICE) platform_get_drvdata(pdev);
    if(!galDevice)
    {
        printk("error: failed in getting drvdata\n");
        return ;
    }

    /* create a kset and register it for 'gpu' */
    kset_gpu = kset_create_and_add("gpu", NULL, &pdev->dev.kobj);
    if(!kset_gpu)
    {
        printk("error: failed in creating kset for 'gpu'\n");
        return ;
    }
    /* FIXME: force kset of kobject 'gpu' linked to itself. */
    kset_gpu->kobj.kset = kset_gpu;

    ret = __create_sysfs_file_common();
    if(ret == 0)
        registered_common = 1;

    ret = __create_sysfs_file_gpufreq();
    if(ret == 0)
        registered_gpufreq = 1;

    ret = create_sysfs_file_pm_test(pdev, &kset_gpu->kobj);
    if(ret == 0)
        registered_pm_test = 1;
}

void remove_gc_sysfs_file(struct platform_device *pdev)
{
    if(!galDevice)
        return;

    if(!kset_gpu)
        return;

    if(registered_pm_test)
    {
        remove_sysfs_file_pm_test(pdev);
        registered_pm_test = 0;
    }

    if(registered_gpufreq)
    {
        __remove_sysfs_file_gpufreq();
        registered_gpufreq = 0;
    }

    if(registered_debug)
    {
        __remove_sysfs_file_debug();
        registered_debug = 0;
    }

    if(registered_common)
    {
        __remove_sysfs_file_common();
        registered_common = 0;
    }

    /* release a kset. */
    kset_unregister(kset_gpu);
}

#endif
