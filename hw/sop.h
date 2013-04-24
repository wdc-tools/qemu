/*
 * Copyright (C) 2012-2013 HGST, Inc.
 *
 * written by:
 *  Robert Bennett <Robert.Bennett@hgst.com>
 *  David Darrington <David.Darrington@hgst.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef SOP_H_
#define SOP_H_

#include "hw.h"
#include "pci/pci.h"
#include "qemu/timer.h"
#include "qemu/queue.h"
#include "loader.h"
#include "sysemu/sysemu.h"
#include "pci/msix.h"
#include <pthread.h>
#include <sched.h>

#define data8 uint8_t
#define data16 uint16_t
#define data32 uint32_t
#define data64 uint64_t

#ifndef min
#define min(x, y) ((x) < (y) ? (x) : (y))
#endif

#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

#define BYTES_PER_BLOCK 512
#define BYTES_PER_MB (1024ULL * 1024ULL)

// Config File names
#define PQI_CONFIG_FILE "PQI_device_PQI_config"
#define PCI_CONFIG_FILE "PQI_device_PCI_config"

// Page size supported by the hardware
#define PAGE_SIZE 4096

// TODO: Should be in pci class someday. BWS - just picked 04 for last byte but that's not defined yet
#define PCI_CLASS_STORAGE_EXPRESS 0x010800
#define PQI_PCI_PROG_IF 0x04
#define PCI_CLASS_STORAGE_OTHER 0x0180    // RBB forcing to class "Other Mass Storage Controller" 01-80-00
// #define PQI_PCI_PROG_IF 0x00

// TODO: Device ID for PQI Device BWS - just picked this since we don't have one yet
#define PQI_DEV_ID 0x0100

// Maximum number of characters on a line in any config file
#define MAX_CHAR_PER_LINE 250

/* Width of SQ/CQ base address in bytes*/
#define QUEUE_BASE_ADDRESS_WIDTH 8

/* Length in bytes of registers in PCI space */
#define PCI_ROM_ADDRESS_LEN 0x04
#define PCI_BIST_LEN 0x01
#define PCI_BASE_ADDRESS_2_LEN 0x04

/* Defines the number of entries to process per execution */
#define ENTRIES_TO_PROCESS 4

/* bytes,word and dword in bytes */
#define BYTE 1
#define WORD 2
#define DWORD 4
#define QWORD 8

/* SUCCESS and FAILURE return values */
#define SUCCESS 0x0
#define FAIL 0x1

/* Macros to check which Interrupt is enabled */
#define IS_MSIX(n) (n->dev.config[n->dev.msix_cap + 0x03] & 0x80)

/* PQI Cntrl Space specific #defines */
#define CC_EN 1
/* Used to create masks */
/* numbr  : Number of 1's required
 * offset : Offset from LSB
 */
#define MASK(numbr, offset) ((0xffffffff ^ (0xffffffff << numbr)) << offset)

/* The spec requires giving the table structure
 * a 4K aligned region all by itself. */
#define MSIX_PAGE_SIZE 0x1000

/* Reserve second half of the page for pending bits */
#define MSIX_PAGE_PENDING (MSIX_PAGE_SIZE / 2)

/* Give 8kB for BAR-0 registers. Should be OK for 512 queues. */
#define PQI_REG_SIZE (1024 * 8)

/* Size of PQI Controller Registers except the Doorbells
 *   0x0000 to 0x00FF : BAR Regs (reserved for the PQI Device Registers defined in Section 5.2.1)
 *   0x0100 to 0x02FF : BAR Regs Assigned IQ PI Registers (64, 64-bit IQ PI registers)
 *   0x0300 to 0x04FF : BAR Regs Assigned OQ CI Registers (64, 64-bit IQ CI registers)
 */
#define PQI_CNTRL_SIZE (0x0500)

/* Maximum Q's allocated for the controller including Admin Q */
#define PQI_MAX_QS_ALLOCATED 64

/* The Q ID starts from 0 for Admin Q and ends at
 * PQI_MAX_QS_ALLOCATED minus 1 for IO Q's */
#define PQI_MAX_QID (PQI_MAX_QS_ALLOCATED - 1)

/* Queue Limit.*/
#define PQI_MSIX_NVECTORS 32

/* Assume that block is 512 bytes */
#define SOP_BUF_SIZE 4096
#define SOP_BLOCK_SIZE(x) (1 << x)

/* The value is reported in terms of a power of two (2^n).
 * LBA data size=2^9=512
 */
#define LBA_SIZE 9

#define SOP_EMPTY 0xffffffff

/* Definitions regarding  Identify Namespace Datastructure */
#define NO_POWER_STATE_SUPPORT 2 /* 0 BASED */
#define SOP_ABORT_COMMAND_LIMIT 10 /* 0 BASED */
#define ASYNC_EVENT_REQ_LIMIT 3 /* 0 BASED */

/* Definitions regarding  Identify Controller Datastructure */
#define NO_LBA_FORMATS 15 /* 0 BASED */
#define LBA_FORMAT_INUSE 0 /* 0 BASED */

#define SOP_SPARE_THRESH 20
#define SOP_TEMPERATURE 0x143
#define SOP_MAX_LUN_SIZE 1048576
#define SOP_MAX_NUM_LUNS 4 // TODO: BWS - should this be 256?

// PQI Device Registers
// Section 5.2.1 (T10/2240-D PQI specification)

enum {

