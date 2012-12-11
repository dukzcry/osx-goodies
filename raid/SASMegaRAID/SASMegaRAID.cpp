/* Notes for code reader:
 - Syncs are bogus and not in all places they ough to be
 - Segments support bits aren't in all places they should to be */

#include "SASMegaRAID.h"
#include "Registers.h"

OSDefineMetaClassAndStructors(SASMegaRAID, IOSCSIParallelInterfaceController)
OSDefineMetaClassAndStructors(mraid_ccbCommand, IOCommand)

bool SASMegaRAID::init (OSDictionary* dict)
{
    BaseClass::init(dict);
    //DbgPrint("IOService->init\n");
    
    fPCIDevice = NULL;
    map = NULL;
    MyWorkLoop = NULL; fInterruptSrc = NULL;
    InterruptsActivated = FirmwareInitialized = fMSIEnabled = ccb_inited = false;
    conf = OSDynamicCast(OSDictionary, getProperty("Settings"));
    
	/* Create an instance of PCI class from Helper Library */
	PCIHelperP = new PCIHelper<SASMegaRAID>;
    
    sc.sc_iop = IONew(mraid_iop_ops, 1);
    sc.sc_ccb_spin = NULL; sc.sc_lock = NULL;
    
    sc.sc_pcq = sc.sc_frames = sc.sc_sense = NULL;
    sc.sc_bbuok = false;
    memset(sc.sc_ld_present, 0, MRAID_MAX_LD);
    
    return true;
}

IOService *SASMegaRAID::probe (IOService* provider, SInt32* score)
{
    //DbgPrint("IOService->probe\n");
    return BaseClass::probe(provider, score);
}

//bool SASMegaRAID::start(IOService *provider)
bool SASMegaRAID::InitializeController(void)
{
    IOService *provider = getProvider();
    IODeviceMemory *MemDesc;
    UInt8 regbar;
    UInt32 type, barval;
    
    //BaseClass::start(provider);
    DbgPrint("super->InitializeController\n");
    
    if (!(fPCIDevice = OSDynamicCast(IOPCIDevice, provider))) {
        IOPrint("Failed to cast provider\n");
        return false;
    }

    fPCIDevice->retain();
    fPCIDevice->open(this);
    
    if(!(mpd = MatchDevice())) {
        IOPrint("Device matching failed\n");
        return false;
    }

	/* Choosing BAR register */
	regbar = (mpd->mpd_iop == MRAID_IOP_GEN2 || mpd->mpd_iop == MRAID_IOP_SKINNY) ?
        MRAID_BAR_GEN2 : MRAID_BAR;

    /* We do DMA transactions */
    fPCIDevice->setBusMasterEnable(true);
    /* Figuring out mapping scheme */
    type = PCIHelperP->MappingType(this, regbar, &barval);
    switch(type) {
        case PCI_MAPREG_TYPE_IO:
#if 1
            IOPrint("Memory mapping failed\n");
            return false;
#else
            fPCIDevice->setIOEnable(true);
        break;
#endif
        case PCI_MAPREG_MEM_TYPE_32BIT_1M:
        case PCI_MAPREG_MEM_TYPE_32BIT:
            fPCIDevice->setMemoryEnable(true);

            if (!(MemDesc = IODeviceMemory::withRange(barval, MRAID_PCI_MEMSIZE))) {
                IOPrint("Memory mapping failed\n");
                return false;
            }
            
            if(MemDesc != NULL) {
                map = MemDesc->map();
                MemDesc->release();
                if(map != NULL) {
                    vAddr = (void *) map->getVirtualAddress();
                    DbgPrint("Memory mapped at virtual address %#x,"
                             " length %d\n", (UInt32)map->getVirtualAddress(),
                             (UInt32)map->getLength());
                }
                else {
                    IOPrint("Can't map controller PCI space\n");
                    return false;
                }
            }
        break;
        case PCI_MAPREG_MEM_TYPE_64BIT:
#ifdef notyet
            IOPrint("PCI-X support to be implemented\n");
            return false;
#else
            fPCIDevice->setMemoryEnable(true);
            
            /* Rework: Mapping with 64-bit address. */
            MemDesc = IODeviceMemory::withRange((IOPhysicalAddress64) barval >> 32, MRAID_PCI_MEMSIZE);
            if(MemDesc != NULL) {
                map = MemDesc->map();
                MemDesc->release();
                if(map != NULL) {
                    vAddr = (void *) map->getVirtualAddress();
                    DbgPrint("Memory mapped at bus address %d, virtual address %#x,"
                             " length %d\n", (UInt32)map->getPhysicalAddress(),
                             (UInt32)map->getVirtualAddress(),
                             (UInt32)map->getLength());
                }
                else {
                    IOPrint("Can't map controller PCI space\n");
                    return false;
                }
            }
        break;
#endif
        default:
            IOPrint("Can't find out mapping scheme\n");
            return false;
    }
    OSBoolean *sPreferMSI = conf ? OSDynamicCast(OSBoolean, conf->getObject("PreferMSI")) : NULL;
    bool PreferMSI = true;
    if (sPreferMSI) PreferMSI = sPreferMSI->isTrue();
    if(!PCIHelperP->CreateDeviceInterrupt(this, provider, PreferMSI, &SASMegaRAID::interruptHandler,
                                          &SASMegaRAID::interruptFilter))
        return false;
    
    if(!Attach()) {
        IOPrint("Can't attach device\n");
        return false;
    }

    return true;
}

//void SASMegaRAID::stop(IOService *provider)
void SASMegaRAID::TerminateController(void)
{
    DbgPrint("super->TerminateController\n");
    
    if (InterruptsActivated) {
        /* XXX: Doesn't work at least on ARM */
        mraid_intr_disable();
        InterruptsActivated = false;
    }
    if (fInterruptSrc) {
        if (MyWorkLoop)
            MyWorkLoop->removeEventSource(fInterruptSrc);
        if (fInterruptSrc) fInterruptSrc->release();
    }
    MRAID_Shutdown();
    if (fPCIDevice) {
        fPCIDevice->close(this);
        fPCIDevice->release();
    }
    PMstop();
    
    //BaseClass::stop(provider);
}

void SASMegaRAID::free(void)
{
    mraid_ccbCommand *command;
    
    DbgPrint("IOService->free\n");
    
    if(map) map->release();
    if (ccb_inited) {
        IODelete(sc.sc_ccb, addr64_t, sc.sc_max_cmds);
        for (int i = 0; i < sc.sc_max_cmds; i++)
        {
            if ((command = (mraid_ccbCommand *) ccbCommandPool->getCommand(false)))
                command->release();
        }
    }
    if (ccbCommandPool) ccbCommandPool->release();
    
    /* Helper Library is not inherited from OSObject */
    /*PCIHelperP->release();*/
    delete PCIHelperP;
    if (sc.sc_iop) IODelete(sc.sc_iop, mraid_iop_ops, 1);
    if (sc.sc_ccb_spin) {
        /*IOSimpleLockUnlock(sc.sc_ccb_spin);*/ IOSimpleLockFree(sc.sc_ccb_spin);
    }
    if (sc.sc_lock) {
        /*IORWLockUnlock(sc.sc_lock);*/ IORWLockFree(sc.sc_lock);
    }
    
    if (sc.sc_pcq) FreeMem(sc.sc_pcq);
    if (sc.sc_frames) FreeMem(sc.sc_frames);
    if (sc.sc_sense) FreeMem(sc.sc_sense);
    
    BaseClass::free();
}

/* */

IOInterruptEventSource *SASMegaRAID::CreateDeviceInterrupt(IOInterruptEventSource::Action action,
                                                           IOFilterInterruptEventSource::Filter filter, IOService *provider)
{
    /* Tell superclass that interrupts are our sole proprietary */
    return NULL;
}

