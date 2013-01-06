/* Written by Artem Falcon <lomka@gero.in> */

//#define io_debug

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
    addr_mask = IOPhysSize == 64 ? 0xFFFFFFFFFFFFFFFFULL : MASK_32BIT;
    
	/* Create an instance of PCI class from Helper Library */
	PCIHelperP = new PCIHelper<SASMegaRAID>;
    
    sc.sc_iop = IONew(mraid_iop_ops, 1);
    
    sc.sc_pcq = sc.sc_frames = sc.sc_sense = NULL;
    //sc.sc_bbuok = false;
    sc.sc_info.info = NULL;
    bzero(sc.sc_ld_present, MRAID_MAX_LD);

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
    UInt32 barval;
    
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
    MappingType = PCIHelperP->MappingType(this, regbar, &barval);
    switch(MappingType) {
        case PCI_MAPREG_TYPE_IO:
#if 1
            IOPrint("We don't support IO ports\n");
            return false;
#else
            fPCIDevice->setIOEnable(true);
        break;
#endif
        case PCI_MAPREG_MEM_TYPE_32BIT_1M:
        case PCI_MAPREG_MEM_TYPE_32BIT:
            MappingType = 32;
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
            MappingType = 64;
#if 1
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
    
    if (map) map->release();
    if (sc.sc_info.info) FreeDataMem(&sc.sc_info.mem);
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
    
    Producer = letoh32(pcq->mpc_producer);
    Consumer = letoh32(pcq->mpc_consumer);
    
    while (Consumer != Producer) {
        DbgPrint("pi: %#x ci: %#x\n", Producer, Consumer);

        Context = pcq->mpc_reply_q[Consumer];
        pcq->mpc_reply_q[Consumer] = MRAID_INVALID_CTX;
        if(Context == MRAID_INVALID_CTX)
            IOPrint("Invalid context, Prod: %d Cons: %d\n", Producer, Consumer);
        else {
            ccb = (mraid_ccbCommand *) sc.sc_ccb[Context];
#if defined DEBUG || defined io_debug
            IOPrint("ccb: %d\n", Context);
#endif
            my_assert(ccb->s.ccb_done);
            ccb->s.ccb_done(ccb);
        }
        Consumer++;
        if(Consumer == (sc.sc_max_cmds + 1))
            Consumer = 0;
    }
    
    pcq->mpc_consumer = htole32(Consumer);

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
    /* __asm__ ("nop"); */
}

