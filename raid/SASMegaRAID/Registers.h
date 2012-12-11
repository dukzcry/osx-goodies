/* PCI config space BAR addresses */
#define	MRAID_BAR		0x10    /* kIOPCIConfigBaseAddress0 */
#define	MRAID_BAR_GEN2	0x14    /* kIOPCIConfigBaseAddress1 */

/* Register offsets */
#define MRAID_OMSG0     0x18    /* Outbound message 0 */
#define MRAID_IDB       0x20    /* Inbound doorbell */
#define MRAID_OSTS      0x30    /* Outbound interrupt status */
#define MRAID_OMSK      0x34    /* Outbound interrupt mask */
#define MRAID_IQP		0x40    /* Inbound queue port */
#define MRAID_ODC       0xa0    /* Outbound doorbell clear */
#define MRAID_OSP       0xb0    /* Outbound scratch-pad */

/* Skinny-specific */
#define MRAID_IQPL      0x000000c0
#define MRAID_IQPH		0x000000c4