    // Start of "standard registers" 
    PQI_SIG       = 0x0000, // Signature, 64bit
    PQI_AQ_CONFIG = 0x0008, // Admin Queue Config Function, 64bit
    PQI_CAP       = 0x0010, // Capability, 64bit
    PQI_INTS      = 0x0018, // Legacy INTx Status, 32bit
    PQI_INTMS     = 0x001C, // Legacy INTx Mask Set, 32bit
    PQI_INTMC     = 0x0020, // Legacy INTx Mask Clear, 32bit
    PQI_STATUS    = 0x0040, // PQI Device Status, 32bit
    PQI_AIQ_PIO   = 0x0048, // Admin IQ PI Offset, 64bit
    PQI_AOQ_CIO   = 0x0050, // Admin OQ CI Offset, 64bit
    PQI_AIQ_EAA   = 0x0058, // Admin IQ Element Array Address, 64bit
    PQI_AOQ_EAA   = 0x0060, // Admin OQ Element Array Address, 64bit
    PQI_AIQ_CIA   = 0x0068, // Admin IQ CI Address, 64bit
    PQI_AOQ_PIA   = 0x0070, // Admin OQ PI Address, 64bit
    PQI_AQ_PARM   = 0x0078, // Admin Queue Parameter, 32bit
    PQI_DEV_ERR   = 0x0080, // PQI Device Error, 32bit
    PQI_DEV_ERRD  = 0x0088, // PQI Device Error Data, 64bit
    PQI_RESET     = 0x0090, // Reset, 32bit
    PQI_PWRACT    = 0x0094, // Power Action, 32bit
    PQI_END_REG   = 0x00FF, 
    // end of "standard registers"

    // Start of "Assigned Registers" ----------------------------------------++
    //
    // 64 IQ/OQ PI/CI regs - 0x0100 through 0x04FF)
    //.

    // base address for 64 IQ PI registers
    // where PQI_IQ_PI_BASE[0] is the admin IQ PI (at 0x0100)
    //                      . 
    //                      . 
    //                      . 
    // where PQI_OQ_CI_BASE[63] is the the 64th IQ PI (at 0x02F8)
    PQI_IQ_PI_BASE  = 0x0100,

    // base address for 64 OQ CI registers
    // where PQI_OQ_CI_BASE[0] is the admin OQ CI (at 0x0300)
    //                      . 
    //                      . 
    //                      . 
    // where PQI_OQ_CI_BASE[63] is the the 64th OQ CI (at 0x04F8)
    PQI_OQ_CI_BASE = 0x0300,

    // end of "Assigned Registers"------------------------------------------++

    PQI_END_ASSIGNED_REG   = 0x0500

};


#define AIQ_ID  0   // Admin in-bound queue ID == 0
#define AOQ_ID  0   // Admin out-bound queue ID == 0

#define PQI_IQ_PI_REG(qid) (PQI_IQ_PI_BASE + (qid * 8))
#define PQI_OQ_CI_REG(qid) (PQI_OQ_CI_BASE + (qid * 8))

#define MAX_Q_ID    (63)

// Admin queue element lengths
// Section 5.2.6
#define ADM_IQ_ELEMENT_LENGTH   (64)
#define ADM_OQ_ELEMENT_LENGTH   (64)


// Address mask for Administration IQ/OQ CI/PI memory addresses
// must hav the least significant 6-bits zero
// Example, section 5.2.15 (T10/2240-D, PQI specification)
//          bytes 0-7, ADMINISTRATOR IQ CI ADDRESS
// 
#define ADMIN_CIA_PIA_MASK (0xFFFFFFFFFFFFFFC0ul)

// Address mask for Operational (non-admin) IQ/OQ CI/PI memory addresses
// must have the least significant 2-bits zero
// Example, section 9.2.5.1 (T10/2240-D, PQI specification)
//          bytes 24-31, CREATE OPERATIONAL OQ request OQ PI ADDRESS
// 
#define OP_CIA_PIA_QC_MASK (0xFFFFFFFFFFFFFFFCul)

#define OP_OQ_INT_MESSAGE_NUMBER_MASK (0x07FF)

// Address mask for non-admin IQ/OQ Element Array memory addresses
// must hav the least significant 6-bits zero
// Example section 9.2.4.2 & 9.2.5.2 (T10/2240-D, PQI specification)
#define ELEMENT_ARRAY_ADDR_MASK (0xFFFFFFFFFFFFFFC0ul)


// Used to hold the Admin Queue state between resets

typedef struct AdminQueueState {

    uint64_t    aiq_pio;
    uint64_t    aoq_cio;
    uint64_t    aiq_eaa;
    uint64_t    aoq_eaa;
    uint64_t    aiq_cia;
    uint64_t    aoq_pia;
    uint32_t    aq_parm;
} AdminQueueState;


// Capability (BAR Register) - all ReadOnly
// Section 5.2.6 (T10/2240-D, PQI specification)
// TODO: BWS - Is this never used?
#pragma pack(push, 1)
typedef struct PQIControllerCapabilities {

    uint8_t  max_admin_iq_elem; // maximum administrator IQ elements  (2)
    uint8_t  max_admin_io_elem; // maximum administrator OQ elements  (2)
    uint8_t  admin_iq_elem_len; // administrator IQ element length    (4)
    uint8_t  admin_oq_elem_len; // administrator OQ element length    (4)
    uint16_t max_reset_timeout; // max time out for reset             (2)
    uint16_t res0;              // reserved                           (0)
} PQIControllerCapabilities;


// PQI Device Status (BAR Register)
// Section 5.2.10 (T10/2240-D, PQI specification)

typedef struct PQIDeviceStatus {

  uint32_t fullreg;
  
  struct {

    uint8_t  state    :4; // PQI device state      (0)
    uint8_t  res0     :4; // reserved              (0)
    uint8_t  opIqErr  :1; // bit 0 OP OQ error     (0)
    uint8_t  opOqErr  :1; // bit 1 - OP IQ error   (0)
    uint8_t  res1     :6; // bits 2-7              (0)
    uint8_t  res2;        // reserved              (0)
  
  } fields;

} PQIDeviceStatus;


// Administrator Queue Parameter (BAR Register)
// Section 5.2.17 (T10/2240-D, PQI specification)

