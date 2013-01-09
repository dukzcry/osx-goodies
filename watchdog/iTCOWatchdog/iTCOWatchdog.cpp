/* Written by Artem Falcon <lomka@gero.in> */

#include "iTCOWatchdog.h"

/* TO-DO: Move them into separate kexts in order to make unloading possible */
OSDefineMetaClassAndStructors(MyLPC, IOService)
OSDefineMetaClassAndStructors(iTCOWatchdog, IOService)

bool iTCOWatchdog::init (OSDictionary* dict)
{
    OSNumber *nkey;
    OSBoolean *bkey;
    bool res;
    
    //DbgPrint(drvid, "init\n");
    res = super::init(dict);
    
    Timeout = DEFAULT_TIMEOUT;
    SelfFeeding = false;
    WorkaroundBug = false;
    
    if ((conf = OSDynamicCast(OSDictionary, getProperty("Settings"))) &&
        (nkey = OSDynamicCast(OSNumber, conf->getObject("Timeout"))))
        Timeout = nkey->unsigned32BitValue();
    if (conf && (bkey = OSDynamicCast(OSBoolean, conf->getObject("SelfFeeding"))))
        SelfFeeding = bkey->isTrue();
    if (conf && (bkey = OSDynamicCast(OSBoolean, conf->getObject("UnsafeWorkaroundBIOSBug"))))
        WorkaroundBug = bkey->isTrue();
    
    first_run = true;
    is_active = false;
    SMIWereEnabled = false;
    //entered_sleep = false;
    
    GCSMem.range = NULL; GCSMem.map = NULL;
    
    lock = IOSimpleLockAlloc();
    
    return res;
}

void iTCOWatchdog::free(void)
{    
    DbgPrint(drvid, "free\n");
    
    free_common();
    
    if (GCSMem.map) GCSMem.map->release();
    if (GCSMem.range) GCSMem.range->release();
    
    LPCNub->close(this);
    LPCNub->release();
    
    PMstop();
    
    IOSimpleLockFree(lock);

    super::free();
}

void iTCOWatchdog::free_common()
{
    if (is_active) {
        tcoWdDisableTimer();
        is_active = false;
    }
    thread_terminate(thread.Thread);
    IODelete(thread.Data, struct thread_data, 1);
    
    if (WorkaroundBug) {
        fPCIDevice->ioWrite32(ITCO_SMIEN, fPCIDevice->ioRead32(ITCO_SMIEN) | (ITCO_SMIEN_ENABLE+1));
        clearStatus();
        WorkaroundBug = false;
    }
    else if (SMIWereEnabled) {
        fPCIDevice->ioWrite32(ITCO_SMIEN, fPCIDevice->ioRead32(ITCO_SMIEN) | ITCO_SMIEN_ENABLE);
        clearStatus();
        SMIWereEnabled = false;
    }
}

void iTCOWatchdog::systemWillShutdown(IOOptionBits spec)
{
    DbgPrint(drvid, "%s: spec = %#x\n", __FUNCTION__, spec);
    
    switch (spec) {
        case kIOMessageSystemWillRestart:
        case kIOMessageSystemWillPowerOff:
            free_common();
        break;
    }
    
    super::systemWillShutdown(spec);
}

#ifdef sleepfixed
IOReturn iTCOWatchdog::setPowerState(unsigned long state, IOService *dev __unused)
{
    DbgPrint(drvid, "%s: spec = %lu\n", __FUNCTION__, state);
    
    switch (state) {
        case 0:
            tcoWdDisableTimer();
            entered_sleep = true;
        break;
        default:
            if (entered_sleep) tcoWdEnableTimer();
        break;
    }
    
    return kIOPMAckImplied;
}
#endif

