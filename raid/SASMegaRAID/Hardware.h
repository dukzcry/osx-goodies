#include "OSDepend.h"

#define MRAID_PCI_MEMSIZE                       0x2000      /* 8k */

#define	PCI_VENDOR_SYMBIOS						0x1000		/* Symbios Logic */
#define	PCI_VENDOR_DELL							0x1028		/* Dell */

#define	PCI_PRODUCT_SYMBIOS_MEGARAID_SAS		0x0411		/* MegaRAID SAS 1064R */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_VERDE_ZCR	0x0413		/* MegaRAID Verde ZCR */
#define	PCI_PRODUCT_SYMBIOS_SAS1078				0x0060		/* SAS1078 */
#define PCI_PRODUCT_SYMBIOS_SAS1078DE           0x007c      /* SAS1078DE */
#define	PCI_PRODUCT_DELL_PERC5					0x0015		/* PERC 5 */
#define	PCI_PRODUCT_SYMBIOS_SAS2108_1			0x0078		/* MegaRAID SAS2108 CRYPTO GEN2 */
#define	PCI_PRODUCT_SYMBIOS_SAS2108_2			0x0079		/* MegaRAID SAS2108 GEN2 */
#define PCI_PRODUCT_SYMBIOS_SAS2008_1           0x0073      /* MegaRAID SAS2008 */

/* Generic purpose constants */
#define MRAID_FRAME_SIZE                        64
#define MRAID_SENSE_SIZE                        128
#define MRAID_OSTS_INTR_VALID                   0x00000002
#define MRAID_OSTS_PPC_INTR_VALID               0x80000000
#define MRAID_OSTS_GEN2_INTR_VALID              (0x00000001 | 0x00000004)
#define MRAID_OSTS_SKINNY_INTR_VALID            0x00000001
#define MRAID_INVALID_CTX                       0xffffffff
#define MRAID_ENABLE_INTR                       0x01
#define MRAID_MAXFER                            MAXPHYS /* XXX: Bogus */

/* PPC-specific */
#define MRAID_PPC_ENABLE_INTR_MASK              0x80000004
/* Skinny-specific */
#define MRAID_SKINNY_ENABLE_INTR_MASK           0x00000001

/* Firmware states */
#define MRAID_STATE_MASK                        0xf0000000
#define MRAID_STATE_UNDEFINED                   0x00000000
#define MRAID_STATE_BB_INIT                     0x10000000
#define MRAID_STATE_FW_INIT                     0x40000000
#define MRAID_STATE_WAIT_HANDSHAKE              0x60000000
#define MRAID_STATE_DEVICE_SCAN                 0x80000000
#define MRAID_STATE_FLUSH_CACHE                 0xa0000000
#define MRAID_STATE_READY                       0xb0000000
#define MRAID_STATE_OPERATIONAL                 0xc0000000
#define MRAID_STATE_FAULT                       0xf0000000
#define MRAID_STATE_MAXSGL_MASK                 0x00ff0000
#define MRAID_STATE_MAXCMD_MASK                 0x0000ffff

/* FW init, clear cmds queue, state resets */
#define MRAID_INIT_READY                        0x00000002 /* Discard queue info on this */
#define MRAID_INIT_CLEAR_HANDSHAKE              0x00000008

/* Frame flags */
#define MRAID_FRAME_DONT_POST_IN_REPLY_QUEUE	0x0001
#define MRAID_FRAME_SGL32                       0x0000
#define MRAID_FRAME_SGL64                       0x0002
#define MRAID_FRAME_DIR_WRITE                   0x0008
#define MRAID_FRAME_DIR_READ                    0x0010

/* Command opcodes */
#define MRAID_CMD_INIT                          0x00
#define MRAID_CMD_LD_READ                       0x01
#define MRAID_CMD_LD_WRITE                      0x02
#define MRAID_CMD_LD_SCSI_IO                    0x03
#define MRAID_CMD_DCMD                          0x05

/* Direct commands */
#define MRAID_DCMD_CTRL_GET_INFO                0x01010000
#define MRAID_DCMD_CTRL_CACHE_FLUSH             0x01101000
#define   MRAID_FLUSH_CTRL_CACHE                0x01
#define   MRAID_FLUSH_DISK_CACHE                0x02
#define MRAID_DCMD_CTRL_SHUTDOWN                0x01050000
#define MRAID_DCMD_BBU_GET_INFO                 0x05010000

