#if __cplusplus < 201103L
#error "I need C++11 for functional jokery ;)"
#endif

#include <sys/systm.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>

#define SPECNAMELEN      63
#include <dev/mfi/mfi_ioctl.h>

#include "SASMegaRAID.h"

class RAID *RAIDP;
class RAID {
    void *raid_devnode;
    int devindex;
    SASMegaRAID *obj;
    
public:
    RAID() {raid_devnode = NULL; devindex = -1;};
    ~RAID();
    
    bool init(SASMegaRAID *, SInt32);
    static int Ioctl(dev_t, u_long, caddr_t, int, struct proc *);
    int MRAID_Ioctl(dev_t, u_long, caddr_t, int, struct proc *);
};

static struct cdevsw mraid_cdevsw = {
    (d_open_t *) &nulldev, // lambda(dev_t, int d_open, int, struct proc *) { return d_open; }
    (d_close_t *) &nulldev, // lambda(dev_t, int d_close, int, struct proc *) { return d_close; }
    (d_read_t *) &nulldev, // lambda(dev_t, struct uio *, int d_read) { return d_read; }
    (d_write_t *) &nulldev, // lambda(dev_t, struct uio *, int d_write) { return d_write; }
    RAID::Ioctl, // lambda(dev_t, u_long, caddr_t, int d_ioctl, struct proc *) { return d_ioctl; }
    (d_stop_t *) &nulldev, // lambda(struct tty*, int d_stop) { return d_stop; }
    (d_reset_t *) &nulldev, // lambda(int d_reset) { return d_reset; }
    0,               // struct tty      **d_ttys;
    (d_select_t *) &nulldev, // lambda(dev_t, int d_select, void *, struct proc *) { return d_select; }
    eno_mmap,        // mmap_fcn_t       *d_mmap;
    eno_strat,       // strategy_fcn_t   *d_strategy;
    eno_getc,        // getc_fcn_t       *d_getc;
    eno_putc,        // putc_fcn_t       *d_putc;
    D_TTY,           // int               d_type;
};

RAID::~RAID() {
    int ret;
    
    if (raid_devnode != NULL) devfs_remove(raid_devnode);
    if (devindex != -1) {
        ret = cdevsw_remove(devindex, &mraid_cdevsw);
        if (ret != devindex)
            DbgPrint("[RAID] cdevsw_remove() failed (returned %d)\n", ret);
    }
}

bool RAID::init(SASMegaRAID *instance, SInt32 domain)
{
    char str[5];
    
    if ((devindex = cdevsw_add(DEVINDEX, &mraid_cdevsw)) == -1) {
        IOPrint("[RAID] cdevsw_add() failed\n");
        return false;
    }
    
    snprintf(str, sizeof(str), "mfi%d", domain);
    
    if ((raid_devnode = devfs_make_node(makedev(devindex, 0),
                                        DEVFS_CHAR,
                                        UID_ROOT,
                                        GID_OPERATOR,
                                        0640,
                                        str)) == NULL)
        return false;
    
    obj = instance;
    
    return true;
}

int RAID::MRAID_Ioctl(__unused dev_t dev, u_long cmd, caddr_t data,
                __unused int flag, __unused struct proc *p)
{
    switch (cmd) {
        case MFIIO_QUERY_DISK: {
            struct mfi_query_disk *qd = (struct mfi_query_disk *) data;

            qd->present = obj->sc.sc_ld_present[qd->array_id];
            bzero(qd->devname, SPECNAMELEN + 1);
            snprintf(qd->devname, SPECNAMELEN, "mfid%d", qd->array_id);
            return 0;
        }
    }
    
    DbgPrint("[RAID] Ioctl 0x%lx not handled\n", cmd);
    return ENOTTY;
}
int RAID::Ioctl(__unused dev_t dev, u_long cmd, caddr_t data,
                      __unused int flag, __unused struct proc *p)
{
    return RAIDP->MRAID_Ioctl(dev, cmd, data, flag, p);
}