/* Written by Artem Falcon <lomka@gero.in> */

#include "iTCOWatchdog.h"

/* TO-DO: freeing */
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
    Auto = true;
    
    if ((conf = OSDynamicCast(OSDictionary, getProperty("Settings"))) &&
        (nkey = OSDynamicCast(OSNumber, conf->getObject("Timeout"))))
        Timeout = nkey->unsigned32BitValue();
    if (conf && (bkey = OSDynamicCast(OSBoolean, conf->getObject("Auto"))))
        Auto = bkey->isTrue();
    
    lock = IOSimpleLockAlloc();
    
    GCSMem.range = NULL; GCSMem.map = NULL;
    
    return res;
}

void iTCOWatchdog::free(void)
{    
    DbgPrint(drvid, "free\n");

    //if (Auto)
    tcoWdDisableTimer();
    
    if (SMIEnabled)
        fPCIDevice->ioWrite32(ITCO_SMIEN, fPCIDevice->ioRead32(ITCO_SMIEN) | ITCO_SMIEN_ENABLE);
    
    clearStatus();
    
    if (GCSMem.map) GCSMem.map->release();
    if (GCSMem.range) GCSMem.range->release();
    
    LPCNub->close(this);
    LPCNub->release();
    
    IOSimpleLockFree(lock);

    super::free();
}

