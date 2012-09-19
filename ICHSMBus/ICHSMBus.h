#include "I2CCommon.h"

//#define ICH_DEBUG 1

#define drvid "[ICHSMBus] "
#define I2CNoOp 0
#define super IOService

#ifdef ICH_DEBUG
#define DbgPrint(arg...) IOLog(drvid arg)
#define PrintBitFieldExpanded(val) IOPrint("Register decoded: 0x%x<BUSY=%d,INTR=%d," \
    "DEVERR=%d,BUSERR=%d,FAILED=%d,SMBAL=%d,INUSE=%d,BDONE=%d>\n", val, \
    (val & ICH_SMB_HS_BUSY) != 0, (val & ICH_SMB_HS_INTR) != 0, \
    (val & ICH_SMB_HS_DEVERR) != 0, (val & ICH_SMB_HS_BUSERR) != 0, \
    (val & ICH_SMB_HS_FAILED) != 0, (val & ICH_SMB_HS_SMBAL) != 0, \
    (val & ICH_SMB_HS_INUSE) != 0, (val & ICH_SMB_HS_BDONE) != 0)
#else
#define DbgPrint(arg...) ;
#define PrintBitFieldExpanded(val) ;
#endif
#define IOPrint(arg...) IOLog(drvid arg)

/* PCI configuration registers */
#define ICH_SMB_HOSTC 0x40
#define ICH_SMB_BASE	0x20
#define ICH_SMB_HOSTC_HSTEN	(1 << 0)
#define ICH_SMB_HOSTC_SMIEN	(1 << 1)

/* SMBus I/O registers */
#define ICH_SMB_HS	0x00		/* host status */
#define ICH_SMB_HS_BUSY		(1 << 0)	/* running a command */
#define ICH_SMB_HS_INTR		(1 << 1)	/* command completed */
#define ICH_SMB_HS_DEVERR	(1 << 2)	/* command error */
#define ICH_SMB_HS_BUSERR	(1 << 3)	/* transaction collision */
#define ICH_SMB_HS_FAILED	(1 << 4)
#define ICH_SMB_HS_SMBAL	(1 << 5)	/* SMBALERT# asserted */
#define ICH_SMB_HS_INUSE	(1 << 6)	/* bus semaphore */
#define ICH_SMB_HS_BDONE	(1 << 7)	/* byte received/transmitted */
#define ICH_SMB_HC	0x02
#define ICH_SMB_HCMD	0x03
#define ICH_SMB_TXSLVA	0x04		/* transmit slave address */
#define ICH_SMB_HD0	0x05		/* host data 0 */
#define ICH_SMB_HD1	0x06
#define ICH_SMB_TXSLVA_READ	(1 << 0)	/* read direction */
#define ICH_SMB_TXSLVA_ADDR(x)	(((x) & 0x7f) << 1) /* 7-bit address */
#define ICH_SMB_HC_INTREN	(1 << 0)	/* enable interrupts */
#define ICH_SMB_HC_CMD_BYTE	(1 << 2)
#define ICH_SMB_HC_CMD_BDATA	(2 << 2)	/* BYTE DATA command */
#define ICH_SMB_HC_CMD_WDATA	(3 << 2)	/* WORD DATA command */
#define ICH_SMB_HC_START	(1 << 6)	/* start transaction */

#define ICHSMBUS_DELAY 100
#define ICHIIC_TIMEOUT 1 /* in sec */