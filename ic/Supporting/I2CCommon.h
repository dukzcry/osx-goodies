#ifndef _I2CCommon_
#define _I2CCommon_

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOFilterInterruptEventSource.h>

namespace I2COperations {
typedef enum {
    I2CReadOp = 1,
    I2CWriteOp,
    //I2CStopOp
} I2COp;
}

class I2CDevice: public IOService
{
    OSDeclareDefaultStructors(I2CDevice)
private:
    IOWorkLoop *MyWorkLoop;
    IOInterruptEventSource *fInterruptSrc;
    IOPCIDevice *fPCIDevice;
    
    UInt32 fBase;
    UInt8 fSt;
    
    struct {
        IOLock *holder;
        bool event;
    } Lock;
    IORWLock *I2C_Lock;
    
    /* Misses serialization */
    struct {
        int op;
        bool error_marker;
        void *buffer;
        size_t length;
    } I2C_Transfer;
    
    virtual bool createWorkLoop(void);
    virtual IOWorkLoop *getWorkLoop(void) const;
    IOInterruptEventSource *CreateDeviceInterrupt(IOInterruptEventSource::Action,
                                                  IOFilterInterruptEventSource::Filter,
                                                  IOService *);
    int I2CExec(I2COperations::I2COp, UInt16, void *, size_t, void *, size_t);
protected:
    virtual bool    init (OSDictionary* dictionary = NULL);
    virtual void    free (void);
    virtual IOService*      probe (IOService* provider, SInt32* score);
    virtual bool    start (IOService* provider);
    virtual void    stop (IOService* provider);
    
    static void interruptHandler(OSObject *owner, IOInterruptEventSource *src, int count);
    static bool interruptFilter(OSObject *owner, IOFilterInterruptEventSource *sender);
public:
    void LockI2CBus();
    void UnlockI2CBus();
    //int StopI2CBus();
    int ReadI2CBus(UInt16, void *, size_t, void *, size_t);
    int WriteI2CBus(UInt16, void *, size_t, void *, size_t);
};

#endif