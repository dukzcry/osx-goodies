#include <sys/systm.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>

#include "OSDepend.h"

class RAID {
    void *raid_devnode;
    int devindex;
    
public:
    RAID() {raid_devnode = NULL; devindex = -1;};
    ~RAID();
    
    bool init();
    static int MRAID_Ioctl(dev_t, u_long, caddr_t, int, struct proc *);
};