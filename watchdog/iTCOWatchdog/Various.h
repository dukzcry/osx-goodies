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
#define RCBA_EN             0x00000001
#define ACPI_BASE_OFFTCO    0x60
#define ACPI_BASE_ENDTCO    0x7f
#define ACPI_BASE_OFFSMI    0x30
#define ACPI_BASE_ENDSMI    0x33
#define ACPI_BASE_OFFGCS    0x3410
#define ACPI_BASE_ENDGCS    0x3414
#define ACPI_CT             0x44

/* Quirks */
#define GEN_PMCON_3         0xa4
#define AFTERG3_ST(x)       (x & 0x0001)
#define AFTERG3_ENABLE(x)   (x | 1 << 0)
/* */

/* These are pci8086 from AppleLPC in same order */
#define PCI_PRODUCT_ICH8ME  0x2811
#define PCI_PRODUCT_ICH8M   0x2815
#define PCI_PRODUCT_ICH7M   0x27b9
#define PCI_PRODUCT_ICH7MDH 0x27bd
#define PCI_PRODUCT_63XXESB 0x2670
//#define PCI_PRODUCT_SCH   0x8119
#define PCI_PRODUCT_ICH9R   0x2916
#define PCI_PRODUCT_ICH10   0x3a18
#define PCI_PRODUCT_PCH     0x3b00
#define PCI_PRODUCT_PCHM    0x3b01
#define PCI_PRODUCT_P55     0x3b02
#define PCI_PRODUCT_HM55    0x3b09
#define PCI_PRODUCT_PPT4    0x1e44

#define PCI_PRODUCT_CPTD    0x1c42
#define PCI_PRODUCT_CPT4    0x1c44
#define PCI_PRODUCT_CPT14   0x1c4e
#define PCI_PRODUCT_PPT12   0x1e4c
#define PCI_PRODUCT_CPT16   0x1c50
#define PCI_PRODUCT_CPT10   0x1c4a
#define PCI_PRODUCT_CPT6    0x1c46
#define PCI_PRODUCT_CPT28   0x1c5c
#define PCI_PRODUCT_CPT18   0x1c52
#define PCI_PRODUCT_CPT20   0x1c54
#define PCI_PRODUCT_CPT22   0x1c56
#define PCI_PRODUCT_CPT3    0x1c43
#define PCI_PRODUCT_CPT15   0x1c4f
#define PCI_PRODUCT_CPT7    0x1c47
#define PCI_PRODUCT_CPT9    0x1c49
#define PCI_PRODUCT_CPT11   0x1c4b
#define PCI_PRODUCT_CPT1    0x1c41
#define PCI_PRODUCT_CPT13   0x1c4d
#define PCI_PRODUCT_PPT2    0x1e42
#define PCI_PRODUCT_PPT21   0x1e55
#define PCI_PRODUCT_PPT24   0x1e58
#define PCI_PRODUCT_PPT23   0x1e57
#define PCI_PRODUCT_PPT25   0x1e59
#define PCI_PRODUCT_PPT29   0x1e5d
#define PCI_PRODUCT_PPT3    0x1e43
#define PCI_PRODUCT_PPT22   0x1e56
/* */

/* Additional ones */
#define PCI_PRODUCT_ICH9    0x2918
#define PCI_PRODUCT_ICH10R  0x3a16
#define PCI_PRODUCT_H55	    0x3b06

typedef struct {
	UInt16						lpc_product;

    char                        name[32];
    UInt32                      itco_version;
} lpc_pci_device;
namespace lpc_structs {
    lpc_pci_device lpc_pci_devices[] = {
        { 0, "Unknown", 2 },
        
        { PCI_PRODUCT_63XXESB, "631xESB/632xESB", 2 },
        { PCI_PRODUCT_ICH7M, "ICH7-M/ICH7-U", 2 },
        { PCI_PRODUCT_ICH7MDH, "ICH7-M DH", 2 },
        { PCI_PRODUCT_ICH8ME, "ICH8-ME", 2 },
        { PCI_PRODUCT_ICH8M, "ICH8M", 2 },
        { PCI_PRODUCT_ICH9, "ICH9", 2 },
        { PCI_PRODUCT_ICH9R, "ICH9R", 2 },
        { PCI_PRODUCT_ICH10, "ICH10", 2 },
        { PCI_PRODUCT_ICH10R, "ICH10R", 2 },
        { PCI_PRODUCT_PCH, "PCH Desktop Full Featured", 2 },
        { PCI_PRODUCT_PCHM, "PCH Mobile Full Featured", 2 },
        { PCI_PRODUCT_P55, "P55", 2 },
        { PCI_PRODUCT_H55, "H55", 2 },
        { PCI_PRODUCT_HM55, "HM55", 2 },
        { PCI_PRODUCT_CPTD, "Cougar Point Desktop", 2 },
        { PCI_PRODUCT_CPT1, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT4, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT3, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT6, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT7, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT9, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT10, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT11, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT13, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT14, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT15, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT16, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT18, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT20, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT22, "Cougar Point", 2 },
        { PCI_PRODUCT_CPT28, "Cougar Point", 2 },
        { PCI_PRODUCT_PPT2, "Panther Point", 2 },
        { PCI_PRODUCT_PPT4, "Panther Point", 2 },
        { PCI_PRODUCT_PPT3, "Panther Point", 2 },
        { PCI_PRODUCT_PPT12, "Panther Point", 2 },
        { PCI_PRODUCT_PPT21, "Panther Point", 2 },
        { PCI_PRODUCT_PPT22, "Panther Point", 2 },
        { PCI_PRODUCT_PPT23, "Panther Point", 2 },
        { PCI_PRODUCT_PPT24, "Panther Point", 2 },
        { PCI_PRODUCT_PPT25, "Panther Point", 2 },
        { PCI_PRODUCT_PPT29, "Panther Point", 2 }
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
    lpc = NULL;
    
    return res;
}

void MyLPC::free(void)
{
    DbgPrint(lpcid, "free\n");

#if 0
    if (AcpiReg >= 0)
        fPCIDevice->configWrite8(ACPI_CT, AcpiReg);
    
    /* Stay in S5 state on next power on */
    if (!AFTERG3_ST(bar = fPCIDevice->configRead16(GEN_PMCON_3)))
        fPCIDevice->configWrite16(GEN_PMCON_3, AFTERG3_ENABLE(bar));
#endif
    
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
    for (int i = 1; i < nitems(lpc_pci_devices); i++)
        if (lpc_pci_devices[i].lpc_product == DeviceId) {
            lpc = &lpc_pci_devices[i];
            DbgPrint(lpcid, "Found LPC: %s\n", lpc->name);
            break;
        }
    if (!lpc) lpc = &lpc_pci_devices[0];
    
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

#if 0
    /* Enable ACPI space */
    AcpiReg = fPCIDevice->configRead8(ACPI_CT);
    fPCIDevice->configWrite8(ACPI_CT, AcpiReg | 0x10);
#endif
    
    /* GCS register, for NO_REBOOT flag */
    if (lpc->itco_version == 2) {
        bar = fPCIDevice->configRead32(RCBA_BASE);
        if (!(bar & RCBA_EN)) {
            //AppleLPC::start - RCBA not enabled
            DbgPrint(lpcid, "RCBA disabled\n");
            return false;
        }
        bar &= ~RCBA_EN;
        
        acpi_gcs.start = bar + ACPI_BASE_OFFGCS;
        acpi_gcs.end = bar + ACPI_BASE_ENDGCS;
    }
    
    return true;
}