typedef union PQIAdminQueueParmLayout {

  uint32_t fullreg;
  
  struct {

    uint8_t  numaiqelem;
    uint8_t  numaoqelem;
    uint16_t msixEntry  :11;
    uint16_t res0       :5;
  
  } fields;

} PQIAdminQueueParmLayout;


// PQI Device Error (BAR Register)
// Section 5.2.18 (T10/2240-D, PQI specification)

typedef union PQIDeviceError {

  uint32_t fullreg;
  
  struct {

    uint8_t errorCode;
    uint8_t errorCodeQualifier;
    uint8_t bytePointer;
    uint8_t RsvdZ              :3;
    uint8_t bitPointer         :3;
    uint8_t BPV                :1;
    uint8_t errorDataValid     :1;
  
  } fields;

} PQIDeviceError;


// PQI Reset (BAR Register)
// Section 5.2.20 (T10/2240-D, PQI specification)

typedef union PQIReset {

  uint32_t fullreg;

  struct {

    uint8_t  resetType          :3;
    uint8_t  RsvZ1              :2;
    uint8_t  resetAction        :3;
    uint8_t  holdInPD1          :1;
    uint8_t  RsvdZ2             :7;
    uint16_t RsvdZ3;

  } fields;

} PQIReset;

// Reset Actions
#define NO_ACTION             0
#define START_RESET           1
#define START_RESET_COMPLETED 2

// Reset Types
#define NO_RESET   0
#define SOFT_RESET 1
#define FIRM_RESET 2
#define HARD_RESET 3


// SGL Descriptor
// Section 7.3.1 (T10/2240-D, PQI specification)

typedef struct sglDesc {

    union desc
    {
        struct data {

            uint64_t address;       // Most significant 64-bits
            //uint8_t  length;
            uint32_t  length;
            
        }data;
        
        struct bitBucket {
            
            uint8_t reserved[8];
            uint32_t length;

        }bitBucket;
        
        struct std {
            
            uint64_t address;       // Most significant 60-bits
            uint32_t length;

        }std;
        
        struct stdLast {

            uint64_t address;       // Most significant 60-bits
            uint32_t length;
            
        }stdLast;
        
        struct altLast {
            
            uint64_t address;       // Most significant 62-bits
            uint32_t length;
            
        }altLast;

    }desc;
    
    uint8_t  reserved[3];
    uint8_t  zero   :4;
    uint8_t  type   :4;

} sglDesc;


// SGL descriptor types
// Section 7.3.1 (T10/2240-D, PQI specification)
enum {

    SGL_DATA_BLOCK                  = 0x00,
    SGL_BIT_BUCKET                  = 0x01,
    SGL_STANDARD_SEGMENT            = 0x02,
    SGL_STANDARD_LAST_SEGMENT       = 0x03,
    SGL_ALTERNATIVE_LAST_SEGMENT    = 0x04,    
    VENDOR_SPECIFIC                 = 0x0F,    

};

#define SGL_LENGTH(s) \
((s)->type == SGL_DATA_BLOCK ? (s)->desc.data.length : \
 (s)->type == SGL_BIT_BUCKET ? (s)->desc.bitBucket.length : \
 (s)->type == SGL_STANDARD_SEGMENT ? (s)->desc.std.length : \
 (s)->type == SGL_STANDARD_LAST_SEGMENT ? (s)->desc.stdLast.length : \
 (s)->type == SGL_ALTERNATIVE_LAST_SEGMENT ? (s)->desc.altLast.length : 0)






// IQ IU header
// Section 8.4 

typedef struct iuHeader {

    uint8_t  type;
    uint8_t  feat;
    uint16_t length;
  
} iuHeader;


// Status Field definitions for Administrator command responses
//  section 9.1.3.1

enum {

    ADM_STAT_GOOD                               = 0x00,
    ADM_STAT_DATA_IN_BUF_UNDERFLOW              = 0x01,     // See 9.1.3.2

    // 02 to 3Fh Reserved

    // Results indicating administrator function failure (40h to FFh)
    // Errors accessing the Data Buffer (40h to 7Fh)

    // Miscellaneous errors accessing the Data Buffer (40h to 5Fh)
    ADM_STAT_DATA_BUF_ERROR                     = 0x40,
    ADM_STAT_DATA_BUF_OVERFLOW                  = 0x41,

    // PCI Express related errors accessing the Data Buffer (60h to 6Fh)
    ADM_STAT_PCIE_FABRIC_ERROR                  = 0x60,
    ADM_STAT_PCIE_COMPLETION_TIMEOUT            = 0x61,
    ADM_STAT_PCIE_COMPLETER_ABORT               = 0x62,
    ADM_STAT_PCIE_POISONED_TLP_RECEIVED         = 0x63,
    ADM_STAT_PCIE_ECRC_CHECK_FAILED             = 0x64,
    ADM_STAT_PCIE_UNSUPPORTED_REQ               = 0x65,
    ADM_STAT_PCIE_ACS_VIOLATION                 = 0x66,
    ADM_STAT_PCIE_TLP_PREFIX_BLOCKED            = 0x67,
    // 0x68 to 0x6F Reserved

    // Other errors accessing the Data Buffer (70h to 7Fh)
    // 0x70 to 0x7F Reserved

    // Other errors (80h to EFh)
    ADM_STAT_GENERIC_ERROR                       = 0x80,
    ADM_STAT_OVERLAPPED_REQ_IDENTIFIER_ATTEMPTED = 0x81,
    ADM_STAT_INVALID_FIELD_IN_REQ_IU             = 0x82,    // See 9.1.3.3
    ADM_STAT_INVALID_FIELD_IN_DATA_OUT_BUF       = 0x83     // See 9.1.3.4
    // 0x84 to 0xEF Reserved

    // Vendor specific (F0h to FFh)
    // 0xF0 to 0xFF Vendor-specific

};


// PQI Administrator Functions 
// (i.e. the "Function Code" field of inbound queue elements)

