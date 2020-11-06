/* Written by Artem Lukyanov <dukzcry@ya.ru> */

/* Synchronous driver for Intel ICH SMBus controller */

#include "ICHSMBus.h"

using namespace I2COperations;
OSDefineMetaClassAndStructors(I2CDevice, IOService)

bool I2CDevice::init (OSDictionary* dict)
{
    bool res = super::init(dict);
    DbgPrint("init\n");
    
    fPCIDevice = NULL;
    MyWorkLoop = NULL; fInterruptSrc = NULL;
    Lock.holder = NULL; I2C_Lock = NULL;
    
    return res;
}


void I2CDevice::free(void)
{
    DbgPrint("free\n");
    
    if (Lock.holder) {
        /*IOLockUnlock(Lock.holder);*/ IOLockFree(Lock.holder);
    }
    if (I2C_Lock) {
        /*IOSimpleLockUnlock(I2C_Lock);*/ IORWLockFree(I2C_Lock);
    }
    
    if (fInterruptSrc && MyWorkLoop)
        MyWorkLoop->removeEventSource(fInterruptSrc);
    if (fInterruptSrc) fInterruptSrc->release();
    
    fPCIDevice->close(this);
    fPCIDevice->release();
    
    super::free();
}

bool I2CDevice::createWorkLoop(void)
{
    MyWorkLoop = IOWorkLoop::workLoop();
    
    return (MyWorkLoop != 0);
}
IOWorkLoop *I2CDevice::getWorkLoop(void) const
{
    return MyWorkLoop;
}


IOService *I2CDevice::probe (IOService* provider, SInt32* score)
{
    DbgPrint("probe\n");
    
    *score = 5000;
    return this;
}

IOInterruptEventSource *I2CDevice::CreateDeviceInterrupt(IOInterruptEventSource::Action action,
                                              IOFilterInterruptEventSource::Filter filter,
                                              IOService *provider)
{
    return IOFilterInterruptEventSource::filterInterruptEventSource(this, action, filter, provider);
}


bool I2CDevice::interruptFilter(OSObject *owner, IOFilterInterruptEventSource *sender)
{
    I2CDevice *obj = (I2CDevice *) owner;
    
    obj->fSt = obj->fPCIDevice->ioRead8(obj->fBase + ICH_SMB_HS);
    if ((obj->fSt & ICH_SMB_HS_BUSY) != 0 || (obj->fSt & (ICH_SMB_HS_INTR |
        ICH_SMB_HS_DEVERR | ICH_SMB_HS_BUSERR | ICH_SMB_HS_FAILED |
        ICH_SMB_HS_SMBAL | ICH_SMB_HS_BDONE)) == 0)
        return false;
    
    return true;
}

void I2CDevice::interruptHandler(OSObject *owner, IOInterruptEventSource *src, int count)
{
    I2CDevice *obj = (I2CDevice *) owner;
    UInt8 *Bp; size_t len;

    PrintBitFieldExpanded(obj->fSt);
    
    obj->fPCIDevice->ioWrite8(obj->fBase + ICH_SMB_HS, obj->fSt);
    /* Catch <DEVERR,INUSE> when cold start */
    if (obj->I2C_Transfer.op == I2CNoOp)
        return;
    
    if (obj->fSt & (ICH_SMB_HS_DEVERR | ICH_SMB_HS_BUSERR | ICH_SMB_HS_FAILED)) {
        obj->I2C_Transfer.error_marker = 1;
        goto done;
    }
    
    if (obj->fSt & ICH_SMB_HS_INTR) {
        if (obj->I2C_Transfer.op == I2CWriteOp)
            goto done;
            
        Bp = (UInt8 *) obj->I2C_Transfer.buffer;
        len = obj->I2C_Transfer.length;
        if (len > 0)
            Bp[0] = obj->fPCIDevice->ioRead8(obj->fBase + ICH_SMB_HD0);
        if (len > 1)
            Bp[1] = obj->fPCIDevice->ioRead8(obj->fBase + ICH_SMB_HD1);
    }
    
done:
    IOLockLock(obj->Lock.holder);
    obj->Lock.event = true;
    IOLockWakeup(obj->Lock.holder, &obj->Lock.event, true);
    IOLockUnlock(obj->Lock.holder);
    return;
}