bool SASMegaRAID::Attach()
{
    OSNumber *val;
    UInt32 status, frames;
    
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
    
    /* Get constraints forming frames pool contiguous memory */
    status = mraid_fw_state();
    sc.sc_max_cmds = status & MRAID_STATE_MAXCMD_MASK;
    sc.sc_max_sgl = (status & MRAID_STATE_MAXSGL_MASK) >> 16;
    
    /* XXX: Can't bump this: probably problem of my LSI-flashed PERC 5 */
    sc.sc_max_sgl = min(sc.sc_max_sgl, FREEBSD_MAXFER / PAGE_SIZE + 1);
    
    /* FW can accept 64-bit SGLs */
    if(IOPhysSize == 64) {
        sc.sc_sgl_size = sizeof(mraid_sg64);
        sc.sc_sgl_flags = MRAID_FRAME_SGL64;
    } else {
        sc.sc_sgl_size = sizeof(mraid_sg32);
        sc.sc_sgl_flags = MRAID_FRAME_SGL32;
    }
    IOPrint("DMA: %d-bit, max commands: %u, Max SGE Count: %u\n", IOPhysSize, sc.sc_max_cmds, sc.sc_max_sgl);
    
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
    if (sc.sc_info.info->mci_pd_present)
        IOPrint("%d of physical drive(s) present\n", sc.sc_info.info->mci_pd_disks_present);
    if (sc.sc_info.info->mci_pd_disks_pred_failure)
        IOPrint("Predicated failure of %d physical drive(s)\n", sc.sc_info.info->mci_pd_disks_pred_failure);
    if (sc.sc_info.info->mci_pd_disks_failed)
        IOPrint("%d of physical drives failed\n", sc.sc_info.info->mci_pd_disks_failed);
    if (sc.sc_info.info->mci_lds_degraded)
        IOPrint("%d of virtual drives degraded\n", sc.sc_info.info->mci_lds_degraded);
    if (sc.sc_info.info->mci_lds_offline)
        IOPrint("%d of virtual drives offline\n", sc.sc_info.info->mci_lds_offline);
    if (sc.sc_info.info->mci_properties.mcp_coercion_mode || sc.sc_info.info->mci_properties.mcp_alarm_enable ||
        !sc.sc_info.info->mci_properties.mcp_disable_auto_rebuild || !sc.sc_info.info->mci_properties.mcp_disable_battery_warn) {
        IOPrint("Enabled options: ");
        if (sc.sc_info.info->mci_properties.mcp_coercion_mode)
            IOLog("Physical Drive Coercion Mode  ");
        if (sc.sc_info.info->mci_properties.mcp_alarm_enable)
            IOLog("Alarm  ");
        if (!sc.sc_info.info->mci_properties.mcp_disable_auto_rebuild)
            IOLog("Auto Rebuild  ");
        if (!sc.sc_info.info->mci_properties.mcp_disable_battery_warn)
            IOLog("Battery Warning  ");
        if (sc.sc_info.info->mci_properties.mcp_restore_hotspare_on_insertion)
            IOLog("Restore HotSpare on Insertion  ");
        if (sc.sc_info.info->mci_properties.mcp_expose_encl_devices)
            IOLog("Expose Enclosure Devices  ");
        if (sc.sc_info.info->mci_properties.mcp_cluster_enable)
            IOLog("Cluster Mode");
        IOLog("\n");
    }
    ExportInfo();
    
    status = true;
    do {
        mraid_bbu_status *bbu_stat;
        mraid_data_mem mem;
        int mraid_bbu_stat;

        if (!(sc.sc_info.info->mci_hw_present & MRAID_INFO_HW_BBU) ||
            ((mraid_bbu_stat = GetBBUInfo(&mem, bbu_stat)) == MRAID_BBU_ERROR)) {
            status = false;
            break;
        }
        IOPrint("BBU type: ");
		switch (bbu_stat->battery_type) {
            case MRAID_BBU_TYPE_BBU:
                IOLog("BBU");
                break;
            case MRAID_BBU_TYPE_IBBU:
                IOLog("IBBU");
                break;
            default:
                IOLog("unknown type %d", bbu_stat->battery_type);
		}
        IOLog(", status ");
		switch(mraid_bbu_stat) {
            case MRAID_BBU_GOOD:
                IOLog("good, %d%% charged", MRAID_BBU_TYPE_IBBU ? bbu_stat->detail.ibbu.relative_charge :
                                                                 bbu_stat->detail.bbu.relative_charge);
                //sc.sc_bbuok = true;
                break;
            case MRAID_BBU_BAD:
                IOLog("bad");
                break;
            case MRAID_BBU_UNKNOWN:
                IOLog("unknown");
                break;
		}
        FreeDataMem(&mem);
    } while (0);
    if (!status)
        IOPrint("BBU not present/read error");
    IOLog("\n");
    
    sc.sc_ld_cnt = sc.sc_info.info->mci_lds_present;
    for (int i = 0; i < sc.sc_ld_cnt; i++) {
        sc.sc_ld_present[i] = true;
    }
    
    mraid_intr_enable();
    /* XXX: Is it possible to get intrs enabled info from controller? */
    InterruptsActivated = true;

    PMinit();
    getProvider()->joinPMtree(this);
    registerPowerDriver(this, PowerStates, 1);
    
#if test
    /* Ensure that interrupts work */
    bzero(sc.info, sizeof(mraid_ctrl_info));
    if (!GetInfo()) {
        IOPrint("Unable to get controller info\n");
        return false;
    }
#endif

#if 0
#if defined DEBUG || defined io_debug
    IOPrint("Formed constraints of: Max Stripe Size: %d, "
            "Max Strips PerIO %d, Max Data Transfer Size: %d sectors\n",
            1 << sc.sc_info.info->mci_stripe_sz_ops.max,
            sc.sc_info.info->mci_max_strips_per_io,
            sc.sc_info.info->mci_max_request_size);
#endif
#endif
    val = OSNumber::withNumber(MaxXferSize = MaxXferSizePerSeg =
#if 0
                               min(min((1 << sc.sc_info.info->mci_stripe_sz_ops.max) *
                                    sc.sc_info.info->mci_max_strips_per_io,
                                    sc.sc_info.info->mci_max_request_size) * SECTOR_LEN,
#else
                               (
#endif
                               (sc.sc_max_sgl - 1) * PAGE_SIZE), 64);
    setProperty(kIOMaximumByteCountReadKey, val);
    setProperty(kIOMaximumByteCountWriteKey, val);
    val->release();
    
    return true;
}

mraid_mem *SASMegaRAID::AllocMem(vm_size_t size)
{
    mraid_mem *mm;
    
    if (!(mm = IONew(mraid_mem, 1)))
        return NULL;
    
    /* Hardware requirement: Page size aligned */
    if (!(mm->bmd = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIOMemoryPhysicallyContiguous, size, PAGE_SIZE))) {
        goto free;
    }
    mm->bmd->prepare();
    mm->vaddr = (IOVirtualAddress) mm->bmd->getBytesNoCopy();
    mm->paddr = mm->bmd->getPhysicalSegment(0, NULL);
    /* Zero the whole region for easy */
    bzero((void *) mm->vaddr, size);
    
    return mm;    
free:
    IODelete(mm, mraid_mem, 1);
    
    return NULL;
}
void SASMegaRAID::FreeMem(mraid_mem *mm)
{
    mm->bmd->complete();
    mm->bmd->release();
    IODelete(mm, mraid_mem, 1);
    mm = NULL;
}