// Section 9.2.1 (T10/2240-D, PQI specification)

enum {

    REPORT_PQI_DEV_CAPABLIITY   = 0x00,
    REPORT_MANUFACTURING_INFO   = 0x01,
    CREATE_OPERATIONAL_IQ       = 0x10,
    CREATE_OPERATIONAL_OQ       = 0x11,
    DELETE_OPERATIONAL_IQ       = 0x12,    
    DELETE_OPERATIONAL_OQ       = 0x13,    
    CHANGE_OPERATIONAL_IQ_PROP  = 0x14,    
    CHANGE_OPERATIONAL_OQ_PROP  = 0x15,    
    REPORT_OPERATIONAL_IQ_LIST  = 0x16,    
    REPORT_OPERATIONAL_OQ_LIST  = 0x17     

};

#define ADMIN_IU_REQUEST    0x60
#define ADMIN_IU_RESPONSE   0xE0


// Section 9.2.2.1 (T10/2240-D, PQI specification)

typedef struct reportPqiDevCapReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC[33];
    uint32_t    dataInBufferSize;
    sglDesc     sglDescriptor;

}reportPqiDevCapReq;


// Section 9.2.2.2 (T10/2240-D, PQI specification)

typedef struct reportPqiDevCapRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint8_t     Reserved[48];

}reportPqiDevCapRsp;


// Section 9.2.2.3 (T10/2240-D, PQI specification)

typedef struct reportPqiDevCapParmData {

    uint16_t    length;
    uint8_t     reserved1[14];
    uint16_t    maxOpIqs;
    uint16_t    maxOpIqElements;
    uint32_t    reserved2;
    uint16_t    maxOpIqElementLength;
    uint16_t    minOpIqElementLength;
    uint8_t     CIC         :1;
    uint8_t     reserved3   :7;
    uint8_t     reserved4;
    uint16_t    maxOpOqs;
    uint16_t    maxOpOqElements;
    uint16_t    intCoalescingTimeGran;
    uint16_t    maxOpOqElementLength;
    uint16_t    minOpOqElementLengty;
    uint8_t     opIqElementArrayAddrAlignmentExp;
    uint8_t     opOqElementArrayAddrAlignmentExp;
    uint8_t     opIqCiAddrAlignmentExp;
    uint8_t     opOqPiAddrAlignmentExp;
    uint32_t    opQueProtocolSupportBitmask;
    uint16_t    adminSglDescTypeSupportBitmask;
    uint8_t     reserved5[14];

}reportPqiDevCapParmData;


// Section 9.2.3.1 (T10/2240-D, PQI specification)

typedef struct reportManInfoReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC_3[33];
    uint32_t    dataInBufferSize;
    sglDesc     sglDescriptor;

}reportManInfoReq;


// Section 9.2.3.2 (T10/2240-D, PQI specification)

typedef struct reportManInfoRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint8_t     RsvdC_3[48];

}reportManInfoRsp;


// Section 9.2.3.3 (T10/2240-D, PQI specification)

typedef struct reportManInfoPrmData {

    uint16_t    length;
    uint16_t    reserved1;
    uint16_t    pciVendorId;
    uint16_t    pciDeviceId;
    uint8_t     pciRevisionId;
    uint8_t     pciProgInterface;
    uint16_t    pciClassCode;
    uint16_t    pciSubsystemVendorId;
    uint16_t    pciSubsystemId;
    uint8_t     productSerialNumber[32];
    uint8_t     T10VendorId[8];
    uint8_t     productId[16];
    uint8_t     productRevLevel[16];
    uint8_t     reserved2[40];


}reportManInfoPrmData;


// Section 9.2.4.1 (T10/2240-D, PQI specification)

typedef struct createOpIqReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC_1;
    uint16_t    iqId;
    uint16_t    RsvdC_2;
    uint64_t    iqElementArrayAddress;
    uint64_t    iqCiAddress;
    uint16_t    numberOfElements;
    uint16_t    elementLength;
    uint8_t     opQueueProtocol;
    uint8_t     RsvdC_3[23];
    uint32_t    vendorSpecific;

}createOpIqReq;


// Section 9.2.4.2 (T10/2240-D, PQI specification)

typedef struct createOpIqRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint64_t    iqPiOffset;
    uint8_t     reserved[40];

}createOpIqRsp;


// Section 9.2.5.1 (T10/2240-D, PQI specification)

typedef struct createOpOqReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC_1;
    uint16_t    oqId;
    uint16_t    RsvdC_2;
    uint64_t    oqElementArrayAddress;
    uint64_t    oqPiAddress;
    uint16_t    numberOfElements;
    uint16_t    elementLength;
    uint16_t    intMsgNumber;
    uint8_t     wairForRearm;
    uint8_t     RsvdC3;
    uint16_t    coalescingCount;
    uint16_t    minCoalescingTime;
    uint16_t    maxCoalescingTime;
    uint8_t     opQueueProtocol;
    uint8_t     RsvdC_5[13];
    uint32_t    vendorSpecific;

}createOpOqReq;


// Section 9.2.5.2 (T10/2240-D, PQI specification)

typedef struct createOpOqRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint64_t    oqCiOffset;
    uint8_t     reserved[40];

}createOpOqRsp;


// Section 9.2.6.1 (T10/2240-D, PQI specification)

typedef struct deleteOpIqReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC_1;
    uint16_t    iqId;
    uint16_t    RsvdC_2[50];

}deleteOpIqReq;


// Section 9.2.6.2 (T10/2240-D, PQI specification)

typedef struct deleteOpIqRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint8_t     reserved[48];

}deleteOpIqRsp;


// Section 9.2.7.1 (T10/2240-D, PQI specification)

typedef struct deleteOpOqReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC_1;
    uint16_t    oqId;
    uint8_t     RsvdC_2[50];

}deleteOpOqReq;


