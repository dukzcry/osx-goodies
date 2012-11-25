/* Written by Artem Falcon <lomka@gero.in> */

#include <IOKit/IOLib.h> /* IOLog */

#define super IOService

#ifdef DEBUG
#define DbgPrint(id, arg...) IOLog(id arg)
#else
#define DbgPrint(id, arg...) ;
#endif
#define IOPrint(id, arg...) IOLog(id arg)

/* Stub for ICHLPC driver */

#include <IOKit/pci/IOPCIDevice.h>
//#include <IOKit/acpi/IOACPIPlatformDevice.h>

#define lpcid "[iTCOWatchdog:MyLPC] "

#define nitems(_a)      (sizeof((_a)) / sizeof((_a)[0]))

#define ACPI_BASE           0x40
#define ACPI_BASE_MASK      0x00007f80
#define RCBA_BASE           0xf0
#define ACPI_BASE_OFFTCO    0x60
#define ACPI_BASE_ENDTCO    0x7f
#define ACPI_BASE_OFFSMI    0x30
#define ACPI_BASE_ENDSMI    0x33
#define ACPI_BASE_OFFGCS    0x3410
#define ACPI_BASE_ENDGCS    0x3414
#define ACPI_CT             0x44

#define PCI_PRODUCT_ICH9R   0x2916

typedef struct {
	UInt16						lpc_product;

    char                        name[32];
    UInt32                      itco_version;
} lpc_pci_device;
namespace lpc_structs {
    lpc_pci_device lpc_pci_devices[] = {
        { PCI_PRODUCT_ICH9R, "ICH9R", 2 }
    };
}

typedef struct {
    UInt32 start;
    UInt32 end;
} my_res;

class MyLPC: public super {
    OSDeclareDefaultStructors(MyLPC)
private:
    SInt32 AcpiReg;
public:
    IOPCIDevice *fPCIDevice;
    
    lpc_pci_device *lpc;
    
    my_res acpi_tco;
    my_res acpi_smi;
    my_res acpi_gcs;
    
    bool InitWatchdog();
protected:
    virtual bool init(OSDictionary *);
    
    virtual IOService* probe (IOService* provider, SInt32* score);
    virtual void free(void);
    
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider) { super::stop(provider); }
};

bool MyLPC::init (OSDictionary* dict)
{
    bool res = super::init(dict);
    //DbgPrint(lpcid, "init\n");

    AcpiReg = -1;
    
    return res;
}

void MyLPC::free(void)
{
    DbgPrint(lpcid, "free\n");
    
    if (AcpiReg >= 0)
        fPCIDevice->configWrite8(ACPI_CT, AcpiReg);
    
    fPCIDevice->close(this);
    fPCIDevice->release();
    
    super::free();
}

IOService *MyLPC::probe (IOService* provider, SInt32* score)
{
    //IOACPIPlatformDevice *acpiDevice;
	UInt16 DeviceId;
    
    using namespace lpc_structs;
    
    //DbgPrint(lpcid, "probe\n");

#if 0
    /* ACPI 2.x */
    if ((acpiDevice = (typeof(acpiDevice)) provider->metaCast("IOACPIPlatformDevice")) &&
        !(acpiDevice->getACPITableData("WDDT", 0))) {
        IOPrint(lpcid, "Watchdog timer isn't present or disabled in BIOS. Unloading\n");
        return NULL;
    }
#endif

    if (!(fPCIDevice = OSDynamicCast(IOPCIDevice, provider))) {
        //AppleLPC::start - no LPC IOPCIDevice found
        IOPrint(lpcid, "Failed to cast provider\n");
        return NULL;
    }
    
    fPCIDevice->retain();
    fPCIDevice->open(this);
    
	DeviceId = fPCIDevice->configRead16(kIOPCIConfigDeviceID);
    for (int i = 0; i < nitems(lpc_pci_devices); i++) {
        lpc = &lpc_pci_devices[i];
        
        if (lpc->lpc_product == DeviceId) {
            DbgPrint(lpcid, "Found LPC: %s\n", lpc->name);
            break;
        }
    }
    
    if (!InitWatchdog()) {
        IOPrint(lpcid, "Failed to init watchdog\n");
        return NULL;
    }

    /* Throw off the AppleLPC (we don't have another choice) */
    return super::probe(provider, score);
}

bool MyLPC::start(IOService *provider)
{
    bool res = super::start(provider);
    this->registerService();
    
    DbgPrint(lpcid, "LPC preparing done\n");
    
    return res;
}

/* */

bool MyLPC::InitWatchdog()
{
    UInt32 bar;
    
    DbgPrint(lpcid, "%s\n", __FUNCTION__);
    
    /* Power management BAR */
    if ((bar = fPCIDevice->configRead32(ACPI_BASE) & ACPI_BASE_MASK) == 0x0) {
        IOPrint(lpcid, "No MMIO region for ACPI reported\n");
        return false;
    }
    
    acpi_tco.start = bar + ACPI_BASE_OFFTCO;
    acpi_tco.end = bar + ACPI_BASE_ENDTCO;
    acpi_smi.start = bar + ACPI_BASE_OFFSMI;
    acpi_smi.end = bar + ACPI_BASE_ENDSMI;
    
    /* Enable ACPI space */
    AcpiReg = fPCIDevice->configRead8(ACPI_CT);
    fPCIDevice->configWrite8(ACPI_CT, AcpiReg | 0x10);
    
    /* GCS register, for NO_REBOOT flag */
    if (lpc->itco_version == 2) {
        bar = fPCIDevice->configRead32(RCBA_BASE);
        if (!(bar & 1)) {
            //AppleLPC::start - RCBA not enabled
            DbgPrint(lpcid, "RCBA disabled\n");
            return false;
        }
        bar &= 0xffffc000;
        
        acpi_gcs.start = bar + ACPI_BASE_OFFGCS;
        acpi_gcs.end = bar + ACPI_BASE_ENDGCS;
    }
    
    return true;
}