bool SASMegaRAID::GenerateSegments(mraid_ccbCommand *ccb)
{
    UInt64 offset = 0;
    
    ccb->s.ccb_sglmem.numSeg = sc.sc_max_sgl;

    if (ccb->s.ccb_sglmem.cmd->genIOVMSegments(&offset,
        ccb->s.ccb_sgl, &ccb->s.ccb_sglmem.numSeg) != kIOReturnSuccess)
        return false;
    
#if defined DEBUG || defined io_debug
    IOPrint("genIOVMSegments: nseg %d\n", ccb->s.ccb_sglmem.numSeg);
#endif

    return true;
}

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
    ccbCommandPool->returnCommand(ccb);
}
mraid_ccbCommand *SASMegaRAID::Getccb()
{
    mraid_ccbCommand *ccb;
    
    ccb = (mraid_ccbCommand *) ccbCommandPool->getCommand(true);
    return ccb;
}

bool SASMegaRAID::Transition_Firmware()
{
    UInt32 fw_state, cur_state;
    int max_wait;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    fw_state = mraid_fw_state() & MRAID_STATE_MASK;
    
    DbgPrint("Firmware state %#x\n", fw_state);
    
    while (fw_state != MRAID_STATE_READY) {
        DbgPrint("Waiting for firmware to become ready\n");
        cur_state = fw_state;
        switch (fw_state) {
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
        /* Rework: Freeze is possible */
        for (int i = 0; i < (max_wait * 10); i++) {
            fw_state = mraid_fw_state() & MRAID_STATE_MASK;
            if(fw_state == cur_state)
                IOSleep(100);
            else break;
        }
        if (fw_state == cur_state) {
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
    
    bzero(qinfo, sizeof(mraid_init_qinfo));
    qinfo->miq_rq_entries = htole32(sc.sc_max_cmds + 1);
    
    qinfo->miq_rq_addr = htole64(MRAID_DVA(sc.sc_pcq) + offsetof(mraid_prod_cons, mpc_reply_q));
	qinfo->miq_pi_addr = htole64(MRAID_DVA(sc.sc_pcq) + offsetof(mraid_prod_cons, mpc_producer));
	qinfo->miq_ci_addr = htole64(MRAID_DVA(sc.sc_pcq) + offsetof(mraid_prod_cons, mpc_consumer));
    
    init->mif_header.mrh_cmd = MRAID_CMD_INIT;
    init->mif_header.mrh_data_len = htole32(sizeof(mraid_init_qinfo));
    init->mif_qinfo_new_addr = htole64(ccb->s.ccb_pframe + MRAID_FRAME_SIZE);
    
    /*DbgPrint("Entries: %#x, rq: %#llx, pi: %#llx, ci: %#llx\n", qinfo->miq_rq_entries,
             qinfo->miq_rq_addr, qinfo->miq_pi_addr, qinfo->miq_ci_addr);*/
    
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
    
    if (!Management(MRAID_DCMD_CTRL_GET_INFO, MRAID_DATA_IN, sizeof(mraid_ctrl_info), &sc.sc_info.mem, NULL)) {
        sc.sc_info.info = NULL;
        return false;
    }
    sc.sc_info.info = (mraid_ctrl_info *) sc.sc_info.mem.bmd->getBytesNoCopy();

#if defined(DEBUG)
    int i;
	for (i = 0; i < sc.sc_info.info->mci_image_component_count; i++) {
		IOPrint("Active FW %s Version %s date %s time %s\n",
               sc.sc_info.info->mci_image_component[i].mic_name,
               sc.sc_info.info->mci_image_component[i].mic_version,
               sc.sc_info.info->mci_image_component[i].mic_build_date,
               sc.sc_info.info->mci_image_component[i].mic_build_time);
	}
    for (i = 0; i < sc.sc_info.info->mci_pending_image_component_count; i++) {
		IOPrint("Pending FW %s Version %s date %s time %s\n",
               sc.sc_info.info->mci_pending_image_component[i].mic_name,
               sc.sc_info.info->mci_pending_image_component[i].mic_version,
               sc.sc_info.info->mci_pending_image_component[i].mic_build_date,
               sc.sc_info.info->mci_pending_image_component[i].mic_build_time);
	}
    IOPrint("Max Arms Per VD %d Max Arrays %d\n",
           sc.sc_info.info->mci_max_arms,
           sc.sc_info.info->mci_max_arrays);
    IOPrint("Serial %s present %#x fw time %d\n",
           sc.sc_info.info->mci_serial_number,
           sc.sc_info.info->mci_hw_present,
           sc.sc_info.info->mci_current_fw_time
           /*,sc.sc_info.info->mci_max_sg_elements
           ,sc.sc_info.info->mci_max_cmds*/);
    IOPrint("NVRAM %d flash %d\n",
           sc.sc_info.info->mci_nvram_size,
           sc.sc_info.info->mci_flash_size);
	IOPrint("Memory Correctable Errors %d Memory Uncorrectable Errors %d Cluster Permitted %d Cluster Active %d\n",
           sc.sc_info.info->mci_ram_correctable_errors,
           sc.sc_info.info->mci_ram_uncorrectable_errors,
           sc.sc_info.info->mci_cluster_allowed,
           sc.sc_info.info->mci_cluster_active);
	IOPrint("raid_lvl %#x adapt_ops %#x ld_ops %#x\n",
           sc.sc_info.info->mci_raid_levels,
           sc.sc_info.info->mci_adapter_ops,
           sc.sc_info.info->mci_ld_ops);
	IOPrint("Min Stripe Size %d pd_ops %#x pd_mix %#x\n",
           sc.sc_info.info->mci_stripe_sz_ops.min,
           sc.sc_info.info->mci_pd_ops,
           sc.sc_info.info->mci_pd_mix_support);
	IOPrint("ECC Bucket Count %d\n",
           sc.sc_info.info->mci_ecc_bucket_count);
	IOPrint("sq_nm %d Predictive Fail Poll Interval: %dsec Interrupt Throttle Active Count %d Interrupt Throttle Completion: %dus\n",
           sc.sc_info.info->mci_properties.mcp_seq_num,
           sc.sc_info.info->mci_properties.mcp_pred_fail_poll_interval,
           sc.sc_info.info->mci_properties.mcp_intr_throttle_cnt,
           sc.sc_info.info->mci_properties.mcp_intr_throttle_timeout);
	IOPrint("Rebuild Rate: %d%% PR Rate: %d%% Background Rate %d Check Consistency Rate: %d%%\n",
           sc.sc_info.info->mci_properties.mcp_rebuild_rate,
           sc.sc_info.info->mci_properties.mcp_patrol_read_rate,
           sc.sc_info.info->mci_properties.mcp_bgi_rate,
           sc.sc_info.info->mci_properties.mcp_cc_rate);
	IOPrint("Reconstruction Rate: %d%% Cache Flush Interval: %ds Max Drives to Spinup at One Time %d Delay Among Spinup Groups: %ds\n",
           sc.sc_info.info->mci_properties.mcp_recon_rate,
           sc.sc_info.info->mci_properties.mcp_cache_flush_interval,
           sc.sc_info.info->mci_properties.mcp_spinup_drv_cnt,
           sc.sc_info.info->mci_properties.mcp_spinup_delay);
	IOPrint("Ecc Bucket Size %d\n", sc.sc_info.info->mci_properties.mcp_ecc_bucket_size);
	IOPrint("Ecc Bucket Leak Rate: %d Minutes\n", sc.sc_info.info->mci_properties.mcp_ecc_bucket_leak_rate);
	IOPrint("Vendor %#x device %#x subvendor %#x subdevice %#x\n",
           sc.sc_info.info->mci_pci.mip_vendor,
           sc.sc_info.info->mci_pci.mip_device,
           sc.sc_info.info->mci_pci.mip_subvendor,
           sc.sc_info.info->mci_pci.mip_subdevice);
	IOPrint("Type %#x (frontend) port_count %d Addresses ",
           sc.sc_info.info->mci_host.mih_type,
           sc.sc_info.info->mci_host.mih_port_count);
	for (i = 0; i < 8; i++)
		IOLog("%.0llx ", sc.sc_info.info->mci_host.mih_port_addr[i]);
	IOLog("\n");
	IOPrint("Type %.x (backend) port_count %d Addresses ",
           sc.sc_info.info->mci_device.mid_type,
           sc.sc_info.info->mci_device.mid_port_count);
	for (i = 0; i < 8; i++)
		IOLog("%.0llx ", sc.sc_info.info->mci_device.mid_port_addr[i]);
	IOLog("\n");
#endif
    
    return true;
}

void SASMegaRAID::ExportInfo()
{
    OSString *string = NULL;
    char str[33];
    
    if ((string = OSString::withCString(sc.sc_info.info->mci_product_name)))
	{
		SetHBAProperty(kIOPropertyProductNameKey, string);
		string->release();
		string = NULL;
	}
    snprintf(str, sizeof(str), "Firmware %s", sc.sc_info.info->mci_package_version);
    if ((string = OSString::withCString(str)))
	{
		SetHBAProperty(kIOPropertyProductRevisionLevelKey, string);
		string->release();
		string = NULL;
	}
    if (sc.sc_info.info->mci_memory_size > 0) {
        snprintf(str, sizeof(str), "Cache %dMB RAM", sc.sc_info.info->mci_memory_size);
        if ((string = OSString::withCString(str))) {
            SetHBAProperty(kIOPropertyPortDescriptionKey, string);
            string->release();
            string = NULL;
        }
    }
}

int SASMegaRAID::GetBBUInfo(mraid_data_mem *mem, mraid_bbu_status *&info)
{
    DbgPrint("%s\n", __FUNCTION__);
    
    if (!Management(MRAID_DCMD_BBU_GET_INFO, MRAID_DATA_IN, sizeof(mraid_bbu_status), mem, NULL))
        return MRAID_BBU_ERROR;
    
    info = (mraid_bbu_status *) mem->bmd->getBytesNoCopy();
    
#if defined(DEBUG)
	IOPrint("BBU voltage %d, current %d, temperature %d, "
           "status 0x%x\n", info->voltage, info->current,
           info->temperature, info->fw_status);
	IOPrint("Details: ");
	switch(info->battery_type) {
        case MRAID_BBU_TYPE_IBBU:
            IOLog("guage %d charger state %d "
                   "charger ctrl %d\n", info->detail.ibbu.gas_guage_status,
                   info->detail.ibbu.charger_system_state,
                   info->detail.ibbu.charger_system_ctrl);
            IOLog("\tcurrent %d abs charge %d max error %d\n",
                   info->detail.ibbu.charging_current,
                   info->detail.ibbu.absolute_charge,
                   info->detail.ibbu.max_error);
            break;
        case MRAID_BBU_TYPE_BBU:
            IOLog("guage %d charger state %d\n",
                   info->detail.ibbu.gas_guage_status,
                   info->detail.bbu.charger_status);
            IOLog("\trem capacity %d full capacity %d\n",
                   info->detail.bbu.remaining_capacity,
                   info->detail.bbu.full_charge_capacity);
            break;
        default:
            IOLog("\n");
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

/* Uglified as preparation for sensors updating: don't do DMA buffer copying */
bool SASMegaRAID::Management(UInt32 opc, UInt32 dir, UInt32 len, mraid_data_mem *mem, UInt8 *mbox)
{
    mraid_ccbCommand* ccb;
    bool res;
    
    ccb = Getccb();
    
    if (dir == MRAID_DATA_IN) {
        /* Support 64-bit DMA */
        if (!(mem->bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
            kIOMemoryPhysicallyContiguous /*| kIOMapInhibitCache // No caching */,
            len, addr_mask)))
            return false;
        
        mem->bmd->prepare();
    }
    
    res = Do_Management(ccb, opc, dir, len, mem, mbox);
    Putccb(ccb);
    
    return res;
}
bool SASMegaRAID::Do_Management(mraid_ccbCommand *ccb, UInt32 opc, UInt32 dir, UInt32 len, mraid_data_mem *mem, UInt8 *mbox)
{
    mraid_dcmd_frame *dcmd;
    
    DbgPrint("%s: opcode: %#x\n", __FUNCTION__, opc);
        
    dcmd = &ccb->s.ccb_frame->mrr_dcmd;
    bzero(dcmd->mdf_mbox, MRAID_MBOX_SIZE);
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
        /* dir == MRAID_DATA_OUT: Expected that caller 'll prepare and fill the buffer */

        dcmd->mdf_header.mrh_data_len = len;
        
        ccb->s.ccb_sglmem.len = len;
        ccb->s.ccb_sgl = &dcmd->mdf_sgl;
        
        PointToData(ccb, mem);
    }

    if (!InterruptsActivated) {
        ccb->s.ccb_done = mraid_empty_done;
        MRAID_Poll(ccb);
    } else
        MRAID_Exec(ccb);

    if (dcmd->mdf_header.mrh_cmd_status != MRAID_STAT_OK)
        goto fail;
    
    //mem->bmd->complete();
    
    return true;
fail:
    FreeDataMem(mem);
    return false;
}
/* */

void SASMegaRAID::PointToData(mraid_ccbCommand *ccb, mraid_data_mem *mem)
{
    mraid_frame_header *hdr = &ccb->s.ccb_frame->mrr_header;
    mraid_sgl *sgl;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    mem->paddr = mem->bmd->getPhysicalSegment(0, NULL);
    
    sgl = ccb->s.ccb_sgl;

    if (IOPhysSize == 64) {
        sgl->sg64[0].addr = htole64(mem->paddr);
        sgl->sg64[0].len = htole32(ccb->s.ccb_sglmem.len);
    } else {
        sgl->sg32[0].addr = (UInt32) htole32(mem->paddr
#ifndef PPC
                                                  & MASK_32BIT
#endif
                                          );
        sgl->sg32[0].len = htole32(ccb->s.ccb_sglmem.len);
    }
    DbgPrint("Paddr: %#llx\n", mem->paddr);

    hdr->mrh_flags |= (ccb->s.ccb_direction == MRAID_DATA_IN) ?
        MRAID_FRAME_DIR_READ : MRAID_FRAME_DIR_WRITE;

    hdr->mrh_flags |= sc.sc_sgl_flags;
    hdr->mrh_sg_count = 1;
    ccb->s.ccb_frame_size += sc.sc_sgl_size;
}
UInt32 SASMegaRAID::MaxXferSizePerSeg;
bool SASMegaRAID::OutputSegment(IODMACommand * __unused, IODMACommand::Segment64 sgd, void *context, UInt32 i)
{
    mraid_sgl *sgl = (mraid_sgl *) context;
    
    my_assert(sgd.fLength <= SASMegaRAID::MaxXferSizePerSeg);
    
    if (IOPhysSize == 64) {
        sgl->sg64[i].addr = htole64(sgd.fIOVMAddr);
        sgl->sg64[i].len = (UInt32) htole64(sgd.fLength);
    } else {
        sgl->sg32[i].addr = (UInt32) htole32(sgd.fIOVMAddr
#ifndef PPC
                                             & MASK_32BIT
#endif
                                             );
        sgl->sg32[i].len = (UInt32) htole64(sgd.fLength);
    }
#if defined DEBUG
    if (!i)
#if IOPhysSize == 64
        IOPrint("Paddr[0]: %#llx\n", sgl->sg64[0].addr);
#else
        IOPrint("Paddr[0]: %#x\n", sgl->sg32[0].addr);
#endif
#endif
    
    return true;
}
bool SASMegaRAID::CreateSGL(mraid_ccbCommand *ccb)
{
    mraid_frame_header *hdr = &ccb->s.ccb_frame->mrr_header;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    my_assert(ccb->s.ccb_sglmem.len <= MaxXferSize);

    if (!GenerateSegments(ccb)) {
        IOPrint("Unable to generate segments\n");
        return false;
    }
    my_assert(ccb->s.ccb_sglmem.numSeg <= sc.sc_max_sgl);

    hdr->mrh_flags |= (ccb->s.ccb_direction == MRAID_DATA_IN) ?
        MRAID_FRAME_DIR_READ : MRAID_FRAME_DIR_WRITE;
    
    hdr->mrh_flags |= sc.sc_sgl_flags;
    hdr->mrh_sg_count = ccb->s.ccb_sglmem.numSeg;
    ccb->s.ccb_frame_size += sc.sc_sgl_size * ccb->s.ccb_sglmem.numSeg;
    ccb->s.ccb_extra_frames = (ccb->s.ccb_frame_size - 1) / MRAID_FRAME_SIZE;
    
    DbgPrint("frame_size: %d, extra_frames: %d\n", ccb->s.ccb_frame_size, ccb->s.ccb_extra_frames);
    
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
    /* Because of flood coming from filter routine */
    //IOPrint("%s: offset %#x data 0x%08x\n", __FUNCTION__, offset, data);
    
    return OSReadLittleInt32(vAddr, offset);
}
void SASMegaRAID::MRAID_Write(UInt8 offset, UInt32 data)
{
    //DbgPrint("%s: offset %#x data 0x%08x\n", __FUNCTION__, offset, data);

    OSWriteLittleInt32(vAddr, offset, data);
#if defined PPC
    OSSynchronizeIO();
#endif
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
    
    /* Rework: Freeze is possible */
    while (1) {
        IOSleep(10);
        
        if (hdr->mrh_cmd_status != 0xff)
            break;
        
        if (cycles++ > 500) {
            IOPrint("Poll timeout\n");
            break;
        }
    }
    
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
    if (ccb->s.ccb_done)
        IOPrint("Warning: ccb_done set\n");
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
#if defined DEBUG
    my_assert(ret == THREAD_AWAKENED);
    if (ret != THREAD_AWAKENED)
        IOPrint("Warning: interrupt didn't come while expected\n");
#endif
    
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
#if defined DEBUG /*|| defined io_debug*/
            IOPrint("Warning: kSCSITaskStatus_No_Status: cmd 0x%02x\n", hdr->mrh_cmd_status);
#endif
#if defined io_debug
            if (cmd->blkcnt > 0)
                IOPrint("Warning: cmd failed with blkcnt %d\n", cmd->blkcnt);
#endif
            if (hdr->mrh_scsi_status) {
                cmd->ts = kSCSITaskStatus_CHECK_CONDITION;
                cmd->sense = &sense;
                memcpy(&sense, ccb->s.ccb_sense, sizeof(SCSI_Sense_Data));
            }
        break;
    }
    
    my_assert(cmd->ts == kSCSITaskStatus_GOOD);
#if defined DEBUG /*|| defined io_debug*/
    if (cmd->ts != kSCSITaskStatus_GOOD)
        IOPrint("Warning: cmd failed with ts 0x%x on opc 0x%x\n", cmd->ts, cmd->opcode);
#endif
    
    cmd->instance->CompleteTask(ccb, cmd);
    IODelete(cmd, cmd_context, 1);
}

void SASMegaRAID::CompleteTask(mraid_ccbCommand *ccb, cmd_context *cmd)
{
    if (ccb->s.ccb_direction != MRAID_DATA_NONE) {
    	cmd->ts == kSCSITaskStatus_GOOD ?
            SetRealizedDataTransferCount(cmd->pr, ccb->s.ccb_sglmem.len)
            : SetRealizedDataTransferCount(cmd->pr, 0);
        
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
    
    mraid_pass_frame *pf;
    cmd_context *cmd;
    
    DbgPrint("%s\n", __FUNCTION__);
    
    pf = &ccb->s.ccb_frame->mrr_pass;
    pf->mpf_header.mrh_cmd = MRAID_CMD_LD_SCSI_IO;
    pf->mpf_header.mrh_target_id = GetTargetIdentifier(pr);
    pf->mpf_header.mrh_lun_id = 0;
    pf->mpf_header.mrh_cdb_len = GetCommandDescriptorBlockSize(pr);
    pf->mpf_header.mrh_timeout = GetTimeoutDuration(pr);
    pf->mpf_header.mrh_data_len = (UInt32) htole64(GetRequestedDataTransferCount(pr)); /* XXX */
    pf->mpf_header.mrh_sense_len = MRAID_SENSE_SIZE;
    
    pf->mpf_sense_addr = htole64(ccb->s.ccb_psense);

    bzero(pf->mpf_cdb, 16);
    GetCommandDescriptorBlock(pr, &cdbData);
	memcpy(pf->mpf_cdb, cdbData, pf->mpf_header.mrh_cdb_len);
    
    ccb->s.ccb_done = mraid_cmd_done;
    
    cmd = IONew(cmd_context, 1);
    cmd->instance = this;
    cmd->pr = pr;
#if defined DEBUG /*|| defined io_debug*/
    cmd->opcode = cdbData[0];
#endif
#if defined DEBUG || defined io_debug
    cmd->blkcnt = 0;
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

    if (GetDataBuffer(pr)) {
        ccb->s.ccb_sglmem.len = (UInt32) GetRequestedDataTransferCount(pr);
        if (!(ccb->s.ccb_sglmem.cmd = GetDMACommand(pr)))
            return false;
        ccb->s.ccb_sglmem.cmd->prepare(GetDataBufferOffset(pr), ccb->s.ccb_sglmem.len, false, false);
        if (!CreateSGL(ccb)) {
            FreeSGL(&ccb->s.ccb_sglmem);
            return false;
        }
    }
    
    return true;
}

bool SASMegaRAID::IOCmd(mraid_ccbCommand *ccb, SCSIParallelTaskIdentifier pr, UInt64 lba, UInt32 len)
{
#if defined DEBUG /*|| defined io_debug*/
    SCSICommandDescriptorBlock cdbData = { 0 };
#endif
    
    mraid_io_frame *io;
    cmd_context *cmd;
    
    if (!GetDataBuffer(pr))
        return false;

#if defined DEBUG || defined io_debug
    IOPrint("%s: trlen: %lld, lba: %lld, blkcnt: %d\n", __FUNCTION__, GetRequestedDataTransferCount(pr), lba, len);
#endif
    
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
    io->mif_header.mrh_timeout = GetTimeoutDuration(pr);
    io->mif_header.mrh_flags = 0;
    io->mif_header.mrh_data_len = htole32(len);
    io->mif_header.mrh_sense_len = MRAID_SENSE_SIZE;
    io->mif_lba = htole64(lba);

    io->mif_sense_addr = htole64(ccb->s.ccb_psense);
    
    ccb->s.ccb_done = mraid_cmd_done;
    
    cmd = IONew(cmd_context, 1);
    cmd->instance = this;
    cmd->pr = pr;
#if defined DEBUG /*|| defined io_debug*/
    GetCommandDescriptorBlock(pr, &cdbData);
    cmd->opcode = cdbData[0];
#endif
#if defined DEBUG || defined io_debug
    cmd->blkcnt = len;
#endif
    ccb->s.ccb_context = cmd;
    
    ccb->s.ccb_frame_size = MRAID_PASS_FRAME_SIZE;
    ccb->s.ccb_sgl = &io->mif_sgl;

    ccb->s.ccb_sglmem.len = (UInt32) GetRequestedDataTransferCount(pr);
    if (!(ccb->s.ccb_sglmem.cmd = GetDMACommand(pr)))
        return false;
    ccb->s.ccb_sglmem.cmd->prepare(GetDataBufferOffset(pr), ccb->s.ccb_sglmem.len, false, false);
    if (!CreateSGL(ccb)) {
        FreeSGL(&ccb->s.ccb_sglmem);
        return false;
    }
    
    return true;
}

SCSIServiceResponse SASMegaRAID::ProcessParallelTask(SCSIParallelTaskIdentifier parallelRequest)
{
    SCSICommandDescriptorBlock cdbData = { 0 };
    
    mraid_ccbCommand *ccb;
    UInt8 mbox[MRAID_MBOX_SIZE];
    
    GetCommandDescriptorBlock(parallelRequest, &cdbData);

    DbgPrint("%s: Opcode 0x%x, Target %llu\n", __FUNCTION__, cdbData[0],
            GetTargetIdentifier(parallelRequest));
    
    /* TO-DO: We need batt status refreshing for this */
    /*if (cdbData[0] == kSCSICmd_SYNCHRONIZE_CACHE && sc.sc_bbuok)
     return kSCSIServiceResponse_TASK_COMPLETE;*/
    
    ccb = Getccb();
    
    switch (cdbData[0]) {
        /* Optional */
        case kSCSICmd_READ_12:
        case kSCSICmd_WRITE_12:
        /* */
#if 0
            if (!IOCmd(ccb, parallelRequest, OSReadBigInt32(cdbData, 2),
                       /* XXX: Check me */
                       OSReadBigInt16(cdbData, 9)))
#endif
                goto fail;
        break;
        case kSCSICmd_READ_10:
        case kSCSICmd_WRITE_10:
            if (!IOCmd(ccb, parallelRequest, (UInt64) OSReadBigInt32(cdbData, 2), (UInt32) OSReadBigInt16(cdbData, 7)))
                goto fail;
        break;
        case kSCSICmd_READ_6:
        case kSCSICmd_WRITE_6:
            if (!IOCmd(ccb, parallelRequest, (UInt64) (OSSwapBigToHostConstInt32(*((UInt32 *) cdbData)) & kSCSICmdFieldMask21Bit),
                       cdbData[4] ? cdbData[4] : 256))
                goto fail;
        break;
        case kSCSICmd_SYNCHRONIZE_CACHE:
            mbox[0] = MRAID_FLUSH_CTRL_CACHE | MRAID_FLUSH_DISK_CACHE;
            if (!Do_Management(ccb, MRAID_DCMD_CTRL_CACHE_FLUSH, MRAID_DATA_NONE, 0, NULL, mbox))
                goto fail;
            goto complete;
        /* No support for them */
        case kSCSICmd_MODE_SENSE_6:
        case kSCSICmd_MODE_SENSE_10:
        /* */
            goto fail;
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
    my_assert(cdbData[0] == kSCSICmd_MODE_SENSE_6 || cdbData[0] == kSCSICmd_MODE_SENSE_10
#if 1
              || cdbData[0] == kSCSICmd_READ_12 || cdbData[0] == kSCSICmd_WRITE_12
#endif
              );
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
        case kSCSIParallelFeature_SynchronousDataTransfer:
            return false;
        case kSCSIParallelFeature_WideDataTransfer:
        case kSCSIParallelFeature_QuickArbitrationAndSelection:
        case kSCSIParallelFeature_DoubleTransitionDataTransfers:
        case kSCSIParallelFeature_InformationUnitTransfers:
            return true;
    }
    
    return false;
}

void SASMegaRAID::ReportHBAConstraints(OSDictionary *constraints)
{
    OSNumber *val;
    
    DbgPrint("%s\n", __FUNCTION__);

    val = OSNumber::withNumber(sc.sc_max_sgl, 64);
    constraints->setObject(kIOMaximumSegmentCountReadKey, val);
    constraints->setObject(kIOMaximumSegmentCountWriteKey, val);
    
    val->setValue(MaxXferSizePerSeg);
    constraints->setObject(kIOMaximumSegmentByteCountReadKey, val);
    constraints->setObject(kIOMaximumSegmentByteCountWriteKey, val);
    
    val->setValue(1);
    constraints->setObject(kIOMinimumSegmentAlignmentByteCountKey, val);
    
    val->setValue(MappingType);
    constraints->setObject(kIOMaximumSegmentAddressableBitCountKey, val);

    /* No alignment restriction, we don't use HBA data from stack */
    val->setValue(addr_mask);
    constraints->setObject(kIOMinimumHBADataAlignmentMaskKey, val);
    /* */

    val->release();
}
bool SASMegaRAID::InitializeDMASpecification(IODMACommand *cmd)
{
    //DbgPrint("%s\n", __FUNCTION__);
    
    return cmd->initWithSpecification(SASMegaRAID::OutputSegment, IOPhysSize, MaxXferSizePerSeg,
                                      IODMACommand::kMapped, MaxXferSize);
}

bool SASMegaRAID::mraid_xscale_intr()
{
    UInt32 Status;
    
    Status = MRAID_Read(MRAID_OSTS);
    if(!(Status & MRAID_OSTS_INTR_VALID))
        return false;
     
    /* Write status back to acknowledge interrupt */
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