bool I2CDevice::start(IOService *provider)
{
    bool res;
    uint32_t hostc;
    
    res = super::start(provider);
    DbgPrint("start\n");
    
    if (!(fPCIDevice = OSDynamicCast(IOPCIDevice, provider))) {
        IOPrint("Failed to cast provider\n");
        return false;
    }
    
    fPCIDevice->retain();
    fPCIDevice->open(this);
    
    hostc = fPCIDevice->configRead8(ICH_SMB_HOSTC);
    DbgPrint("conf: 0x%x\n", hostc);
    
    if ((hostc & ICH_SMB_HOSTC_HSTEN) == 0) {
        IOPrint("SMBus disabled\n");
        return false;
    }
    
    fBase = fPCIDevice->configRead16(ICH_SMB_BASE) & 0xFFFE;
    
    fPCIDevice->setIOEnable(true);
    
    if (hostc & ICH_SMB_HOSTC_SMIEN) {
        IOPrint("No PCI IRQ. Poll mode is not implemented. Unloading.\n");
        return false;
    }
    
    I2C_Transfer.op = I2CNoOp;
    createWorkLoop();
    if (!(MyWorkLoop = (IOWorkLoop *) getWorkLoop()))
        return false;
    /* Interrupt support exists on chips starting from 82801EB */
    if (!(fInterruptSrc = CreateDeviceInterrupt(&I2CDevice::interruptHandler, &I2CDevice::interruptFilter,
                                                provider))) {
        IOPrint("Installing interrupt handler failed. Poll mode is not implemented. Unloading.\n");
        return false;
    }
    MyWorkLoop->addEventSource(fInterruptSrc);
    /* Avoid masking interrupts for other devices that are sharing the interrupt line
     * by immediate enabling of the event source */
    fInterruptSrc->enable();
    IOPrint("IRQ: %d\n", (UInt8) fPCIDevice->configRead32(kIOPCIConfigInterruptLine));
    
    Lock.holder = IOLockAlloc();
    Lock.event = false;
    I2C_Lock = IORWLockAlloc();
    
    /* Allow matching of drivers which use our class as a provider class */
    this->registerService();

    return res;
}

void I2CDevice::stop(IOService *provider)
{
    DbgPrint("stop\n");
    super::stop(provider);
}

/* Export methods */
void I2CDevice::LockI2CBus()
{
    IORWLockWrite(I2C_Lock);
}
void I2CDevice::UnlockI2CBus()
{
    IORWLockUnlock(I2C_Lock);
}
int I2CDevice::I2CExec(I2COp op, UInt16 addr, void *cmdbuf, size_t cmdlen, void *buf, size_t len)
{
    int ret;
    UInt8 St, ctl;
    AbsoluteTime deadline;
    
    DbgPrint("exec: op %d, addr 0x%02x, cmdlen %d, len %d\n", op, addr, (int)cmdlen, (int)len);
    
    /* Wait for bus to be idle */
    for (int retries = 100; retries > 0; retries--) {
        St = fPCIDevice->ioRead8(fBase + ICH_SMB_HS);
        if ((St & ICH_SMB_HS_BUSY) == 0)
            break;
        IODelay(ICHSMBUS_DELAY);
    }
    
    DbgPrint("exec: St 0x%x\n", St & ICH_SMB_HS_BUSY);
    if (St & ICH_SMB_HS_BUSY)
        return 1;
    
    if (/* Limited to 2-byte data */
        cmdlen > 1 || len > 2)
        return 1;
    
    fPCIDevice->ioWrite8(fBase + ICH_SMB_TXSLVA, ICH_SMB_TXSLVA_ADDR(addr) |
                         (op == I2CReadOp ? ICH_SMB_TXSLVA_READ : 0));
    
    if (cmdlen > 0)
        fPCIDevice->ioWrite8(fBase + ICH_SMB_HCMD, ((UInt8 *) cmdbuf)[0]);
    
    I2C_Transfer.op = op;
    I2C_Transfer.error_marker = 0;
    if (op == I2CReadOp) {
        I2C_Transfer.buffer = buf;
        I2C_Transfer.length = len;
    } else {
        if (len > 0)
            fPCIDevice->ioWrite8(fBase + ICH_SMB_HD0, ((UInt8 *) buf)[0]);
        if (len > 1)
            fPCIDevice->ioWrite8(fBase + ICH_SMB_HD0, ((UInt8 *) buf)[1]);
    }
    
    if (!len)
        ctl = ICH_SMB_HC_CMD_BYTE;
    else if (len == 1)
        ctl = ICH_SMB_HC_CMD_BDATA;
    else /* len == 2 */
        ctl = ICH_SMB_HC_CMD_WDATA;
    
    ctl |= ICH_SMB_HC_INTREN | ICH_SMB_HC_START;
    fPCIDevice->ioWrite8(fBase + ICH_SMB_HC, ctl);

    clock_interval_to_deadline(ICHIIC_TIMEOUT, kSecondScale, (UInt64 *) &deadline);
    IOLockLock(Lock.holder);
    if (Lock.event) {
        Lock.event = false;
        IOLockUnlock(Lock.holder);
        goto done;
    }
    /* Don't block forever */
    ret = IOLockSleepDeadline(Lock.holder, &Lock.event, deadline, THREAD_INTERRUPTIBLE);
    Lock.event = false;
    IOLockUnlock(Lock.holder);
    if (ret != THREAD_AWAKENED)
        return 1;
    
done:
    if (I2C_Transfer.error_marker)
        return 1;
    
    return 0;
}
/*int I2CDevice::StopI2CBus()
{
    return I2CExec(I2CStopOp, 0, NULL, 0, NULL, 0);
}*/
int I2CDevice::ReadI2CBus(UInt16 addr, void *cmdbuf, size_t cmdlen, void *buf, size_t len)
{
    return I2CExec(I2CReadOp, addr, cmdbuf, cmdlen, buf, len);
}
int I2CDevice::WriteI2CBus(UInt16 addr, void *cmdbuf, size_t cmdlen, void *buf, size_t len)
{
    return I2CExec(I2CWriteOp, addr, cmdbuf, cmdlen, buf, len);
}
/* */
