#if __cplusplus < 201103L
    #error "I need C++11 for functional jokery ;)"
#endif

#include "OSDepend.h"
#include "RAID.h"

#include <sys/stat.h>

kern_return_t RAID::MakeNode()
{
    if ((devindex = cdevsw_add(-1, &my_cdevsw)) == -1) {
        IOPrint("[RAID] cdevsw_add() failed\n");
        return KERN_FAILURE;
    }
    
    if ((raid_devnode = devfs_make_node(makedev(devindex, 0),
                                     DEVFS_CHAR,
                                     UID_ROOT,
                                     GID_OPERATOR,
                                     0640,
                                     "mfi")) == NULL)
        return KERN_FAILURE;
    
    return KERN_SUCCESS;
}