IOService *iTCOWatchdog::probe (IOService* provider, SInt32* score)
{
    //DbgPrint(drvid, "probe\n");
    
    if (!(LPCNub = OSDynamicCast(MyLPC, provider))) {
        IOPrint("Failed to cast to MyLPC\n");
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
    
    if (!allowReboots()) {
        IOPrint("Watchdog disabled in BIOS or hardware. Unloading\n");
        return NULL;
    }
    
    if ((fPCIDevice->ioRead16(ITCO_ST2) & ITCO_SECOND_TO_ST)) {
        IOPrint("Recovered after system failure\n");
    }
    
    //disableReboots();
    
    if ((SMIEnabled = (fPCIDevice->ioRead32(ITCO_SMIEN) & ITCO_SMIEN_ST) != 0))
    {
        UInt32 val;
        
        /* Some BIOSes install SMI handlers that reset or disable the watchdog timer 
           instead of resetting the system, so we disable the SMI */
        val = fPCIDevice->ioRead32(ITCO_SMIEN);
        //IOPrint(drvid, "ITCO_SMIEN was: %04X\n", val);
        val &= ~ITCO_SMIEN_ENABLE;
        //IOPrint(drvid, "Will try to set ITCO_SMIEN to: %04X\n", val);
        fPCIDevice->ioWrite32(ITCO_SMIEN, val);
        //IOPrint(drvid, "ITCO_SMIEN is: %04X\n", fPCIDevice->ioRead32(ITCO_SMIEN));
    }
    
    IOPrint(drvid, "Attached %s iTCO v%d at phys 0x%04llx\n", LPCNub->lpc->name, LPCNub->lpc->itco_version,
            (UInt64) ITCO_BASE);
    
    clearStatus();
    tcoWdDisableTimer();
    
    IOPrint(drvid, "Initialized\n");
    
    return super::probe(provider, score);
}

bool iTCOWatchdog::start(IOService *provider)
{    
    bool res = super::start(provider);
    //DbgPrint(drvid, "start\n");
    
#if 0
    tcoWdSetTimer(Timeout);
    tcoWdEnableTimer();
    
    for (int i = 0; (i = readTimeleft()) > 0; ) {
        //IOPrint(drvid, "Time left: %d\n", i);
        IODelay(5000);
    }
    
    IOPrint(drvid, "ITCO_SMIEN is: %04X\n", fPCIDevice->ioRead32(ITCO_SMIEN));
    IOPrint(drvid, "ICHLPC_GCS is: %04X\n", OSReadLittleInt32(GCSMem.vaddr, 0x0));
#endif
    
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

bool iTCOWatchdog::allowReboots()
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    /* Clear NO_REBOOT flag */
    switch (LPCNub->lpc->itco_version) {
        case 1:
            fPCIDevice->configWrite32(ICHLPC_GEN_STA, LPCNub->fPCIDevice->configRead32(ICHLPC_GEN_STA) & ~ICHLPC_GEN_STA_NO_REBOOT);
            if (fPCIDevice->configRead32(ICHLPC_GEN_STA) & ICHLPC_GEN_STA_NO_REBOOT)
                return false;
        break;
        case 2:
            UInt32 val;
            
            val = OSReadLittleInt32(GCSMem.vaddr, 0x0);
            IOPrint(drvid, "ICHLPC_GCS was: %04X\n", val);
            val &= ~ICHLPC_GCS_NO_REBOOT;
            IOPrint(drvid, "Will try to set ICHLPC_GCS to: %04X\n", val);
            OSWriteLittleInt32(GCSMem.vaddr, 0x0, val);
            val = OSReadLittleInt32(GCSMem.vaddr, 0x0);
            IOPrint(drvid, "ICHLPC_GCS is: %04X\n", val);
            if (val & ICHLPC_GCS_NO_REBOOT)
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
            fPCIDevice->configWrite32(ICHLPC_GEN_STA, LPCNub->fPCIDevice->configRead32(ICHLPC_GEN_STA) | ICHLPC_GEN_STA_NO_REBOOT);
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
int iTCOWatchdog::readTimeleft()
{
    UInt8 val;
    
    //DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    switch (LPCNub->lpc->itco_version) {
        case 1:
            val = fPCIDevice->ioRead8(ITCO_RL) & ITCO1_RLD_TMR_MAX;
            if (!(fPCIDevice->ioRead16(ITCO_ST1) & ITCO_TIMEOUT_ST))
                val += fPCIDevice->ioRead8(ITCO1_TM) & ITCO1_RLD_TMR_MAX;
        break;
        case 2:
            val = fPCIDevice->ioRead16(ITCO_RL) & ITCO2_RLD_TMR_MAX;
        break;
    }
    
    return (val * 3) / 5;
}
#endif

/* */

void iTCOWatchdog::tcoWdSetTimer(UInt32 time)
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    /* 1 tick = 0.6 sec */
    time = (time * 5) / 3;
    if (LPCNub->lpc->itco_version == 1)
        time /= 2;

    /* Throw away 0x0-0x3, otherwise write value into hw */
    if (time < ITCO_RLD_TMR_MIN)
        goto fail;
    switch (LPCNub->lpc->itco_version) {
        case 1:
            if (time > ITCO1_RLD_TMR_MAX) {
                if (init_stage) {
                    IOPrint(drvid, "Timeout is not in range [5..76], using %d instead\n", DEFAULT_TIMEOUT);
                    init_stage = false;
                }
                goto fail;
            }
            
            IOSimpleLockLock(lock);
            fPCIDevice->ioWrite8(ITCO1_TM, (fPCIDevice->ioRead8(ITCO1_TM) & 0xc0) | (time & 0xff));
            
#ifdef DEBUG
            if ((fPCIDevice->ioRead8(ITCO1_TM) & ITCO1_RLD_TMR_MAX) != time)
                IOPrint(drvid, "Failed to set time\n");
#endif
            
            IOSimpleLockUnlock(lock);
        break;
        case 2:
            if (time > ITCO2_RLD_TMR_MAX) {
                if (init_stage) {
                    IOPrint(drvid, "Timeout is not in range [3..614], using %d instead\n", DEFAULT_TIMEOUT);
                    init_stage = false;
                }
                goto fail;
            }
            
            IOSimpleLockLock(lock);
            fPCIDevice->ioWrite16(ITCO2_TM, (fPCIDevice->ioRead16(ITCO2_TM) & 0xfc00) | time);
            
#ifdef DEBUG
            if ((fPCIDevice->ioRead16(ITCO2_TM) & ITCO2_RLD_TMR_MAX) != time)
                IOPrint(drvid, "Failed to set time\n");
#endif
            
            IOSimpleLockUnlock(lock);
        break;
    }
    
    return;
fail:
    tcoWdSetTimer(DEFAULT_TIMEOUT);
}

void iTCOWatchdog::tcoWdDisableTimer()
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    IOSimpleLockLock(lock);
    fPCIDevice->ioWrite16(ITCO_CR, (fPCIDevice->ioRead16(ITCO_CR) & ITCO_CR_PRESERVE) | ITCO_TMR_HALT);
    
    //disableReboots();

#ifdef DEBUG
    if (!(fPCIDevice->ioRead16(ITCO_CR) & ITCO_TMR_HALT))
        IOPrint(drvid, "Failed to disable timer\n");
#endif
    
    IOSimpleLockUnlock(lock);
}

void iTCOWatchdog::tcoWdEnableTimer()
{
    DbgPrint(drvid, "%s\n", __FUNCTION__);
    
    IOSimpleLockLock(lock);
    
    
    //allowReboots();
    reloadTimer();
    
    fPCIDevice->ioWrite16(ITCO_CR, (fPCIDevice->ioRead16(ITCO_CR) & ITCO_CR_PRESERVE) & ~ITCO_TMR_HALT);
    
#ifdef DEBUG
    if (fPCIDevice->ioRead16(ITCO_CR) & ITCO_TMR_HALT)
        IOPrint(drvid, "Failed to enable timer\n");
#endif

    IOSimpleLockUnlock(lock);
}