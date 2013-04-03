#include <sys/systm.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>

#include "OSDepend.h"
#define SPECNAMELEN      63
#include <dev/mfi/mfi_ioctl.h>

class RAID {
    void *raid_devnode;
    int devindex;
    
public:
    RAID() {raid_devnode = NULL; devindex = -1;};
    ~RAID();
    
    bool init(SInt32);
    static int MRAID_Ioctl(dev_t, u_long, caddr_t, int, struct proc *);
};