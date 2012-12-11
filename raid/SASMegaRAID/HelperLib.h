#define PCI_MAPREG_MEM_ADDR_MASK        0xfffffff0

#define	PCI_MAPREG_TYPE_MASK            0x00000001
#define	PCI_MAPREG_MEM_TYPE_MASK        0x00000006

#define PCI_MAPREG_TYPE_IO              0x00000001
#define PCI_MAPREG_MEM_TYPE_32BIT       0x00000000
#define PCI_MAPREG_MEM_TYPE_32BIT_1M    0x00000002
#define PCI_MAPREG_MEM_TYPE_64BIT       0x00000004


/* Templates are allowed in non-IOKit classes */
template <typename UserClass> class PCIHelper
{
    typedef void(UserClass::*IH)(OSObject *, void *, IOService *, int);
    typedef bool(UserClass::*IF)(OSObject *, IOFilterInterruptEventSource *);
public:
	UInt32 MappingType(UserClass* CPtr, UInt8 regbar, UInt32 *barval = NULL);
    bool CreateDeviceInterrupt(UserClass *CPtr, IOService *provider, 
        bool Prefer_MSI, IH InterruptHandler, IF InterruptFilter = NULL);
};

template <typename UserClass>
UInt32 PCIHelper<UserClass>::MappingType(UserClass* CPtr, UInt8 regbar, UInt32 *barval)
{
    UInt32 bar, iobar, result;
    
    bar = CPtr->fPCIDevice->configRead32(regbar) & PCI_MAPREG_MEM_ADDR_MASK;
    if(barval) *barval = bar;
    DbgPrint("[Helper] Region starting at %#x\n", bar);
    iobar = bar & PCI_MAPREG_TYPE_MASK;
    
    /* I/O or MMR? */
    result = (iobar == PCI_MAPREG_TYPE_IO) ? iobar :
    /* 0 00 0   32bit
     * 0 01 0   32bit < 1M
     * 0 10 0   64bit */
    /*              0111 */
    bar & (PCI_MAPREG_TYPE_MASK|PCI_MAPREG_MEM_TYPE_MASK);
    
    switch(result & PCI_MAPREG_MEM_TYPE_MASK) {
        case PCI_MAPREG_MEM_TYPE_32BIT_1M:
            return PCI_MAPREG_MEM_TYPE_32BIT_1M;
            break;
        case PCI_MAPREG_MEM_TYPE_32BIT:
            return PCI_MAPREG_MEM_TYPE_32BIT;
            break;
        case PCI_MAPREG_MEM_TYPE_64BIT:
            if(barval != NULL)
                /* UInt64 *wbarval = CPtr->fPCIDevice->configRead32(regbar & PCI_MAPREG_MEM_ADDR_MASK) >> 32;
                 * wbarval = CPtr->fPCIDevice->configRead32(regbar + 4 & 0xffffffff); */
                *barval = CPtr->fPCIDevice->configRead32(regbar + 4);
            
            return PCI_MAPREG_MEM_TYPE_64BIT;
    }
    
    return result;
}

template <typename UserClass>
bool PCIHelper<UserClass>::CreateDeviceInterrupt(UserClass *CPtr, IOService *provider, bool Prefer_MSI,
    IH InterruptHandler, IF InterruptFilter)
{
    int intr_type, msi_index, legacy_index, intr_index;
    IOReturn intr_ret;
    
    if(CPtr->fPCIDevice->findPCICapability(kIOPCIMSICapability)) /* MCR */
        DbgPrint("[Helper] Looks like MSI interrupts are supported\n");

    /* Search for the indexes for legacy and MSI interrupts */
    /* From http://lists.apple.com/archives/darwin-drivers/2011/Mar/msg00024.html */
    intr_index = 0;
    msi_index = legacy_index = -1;

    while(1) {
        if ((intr_ret = provider->getInterruptType(intr_index, &intr_type)) != kIOReturnSuccess)
            break;
        if (intr_type == kIOInterruptTypeLevel)
            legacy_index = intr_index;
        else if (intr_type & kIOInterruptTypePCIMessaged)
            msi_index = intr_index;
        intr_index++;
    }
    intr_index = (Prefer_MSI && msi_index != -1) ? msi_index : intr_index = legacy_index;
    if (intr_index == msi_index)
        CPtr->fMSIEnabled = true;

    if(!(CPtr->MyWorkLoop = IOWorkLoop::workLoop())) {
        IOPrint("[Helper] My Workloop creation failed\n");
        return false;
    }
    
    if(CPtr->fMSIEnabled) {
        IOPrint("[Helper] MSI interrupt index: %d\n", msi_index);
        /* MSI interrupts can't be shared, so we don't use a filter. */
        CPtr->fInterruptSrc = IOInterruptEventSource::interruptEventSource(CPtr, 
            OSMemberFunctionCast(IOInterruptEventSource::Action, CPtr, InterruptHandler), 
            provider, intr_index);
    }
    else {
        if (!InterruptFilter)
            CPtr->fInterruptSrc = IOInterruptEventSource::interruptEventSource(CPtr, 
            OSMemberFunctionCast(IOInterruptEventSource::Action, CPtr, InterruptHandler), 
            provider);
        else
            /* Apple suggests of using IOFilterInterruptEventSource class instead of IOInterruptEventSource
             * to significantly improve perfomance in environments, where an interrupt line gets shared */
            CPtr->fInterruptSrc = IOFilterInterruptEventSource::filterInterruptEventSource(CPtr,  
                OSMemberFunctionCast(IOInterruptEventSource::Action, CPtr, InterruptHandler),
                OSMemberFunctionCast(IOFilterInterruptEventSource::Filter, CPtr, InterruptFilter),
                provider);
    }
    if (!CPtr->fInterruptSrc || CPtr->MyWorkLoop->addEventSource(CPtr->fInterruptSrc) != kIOReturnSuccess)
    {
        if (!CPtr->fInterruptSrc) IOPrint("[Helper] Couldn't create interrupt source\n");
            else IOPrint("[Helper] Couldn't attach interrupt source\n");
        return false;
    }
    
    if (!CPtr->fMSIEnabled) {
        /* Avoiding of masking interrupts for other devices that are sharing the interrupt line 
         * by immediate enabling of the event source */
        CPtr->fInterruptSrc->enable();
        IOPrint("IRQ: %d\n", (UInt8) CPtr->fPCIDevice->configRead32(kIOPCIConfigInterruptLine));
    }
    
    return true;
}