bool SASMegaRAID::interruptFilter(OSObject *owner, IOFilterInterruptEventSource *sender)
{
    /* Check by which device the interrupt line is occupied (mine or other) */
    return mraid_my_intr();
}
void SASMegaRAID::interruptHandler(OSObject *owner, void *src, IOService *nub, int count)
{
    mraid_prod_cons *pcq = (mraid_prod_cons *) MRAID_KVA(sc.sc_pcq);
    mraid_ccbCommand *ccb;
    UInt32 Producer, Consumer, Context;
    
    DbgPrint("%s: pcq vaddr %p\n", __FUNCTION__, pcq);

    //sc.sc_pcq->cmd->synchronize(kIODirectionInOut);
    
    Producer = pcq->mpc_producer;
    Consumer = pcq->mpc_consumer;
    
    while (Consumer != Producer) {
        DbgPrint("pi: %#x ci: %#x\n", Producer, Consumer);

        Context = pcq->mpc_reply_q[Consumer];
        pcq->mpc_reply_q[Consumer] = MRAID_INVALID_CTX;
        if(Context == MRAID_INVALID_CTX)
            IOPrint("Invalid context, Prod: %d Cons: %d\n", Producer, Consumer);
        else {
            /* TO-DO: Remove from queue */
            ccb = (mraid_ccbCommand *) sc.sc_ccb[Context];
            DbgPrint("ccb: %#x\n", Context);
#if segmem
            if (ccb->s.ccb_sglmem.len > 0)
                ccb->s.ccb_sglmem.cmd->synchronize((ccb->s.ccb_direction & MRAID_DATA_IN) ?
                                                   kIODirectionIn : kIODirectionOut);
#endif
            ccb->s.ccb_done(ccb);
        }
        Consumer++;
        if(Consumer == (sc.sc_max_cmds + 1))
            Consumer = 0;
    }
    
    pcq->mpc_consumer = htole32(Consumer);
    
    //sc.sc_pcq->cmd->synchronize(kIODirectionInOut);

    return;
}

/* */

const mraid_pci_device* SASMegaRAID::MatchDevice()
{
	using mraid_structs::mraid_pci_devices;
    
	const mraid_pci_device *mpd;
	UInt16 VendorId, DeviceId;
	
	VendorId = fPCIDevice->configRead16(kIOPCIConfigVendorID);
	DeviceId = fPCIDevice->configRead16(kIOPCIConfigDeviceID);
	
	for (int i = 0; i < nitems(mraid_pci_devices); i++) {
		mpd = &mraid_pci_devices[i];
		
		if (mpd->mpd_vendor == VendorId &&
			mpd->mpd_product == DeviceId)
            return mpd;
	}
    
	return NULL;
}

bool SASMegaRAID::Probe()
{
    bool uiop = (mpd->mpd_iop == MRAID_IOP_XSCALE || mpd->mpd_iop == MRAID_IOP_PPC || mpd->mpd_iop == MRAID_IOP_GEN2
                 || mpd->mpd_iop == MRAID_IOP_SKINNY);
    if(sc.sc_iop->is_set() && !uiop) {
        IOPrint("%s: Unknown IOP %d. The driver will unload\n", __FUNCTION__, mpd->mpd_iop);
        return false;
    }
    
    return true;
}

void mraid_empty_done(mraid_ccbCommand *)
{
    /* ;) */
    __asm__ /*volatile*/("nop");
}

bool SASMegaRAID::Attach()
{
    UInt32 status, max_sgl, frames;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    sc.sc_iop->init(mpd->mpd_iop);
    if (!this->Probe())
        return false;
    
    /* Set firmware to working state */
    if(!Transition_Firmware())
        return false;
    
    if(!(ccbCommandPool = IOCommandPool::withWorkLoop(MyWorkLoop))) {
        IOPrint("Can't init command pool\n");
        return false;
    }
    
    sc.sc_ccb_spin = IOSimpleLockAlloc(); sc.sc_lock = IORWLockAlloc();
    
    /* Get constraints forming frames pool contiguous memory */
    status = mraid_fw_state();
    sc.sc_max_cmds = status & MRAID_STATE_MAXCMD_MASK;
    max_sgl = (status & MRAID_STATE_MAXSGL_MASK) >> 16;
    /* FW can accept 64-bit SGLs */
    if(IOPhysSize == 64) {
        sc.sc_max_sgl = min(max_sgl, (128 * 1024) / PAGE_SIZE + 1);
        sc.sc_sgl_size = sizeof(mraid_sg64);
        sc.sc_sgl_flags = MRAID_FRAME_SGL64;
    } else {
        sc.sc_max_sgl = max_sgl;
        sc.sc_sgl_size = sizeof(mraid_sg32);
        sc.sc_sgl_flags = MRAID_FRAME_SGL32;
    }
    IOPrint("DMA: %d-bit, max commands: %u, max SGL count: %u\n", IOPhysSize, sc.sc_max_cmds,
              sc.sc_max_sgl);
    
    /* Allocate united mem for reply queue & producer-consumer */
    if (!(sc.sc_pcq = AllocMem(sizeof(UInt32) /* Context size */
                               * sc.sc_max_cmds +
                               /* FW = producer of completed cmds
                                  driver = consumer */
                               sizeof(mraid_prod_cons)))) {
        IOPrint("Unable to allocate reply queue memory\n");
        return false;
    }

    /* Command frames memory */
    frames = (sc.sc_sgl_size * sc.sc_max_sgl + MRAID_FRAME_SIZE - 1) / MRAID_FRAME_SIZE;
    /* Extra frame for MRAID_CMD */
    frames++;
    sc.sc_frames_size = frames * MRAID_FRAME_SIZE;
    if (!(sc.sc_frames = AllocMem(sc.sc_frames_size * sc.sc_max_cmds))) {
        IOPrint("Unable to allocate frame memory\n");
        return false;
    }
    /* Rework: handle this case */
    if (MRAID_DVA(sc.sc_frames) & 0x3f) {
        IOPrint("Wrong frame alignment. Addr %#llx\n", MRAID_DVA(sc.sc_frames));
        return false;
    }
    
    /* Frame sense memory */
    if (!(sc.sc_sense = AllocMem(sc.sc_max_cmds * MRAID_SENSE_SIZE))) {
        IOPrint("Unable to allocate sense memory\n");
        return false;
    }
    
    /* Init pool of commands */
    Initccb();

    /* Init firmware with all pointers */
    if (!Initialize_Firmware()) {
        IOPrint("Unable to init firmware\n");
        return false;
    }
    FirmwareInitialized = true;
    
    if (!GetInfo()) {
        IOPrint("Unable to get controller info\n");
        return false;
    }
    ExportInfo();
    
    if (sc.sc_info.mci_hw_present & MRAID_INFO_HW_BBU) {
        mraid_bbu_status bbu_stat;
        /* Retrieve battery info */
        int mraid_bbu_status = GetBBUInfo(&bbu_stat);
        IOPrint("BBU type: ");
		switch (bbu_stat.battery_type) {
            case MRAID_BBU_TYPE_BBU:
                IOLog("BBU");
                break;
            case MRAID_BBU_TYPE_IBBU:
                IOLog("IBBU");
                break;
            default:
                IOLog("unknown type %d", bbu_stat.battery_type);
		}
        IOLog(", status ");
		switch(mraid_bbu_status) {
            case MRAID_BBU_GOOD:
                IOLog("good");
                sc.sc_bbuok = true;
                break;
            case MRAID_BBU_BAD:
                IOLog("bad");
                break;
            case MRAID_BBU_UNKNOWN:
                IOLog("unknown");
                break;
		}
    } else
        IOPrint("BBU not present");
    IOLog("\n");
    
    sc.sc_ld_cnt = sc.sc_info.mci_lds_present;
    for (int i = 0; i < sc.sc_ld_cnt; i++) {
        sc.sc_ld_present[i] = true;
    }
    
    mraid_intr_enable();
    /* XXX: Is it possible to get intrs enabled info from controller? */
    InterruptsActivated = true;
    /* Rework: InitializePowerManagement() */
    PMinit();
    getProvider()->joinPMtree(this);
    registerPowerDriver(this, PowerStates, 1);
    
#if test
    /* Ensure that interrupts work */
    memset(&sc.sc_info, 0, sizeof(sc.sc_info));
    if (!GetInfo()) {
        IOPrint("Unable to get controller info\n");
        return false;
    }
#endif
    
    return true;
}

mraid_mem *SASMegaRAID::AllocMem(vm_size_t size)
{
#if segmem
    IOReturn st = kIOReturnSuccess;
    UInt64 offset = 0;
    UInt32 numSeg;
#endif
    IOByteCount length;
    
    mraid_mem *mm;
    
    if (!(mm = IONew(mraid_mem, 1)))
        return NULL;
    
    /* Below 4gb for fast access */
    if (!(mm->bmd = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIOMemoryPhysicallyContiguous, size, PAGE_SIZE))) {
        goto free;
    }