IOService *iTCOWatchdog::probe (IOService* provider, SInt32* score)
{
    UInt32 reg;
    
    //DbgPrint(drvid, "probe\n");
    
    if (!(LPCNub = OSDynamicCast(MyLPC, provider))) {
        IOPrint(drvid, "Failed to cast to MyLPC\n");
        return NULL;
    }

    LPCNub->retain();
    LPCNub->open(this);
    this->fPCIDevice = LPCNub->fPCIDevice;

    fPCIDevice->setIOEnable(true);
    fPCIDevice->setMemoryEnable(true);

    if (!(GCSMem.range = IODeviceMemory::withRange(LPCNub->acpi_gcs.start,
                                                   LPCNub->acpi_gcs.end - LPCNub->acpi_gcs.start)) ||
        !(GCSMem.map = GCSMem.range->map()))
        return NULL;
    GCSMem.vaddr = (void *) GCSMem.map->getVirtualAddress();
    
    reg = fPCIDevice->ioRead32(ITCO_SMIEN);
    if (!enableReboots()) {
        IOPrint(drvid, "Watchdog disabled in hardware. Trying with SMI handler\n");
        //return NULL;
        
        reg |= ITCO_SMIEN_ENABLE;
        SMIWereEnabled = false;
    } else {
        if (WorkaroundBug)
            /* Not safe: disable SMI globally */
            reg &= ~(ITCO_SMIEN_ENABLE+1);
        else if ((SMIWereEnabled = (reg & ITCO_SMIEN_ST) != 0))
             /* Some BIOSes install SMI handlers that reset or disable the watchdog timer 
                instead of resetting the system, so we disable the SMI */
            reg &= ~ITCO_SMIEN_ENABLE;
    }
    
    /* May be cleared and not work */
    if ((fPCIDevice->ioRead16(ITCO_ST2) & ITCO_SECOND_TO_ST)) {
        IOPrint(drvid, "Recovered after system failure\n");
    }
    
    //disableReboots();
    
    fPCIDevice->ioWrite32(ITCO_SMIEN, reg);
    
    clearStatus();
    tcoWdDisableTimer();

    IOPrint(drvid, "Attached %s (%#x) iTCO v%d. Base: 0x%04llx\n", LPCNub->lpc->name,
            LPCNub->DeviceId,
            LPCNub->lpc->itco_version,
            (UInt64) ITCO_BASE);
    
    return super::probe(provider, score);
}

IOThread iTCOWatchdog::IOCreateThread(IOThreadFunc fcn, void *arg)
{
    thread_t thread;
    
    if (kernel_thread_start((thread_continue_t) fcn, arg, &thread) != KERN_SUCCESS)
        return NULL;
    
    thread_deallocate(thread);
    return thread;
}

static void thread_fcn(void *context)
{
    struct thread_data *data = (struct thread_data *) context;
    
    while (1) {
        IOSleep(data->msecs);
        data->instance->tcoWdLoadTimer();
    }
}

bool iTCOWatchdog::start(IOService *provider)
{    
    bool res;
    
    //DbgPrint(drvid, "start\n");
    
    res = super::start(provider);
    
    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, PowerStates, 2);
    
    if (SelfFeeding) {
        thread.Data = IONew(struct thread_data, 1);
        thread.Data->msecs = (Timeout - 2) * 1000;
        thread.Data->instance = this;
        
        tcoWdSetTimer(Timeout);
        tcoWdEnableTimer();
        
        thread.Thread = IOCreateThread(&thread_fcn, (void *) thread.Data);
        return res;
    }
    
#if test
    for (int i = 0; i < 3; i++) {
        IODelay(25000000);
        IOPrint(drvid, "Time left: %d\n", readTimeleft());
        tcoWdLoadTimer();
    }
    
    return false;
#endif
    
    this->registerService();
    return res;
}

void iTCOWatchdog::stop(IOService *provider)
{
    //DbgPrint(drvid, "stop\n");
    super::stop(provider);
}

/* */

void iTCOWatchdog::clearStatus()
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    /* Clearing (out-of-date?) status */
    fPCIDevice->ioWrite16(ITCO_ST1, ITCO_TIMEOUT_ST);
    /* Must be done in separate operations */
    fPCIDevice->ioWrite16(ITCO_ST2, ITCO_SECOND_TO_ST);
    fPCIDevice->ioWrite16(ITCO_ST2, ITCO_BOOT_ST);
}