/* Mailbox bytes in direct command */
#define MRAID_MBOX_SIZE                         12

/* Driver defs */
#define MRAID_MAX_LD                            64
#define MRAID_MAX_LUN                           8

typedef enum {
    MRAID_IOP_XSCALE,
    MRAID_IOP_PPC,
    MRAID_IOP_GEN2,
    MRAID_IOP_SKINNY
} mraid_iop;
typedef struct {
	UInt16							mpd_vendor;
	UInt16							mpd_product;
	mraid_iop                       mpd_iop;
} mraid_pci_device;
namespace mraid_structs {
    static const mraid_pci_device mraid_pci_devices[] = {
        { PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_SAS,
            MRAID_IOP_XSCALE	},
        { PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_MEGARAID_VERDE_ZCR,
            MRAID_IOP_XSCALE },
        { PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS1078,
            MRAID_IOP_PPC },
        { PCI_VENDOR_SYMBIOS,   PCI_PRODUCT_SYMBIOS_SAS1078DE,
            MRAID_IOP_PPC },
        { PCI_VENDOR_DELL,		PCI_PRODUCT_DELL_PERC5,
            MRAID_IOP_XSCALE },
        { PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2108_1,
            MRAID_IOP_GEN2 },
        { PCI_VENDOR_SYMBIOS,	PCI_PRODUCT_SYMBIOS_SAS2108_2,
            MRAID_IOP_GEN2 },
        { PCI_VENDOR_SYMBIOS,   PCI_PRODUCT_SYMBIOS_SAS2008_1,
            MRAID_IOP_SKINNY }
    };
}

/* Command completion codes */
typedef enum {
	MRAID_STAT_OK = 0x00,
    MRAID_STAT_DEVICE_NOT_FOUND = 0x0c,
    MRAID_STAT_SCSI_DONE_WITH_ERROR = 0x2d
} mraid_status_t;

/* Sense buffer */
typedef struct {
	uint8_t			mse_data[MRAID_SENSE_SIZE];
} __attribute__((packed)) mraid_sense;

/* Scatter gather access elements */
typedef struct {
	uint32_t		addr;
	uint32_t		len;
} __attribute__((packed)) mraid_sg32;
typedef struct {
	uint64_t		addr;
	uint32_t		len;
} __attribute__((packed)) mraid_sg64;
typedef union {
	mraid_sg32		sg32[1];
	mraid_sg64		sg64[1];
} __attribute__((packed)) mraid_sgl;

typedef struct {
	uint8_t			mrh_cmd;
	uint8_t			mrh_sense_len;
	uint8_t         mrh_cmd_status;
	uint8_t			mrh_scsi_status;
	uint8_t			mrh_target_id;
	uint8_t			mrh_lun_id;
	uint8_t			mrh_cdb_len;
	uint8_t			mrh_sg_count;
	uint32_t		mrh_context;
	uint32_t		mrh_pad0;
	uint16_t		mrh_flags;
	uint16_t		mrh_timeout;
	uint32_t		mrh_data_len;
} __attribute__((packed)) mraid_frame_header;
typedef struct {
	mraid_frame_header	mif_header;
	uint64_t            mif_qinfo_new_addr;
	uint64_t            mif_qinfo_old_addr;
	uint32_t            mif_reserved[6];
} __attribute__((packed)) mraid_init_frame;
/* Queue init */
typedef struct {
	uint32_t		miq_flags;
	uint32_t		miq_rq_entries;
	uint64_t		miq_rq_addr;
	uint64_t		miq_pi_addr;
	uint64_t		miq_ci_addr;
} __attribute__((packed)) mraid_init_qinfo;
typedef struct {
    mraid_frame_header  mif_header;
	uint64_t            mif_sense_addr;
	uint64_t            mif_lba;
    mraid_sgl           mif_sgl;
} __attribute__((packed)) mraid_io_frame;
#define MRAID_PASS_FRAME_SIZE 48
typedef struct {
    mraid_frame_header  mpf_header;
	uint64_t            mpf_sense_addr;
	uint8_t             mpf_cdb[16];
    mraid_sgl           mpf_sgl;
} __attribute__((packed)) mraid_pass_frame;
#define MRAID_DCMD_FRAME_SIZE 40
typedef struct {
	mraid_frame_header  mdf_header;
	uint32_t            mdf_opcode;
	uint8_t             mdf_mbox[MRAID_MBOX_SIZE];
	mraid_sgl           mdf_sgl;
} __attribute__((packed)) mraid_dcmd_frame;
typedef struct {
    mraid_frame_header  maf_header;
	uint32_t            maf_abort_context;
	uint32_t            maf_pad;
	uint64_t            maf_abort_mfi_addr;
	uint32_t            maf_reserved[6];
} __attribute__((packed)) mraid_abort_frame;
typedef struct {
    mraid_frame_header  msf_header;
    uint64_t            msf_sas_addr;
    union {
        mraid_sg32      sg32[2];
        mraid_sg64      sg64[2];
    } msf_sgl;
} __attribute__((packed)) mraid_smp_frame;
typedef struct {
    mraid_frame_header  msf_header;
	uint16_t            msf_fis[10];
	uint32_t            msf_stp_flags;
    union {
        mraid_sg32      sg32[2];
        mraid_sg64      sg64[2];
    } msf_sgl;
} __attribute__((packed)) mraid_stp_frame;