#ifndef segmem
    mm->bmd->prepare();
    mm->vaddr = (IOVirtualAddress) mm->bmd->getBytesNoCopy();
    mm->paddr = mm->bmd->getPhysicalSegment(0, &length);
    /* Zero the whole region for easy */
    memset((void *) mm->vaddr, 0, size);
    
    return mm;
#else
    if (!(mm->cmd = IODMACommand::withSpecification(kIODMACommandOutputHost32, 32, 0, IODMACommand::kMapped, size, PAGE_SIZE)))
        goto bmd_free;
    
    if (mm->cmd->setMemoryDescriptor(mm->bmd /*, autoPrepare = true*/) != kIOReturnSuccess)
        goto cmd_free;
    
    while ((st == kIOReturnSuccess) && (offset < mm->bmd->getLength()))
    {
        numSeg = 1;
        
        st = mm->cmd->gen32IOVMSegments(&offset, &mm->segment, &numSeg);
        DbgPrint("gen32IOVMSegments(err = %d) paddr %#x, len %d, nsegs %d\n",
              st, mm->segment.fIOVMAddr, mm->segment.fLength, numSeg);
    }
    if (st == kIOReturnSuccess) {
        mm->map = mm->bmd->map();
        memset((void *) mm->map->getVirtualAddress(), 0, size);
        return mm;
    }
    
cmd_free:
    mm->cmd->clearMemoryDescriptor(/*autoComplete = true*/);
    mm->cmd->release();
bmd_free:
    mm->bmd->release();
#endif
    
free:
    IODelete(mm, mraid_mem, 1);
    
    return NULL;
}
void SASMegaRAID::FreeMem(mraid_mem *mm)
{
#if segmem
    mm->map->release();
    mm->cmd->clearMemoryDescriptor(/*autoComplete = true*/);
    mm->cmd->release();
#endif
    mm->bmd->complete();
    mm->bmd->release();
    IODelete(mm, mraid_mem, 1);
    mm = NULL;
}

#if segmem
bool SASMegaRAID::GenerateSegments(mraid_ccbCommand *ccb)
{
    IOReturn st = kIOReturnSuccess;
    UInt64 offset = 0;

    if (!(ccb->s.ccb_sglmem.cmd = IODMACommand::withSpecification(IOPhysSize == 64 ?
                                                                      kIODMACommandOutputHost64:
                                                                      kIODMACommandOutputHost32,
                                                                      IOPhysSize, MAXPHYS,
                                                                      IODMACommand::kMapped, MAXPHYS)))
        return false;
    
    /* TO-DO: Set kIOMemoryMapperNone or ~kIOMemoryMapperNone */
    if (ccb->s.ccb_sglmem.cmd->setMemoryDescriptor(ccb->s.ccb_sglmem.bmd, /* autoPrepare */ false) != kIOReturnSuccess)
        return false;
    ccb->s.ccb_sglmem.cmd->prepare(0, 0, false, /*synchronize*/ false);
    
    ccb->s.ccb_sglmem.numSeg = sc.sc_max_sgl;
    while ((st == kIOReturnSuccess) && (offset < ccb->s.ccb_sglmem.bmd->getLength()))
    {
        /* TO-DO: gen64IOVMSegments */
        ccb->s.ccb_sglmem.segments = IONew(IODMACommand::Segment32, sc.sc_max_sgl);
        
        st = ccb->s.ccb_sglmem.cmd->gen32IOVMSegments(&offset, &ccb->s.ccb_sglmem.segments[0], &ccb->s.ccb_sglmem.numSeg);
        if (ccb->s.ccb_sglmem.numSeg > 1)
            return false;
    }
    if (st != kIOReturnSuccess)
        return false;
    
    DbgPrint("gen32IOVMSegments: nseg %d, perseg %d, totlen %lld\n", ccb->s.ccb_sglmem.numSeg,
                ccb->s.ccb_sglmem.segments[0].fLength, ccb->s.ccb_sglmem.bmd->getLength());
    return true;
}
#endif

void SASMegaRAID::Initccb()
{
    mraid_ccbCommand *ccb;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    sc.sc_ccb = IONew(addr64_t, sc.sc_max_cmds);
    
    /* Reverse init since IOCommandPool is LIFO */
    for (int i = sc.sc_max_cmds - 1; i >= 0; i--) {
        ccb = mraid_ccbCommand::NewCommand();
        sc.sc_ccb[i] = (addr64_t) ccb;

        /* Pick i'th frame & i'th sense */
        
        ccb->s.ccb_frame = (mraid_frame *) ((char *) MRAID_KVA(sc.sc_frames) + sc.sc_frames_size * i);
        ccb->s.ccb_pframe = MRAID_DVA(sc.sc_frames) + sc.sc_frames_size * i;
        //ccb->s.ccb_pframe_offset = sc.sc_frames_size * i;
        ccb->s.ccb_frame->mrr_header.mrh_context = i;
        
        ccb->s.ccb_sense = (mraid_sense *) ((char *) MRAID_KVA(sc.sc_sense) + MRAID_SENSE_SIZE * i);
        ccb->s.ccb_psense = MRAID_DVA(sc.sc_sense) + MRAID_SENSE_SIZE * i;

        /*DbgPrint(MRAID_D_CCB, "ccb(%d) frame: %p (%lx) sense: %p (%lx)\n",
                  cb->s.ccb_frame->mrr_header.mrh_context, ccb->ccb_frame, ccb->ccb_pframe, ccb->ccb_sense, ccb->ccb_psense);*/
        
        Putccb(ccb);
    }
    
    ccb_inited = true;
}
void SASMegaRAID::Putccb(mraid_ccbCommand *ccb)
{
    ccb->scrubCommand();
    
    IOSimpleLockLock(sc.sc_ccb_spin);
    ccbCommandPool->returnCommand(ccb);
    IOSimpleLockUnlock(sc.sc_ccb_spin);
}
mraid_ccbCommand *SASMegaRAID::Getccb()
{
    mraid_ccbCommand *ccb;
    
    IOSimpleLockLock(sc.sc_ccb_spin);
    ccb = (mraid_ccbCommand *) ccbCommandPool->getCommand(true);
    IOSimpleLockUnlock(sc.sc_ccb_spin);
    
    return ccb;
}

bool SASMegaRAID::Transition_Firmware()
{
    UInt32 fw_state, cur_state;
    int max_wait;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    fw_state = mraid_fw_state() & MRAID_STATE_MASK;
    
    DbgPrint("Firmware state %#x\n", fw_state);
    
    while(fw_state != MRAID_STATE_READY) {
        DbgPrint("Waiting for firmware to become ready\n");
        cur_state = fw_state;
        switch(fw_state) {
            case MRAID_STATE_FAULT:
                IOPrint("Firmware fault\n");
                return false;
            case MRAID_STATE_WAIT_HANDSHAKE:
                MRAID_Write(MRAID_IDB, MRAID_INIT_CLEAR_HANDSHAKE);
                max_wait = 2;
                break;
            case MRAID_STATE_OPERATIONAL:
                MRAID_Write(MRAID_IDB, MRAID_INIT_READY);
                max_wait = 10;
                break;
            case MRAID_STATE_UNDEFINED:
            case MRAID_STATE_BB_INIT:
                max_wait = 2;
                break;
            case MRAID_STATE_FW_INIT:
            case MRAID_STATE_DEVICE_SCAN:
            case MRAID_STATE_FLUSH_CACHE:
                max_wait = 20;
                break;
            default:
                IOPrint("Unknown firmware state\n");
                return false;
        }
        for(int i = 0; i < (max_wait * 10); i++) {
            fw_state = mraid_fw_state() & MRAID_STATE_MASK;
            if(fw_state == cur_state)
                IOSleep(100);
            else break;
        }
        if(fw_state == cur_state) {
            IOPrint("Firmware stuck in state: %#x\n", fw_state);
            return false;
        }
    }
    
    return true;
}