bool iTCOWatchdog::enableReboots()
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    /* Clear NO_REBOOT flag */
    switch (LPCNub->lpc->itco_version) {
        case 1:
            fPCIDevice->configWrite32(ICHLPC_GEN_STA, LPCNub->fPCIDevice->configRead32(ICHLPC_GEN_STA)
                                      & ~ICHLPC_GEN_STA_NO_REBOOT);
            if (fPCIDevice->configRead32(ICHLPC_GEN_STA) & ICHLPC_GEN_STA_NO_REBOOT)
                return false;
        break;
        case 2:
            OSWriteLittleInt32(GCSMem.vaddr, 0x0, OSReadLittleInt32(GCSMem.vaddr, 0x0) & ~ICHLPC_GCS_NO_REBOOT);
            if (OSReadLittleInt32(GCSMem.vaddr, 0x0) & ICHLPC_GCS_NO_REBOOT)
                return false;
        break;
    }
    
    return true;
}
#if 0
void iTCOWatchdog::disableReboots()
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    switch (LPCNub->lpc->itco_version) {
        case 1:
            fPCIDevice->configWrite32(ICHLPC_GEN_STA, LPCNub->fPCIDevice->configRead32(ICHLPC_GEN_STA)
                                      | ICHLPC_GEN_STA_NO_REBOOT);
            break;
        case 2:
            OSWriteLittleInt32(GCSMem.vaddr, 0x0, OSReadLittleInt32(GCSMem.vaddr, 0x0) | ICHLPC_GCS_NO_REBOOT);
            break;
    }
}
#endif

void iTCOWatchdog::reloadTimer()
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    /* Reload the timer from the last value written to ITCO_TM */
    switch (LPCNub->lpc->itco_version) {
        case 1:
            fPCIDevice->ioWrite8(ITCO_RL, 0x01);
        break;
        case 2:
            fPCIDevice->ioWrite16(ITCO_RL, 0x01);
        break;
    }
}

#if defined DEBUG
UInt32 iTCOWatchdog::readTimeleft()
{
    UInt8 val = 0;
    
    //DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    switch (LPCNub->lpc->itco_version) {
        case 1:
            val = fPCIDevice->ioRead8(ITCO_RL) & ITCO1_RL_TM_MAX;
            if (!(fPCIDevice->ioRead16(ITCO_ST1) & ITCO_TIMEOUT_ST))
                val += fPCIDevice->ioRead8(ITCO1_TM) & ITCO1_RL_TM_MAX;
        break;
        case 2:
            val = fPCIDevice->ioRead16(ITCO_RL) & ITCO2_RL_TM_MAX;
        break;
    }
    
    return (val * 3) / 5;
}
#endif

/* */

void iTCOWatchdog::tcoWdSetTimer(UInt32 time)
{
    IOPrint(drvid, "Setting timeout to: %d\n", time);
    
    /* 1 tick = 0.6 sec */
    time = (time * 5) / 3;
    if (LPCNub->lpc->itco_version == 1)
        time /= 2;

    /* Throw away 0x0-0x3, otherwise write value into hw */
    if (time < ITCO_RL_TM_MIN)
        goto fail;
    switch (LPCNub->lpc->itco_version) {
        case 1:
            IOSimpleLockLock(lock);
            if (time > ITCO1_RL_TM_MAX) {
                if (first_run) {
                    IOPrint(drvid, "Timeout is not in range [5..76], using %d instead\n", DEFAULT_TIMEOUT);
                    first_run = false;
                }
                IOSimpleLockUnlock(lock);
                goto fail;
            }
            
            fPCIDevice->ioWrite8(ITCO1_TM, (fPCIDevice->ioRead8(ITCO1_TM) & 0xc0) | (time & 0xff));
            
#ifdef DEBUG
            if ((fPCIDevice->ioRead8(ITCO1_TM) & ITCO1_RL_TM_MAX) != time)
                IOPrint(drvid, "Failed to set time\n");
#endif
            
            IOSimpleLockUnlock(lock);
        break;
        case 2:
            IOSimpleLockLock(lock);
            if (time > ITCO2_RL_TM_MAX) {
                if (first_run) {
                    IOPrint(drvid, "Timeout is not in range [3..614], using %d instead\n", DEFAULT_TIMEOUT);
                    first_run = false;
                }
                IOSimpleLockUnlock(lock);
                goto fail;
            }
            
            fPCIDevice->ioWrite16(ITCO2_TM, (fPCIDevice->ioRead16(ITCO2_TM) & 0xfc00) | time);
            
#ifdef DEBUG
            if ((fPCIDevice->ioRead16(ITCO2_TM) & ITCO2_RL_TM_MAX) != time)
                IOPrint(drvid, "Failed to set time\n");
#endif
            
            IOSimpleLockUnlock(lock);
        break;
    }
    
    if (first_run) first_run = false;
    
    return;
fail:
    tcoWdSetTimer(DEFAULT_TIMEOUT);
}