// Section 9.2.7.2 (T10/2240-D, PQI specification)

typedef struct deleteOpOqRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint8_t     reserved[48];

}deleteOpOqRsp;



// Section 9.2.8.1 (T10/2240-D, PQI specification)

typedef struct changeOpIqPropReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC_1;
    uint16_t    iqId;
    uint8_t     RsvdC_2[46];
    uint32_t    vendorSpecific;

}changeOpIqPropReq;


// Section 9.2.8.2 (T10/2240-D, PQI specification)

typedef struct changeOpIqPropRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint8_t     reserved[48];

}changeOpIqPropRsp;


// Section 9.2.9.1 (T10/2240-D, PQI specification)

typedef struct changeOpOqPropReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC_1;
    uint16_t    oqId;
    uint32_t    RsvdC_2;
    uint8_t     wairForRearm;
    uint8_t     RsvdC3;
    uint16_t    coalescingCount;
    uint16_t    minCoalescingTime;
    uint16_t    maxCoalescingTime;
    uint8_t     RsvdC_5[34];
    uint32_t    vendorSpecific;

}changeOpOqPropReq;


// Section 9.2.9.2 (T10/2240-D, PQI specification)

typedef struct changeOpOqPropRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint8_t     reserved[48];

}changeOpOqPropRsp;


// Section 9.2.10.1 (T10/2240-D, PQI specification)

typedef struct reportOpIqListReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC[33];
    uint32_t    dataInBufferSize;
    sglDesc     sglDescriptor;

}reportOpIqListReq;


// Section 9.2.10.2 (T10/2240-D, PQI specification)

typedef struct reportOpIqListRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint8_t     reserved[48];

}reportOpIqListRsp;


// Section 9.2.10.3 (T10/2240-D, PQI specification)

typedef struct reportOpIqListParmDataHeader {

    uint8_t  reserved[6];
    uint16_t number;

}reportOpIqListParmDataHeader;


// Section 9.2.10.3 (T10/2240-D, PQI specification)

typedef struct reportOpIqPropDescriptor {

    uint8_t     reserved1[12];
    uint16_t    iqId;
    uint8_t     iqError     :1;
    uint8_t     reserved2   :7;
    uint8_t     reserved3;
    uint64_t    iqElementArrayAddress;
    uint64_t    iqCiAddress;
    uint16_t    numberOfElements;
    uint16_t    elementLength;
    uint8_t     protocol    :5;
    uint8_t     reserved4   :3;
    uint8_t     reserved5[23];
    uint32_t    vendorSpecific;
    uint64_t    iqPiOffset;
    uint8_t     reserved6[56];

}reportOpIqPropDescriptor;



// Section 9.2.11.1 (T10/2240-D, PQI specification)

typedef struct reportOpOqListReq {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     RsvdC_1;
    uint32_t    dataInBufferSize;
    sglDesc     sglDescriptor;

}reportOpOqListReq;


// Section 9.2.11.2 (T10/2240-D, PQI specification)

typedef struct reportOpOqListRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint8_t     functionCode;
    uint8_t     status;
    uint32_t    additionalStatusDescriptor;
    uint8_t     reserved[48];

}reportOpOqListRsp;


// Section 9.2.11.3 (T10/2240-D, PQI specification)

typedef struct reportOpOqListParmDataHeader {

    uint8_t     reserved[6];
    uint16_t    number;

}reportOpOqListParmDataHeader;


// Section 9.2.11.3 (T10/2240-D, PQI specification)

typedef struct reportOpOqPropDescriptor {

    uint8_t     reserved1[12];
    uint16_t    oqId;
    uint8_t     oqError             :1;
    uint8_t     reserved2           :7;
    uint8_t     reserved3;
    uint64_t    oqElementArrayAddress;
    uint64_t    oqPiAddress;
    uint16_t    numberOfElements;
    uint16_t    elementLength;
    uint16_t    intMessageNumber    :11;
    uint16_t    reserved4           :5;
    uint8_t     waitForRearm        :1;
    uint8_t     reserved5           :7;
    uint8_t     reserved6;
    uint16_t    coalescingCount;
    uint16_t    minCoalescingTime;
    uint16_t    maxCoalescingTime;
    uint8_t     protocol;
    uint8_t     reserved7[13];
    uint32_t    vendorSpecific;
    uint64_t    oqCiOffset;
    uint8_t     reserved8[56];

}reportOpOqPropDescriptor;
#pragma pack(pop)



typedef struct PQIInboundQueue {

    uint16_t    id;         // use '0' for admin queue
    uint32_t    pi;
    uint64_t    ci_addr;
    uint32_t    ci_work;
    uint32_t    ci_local;
    uint64_t    ea_addr;    // DMA Address
    uint16_t    length;     // element length
    uint8_t     protocol;   // Operational Queue Protocol
    uint16_t    size;       // number of elements in the queue
    uint32_t    vendor;

} PQIInboundQueue;


typedef struct PQIOutboundQueue {

    uint16_t    id;             // use '0' for admin queue
    uint64_t    pi_addr;    
    uint32_t    pi_work;    
    uint32_t    pi_local;   
    uint32_t    ci;         
    uint64_t    ea_addr;        // DMA Address
    uint16_t    length;         // element length
    uint8_t     protocol;       // Operational Queue Protocol
    uint16_t    size;           // number of elements in the queue
    uint16_t    msixEntry;
    uint8_t     waitForRearm;
    uint16_t    coCount;        // coalescing info
    uint16_t    minCoTime;
    uint16_t    maxCoTime;
    uint32_t    vendor;

} PQIOutboundQueue;


// PQI state values, all values above 0x5 are reserved