bool SASMegaRAID::Initialize_Firmware()
{
    bool res = true;
    mraid_ccbCommand* ccb;
    mraid_init_frame *init;
    mraid_init_qinfo *qinfo;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    ccb = Getccb();
    
    init = &ccb->s.ccb_frame->mrr_init;
    /* Skip header */
    qinfo = (mraid_init_qinfo *)((UInt8 *) init + MRAID_FRAME_SIZE);
    
    memset(qinfo, 0, sizeof(mraid_init_qinfo));
    qinfo->miq_rq_entries = sc.sc_max_cmds + 1;
    
    qinfo->miq_rq_addr = htole64(MRAID_DVA(sc.sc_pcq) + offsetof(mraid_prod_cons, mpc_reply_q));
	qinfo->miq_pi_addr = htole64(MRAID_DVA(sc.sc_pcq) + offsetof(mraid_prod_cons, mpc_producer));
	qinfo->miq_ci_addr = htole64(MRAID_DVA(sc.sc_pcq) + offsetof(mraid_prod_cons, mpc_consumer));
    
    init->mif_header.mrh_cmd = MRAID_CMD_INIT;
    init->mif_header.mrh_data_len = sizeof(mraid_init_qinfo);
    init->mif_qinfo_new_addr = htole64(ccb->s.ccb_pframe + MRAID_FRAME_SIZE);
    
    /*DbgPrint("Entries: %#x, rq: %#llx, pi: %#llx, ci: %#llx\n", qinfo->miq_rq_entries,
             qinfo->miq_rq_addr, qinfo->miq_pi_addr, qinfo->miq_ci_addr);*/
    
    //sc.sc_pcq->cmd->synchronize(kIODirectionInOut);
    
    ccb->s.ccb_done = mraid_empty_done;
    MRAID_Poll(ccb);
    if (init->mif_header.mrh_cmd_status != MRAID_STAT_OK)
        res = false;
    
    Putccb(ccb);
    
    return res;
}

bool SASMegaRAID::GetInfo()
{
    DbgPrint("%s\n", __FUNCTION__);
    
    if (!Management(MRAID_DCMD_CTRL_GET_INFO, MRAID_DATA_IN, sizeof(sc.sc_info), &sc.sc_info, NULL))
        return false;

#if defined(DEBUG)
    int i;
	for (i = 0; i < sc.sc_info.mci_image_component_count; i++) {
		IOPrint("Active FW %s Version %s date %s time %s\n",
               sc.sc_info.mci_image_component[i].mic_name,
               sc.sc_info.mci_image_component[i].mic_version,
               sc.sc_info.mci_image_component[i].mic_build_date,
               sc.sc_info.mci_image_component[i].mic_build_time);
	}
    for (i = 0; i < sc.sc_info.mci_pending_image_component_count; i++) {
		IOPrint("Pending FW %s Version %s date %s time %s\n",
               sc.sc_info.mci_pending_image_component[i].mic_name,
               sc.sc_info.mci_pending_image_component[i].mic_version,
               sc.sc_info.mci_pending_image_component[i].mic_build_date,
               sc.sc_info.mci_pending_image_component[i].mic_build_time);
	}
    IOPrint("max_arms %d max_spans %d max_arrs %d max_lds %d\n",
           sc.sc_info.mci_max_arms,
           sc.sc_info.mci_max_spans,
           sc.sc_info.mci_max_arrays,
           sc.sc_info.mci_max_lds);
    IOPrint("Serial %s present %#x fw time %d max_cmds %d max_sg %d\n",
           sc.sc_info.mci_serial_number,
           sc.sc_info.mci_hw_present,
           sc.sc_info.mci_current_fw_time,
           sc.sc_info.mci_max_cmds,
           sc.sc_info.mci_max_sg_elements);
    IOPrint("max_rq %d lds_deg %d lds_off %d pd_pres %d\n",
           sc.sc_info.mci_max_request_size,
           sc.sc_info.mci_lds_degraded,
           sc.sc_info.mci_lds_offline,
           sc.sc_info.mci_pd_present);
	IOPrint("pd_dsk_prs %d pd_dsk_pred_fail %d pd_dsk_fail %d\n",
           sc.sc_info.mci_pd_disks_present,
           sc.sc_info.mci_pd_disks_pred_failure,
           sc.sc_info.mci_pd_disks_failed);
    IOPrint("nvram %d flash %d\n",
           sc.sc_info.mci_nvram_size,
           sc.sc_info.mci_flash_size);
	IOPrint("ram_cor %d ram_uncor %d clus_all %d clus_act %d\n",
           sc.sc_info.mci_ram_correctable_errors,
           sc.sc_info.mci_ram_uncorrectable_errors,
           sc.sc_info.mci_cluster_allowed,
           sc.sc_info.mci_cluster_active);
	IOPrint("max_strps_io %d raid_lvl %#x adapt_ops %#x ld_ops %#x\n",
           sc.sc_info.mci_max_strips_per_io,
           sc.sc_info.mci_raid_levels,
           sc.sc_info.mci_adapter_ops,
           sc.sc_info.mci_ld_ops);
	IOPrint("strp_sz_min %d strp_sz_max %d pd_ops %#x pd_mix %#x\n",
           sc.sc_info.mci_stripe_sz_ops.min,
           sc.sc_info.mci_stripe_sz_ops.max,
           sc.sc_info.mci_pd_ops,
           sc.sc_info.mci_pd_mix_support);
	IOPrint("ecc_bucket %d\n",
           sc.sc_info.mci_ecc_bucket_count);
	IOPrint("sq_nm %d prd_fail_poll %d intr_thrtl %d intr_thrtl_to %d\n",
           sc.sc_info.mci_properties.mcp_seq_num,
           sc.sc_info.mci_properties.mcp_pred_fail_poll_interval,
           sc.sc_info.mci_properties.mcp_intr_throttle_cnt,
           sc.sc_info.mci_properties.mcp_intr_throttle_timeout);
	IOPrint("rbld_rate %d patr_rd_rate %d bgi_rate %d cc_rate %d\n",
           sc.sc_info.mci_properties.mcp_rebuild_rate,
           sc.sc_info.mci_properties.mcp_patrol_read_rate,
           sc.sc_info.mci_properties.mcp_bgi_rate,
           sc.sc_info.mci_properties.mcp_cc_rate);
	IOPrint("rc_rate %d ch_flsh %d spin_cnt %d spin_dly %d clus_en %d\n",
           sc.sc_info.mci_properties.mcp_recon_rate,
           sc.sc_info.mci_properties.mcp_cache_flush_interval,
           sc.sc_info.mci_properties.mcp_spinup_drv_cnt,
           sc.sc_info.mci_properties.mcp_spinup_delay,
           sc.sc_info.mci_properties.mcp_cluster_enable);
	IOPrint("coerc %d alarm %d dis_auto_rbld %d dis_bat_wrn %d ecc %d\n",
           sc.sc_info.mci_properties.mcp_coercion_mode,
           sc.sc_info.mci_properties.mcp_alarm_enable,
           sc.sc_info.mci_properties.mcp_disable_auto_rebuild,
           sc.sc_info.mci_properties.mcp_disable_battery_warn,
           sc.sc_info.mci_properties.mcp_ecc_bucket_size);
	IOPrint("ecc_leak %d rest_hs %d exp_encl_dev %d\n",
           sc.sc_info.mci_properties.mcp_ecc_bucket_leak_rate,
           sc.sc_info.mci_properties.mcp_restore_hotspare_on_insertion,
           sc.sc_info.mci_properties.mcp_expose_encl_devices);
	IOPrint("Vendor %#x device %#x subvendor %#x subdevice %#x\n",
           sc.sc_info.mci_pci.mip_vendor,
           sc.sc_info.mci_pci.mip_device,
           sc.sc_info.mci_pci.mip_subvendor,
           sc.sc_info.mci_pci.mip_subdevice);
	IOPrint("Type %#x port_count %d port_addr ",
           sc.sc_info.mci_host.mih_type,
           sc.sc_info.mci_host.mih_port_count);
	for (i = 0; i < 8; i++)
		IOLog("%.0llx ", sc.sc_info.mci_host.mih_port_addr[i]);
	IOLog("\n");
	IOPrint("Type %.x port_count %d port_addr ",
           sc.sc_info.mci_device.mid_type,
           sc.sc_info.mci_device.mid_port_count);
	for (i = 0; i < 8; i++)
		IOLog("%.0llx ", sc.sc_info.mci_device.mid_port_addr[i]);
	IOLog("\n");
#endif
    
    return true;
}