typedef union {
	mraid_frame_header  mrr_header;
	mraid_init_frame	mrr_init;
	mraid_io_frame      mrr_io;
	mraid_pass_frame	mrr_pass;
	mraid_dcmd_frame	mrr_dcmd;
	mraid_abort_frame	mrr_abort;
	mraid_smp_frame     mrr_smp;
	mraid_stp_frame     mrr_stp;
	uint8_t             mrr_bytes[MRAID_FRAME_SIZE];
} mraid_frame;

typedef struct {
	uint32_t		mpc_producer;
	uint32_t		mpc_consumer;
	uint32_t		mpc_reply_q[1]; /* Compensate for 1 extra reply per spec */
} mraid_prod_cons;

/* mraid_ctrl_info */
typedef struct {
	uint16_t		mcp_seq_num;
	uint16_t		mcp_pred_fail_poll_interval;
	uint16_t		mcp_intr_throttle_cnt;
	uint16_t		mcp_intr_throttle_timeout;
	uint8_t			mcp_rebuild_rate;
	uint8_t			mcp_patrol_read_rate;
	uint8_t			mcp_bgi_rate;
	uint8_t			mcp_cc_rate;
	uint8_t			mcp_recon_rate;
	uint8_t			mcp_cache_flush_interval;
	uint8_t			mcp_spinup_drv_cnt;
	uint8_t			mcp_spinup_delay;
	uint8_t			mcp_cluster_enable;
	uint8_t			mcp_coercion_mode;
	uint8_t			mcp_alarm_enable;
	uint8_t			mcp_disable_auto_rebuild;
	uint8_t			mcp_disable_battery_warn;
	uint8_t			mcp_ecc_bucket_size;
	uint16_t		mcp_ecc_bucket_leak_rate;
	uint8_t			mcp_restore_hotspare_on_insertion;
	uint8_t			mcp_expose_encl_devices;
	uint8_t			mcp_reserved[38];
} __attribute__((packed)) mraid_ctrl_props;
typedef struct {
	uint16_t		mip_vendor;
	uint16_t		mip_device;
	uint16_t		mip_subvendor;
	uint16_t		mip_subdevice;
	uint8_t			mip_reserved[24];
} __attribute__((packed)) mraid_info_pci;
/* Host interface info */
typedef struct {
	uint8_t			mih_type;
	uint8_t			mih_reserved[6];
	uint8_t			mih_port_count;
	uint64_t		mih_port_addr[8];
} __attribute__((packed)) mraid_info_host;
/* Device interface info */
typedef struct {
	uint8_t			mid_type;
	uint8_t			mid_reserved[6];
	uint8_t			mid_port_count;
	uint64_t		mid_port_addr[8];
} __attribute__((packed)) mraid_info_device;
/* Firmware component info */
typedef struct {
	char			mic_name[8];
	char			mic_version[32];
	char			mic_build_date[16];
	char			mic_build_time[16];
} __attribute__((packed)) mraid_info_component;