typedef enum pqi_device_state {

    PQI_DEVICE_STATE_PD0 = 0x0,  // Power On And Reset
    PQI_DEVICE_STATE_PD1 = 0x1,  // Configuration Space Ready
    PQI_DEVICE_STATE_PD2 = 0x2,  // BAR Registers Ready
    PQI_DEVICE_STATE_PD3 = 0x3,  // Administrator Queue Pair Ready
    PQI_DEVICE_STATE_PD4 = 0x4   // Error

} pqi_device_state;


enum {

  PQI_REGISTERS_UNLOCKED = 0x0,
  PQI_REGISTERS_LOCKED   = 0x1

};


// PQI Administrator Queue Configuration Function

typedef enum pqi_admin_queue_config_Function {

    CREATE_ADMIN_QUEUE_PAIR = 0x01, // Administrator function: Create Admin Queue Pair
    DELETE_ADMIN_QUEUE_PAIR = 0x02  // Administrator function: Delete Admin Queue Pair

} pqi_admin_queue_config_Function;


// PQI Administrator Queue Configuration Function

typedef enum pqi_admin_queue_status_code {

    IDLE                        = 0x00, // Administrator status: idle (not creating/deleting)
    CREATING_ADMIN_QUEUE_PAIR   = 0x01, // Administrator status: creating Admin Queue Pair
    DELETING_ADMIN_QUEUE_PAIR   = 0x02  // Administrator status: deleting Admin Queue Pair

} pqi_admin_queue_status_code;


typedef struct PQIStateMachine {

  pqi_device_state state;
  uint16_t reglock;

} PQIStateMachine;


typedef struct DiskInfo {

    int fd;
    int mfd;
    int lunid;
    size_t mapping_size;
    uint8_t *mapping_addr;

    size_t meta_mapping_size;
    uint8_t *meta_mapping_addr;

} DiskInfo;


typedef struct PQIState {

    PCIDevice dev;

    //int mmio_index;
    MemoryRegion mem_region;
    MemoryRegion mem_region_mmio;
    pcibus_t bar0;
    int bar0_size;
    uint8_t nvectors;

    // Space for PQI Control Space
    uint8_t *cntrl_reg;

    // Masks for PQI Control Registers
    uint8_t *rw_mask; // RW/RO mask
    uint8_t *rwc_mask; // RW1C mask
    uint8_t *rws_mask; // RW1S mask
    uint8_t *used_mask; // Used/Reserved mask

    //BWS struct pqi_features feature;

    struct PQIStateMachine sm;

    pqi_admin_queue_status_code adminQueueStatus;

    PQIOutboundQueue oq[PQI_MAX_QS_ALLOCATED];
    PQIInboundQueue iq[PQI_MAX_QS_ALLOCATED];

    DiskInfo *disk;
    uint32_t lun_size;
    uint32_t num_luns;
    uint32_t instance;
    char*    working_dir; // storage backing files are stored here

    time_t start_time;

    // Used to store the Admin queue offsets between resets
    struct AdminQueueState aqstat;

    // Used for PIN based and MSI interrupts
    uint32_t intr_vect;
    // Page Size used by the hardware
    uint32_t page_size;

} PQIState;


// Structure used for default initialization sequence

struct PQIReg {

  uint32_t offset;    // Offset in PQI space
  uint32_t len;       // len in bytes
  uint32_t reset;     // reset value
  uint32_t rw_mask;   // RW/RO mask
  uint32_t rwc_mask;  // RW1C mask
  uint32_t rws_mask;  // RW1S mask

};


// static struct for default initialization sequence
// TODO: BWS - update the mask values here!

static const struct PQIReg pqi_reg[] = {

    {   .offset   = PQI_SIG,
        .len      = 0x04,
        .reset    = 0x20495150,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_SIG+4,
        .len      = 0x04,
        .reset    = 0x47455244,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_AQ_CONFIG,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x000000FF,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },
    
    {   .offset   = PQI_AQ_CONFIG+4,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_CAP,
        .len      = 0x04,
        .reset    = 0x04040202,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },
    
    {   .offset   = PQI_CAP+4,
        .len      = 0x04,
        .reset    = 0x00000002,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_INTS,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_INTMS,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000001
    },

    {   .offset   = PQI_INTMC,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000001,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_STATUS,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x0000030F,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_AIQ_PIO,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },
    
    {   .offset   = PQI_AIQ_PIO+4,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_AOQ_CIO,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },
    
    {   .offset   = PQI_AOQ_CIO+4,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_AIQ_EAA,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0xFFFFFFC0,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },
    
    {   .offset   = PQI_AIQ_EAA+4,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0xFFFFFFFF,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_AOQ_EAA,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0xFFFFFFC0,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },
    
    {   .offset   = PQI_AOQ_EAA+4,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0xFFFFFFFF,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_AIQ_CIA,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0xFFFFFFC0,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },
    
    {   .offset   = PQI_AIQ_CIA+4,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0xFFFFFFFF,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_AOQ_PIA,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0xFFFFFFC0,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },
    
    {   .offset   = PQI_AOQ_PIA+4,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0xFFFFFFFF,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_AQ_PARM,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x07FFFFFF,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_DEV_ERR,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_DEV_ERRD,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },
    
    {   .offset   = PQI_DEV_ERRD+4,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00000000,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_RESET,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x000001E7,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    },

    {   .offset   = PQI_PWRACT,
        .len      = 0x04,
        .reset    = 0x00000000,
        .rw_mask  = 0x00001FFF,
        .rwc_mask = 0x00000000,
        .rws_mask = 0x00000000
    }

};


// Config File Read Strucutre

typedef struct FILERead {

    uint32_t offset;
    uint32_t len;
    uint32_t val;
    uint32_t ro_mask;
    uint32_t rw_mask;
    uint32_t rwc_mask;
    uint32_t rws_mask;
    char *cfg_name;
} FILERead;


enum {PCI_SPACE = 0, PQI_SPACE = 1};