void SASMegaRAID::ExportInfo()
{
    OSString *string = NULL;
    char str[33];
    
    if ((string = OSString::withCString(sc.sc_info.mci_product_name)))
	{
		SetHBAProperty(kIOPropertyProductNameKey, string);
		string->release();
		string = NULL;
	}
    snprintf(str, sizeof(str), "Firmware %s", sc.sc_info.mci_package_version);
    if ((string = OSString::withCString(str)))
	{
		SetHBAProperty(kIOPropertyProductRevisionLevelKey, string);
		string->release();
		string = NULL;
	}
    if (sc.sc_info.mci_memory_size > 0) {
        snprintf(str, sizeof(str), "Cache %dMB RAM", sc.sc_info.mci_memory_size);
        if ((string = OSString::withCString(str))) {
            SetHBAProperty(kIOPropertyPortDescriptionKey, string);
            string->release();
            string = NULL;
        }
    }
}

int SASMegaRAID::GetBBUInfo(mraid_bbu_status *info)
{
    DbgPrint("%s\n", __FUNCTION__);
    
    if (!Management(MRAID_DCMD_BBU_GET_INFO, MRAID_DATA_IN, sizeof(*info), info, NULL))
        return MRAID_BBU_UNKNOWN;
    
#if defined(DEBUG)
	IOPrint("BBU voltage %d, current %d, temperature %d, "
           "status 0x%x\n", info->voltage, info->current,
           info->temperature, info->fw_status);
	IOPrint("Details: ");
	switch(info->battery_type) {
        case MRAID_BBU_TYPE_IBBU:
            IOLog("guage %d relative charge %d charger state %d "
                   "charger ctrl %d\n", info->detail.ibbu.gas_guage_status,
                   info->detail.ibbu.relative_charge ,
                   info->detail.ibbu.charger_system_state ,
                   info->detail.ibbu.charger_system_ctrl);
            IOLog("\tcurrent %d abs charge %d max error %d\n",
                   info->detail.ibbu.charging_current ,
                   info->detail.ibbu.absolute_charge ,
                   info->detail.ibbu.max_error);
            break;
        case MRAID_BBU_TYPE_BBU:
            IOLog("guage %d relative charge %d charger state %d\n",
                   info->detail.ibbu.gas_guage_status,
                   info->detail.bbu.relative_charge ,
                   info->detail.bbu.charger_status );
            IOLog("\trem capacity %d full capacity %d\n",
                   info->detail.bbu.remaining_capacity,
                   info->detail.bbu.full_charge_capacity);
            break;
        default:
            IOPrint("\n");
	}
#endif
	switch(info->battery_type) {
        case MRAID_BBU_TYPE_BBU:
            return (info->detail.bbu.is_SOH_good ?
                    MRAID_BBU_GOOD : MRAID_BBU_BAD);
        case MRAID_BBU_TYPE_NONE:
            return MRAID_BBU_UNKNOWN;
        default:
            if (info->fw_status &
                (MRAID_BBU_STATE_PACK_MISSING |
                 MRAID_BBU_STATE_VOLTAGE_LOW |
                 MRAID_BBU_STATE_TEMPERATURE_HIGH |
                 MRAID_BBU_STATE_LEARN_CYC_FAIL |
                 MRAID_BBU_STATE_LEARN_CYC_TIMEOUT |
                 MRAID_BBU_STATE_I2C_ERR_DETECT))
                return MRAID_BBU_BAD;
            return MRAID_BBU_GOOD;
	}
}

bool SASMegaRAID::Management(UInt32 opc, UInt32 dir, UInt32 len, void *buf, UInt8 *mbox)
{
    mraid_ccbCommand* ccb;
    bool res;
    
    ccb = Getccb();    
    res = Do_Management(ccb, opc, dir, len, buf, mbox);
    Putccb(ccb);
    
    return res;
}
bool SASMegaRAID::Do_Management(mraid_ccbCommand *ccb, UInt32 opc, UInt32 dir, UInt32 len, void *buf, UInt8 *mbox)
{
    mraid_dcmd_frame *dcmd;
    IOVirtualAddress addr;
    
    DbgPrint("%s: ccb_num: %d, opcode: %#x\n", __FUNCTION__, ccb->s.ccb_frame->mrr_header.mrh_context, opc);
        
    dcmd = &ccb->s.ccb_frame->mrr_dcmd;
    memset(dcmd->mdf_mbox, 0, MRAID_MBOX_SIZE);
    dcmd->mdf_header.mrh_cmd = MRAID_CMD_DCMD;
    dcmd->mdf_header.mrh_timeout = 0;
    
    dcmd->mdf_opcode = opc;
    dcmd->mdf_header.mrh_data_len = 0;
    ccb->s.ccb_direction = dir;
    
    ccb->s.ccb_frame_size = MRAID_DCMD_FRAME_SIZE;
    
    /* Additional opcodes */
    if (mbox)
        memcpy(dcmd->mdf_mbox, mbox, MRAID_MBOX_SIZE);
    
    if (dir != MRAID_DATA_NONE) {
        /* Support 64-bit DMA */
        if (!(ccb->s.ccb_sglmem.bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
            kIOMemoryPhysicallyContiguous | kIOMapInhibitCache /* Disable caching */
            ,len, IOPhysSize == 64 ? 0xFFFFFFFFFFFFFFFFULL : 0x00000000FFFFF000ULL)))
            return false;
        
#ifdef segmem
        ccb->s.ccb_sglmem.map = ccb->s.ccb_sglmem.bmd->map();
        addr = ccb->s.ccb_sglmem.map->getVirtualAddress();
#else
        ccb->s.ccb_sglmem.bmd->prepare();
        addr = (IOVirtualAddress) ccb->s.ccb_sglmem.bmd->getBytesNoCopy();
#endif
        
        if (dir == MRAID_DATA_OUT)
            bcopy(buf, (void *) addr, len);
        dcmd->mdf_header.mrh_data_len = len;
        
        ccb->s.ccb_sglmem.len = len;
        
        ccb->s.ccb_sgl = &dcmd->mdf_sgl;
        
        if (!CreateSGL(ccb))
            goto fail;
    }

    if (!InterruptsActivated) {
        ccb->s.ccb_done = mraid_empty_done;
        MRAID_Poll(ccb);
    } else
        MRAID_Exec(ccb);

    if (dcmd->mdf_header.mrh_cmd_status != MRAID_STAT_OK)
        goto fail;

    if (ccb->s.ccb_direction == MRAID_DATA_IN)
        bcopy((void *) addr, buf, len);
    
    FreeSGL(&ccb->s.ccb_sglmem);
    return true;
fail:
    FreeSGL(&ccb->s.ccb_sglmem);
    return false;
}

bool SASMegaRAID::CreateSGL(mraid_ccbCommand *ccb)
{
    mraid_frame_header *hdr = &ccb->s.ccb_frame->mrr_header;
    mraid_sgl *sgl;
    //IODMACommand::Segment32 *sgd;
    IOByteCount length;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    ccb->s.ccb_sglmem.paddr = ccb->s.ccb_sglmem.bmd->getPhysicalSegment(0, &length);
#if segmem
    if (!GenerateSegments(ccb)) {
        IOPrint("Unable to generate segments\n");
        return false;
    }
#endif
    
    sgl = ccb->s.ccb_sgl;
#if segmem
    sgd = ccb->s.ccb_sglmem.segments;
    for (int i = 0; i < ccb->s.ccb_sglmem.numSeg; i++) {
#endif
        if (IOPhysSize == 64) {
#ifndef segmem
            sgl->sg64[0].addr = htole64(ccb->s.ccb_sglmem.paddr);
            sgl->sg64[0].len = htole32(ccb->s.ccb_sglmem.len);
        } else {
            sgl->sg32[0].addr = (UInt32)(htole32(ccb->s.ccb_sglmem.paddr) & 0x00000000ffffffff);
            sgl->sg32[0].len = htole32(ccb->s.ccb_sglmem.len);
        }
        DbgPrint("Paddr: %#llx\n", ccb->s.ccb_sglmem.paddr);
#else
        sgl->sg64[0].addr = htole64(sgd[i].fIOVMAddr);
        sgl->sg64[0].len = htole32(sgd[i].fLength);
    } else {
        sgl->sg32[0].addr = htole32(sgd[i].fIOVMAddr);
        sgl->sg32[0].len = htole32(sgd[i].fLength);
    }
    }