void iTCOWatchdog::tcoWdDisableTimer()
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    IOSimpleLockLock(lock);
    
    fPCIDevice->ioWrite16(ITCO_CR, (fPCIDevice->ioRead16(ITCO_CR) & ITCO_CR_PRESERVE) | ITCO_TM_HALT);
    
    //disableReboots();

#ifdef DEBUG
    if (!(fPCIDevice->ioRead16(ITCO_CR) & ITCO_TM_HALT))
        IOPrint(drvid, "Failed to disable timer\n");
#endif
    
    is_active = false;
    
    IOSimpleLockUnlock(lock);
    
    if (!first_run) IOPrint(drvid, "Disabled\n");
}

void iTCOWatchdog::tcoWdEnableTimer()
{
    IOSimpleLockLock(lock);

    //enableReboots();
    reloadTimer();
    
    fPCIDevice->ioWrite16(ITCO_CR, (fPCIDevice->ioRead16(ITCO_CR) & ITCO_CR_PRESERVE) & ~ITCO_TM_HALT);
    
#ifdef DEBUG
    if (fPCIDevice->ioRead16(ITCO_CR) & ITCO_TM_HALT)
        IOPrint(drvid, "Failed to enable timer\n");
#endif

    is_active = true;
    IOSimpleLockUnlock(lock);
    
    IOPrint(drvid, "Activated\n");
}

void iTCOWatchdog::tcoWdLoadTimer()
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    IOSimpleLockLock(lock);
    switch (LPCNub->lpc->itco_version) {
        case 1:
            /* time *= 2 */
            fPCIDevice->ioWrite16(ITCO_ST1, ITCO_TIMEOUT_ST);
            
            fPCIDevice->ioWrite8(ITCO_RL, 0x01);
        break;
        case 2:
            fPCIDevice->ioWrite16(ITCO_RL, 0x01);
        break;
    }
    IOSimpleLockUnlock(lock);
}

/* A very easy way of communicating with userland */
IOReturn iTCOWatchdog::setProperties(OSObject *properties)
{
    OSDictionary *dict;
    OSNumber *num;
    OSString *str;
    
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    if ((dict = OSDynamicCast(OSDictionary, properties))) {
        if ((num = OSDynamicCast(OSNumber, dict->getObject("tcoWdSetTimer")))) {
            tcoWdSetTimer(num->unsigned32BitValue());
            return kIOReturnSuccess;
        } else if ((str = OSDynamicCast(OSString, dict->getObject("tcoWdEnableTimer")))) {
            tcoWdEnableTimer();
            return kIOReturnSuccess;
        } else if ((str = OSDynamicCast(OSString, dict->getObject("tcoWdDisableTimer")))) {
            tcoWdDisableTimer();
            return kIOReturnSuccess;
        } else if ((str = OSDynamicCast(OSString, dict->getObject("tcoWdLoadTimer")))) {
            tcoWdLoadTimer();
            return kIOReturnSuccess;
        }
    }
        
    return kIOReturnUnsupported;
}