// Storage Disk
int pqi_create_storage_disks(PQIState *n);
int pqi_create_storage_disk(uint32_t instance, uint32_t nsid, DiskInfo *disk, PQIState *n);
int pqi_close_storage_disks(PQIState *n);
int pqi_close_storage_disk(DiskInfo *disk);

void pqi_dma_mem_read(hwaddr addr, uint8_t *buf, int len);
void pqi_dma_mem_write(hwaddr addr, uint8_t *buf, int len);

// Config file read functions
int pqi_read_config_file(FILE *, PQIState *, uint8_t);

// Functions for PQI Controller space reads and writes
uint32_t pqi_cntrl_read_config(PQIState* pqiDev, hwaddr addr, uint8_t len);
void pqi_cntrl_write_config(PQIState *, hwaddr, uint32_t, uint8_t);
void pqi_reset_request(PQIState* pqiDev, uint32_t val);

// Functions for IQ/OQ queue processing
void process_iq_event(PQIState* pqiDev, uint32_t qid);
void process_oq_event(PQIState* pqiDev, uint32_t qid);

// Functions for admin IQ and OQ message processing
void sop_execute_admin_command(PQIState* pqiDev, PQIInboundQueue* iq, uint16_t ci);
void admin_report_caps(PQIState* pqiDev, reportPqiDevCapReq* iu);
void admin_report_man_info(PQIState* pqiDev, reportManInfoReq* iu);
uint32_t copy_sgl(sglDesc* sgl, uint8_t* pData, uint32_t len, uint8_t dir);
uint32_t copy_from_sgl(sglDesc* sgl, uint8_t* pData, uint32_t len);
uint32_t copy_to_sgl(sglDesc* sgl, uint8_t* pData, uint32_t len);
sglDesc * download_sgl_segment(hwaddr address, uint32_t len);
void admin_create_op_iq(PQIState* pqiDev, createOpIqReq* iu);
void admin_create_op_oq(PQIState* pqiDev, createOpOqReq* iu);
void admin_delete_op_iq(PQIState* pqiDev, deleteOpIqReq* iu);
void admin_delete_op_oq(PQIState* pqiDev, deleteOpOqReq* iu);
void admin_change_op_iq_props(PQIState* pqiDev, changeOpIqPropReq* iu);
void admin_change_op_oq_props(PQIState* pqiDev, changeOpOqPropReq* iu);
void admin_report_op_iq_list(PQIState* pqiDev, reportOpIqListReq* iu);
void admin_report_op_oq_list(PQIState* pqiDev, reportOpOqListReq* iu);

void admin_create_op_iq_response(PQIState* pqiDev, createOpIqReq* iu, uint32_t  status, uint32_t  add_status);
void admin_create_op_oq_response(PQIState* pqiDev, createOpOqReq* iu, uint32_t  status, uint32_t  add_status);
void admin_delete_op_iq_response(PQIState* pqiDev, deleteOpIqReq* iu, uint32_t  status, uint32_t  add_status);
void admin_delete_op_oq_response(PQIState* pqiDev, deleteOpOqReq* iu, uint32_t  status, uint32_t  add_status);
void admin_change_op_iq_response(PQIState* pqiDev, changeOpIqPropReq* iu, uint32_t  status, uint32_t  add_status);
void admin_change_op_oq_response(PQIState* pqiDev, changeOpOqPropReq* iu, uint32_t  status, uint32_t  add_status);
void admin_report_op_iq_response(PQIState* pqiDev, reportOpIqListReq* iu, uint32_t status, uint32_t  add_status);
void admin_report_op_oq_response(PQIState* pqiDev, reportOpOqListReq* iu, uint32_t status, uint32_t  add_status);


void sop_execute_sop_command(PQIState* pqiDev, uint32_t qid, uint16_t ci);


uint16_t get_iq_pi(PQIState* pqiDev, uint16_t qid);
uint16_t get_iq_ci(PQIState* pqiDev, uint16_t qid);
void set_iq_ci(PQIState* pqiDev, uint16_t qid, uint16_t ciVal);
uint16_t get_oq_ci(PQIState* pqiDev, uint16_t qid);
uint16_t get_oq_pi(PQIState* pqiDev, uint16_t qid);
void set_oq_pi(PQIState* pqiDev, uint16_t qid, uint16_t piVal);
void post_to_oq (PQIState* pqiDev, int qid, void *iu, int length);


// DEBUG PRINT LOGGING macros
//
#define SOP_APPNAME         "qsop"
#define SOP_LEVEL           SOP_APPNAME

// LOG TYPE BITS
//
#define SOP_LOG_NORMAL      1
#define SOP_LOG_DEBUG       2
#define SOP_LOG_ERROR       4

// THE 8 POSSIBLE LOG MODES
//
#define SOP_LOG_NONE            (      0        |      0        |      0         )
#define SOP_LOG_NRM_ONLY        (      0        |      0        | SOP_LOG_NORMAL )
#define SOP_LOG_DBG_ONLY        (      0        | SOP_LOG_DEBUG |      0         )
#define SOP_LOG_ERR_ONLY        ( SOP_LOG_ERROR |      0        |      0         )
#define SOP_LOG_DBG_AND_NRM     (      0        | SOP_LOG_DEBUG | SOP_LOG_NORMAL )
#define SOP_LOG_ERR_AND_NRM     ( SOP_LOG_ERROR |      0        | SOP_LOG_NORMAL )
#define SOP_LOG_ERR_AND_DBG     ( SOP_LOG_ERROR | SOP_LOG_DEBUG |      0         )
#define SOP_LOG_ALL             ( SOP_LOG_ERROR | SOP_LOG_DEBUG | SOP_LOG_NORMAL )

// LOG MODE SELECTION
//
//#define SOP_LOG_MODE    SOP_LOG_ERR_AND_NRM
//#define SOP_LOG_MODE    SOP_LOG_ALL
#define SOP_LOG_MODE SOP_LOG_ERROR