#endif

    if (ccb->s.ccb_direction == MRAID_DATA_IN) {
        hdr->mrh_flags |= MRAID_FRAME_DIR_READ;
        //ccb->s.ccb_sglmem.cmd->synchronize(kIODirectionIn);
    } else {
        hdr->mrh_flags |= MRAID_FRAME_DIR_WRITE;
        //ccb->s.ccb_sglmem.cmd->synchronize(kIODirectionOut);
    }
    
    hdr->mrh_flags |= sc.sc_sgl_flags;
    hdr->mrh_sg_count = 1; //ccb->s.ccb_sglmem.numSeg;
    ccb->s.ccb_frame_size += sc.sc_sgl_size; //* ccb->s.ccb_sglmem.numSeg;
    ccb->s.ccb_extra_frames = (ccb->s.ccb_frame_size - 1) / MRAID_FRAME_SIZE;
    
    DbgPrint("frame_size: %d extra_frames: %d\n",
             ccb->s.ccb_frame_size, ccb->s.ccb_extra_frames);
    
    return true;
}

void SASMegaRAID::MRAID_Shutdown()
{
    UInt8 mbox[MRAID_MBOX_SIZE];
    
    DbgPrint("%s\n", __FUNCTION__);
    
    if (FirmwareInitialized) {
        mbox[0] = MRAID_FLUSH_CTRL_CACHE | MRAID_FLUSH_DISK_CACHE;
        if (!Management(MRAID_DCMD_CTRL_CACHE_FLUSH, MRAID_DATA_NONE, 0, NULL, mbox)) {
            DbgPrint("Warning: failed to flush cache\n");
            return;
        }
        mbox[0] = 0;
        if (!Management(MRAID_DCMD_CTRL_SHUTDOWN, MRAID_DATA_NONE, 0, NULL, mbox)) {
            DbgPrint("Warning: failed to shutdown firmware\n");
            return;
        }
        FirmwareInitialized = false;
    }
}

/* TO-DO: HandlePowerOn/Off() */
void SASMegaRAID::systemWillShutdown(IOOptionBits spec)
{
    DbgPrint("%s: spec = %#x\n", __FUNCTION__, spec);
    
    switch (spec) {
        case kIOMessageSystemWillRestart:
        case kIOMessageSystemWillPowerOff:
            MRAID_Shutdown();
        break;
    }
    
    BaseClass::systemWillShutdown(spec);
}

UInt32 SASMegaRAID::MRAID_Read(UInt8 offset)
{
    UInt32 data;
    /*if(MemDesc.readBytes(offset, &data, 4) != 4) {
     DbgPrint("%s[%p]::Read(): Unsuccessfull.\n", getName(), this);
     return(0);
     }*/
    data = OSReadLittleInt32(vAddr, offset);
#if defined (DEBUG)
    if (fMSIEnabled)
        IOPrint("%s: offset %#x data 0x%08x\n", __FUNCTION__, offset, data);
#endif
    
    return data;
}
/*bool*/
void SASMegaRAID::MRAID_Write(UInt8 offset, uint32_t data)
{
    DbgPrint("%s: offset %#x data 0x%08x\n", __FUNCTION__, offset, data);

    OSWriteLittleInt32(vAddr, offset, data);
    OSSynchronizeIO();
    
    /*if(MemDesc.writeBytes(offset, &data, 4) != 4) {
     DbgPrint("%s[%p]::Write(): Unsuccessfull.\n", getName(), this);
     return FALSE;
     }
     
     return true;*/
}

void SASMegaRAID::MRAID_Poll(mraid_ccbCommand *ccb)
{
    mraid_frame_header *hdr;
    int cycles = 0;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    hdr = &ccb->s.ccb_frame->mrr_header;
    hdr->mrh_cmd_status = 0xff;
    hdr->mrh_flags |= MRAID_FRAME_DONT_POST_IN_REPLY_QUEUE;
    
    mraid_post(ccb);
    
    while (1) {
        IOSleep(10);
        
        //sc.sc_frames->cmd->synchronize(kIODirectionInOut);
        
        if (hdr->mrh_cmd_status != 0xff)
            break;
        
        if (cycles++ > 500) {
            IOPrint("Poll timeout\n");
            break;
        }
        
        //sc.sc_frames->cmd->synchronize(kIODirectionInOut);
    }

#if segmem
    if (ccb->s.ccb_sglmem.len > 0)
        ccb->s.ccb_sglmem.cmd->synchronize((ccb->s.ccb_direction & MRAID_DATA_IN) ?
                                         kIODirectionIn : kIODirectionOut);
#endif
    
    ccb->s.ccb_done(ccb);
}

/* Interrupt driven */
void mraid_exec_done(mraid_ccbCommand *ccb)
{
    lock *ccb_lock;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    ccb_lock = (lock *) ccb->s.ccb_context;
    
    IOLockLock(ccb_lock->holder);
    ccb->s.ccb_context = NULL;
    ccb_lock->event = true;
    IOLockWakeup(ccb_lock->holder, &ccb_lock->event, true);
    IOLockUnlock(ccb_lock->holder);
}
void SASMegaRAID::MRAID_Exec(mraid_ccbCommand *ccb)
{
    AbsoluteTime deadline;
    int ret;
    lock ccb_lock;
    
    DbgPrint("%s\n", __FUNCTION__);
    
#if defined(DEBUG)
    if (ccb->s.ccb_context || ccb->s.ccb_done)
        IOPrint("Warning: event or done set\n");
#endif
    ccb_lock.holder = IOLockAlloc();
    ccb_lock.event = false;
    ccb->s.ccb_context = &ccb_lock;
    
    ccb->s.ccb_done = mraid_exec_done;
    mraid_post(ccb);
    
    clock_interval_to_deadline(1, kSecondScale, (UInt64 *) &deadline);
    
    IOLockLock(ccb_lock.holder);
    ret = IOLockSleepDeadline(ccb_lock.holder, &ccb_lock.event, deadline, THREAD_INTERRUPTIBLE);
    ccb_lock.event = false;
    IOLockUnlock(ccb_lock.holder);
    if (ret != THREAD_AWAKENED)
        DbgPrint("Warning: interrupt didn't come while expected\n");
    
    IOLockFree(ccb_lock.holder);
}
/* */

/* SCSI part */
void mraid_cmd_done(mraid_ccbCommand *ccb)
{
    SCSI_Sense_Data sense = { 0 };
    
    cmd_context *cmd;
    mraid_frame_header *hdr;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    cmd = (cmd_context *) ccb->s.ccb_context;
    ccb->s.ccb_context = NULL;
    hdr = &ccb->s.ccb_frame->mrr_header;
    
    switch (hdr->mrh_cmd_status) {
        case MRAID_STAT_OK:
            cmd->ts = kSCSITaskStatus_GOOD;
        break;
        case MRAID_STAT_SCSI_DONE_WITH_ERROR:
            cmd->ts = kSCSITaskStatus_CHECK_CONDITION;
            cmd->sense = &sense;
            memcpy(&sense, ccb->s.ccb_sense, sizeof(SCSI_Sense_Data));
        break;
        case MRAID_STAT_DEVICE_NOT_FOUND:
            cmd->ts = kSCSITaskStatus_BUSY;
        break;
        default:
            cmd->ts = kSCSITaskStatus_No_Status;
#if defined(DEBUG)
            IOPrint("kSCSITaskStatus_No_Status: %02x on %02x\n", hdr->mrh_cmd_status,
                    cmd->opcode);
#endif
            if (hdr->mrh_scsi_status) {
                DbgPrint("Sense %#x %p\n", hdr->mrh_scsi_status, ccb->s.ccb_sense);
                cmd->ts = kSCSITaskStatus_CHECK_CONDITION;
                cmd->sense = &sense;
                memcpy(&sense, ccb->s.ccb_sense, sizeof(SCSI_Sense_Data));
            }
        break;
    }
    
    DbgPrint("Task status 0x%x\n", cmd->ts);
    
    cmd->instance->CompleteTask(ccb, cmd);
    IODelete(cmd, cmd_context, 1);
}

