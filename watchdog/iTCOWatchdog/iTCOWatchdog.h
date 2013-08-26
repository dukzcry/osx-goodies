#include "Various.h"

#define drvid "[iTCOWatchdog] "

#define DEFAULT_TIMEOUT    30

#define ITCO_BASE   (LPCNub->acpi_tco.start)
#define ITCO_SMIEN  (LPCNub->acpi_smi.start)
#define ITCO_ST1    (ITCO_BASE + 0x04)
#define ITCO_ST2    (ITCO_BASE + 0x06)
#define ITCO_RL     (ITCO_BASE + 0x00)
#define ITCO1_TM    (ITCO_BASE + 0x01)
#define ITCO2_TM    (ITCO_BASE + 0x12)
#define ITCO_CR     (ITCO_BASE + 0x08)
#define ITCO_CR_PRESERVE   0x0200
#define ITCO_SMIEN_ENABLE  0x2000
#define ITCO_SMIEN_ST      ITCO_SMIEN_ENABLE 
#define ITCO_RL_TM_MIN     0x04
#define ITCO1_RL_TM_MAX    0x03f
#define ITCO2_RL_TM_MAX    0x3ff
/* TCO1 */
#define ITCO_TM_HALT       0x0800
#define ITCO_TIMEOUT_ST    0x08
/* TCO2 */
#define ITCO_SECOND_TO_ST  0x02
#define ITCO_BOOT_ST       0x04 /* Failed to come out of reset */

#define ICHLPC_GEN_STA_NO_REBOOT 0x02
#define ICHLPC_GCS_NO_REBOOT     0x20

class iTCOWatchdog: public IOService {
    OSDeclareDefaultStructors(iTCOWatchdog)
private:
    OSDictionary *conf;
    IOPCIDevice *fPCIDevice;
    IOSimpleLock *lock;
    struct {
        IODeviceMemory *range;
        IOMemoryMap *map;
        void *vaddr;
    } GCSMem;
    struct {
        IOThread Thread;
        struct thread_data *Data;
    } thread;
    
    MyLPC *LPCNub;
    bool SelfFeeding, WorkaroundBug; UInt32 Timeout;
    bool SMIWereEnabled, first_run, is_active;

    void clearStatus();
    bool enableReboots();
    //void disableReboots();
    void reloadTimer();
#if defined DEBUG
    UInt32 readTimeleft();
#endif
    void free_common();
    IOThread IOCreateThread(IOThreadFunc, void *);
    
    void tcoWdSetTimer(UInt32);
    void tcoWdDisableTimer();
    void tcoWdEnableTimer();
public:
    void tcoWdLoadTimer();
protected:
    virtual bool init(OSDictionary *);
    
    virtual IOService* probe (IOService* provider, SInt32* score);
    virtual void free(void);
    
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    
    virtual IOReturn setProperties(OSObject *);
    virtual void systemWillShutdown(IOOptionBits);
    
    virtual unsigned long initialPowerStateForDomainState(IOPMPowerFlags);
    virtual IOReturn setPowerState(unsigned long, IOService *);
};

struct thread_data {
    class iTCOWatchdog *instance;
    unsigned msecs;
};