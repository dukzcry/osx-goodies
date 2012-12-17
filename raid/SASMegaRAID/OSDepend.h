#include <IOKit/IOLib.h>

#define drvid               "[SASMegaRAID] "

#define INTSIZE             (64 * 1024)

#define IOPrint(arg...)     IOLog(drvid arg)
#if defined(DEBUG)
#define DbgPrint(arg...)    IOLog(drvid arg)
#else
#define DbgPrint(arg...)
#endif
#define my_assert(x)        (x ? (void) 0 : Assert(__FILE__, __LINE__, # x))

#define nitems(_a)          (sizeof((_a)) / sizeof((_a)[0]))
#define MASK_32BIT          0x00000000FFFFFFFFULL

#if defined PPC
#include <libkern/OSByteOrder.h>

#define letoh32(x) OSSwapLittleToHostInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#else
#define letoh32(x) (x)
#define htole32(x) (x)
#define htole64(x) (x)
#endif