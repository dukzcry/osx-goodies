#include <sys/systm.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>

static struct cdevsw my_cdevsw {
    lambda(dev_t, int x, int, struct proc *) { return x; },        // open_close_fcn_t *d_open;
    lambda(dev_t, int x, int, struct proc *) { return x; },       // open_close_fcn_t *d_close;
    lambda(dev_t, struct uio *, int x) { return x; },       // read_write_fcn_t *d_read;
    lambda(dev_t, struct uio *, int x) { return x; },       // read_write_fcn_t *d_write;
    lambda(dev_t, u_long, caddr_t, int x, struct proc *) { return x; },      // ioctl_fcn_t      *d_ioctl;
    lambda(struct tty*, int x) { return x; },        // stop_fcn_t       *d_stop;
    lambda(int x) { return x; },       // reset_fcn_t      *d_reset;
    0,               // struct tty      **d_ttys;
    lambda(dev_t, int x, void *, struct proc *) { return x; },     // select_fcn_t     *d_select;
    eno_mmap,        // mmap_fcn_t       *d_mmap;
    eno_strat,       // strategy_fcn_t   *d_strategy;
    eno_getc,        // getc_fcn_t       *d_getc;
    eno_putc,        // putc_fcn_t       *d_putc;
    D_TTY,           // int               d_type;
};

class RAID {
    void *raid_devnode;
    int devindex;
public:
    RAID() {raid_devnode = NULL; devindex = -1;};
    ~RAID() {
        int ret;
        
        if (raid_devnode != NULL) devfs_remove(raid_devnode);
        if (devindex != -1) {
            ret = cdevsw_remove(devindex, &my_cdevsw);
            if (ret != devindex)
                DbgPrint("[RAID] cdevsw_remove() failed (returned %d)\n", ret);
        }
    }
    kern_return_t MakeNode();
};