void SASMegaRAID::CompleteTask(mraid_ccbCommand *ccb, cmd_context *cmd)
{
    if (ccb->s.ccb_direction != MRAID_DATA_NONE) {
    	if (cmd->ts == kSCSITaskStatus_GOOD) {
        	if (ccb->s.ccb_direction == MRAID_DATA_IN)
            		GetDataBuffer(cmd->pr)->writeBytes(cmd->instance->GetDataBufferOffset(cmd->pr),
                                   (void *) ccb->s.ccb_sglmem.bmd->getBytesNoCopy(),
                                    ccb->s.ccb_sglmem.len);
            SetRealizedDataTransferCount(cmd->pr, ccb->s.ccb_sglmem.len);
    	} else
    		SetRealizedDataTransferCount(cmd->pr, 0);
        
        FreeSGL(&ccb->s.ccb_sglmem);
    }
    
    Putccb(ccb);

    if (cmd->ts == kSCSITaskStatus_CHECK_CONDITION)
        SetAutoSenseData(cmd->pr, cmd->sense, sizeof(SCSI_Sense_Data));
    
    CompleteParallelTask(cmd->pr, cmd->ts, kSCSIServiceResponse_TASK_COMPLETE);
}

bool SASMegaRAID::LogicalDiskCmd(mraid_ccbCommand *ccb, SCSIParallelTaskIdentifier pr)
{
    SCSICommandDescriptorBlock cdbData = { 0 };
    IOMemoryDescriptor* transferMemDesc;
    IOByteCount transferLen;
    
    mraid_pass_frame *pf;
    cmd_context *cmd;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    pf = &ccb->s.ccb_frame->mrr_pass;
    pf->mpf_header.mrh_cmd = MRAID_CMD_LD_SCSI_IO;
    pf->mpf_header.mrh_target_id = GetTargetIdentifier(pr);
    pf->mpf_header.mrh_lun_id = 0;
    pf->mpf_header.mrh_cdb_len = GetCommandDescriptorBlockSize(pr);
    pf->mpf_header.mrh_timeout = 0;
    /* XXX */
    pf->mpf_header.mrh_data_len = (UInt32) GetRequestedDataTransferCount(pr);
    pf->mpf_header.mrh_sense_len = MRAID_SENSE_SIZE;
    
    pf->mpf_sense_addr = htole64(ccb->s.ccb_psense);
    
    memset(pf->mpf_cdb, 0, 16);
    GetCommandDescriptorBlock(pr, &cdbData);
	memcpy(pf->mpf_cdb, cdbData, pf->mpf_header.mrh_cdb_len);
    
    ccb->s.ccb_done = mraid_cmd_done;
    
    cmd = IONew(cmd_context, 1);
    cmd->instance = this;
    cmd->pr = pr;
#if defined(DEBUG)
    cmd->opcode = cdbData[0];
#endif
    ccb->s.ccb_context = cmd;
    
    ccb->s.ccb_frame_size = MRAID_PASS_FRAME_SIZE;
    ccb->s.ccb_sgl = &pf->mpf_sgl;
    
    switch (GetDataTransferDirection(pr)) {
        case kSCSIDataTransfer_FromTargetToInitiator:
            ccb->s.ccb_direction = MRAID_DATA_IN;
        break;
        case kSCSIDataTransfer_FromInitiatorToTarget:
            ccb->s.ccb_direction = MRAID_DATA_OUT;
        break;
        default:
            ccb->s.ccb_direction = MRAID_DATA_NONE;
        break;
    }

    if ((transferMemDesc = GetDataBuffer(pr))) {
        if (!(ccb->s.ccb_sglmem.bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
                                                    kernel_task,
                                                    kIOMemoryPhysicallyContiguous,
                                                    (transferLen = transferMemDesc->getLength()),
                                                    IOPhysSize == 64 ? 0xFFFFFFFFFFFFFFFFULL : 0x00000000FFFFF000ULL)))
            return false;
        ccb->s.ccb_sglmem.bmd->prepare();

        ccb->s.ccb_sglmem.len = (UInt32) transferLen;
        if (ccb->s.ccb_direction == MRAID_DATA_OUT)
            transferMemDesc->readBytes(GetDataBufferOffset(pr), (void *) ccb->s.ccb_sglmem.bmd->getBytesNoCopy(), transferLen);
        
        if (!CreateSGL(ccb))
            return false;
    }
    
    return true;
}

bool SASMegaRAID::IOCmd(mraid_ccbCommand *ccb, SCSIParallelTaskIdentifier pr, UInt32 lba, UInt16 len)
{
    SCSICommandDescriptorBlock cdbData = { 0 };
    IOMemoryDescriptor* transferMemDesc;
    IOByteCount transferLen;
    
    mraid_io_frame *io;
    cmd_context *cmd;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    if (!(transferMemDesc = GetDataBuffer(pr)))
        return false;
    
    io = &ccb->s.ccb_frame->mrr_io;
    switch (GetDataTransferDirection(pr)) {
        case kSCSIDataTransfer_FromTargetToInitiator:
            io->mif_header.mrh_cmd = MRAID_CMD_LD_READ;
            ccb->s.ccb_direction = MRAID_DATA_IN;
            break;
        case kSCSIDataTransfer_FromInitiatorToTarget:
            io->mif_header.mrh_cmd = MRAID_CMD_LD_WRITE;
            ccb->s.ccb_direction = MRAID_DATA_OUT;
            break;
    }
    io->mif_header.mrh_target_id = GetTargetIdentifier(pr);
    io->mif_header.mrh_timeout = 0;
    io->mif_header.mrh_flags = 0;
    io->mif_header.mrh_data_len = len;
    io->mif_header.mrh_sense_len = MRAID_SENSE_SIZE;
    io->mif_lba = htole64(lba);

    io->mif_sense_addr = htole64(ccb->s.ccb_psense);
    
    ccb->s.ccb_done = mraid_cmd_done;
    
    cmd = IONew(cmd_context, 1);
    cmd->instance = this;
    cmd->pr = pr;
#if defined(DEBUG)
    cmd->opcode = cdbData[0];
#endif
    ccb->s.ccb_context = cmd;
    
    ccb->s.ccb_frame_size = MRAID_PASS_FRAME_SIZE;
    ccb->s.ccb_sgl = &io->mif_sgl;
    
    if (!(ccb->s.ccb_sglmem.bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
                                                                                    kernel_task,
                                                                                    kIOMemoryPhysicallyContiguous,
                                                                                    (transferLen = transferMemDesc->getLength()),
                                                                                    IOPhysSize == 64 ? 0xFFFFFFFFFFFFFFFFULL : 0x00000000FFFFF000ULL)))
        return false;
    ccb->s.ccb_sglmem.bmd->prepare();
        
    ccb->s.ccb_sglmem.len = (UInt32) transferLen;
    if (ccb->s.ccb_direction == MRAID_DATA_OUT)
        transferMemDesc->readBytes(GetDataBufferOffset(pr), (void *) ccb->s.ccb_sglmem.bmd->getBytesNoCopy(), transferLen);
        
    if (!CreateSGL(ccb))
        return false;
    
    return true;
}