/* MRAID_DCMD_CTRL_GETINFO */
typedef struct {
	mraid_info_pci          mci_pci;
	mraid_info_host         mci_host;
	mraid_info_device       mci_device;
    
	/* Active firmware components */
	uint32_t                mci_image_check_word;
	uint32_t                mci_image_component_count;
	mraid_info_component    mci_image_component[8];
    
	/* Inactive firmware components */
	uint32_t                mci_pending_image_component_count;
	mraid_info_component    mci_pending_image_component[8];
    
	uint8_t                 mci_max_arms;
	uint8_t                 mci_max_spans;
	uint8_t                 mci_max_arrays;
	uint8_t                 mci_max_lds;
	char                    mci_product_name[80];
	char                    mci_serial_number[32];
	uint32_t                mci_hw_present;
#define MRAID_INFO_HW_BBU	0x01
	uint32_t                mci_current_fw_time;
	uint16_t                mci_max_cmds;
	uint16_t                mci_max_sg_elements;
	uint32_t                mci_max_request_size;
	uint16_t                mci_lds_present;
	uint16_t                mci_lds_degraded;
	uint16_t                mci_lds_offline;
	uint16_t                mci_pd_present;
	uint16_t                mci_pd_disks_present;
	uint16_t                mci_pd_disks_pred_failure;
	uint16_t                mci_pd_disks_failed;
	uint16_t                mci_nvram_size;
	uint16_t                mci_memory_size;
	uint16_t                mci_flash_size;
	uint16_t                mci_ram_correctable_errors;
	uint16_t                mci_ram_uncorrectable_errors;
	uint8_t                 mci_cluster_allowed;
	uint8_t                 mci_cluster_active;
	uint16_t                mci_max_strips_per_io;
    
	uint32_t                mci_raid_levels;
    
	uint32_t                mci_adapter_ops;
    
	uint32_t                mci_ld_ops;
    
	struct {
		uint8_t             min;
		uint8_t             max;
		uint8_t             reserved[2];
	} __attribute__((packed)) mci_stripe_sz_ops;
    
	uint32_t                mci_pd_ops;
    
	uint32_t                mci_pd_mix_support;
    
	uint8_t                 mci_ecc_bucket_count;
	uint8_t                 mci_reserved2[11];
	mraid_ctrl_props        mci_properties;
	char                    mci_package_version[0x60];
	uint8_t                 mci_pad[0x800 - 0x6a0];
} __attribute__((packed)) mraid_ctrl_info;

typedef struct {
	uint16_t		gas_guage_status;
	uint16_t		relative_charge;
	uint16_t		charger_system_state;
	uint16_t		charger_system_ctrl;
	uint16_t		charging_current;
	uint16_t		absolute_charge;
	uint16_t		max_error;
	uint8_t			reserved[18];
} __attribute__((packed)) mraid_ibbu_state;
typedef struct {
	uint16_t		gas_guage_status;
	uint16_t		relative_charge;
	uint16_t		charger_status;
	uint16_t		remaining_capacity;
	uint16_t		full_charge_capacity;
	uint8_t			is_SOH_good;
	uint8_t			reserved[21];
} __attribute__((packed)) mraid_bbu_state;
typedef union {
	mraid_ibbu_state	ibbu;
	mraid_bbu_state	bbu;
} mraid_bbu_status_detail;
/* MRAID_DCMD_BBU_GET_STATUS */
typedef struct {
	uint8_t			battery_type;
#define MRAID_BBU_TYPE_NONE                 0
#define MRAID_BBU_TYPE_IBBU                 1
#define MRAID_BBU_TYPE_BBU                  2
	uint8_t			reserved;
	uint16_t		voltage;
	int16_t			current;
	uint16_t		temperature;
	uint32_t		fw_status;
#define MRAID_BBU_STATE_PACK_MISSING        (1 << 0)
#define MRAID_BBU_STATE_VOLTAGE_LOW         (1 << 1)
#define MRAID_BBU_STATE_TEMPERATURE_HIGH    (1 << 2)
#define MRAID_BBU_STATE_LEARN_CYC_FAIL      (1 << 7)
#define MRAID_BBU_STATE_LEARN_CYC_TIMEOUT   (1 << 8)
#define MRAID_BBU_STATE_I2C_ERR_DETECT      (1 << 9)
	uint8_t			pad[20];
	mraid_bbu_status_detail detail;
} __attribute__((packed)) mraid_bbu_status;