#if ((SOP_LOG_MODE & SOP_LOG_NORMAL) != 0)
  #define SOP_LOG_NORM(fmt, ...) printf("%s: " fmt "\n", SOP_LEVEL, ## __VA_ARGS__)
#else
  #define SOP_LOG_NORM(fmt, ...)
//  #define SOP_LOG_NORM(fmt, ...) fprintf((FILE*)0, "%s: " fmt "\n", SOP_LEVEL, ## __VA_ARGS__)
#endif  // (SOP_LOG_MODE & SOP_LOG_NORMAL)

#if ((SOP_LOG_MODE & SOP_LOG_DEBUG) != 0)
  #define SOP_LOG_DBG(fmt, ...) printf("%s-DBG:%s:%d: " fmt "\n", SOP_LEVEL, __FILE__, __LINE__, ## __VA_ARGS__)
#else
  #define SOP_LOG_DBG(fmt, ...)
//  #define SOP_LOG_DBG(fmt, ...) fprintf((FILE*)0, "%s-DBG:%s:%d: " fmt "\n", SOP_LEVEL, __FILE__, __LINE__, ## __VA_ARGS__)
#endif  // (SOP_LOG_MODE & SOP_LOG_NORMAL)

#if ((SOP_LOG_MODE & SOP_LOG_ERROR) != 0)
  #define SOP_LOG_ERR(fmt, ...) printf("%s-ERR:%s:%d: " fmt "\n", SOP_LEVEL, __FILE__, __LINE__, ## __VA_ARGS__)
#else
  #define SOP_LOG_ERR(fmt, ...)
// #define SOP_LOG_ERR(fmt, ...) fprintf((FILE*)0, "%s-ERR:%s:%d: " fmt "\n", SOP_LEVEL, __FILE__, __LINE__, ## __VA_ARGS__)
#endif  // (SOP_LOG_MODE & SOP_LOG_NORMAL)



#pragma pack(push,1)
typedef struct sopLimitedCommandIU {
#define SOP_LIMITED_CMD_IU	0x10
	iuHeader header;
	uint16_t queue_id;
	uint16_t work_area;
	uint16_t request_id;
	uint8_t  direction :2;
	uint8_t  partial :1;
	uint8_t  res   :5;
//	uint8_t flags;
#define SOP_DATA_DIR_NONE		0x00
#define SOP_DATA_DIR_FROM_DEVICE	0x01
#define SOP_DATA_DIR_TO_DEVICE		0x02
//#define SOP_DATA_DIR_RESERVED		0x03
//#define SOP_PARTIAL_DATA_BUFFER		0x04
	uint8_t reserved;
	uint32_t xfer_size;
	uint8_t cdb[16];
	sglDesc sg[2];
} sopLimitedCommandIU;
#pragma pack(pop)

#define print_cdb(r) { \
	int i=0; \
	uint8_t *b=(uint8_t *)&r->cdb[0];\
    printf("IU ..\n"); \
    for(i=0;i<16;i++) {\
        printf("%02x ",*b++);\
        if (!((i+1) % 8))\
        	printf("\n");\
    }\
    printf("\n");\
}
#define print_bytes(label,p,l) { \
	int i=0; \
	uint8_t *b=(uint8_t *)p;\
    printf("%s ..\n",label); \
    for(i=0;i<l;i++) {\
        printf("%02x ",*b++);\
        if (!((i+1) % 8))\
        	printf("\n");\
    }\
    printf("\n");\
}

#pragma pack(push,1)
typedef struct sopCommandRspIU {
#define SOP_CMD_RSP_IU	0x91
	iuHeader header;
	uint16_t queue_id;
	uint16_t work_area;
	uint16_t request_id;
	uint16_t nexus_id;
	uint8_t data_in_xfer_result;
	uint8_t data_out_xfer_result;
	uint8_t res[3];
	uint8_t status;
#define SOP_CHECK_CONDITION 0x02
	uint16_t qualifier;
#define SOP_ILLEGAL_REQUEST 0x05
} sopCommandRspIU;
#pragma pack(pop)


#pragma pack(push,1)
typedef struct inquiryCDB {
#define OP_INQUIRY	0x12
	uint8_t  op;
    uint8_t  res1  :6;
    uint8_t  obs   :1;
    uint8_t  evpo  :1;
    uint8_t  page_code;
    uint16_t alloc_len;
    uint8_t  control;
} inquiryCDB;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct read10CDB {
#define OP_READ_10	0x28
	uint8_t  op;
    uint8_t  rdprotect :3;
    uint8_t  dpo       :1;
    uint8_t  fua       :1;
    uint8_t  reserved  :2;
    uint8_t  reladr    :1;
    uint32_t lba;
    uint8_t  res2;
    uint16_t xfer_len;
    uint8_t  control;
} read10CDB;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct write10CDB {
#define OP_WRITE_10	0x2A
	uint8_t  op;
    uint8_t  wrprotect :3;
    uint8_t  dpo       :1;
    uint8_t  fua       :1;
    uint8_t  reserved  :1;
    uint8_t  fua_nv    :1;
    uint8_t  obsolete  :1;
    uint32_t lba;
    uint8_t  res2      :3;
    uint8_t  group     :5;
    uint16_t xfer_len;
    uint8_t  control;
} write10CDB;
#pragma pack(pop)

// CDBs
#define OP_TEST_UNIT_READY 0x0
#define OP_READ_CAPACITY   0x25

typedef struct sopSuccessRsp {

    iuHeader    header;
    uint32_t    qisd;
    uint16_t    requestIdentifier;
    uint16_t    nexusIdentifier;
    uint8_t     Reserved[4];

} sopSuccessRsp;

void sop_cdb_read_10(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r);
void sop_cdb_write_10(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r);
void sop_cdb_read_capacity(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r);
void sop_cdb_inquiry(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r);
void sop_cdb_test_unit_ready(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r);

#endif // SOP_H_