SCSIServiceResponse SASMegaRAID::ProcessParallelTask(SCSIParallelTaskIdentifier parallelRequest)
{
    SCSICommandDescriptorBlock cdbData = { 0 };
    
    mraid_ccbCommand *ccb;
    UInt8 mbox[MRAID_MBOX_SIZE];
    
    GetCommandDescriptorBlock(parallelRequest, &cdbData);
    
#if defined(DEBUG)
    SCSITargetIdentifier targetID = GetTargetIdentifier(parallelRequest);
    SCSILogicalUnitNumber logicalUnitNumber = GetLogicalUnitNumber(parallelRequest);
    IOPrint("%s: Opcode 0x%x, Target %d, LUN %d\n", __FUNCTION__, cdbData[0],
            int(targetID << 32), int(logicalUnitNumber << 32));
#endif
    
    /* TO-DO: We need batt status refreshing for this */
    /*if (cdbData[0] == kSCSICmd_SYNCHRONIZE_CACHE && sc.sc_bbuok)
     return kSCSIServiceResponse_TASK_COMPLETE;*/
    
    ccb = Getccb();
    
    switch (cdbData[0]) {
        case kSCSICmd_READ_12:
        case kSCSICmd_WRITE_12:
            if (!IOCmd(ccb, parallelRequest, OSReadBigInt32(cdbData, 2),
                       /* XXX: Check me */
                       OSReadBigInt16(cdbData, 9)))
                goto fail;
            break;
        case kSCSICmd_READ_10:
        case kSCSICmd_WRITE_10:
            if (!IOCmd(ccb, parallelRequest, OSReadBigInt32(cdbData, 2), OSReadBigInt16(cdbData, 7)))
                goto fail;
        break;
        case kSCSICmd_READ_6:
        case kSCSICmd_WRITE_6:
            if (!IOCmd(ccb, parallelRequest, OSSwapBigToHostConstInt32(*((UInt32 *)cdbData)) & kSCSICmdFieldMask21Bit,
                       cdbData[4] ? cdbData[4] : 256))
                goto fail;
        break;
        case kSCSICmd_SYNCHRONIZE_CACHE:
            mbox[0] = MRAID_FLUSH_CTRL_CACHE | MRAID_FLUSH_DISK_CACHE;
            if (!Do_Management(ccb, MRAID_DCMD_CTRL_CACHE_FLUSH, MRAID_DATA_NONE, 0, NULL, mbox))
                goto fail;
            goto complete;
        default:
            if (!LogicalDiskCmd(ccb, parallelRequest))
                goto fail;
    }
    
    DbgPrint("Started processing\n");
    
    /* Rework: it shouldn't rely on CDB of concrete cmd */
#if 0
    /* IMMED */
    if (cdbData[1] & 0x01) {
        MRAID_Poll(ccb);
        if (ccb->s.ccb_frame->mrr_header.mrh_cmd_status != MRAID_STAT_OK) {
            IOPrint("Polled command failed\n");
            goto fail;
        } else {
            DbgPrint("Polled command completed\n");
            goto complete;
        }
    }
#endif
    
    mraid_post(ccb);
    DbgPrint("Command queued\n");
    return kSCSIServiceResponse_Request_In_Process;
fail:
    Putccb(ccb);
    return kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
complete:
    Putccb(ccb);
    return kSCSIServiceResponse_TASK_COMPLETE;
}

/* */

bool SASMegaRAID::InitializeTargetForID(SCSITargetIdentifier targetID)
{
    //DbgPrint("%s\n", __FUNCTION__);
    
    if (!sc.sc_ld_present[targetID])
        return false;
    
    return true;
}

bool SASMegaRAID::DoesHBASupportSCSIParallelFeature(SCSIParallelFeature theFeature)
{
    DbgPrint("%s\n", __FUNCTION__);
    
    switch (theFeature) {
        case kSCSIParallelFeature_WideDataTransfer:
        case kSCSIParallelFeature_SynchronousDataTransfer:
        case kSCSIParallelFeature_QuickArbitrationAndSelection:
        case kSCSIParallelFeature_DoubleTransitionDataTransfers:
        case kSCSIParallelFeature_InformationUnitTransfers:
        return true;
    }
    
    return false;
}

/* Optionals */

void SASMegaRAID::ReportHBAConstraints(OSDictionary *constraints )
{
#if 0
    OSNumber *byteCount = NULL;
    UInt64 align_mask = 0xFFFFFFFFFFFFFFFFULL;
    
    byteCount = OSDynamicCast(OSNumber, getProperty(kIOMaximumSegmentCountReadKey));
    setProperty(kIOMaximumSegmentCountReadKey, byteCount); byteCount = NULL;
    
    byteCount = OSDynamicCast(OSNumber, getProperty(kIOMaximumSegmentCountWriteKey));
    setProperty(kIOMaximumSegmentCountWriteKey, byteCount); byteCount = NULL;
#endif
    
    BaseClass::ReportHBAConstraints(constraints);
}

/* */

bool SASMegaRAID::mraid_xscale_intr()
{
    UInt32 Status;
    
    Status = MRAID_Read(MRAID_OSTS);
    if(!(Status & MRAID_OSTS_INTR_VALID))
        return false;
    
    /* Write status back to acknowledge interrupt */
    /*if(!MRAID_Write(MRAID_OSTS, Status))
     return false;*/
    MRAID_Write(MRAID_OSTS, Status);
    
    return true;
}
void SASMegaRAID::mraid_xscale_intr_ena()
{
    MRAID_Write(MRAID_OMSK, MRAID_ENABLE_INTR);
}
void SASMegaRAID::mraid_xscale_intr_dis()
{
    MRAID_Write(MRAID_OMSK, 0);
}
UInt32 SASMegaRAID::mraid_xscale_fw_state()
{
    return MRAID_Read(MRAID_OMSG0);
}
void SASMegaRAID::mraid_xscale_post(mraid_ccbCommand *ccb)
{
    MRAID_Write(MRAID_IQP, (ccb->s.ccb_pframe >> 3) | ccb->s.ccb_extra_frames);
}
bool SASMegaRAID::mraid_ppc_intr()
{
    UInt32 Status;
    
    Status = MRAID_Read(MRAID_OSTS);
    if(!(Status & MRAID_OSTS_PPC_INTR_VALID))
        return false;
    
    /* Write status back to acknowledge interrupt */
    /*if(!MRAID_Write(MRAID_ODC, Status))
     return false;*/
    MRAID_Write(MRAID_ODC, Status);
    
    return true;
}
void SASMegaRAID::mraid_ppc_intr_ena()
{
    MRAID_Write(MRAID_ODC, 0xffffffff);
    MRAID_Write(MRAID_OMSK, ~MRAID_PPC_ENABLE_INTR_MASK);
}
void SASMegaRAID::mraid_ppc_intr_dis()
{
    /* XXX: Unchecked */
    MRAID_Write(MRAID_OMSK, ~(UInt32) 0x0);
    MRAID_Write(MRAID_ODC, 0xffffffff);
}
UInt32 SASMegaRAID::mraid_ppc_fw_state()
{
    return(MRAID_Read(MRAID_OSP));
}
void SASMegaRAID::mraid_ppc_post(mraid_ccbCommand *ccb)
{
    MRAID_Write(MRAID_IQP, 0x1 | ccb->s.ccb_pframe | (ccb->s.ccb_extra_frames << 1));
}
bool SASMegaRAID::mraid_gen2_intr()
{
    UInt32 Status;
    
    Status = MRAID_Read(MRAID_OSTS);
    if(!(Status & MRAID_OSTS_GEN2_INTR_VALID))
        return false;
    
    /* Write status back to acknowledge interrupt */
    /*if(!MRAID_Write(MRAID_ODC, Status))
     return false;*/
    MRAID_Write(MRAID_ODC, Status);
    
    return true;
}
void SASMegaRAID::mraid_gen2_intr_ena()
{
    MRAID_Write(MRAID_ODC, 0xffffffff);
    MRAID_Write(MRAID_OMSK, ~MRAID_OSTS_GEN2_INTR_VALID);
}
void SASMegaRAID::mraid_gen2_intr_dis()
{
    MRAID_Write(MRAID_OMSK, 0xffffffff);
    MRAID_Write(MRAID_ODC, 0xffffffff);
}
UInt32 SASMegaRAID::mraid_gen2_fw_state()
{
    return(MRAID_Read(MRAID_OSP));
}
bool SASMegaRAID::mraid_skinny_intr()
{
    UInt32 Status;
    
    Status = MRAID_Read(MRAID_OSTS);
    if(!(Status & MRAID_OSTS_SKINNY_INTR_VALID))
        return false;
    
    /* Write status back to acknowledge interrupt */
    /*if(!MRAID_Write(MRAID_ODC, Status))
     return false;*/
    MRAID_Write(MRAID_ODC, Status);
    
    return true;
}
void SASMegaRAID::mraid_skinny_intr_ena()
{
    MRAID_Write(MRAID_OMSK, ~MRAID_SKINNY_ENABLE_INTR_MASK);
}
UInt32 SASMegaRAID::mraid_skinny_fw_state()
{
    return MRAID_Read(MRAID_OSP);
}
void SASMegaRAID::mraid_skinny_post(mraid_ccbCommand *ccb)
{
    MRAID_Write(MRAID_IQPL, 0x1 | ccb->s.ccb_pframe | (ccb->s.ccb_extra_frames << 1));
    MRAID_Write(MRAID_IQPH, 0x00000000);
}