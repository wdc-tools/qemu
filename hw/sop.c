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
//*****************************************************************************
//
//    Module      : sop
//
//    Function    : SCSI Over PCIe (SOP) device emulation
//                  QEMU device initialization is located at the bottom of 
//                  this file; "device_init(pqi_register_devices);"
//
//                  SCSI targets are communicated with via the PQI transfer
//                  layer. Information to/from the PQI queues are presented
//                  to/from the SOP host-interface for processing.
//
//    $Id: ih_mgr.hpp 304 2012-10-23 14:09:21Z RBenn7142835 $
//
//*****************************************************************************
//
// Mental Notes:
//
//  1. pc_piix.c - modified to include the following code:
//
//      if (pci_enabled) {
//          pc_sop_pqi_init(pci_bus);
//      }
//
//      this code was added in the "pc_init1()" function after all other 
//      standard PCI devices (e.g., vga, audio, network) were initialized in 
//      a like manner.
//
//  2. pc.c (and pc.h) - modified to add the "pc_sop_pqi_init()" function.
//     The following function was added after the point where the same kind 
//     of thing is done for the VGA hardware:
//
//          void pc_sop_pqi_init(PCIBus *pci_bus)
//          {
//              sop_pci_bus_init(pci_bus);
//          }
//
//  3. sop.c - modified to add the "sop_pci_bus_init()" function.
//
//          sop_pci_bus_init(pci_bus)
//
//     This function causes "qdev_create()" to be called for the "soppqi" 
//     named device. The device creation process causes the "pci_pqi_init()",
//     which is in this sop.c file, starts up the SOP device emulation.
//
//     Starting up SOP device emulation causes:
//      - the device state machine 
//      - memory allocation required for register emulation
//      - initializing PCI reg & BAR reg spaces
//      - etc...
//
//*****************************************************************************

#include "sop.h"
#include "range.h"
#include "pqi_constants.h"

static const VMStateDescription vmstate_soppqi = {.name = "soppqi", .version_id = 1};

// File Level scope functions
void sop_pci_bus_init(PCIBus *bus);
static void pqi_pci_space_init(PCIDevice* pciDev);
static void pqi_set_registry(PQIState* n);
static void qdev_pqi_reset(DeviceState* dev);
static void pqi_pci_write_config(PCIDevice* pciDev, uint32_t addr, uint32_t val, int len);
static uint32_t pqi_pci_read_config(PCIDevice* pciDev, uint32_t addr, int len);
static inline uint8_t range_covers_reg(uint64_t addr, uint64_t len, uint64_t reg, uint64_t regSize);
static void process_aq_config(PQIState* pqiDev);
static void register_error(PQIState*  pqiDev, uint16_t errorVal, uint8_t bytePtr, uint8_t bitPtr);

static void register_error(PQIState*  pqiDev, uint16_t errorVal, uint8_t bytePtr, uint8_t bitPtr) {

    SOP_LOG_NORM("%s(): pqiDev = 0x%08lu", __func__, (uint64_t)pqiDev);

    // Transition to an error state
    pqiDev->sm.state = PQI_DEVICE_STATE_PD4;

    // Write the value to the first word of the error register
    PQIDevErrReg*  dev_err_reg = (PQIDevErrReg* )(pqiDev->cntrl_reg + PQI_DEV_ERR);
    dev_err_reg->err_code_plus_qual = errorVal;

    // If the errorVal has a valid byte/bit pointer, place that in the register
    
    switch (errorVal) {

        case INVALID_PARAM_IN_PQI_REG:
        case ADMIN_QUEUE_PAIR_CREATE_DELETE_ERR:
        case PQI_SOFT_RESET_ERROR:
        case PQI_FIRM_RESET_ERROR:
        case PQI_HARD_RESET_ERROR:
            dev_err_reg->bpv = 1;
            dev_err_reg->byte_pointer = bytePtr;
            dev_err_reg->bit_pointer = bitPtr;
            break;
        default:
            dev_err_reg->bpv = 0;
            break;
    }

    SOP_LOG_NORM("%s() exit...", __func__);
}


static void process_aq_config(PQIState* pqiDev) {

    // this function should be called once the entire AQCF reg has been written
    SOP_LOG_NORM("%s(): pqiDev = 0x%08lu", __func__, (uint64_t)pqiDev);

    // Extract (and validate) the configuration register
    PQIAQConfigReg*  aq_config_reg = (PQIAQConfigReg*)(pqiDev->cntrl_reg + PQI_AQ_CONFIG);

    switch (aq_config_reg->function_status_code) {

    case CREATE_ADMIN_QUEUE_PAIR:

        SOP_LOG_NORM(" AQ Function: 0x%02x - CREATE_ADMIN_QUEUE_PAIR", aq_config_reg->function_status_code);
	
        // Queue Idle & PCIe BAR Registers Ready
        if ((pqiDev->adminQueueStatus == IDLE) && (pqiDev->sm.state == PQI_DEVICE_STATE_PD2)) {

            // set Admin Queue status
            pqiDev->adminQueueStatus = CREATING_ADMIN_QUEUE_PAIR;

            // Create the IQ queue (based on register data set by host)
            // Section 4.2.3.2 (Creating the administrator queue pair)

            // get "Administrator Queue Parameter" values into local IQ/OQ structs
            PQIAdminQueueParmLayout aqparm;

            aqparm = (PQIAdminQueueParmLayout)pqi_cntrl_read_config(pqiDev, PQI_AQ_PARM, DWORD);
            pqiDev->oq[AIQ_ID].size = aqparm.fields.numaoqelem;
            pqiDev->oq[AIQ_ID].msixEntry = aqparm.fields.msixEntry;

            // setup device admin IQ 
            PQIInboundQueue* iq = &pqiDev->iq[AIQ_ID];
            iq->id = AIQ_ID;
            iq->pi = 0;                                                         // IQ PI local copy 
            pqi_cntrl_write_config(pqiDev, PQI_IQ_PI_REG(AIQ_ID), 0, DWORD);    // IQ PI (actual)
            iq->ci_addr  = pqi_cntrl_read_config(pqiDev, PQI_AIQ_CIA, DWORD) & 0xFFFFFFC0;
            iq->ci_addr += (uint64_t)(pqi_cntrl_read_config(pqiDev, PQI_AIQ_CIA + 4, DWORD)) << 32;
            iq->ci_work  = 0;
            iq->ci_local = 0;
            iq->ea_addr  = pqi_cntrl_read_config(pqiDev, PQI_AIQ_EAA, DWORD);
            iq->ea_addr += (uint64_t)(pqi_cntrl_read_config(pqiDev, PQI_AIQ_EAA + 4, DWORD)) << 32;
            iq->size = aqparm.fields.numaiqelem;
            iq->length = ADM_IQ_ELEMENT_LENGTH;

            uint64_t queRegAddr = (uint64_t)(PQI_IQ_PI_REG(AIQ_ID));
            uint32_t reglower = (uint32_t)queRegAddr;
            uint32_t regupper = (uint32_t)(queRegAddr >> 32);
            pqi_cntrl_write_config(pqiDev, PQI_AIQ_PIO, reglower, DWORD);
            pqi_cntrl_write_config(pqiDev, PQI_AIQ_PIO + 4, regupper, DWORD);


            // setup device admin OQ
            PQIOutboundQueue* oq = &pqiDev->oq[AOQ_ID];
            oq->id = AOQ_ID;
            oq->pi_addr  = pqi_cntrl_read_config(pqiDev, PQI_AOQ_PIA, DWORD) & 0xFFFFFFC0;
            oq->pi_addr += (uint64_t)pqi_cntrl_read_config(pqiDev, PQI_AOQ_PIA + 4, DWORD) << 32;
            oq->pi_work  = 0;
            oq->pi_local = 0;
            oq->ci = 0;                                                         // OQ CI local copy
            pqi_cntrl_write_config(pqiDev, PQI_OQ_CI_REG(AOQ_ID), 0, DWORD);    // OQ CI (actual)
            oq->ea_addr  = pqi_cntrl_read_config(pqiDev, PQI_AOQ_EAA, DWORD);
            oq->ea_addr += (uint64_t)(pqi_cntrl_read_config(pqiDev, PQI_AOQ_EAA + 4, DWORD)) << 32;
            oq->length = ADM_OQ_ELEMENT_LENGTH;

            queRegAddr = (uint64_t)(PQI_OQ_CI_REG(AOQ_ID));
            reglower = (uint32_t)queRegAddr;
            regupper = (uint32_t)(queRegAddr >> 32);
            pqi_cntrl_write_config(pqiDev, PQI_AOQ_CIO, reglower, DWORD);
            pqi_cntrl_write_config(pqiDev, PQI_AOQ_CIO + 4, regupper, DWORD);
            

            // PD3:Administrator_Queue_Pair_Ready state
            pqiDev->adminQueueStatus = IDLE;
            pqiDev->sm.state = PQI_DEVICE_STATE_PD3;   // Queue pair Ready State
            SOP_LOG_NORM("Admin Queue Pair created");

        } else {

            // Fail the command & go to PD4
            
            pqiDev->adminQueueStatus = CREATING_ADMIN_QUEUE_PAIR;

            // Set "ADMINISTRATOR QUEUE PAIR CREATE ERROR" :
            PQIDeviceError pde;     
            pde.fullreg = 0;        //      pde.bytePointer = 0;
                                    //      pde.bitPointer = 0;
                                    //      pde.BPV = 0;;
                                    //      pde.errorCodeQualifier = 0;
            pde.fields.errorCode = 3;

            pqi_cntrl_write_config(pqiDev, PQI_DEV_ERR, pde.fullreg, DWORD);
            
            pqiDev->sm.state = PQI_DEVICE_STATE_PD4;    // Error State
            SOP_LOG_ERR("Error %s() creating the Admin Queue Pair", __func__);

        }

        break;

    case DELETE_ADMIN_QUEUE_PAIR:

        SOP_LOG_NORM(" AQ Function: 0x%02x - DELETE_ADMIN_QUEUE_PAIR", aq_config_reg->function_status_code);
		
        // Queue idle & Administrator Queue Pair Ready
        if ((pqiDev->adminQueueStatus == IDLE) && (pqiDev->sm.state == PQI_DEVICE_STATE_PD3)) {

            pqiDev->adminQueueStatus = DELETING_ADMIN_QUEUE_PAIR;

            // section 4.2.4.2 (Deleting the administrator queue pair)
            // the device does nothing except change to PD2 ?
             
            pqiDev->adminQueueStatus = IDLE;
            pqiDev->sm.state = PQI_DEVICE_STATE_PD2;        // BAR Registers Ready

        } else {

            // Set "ADMINISTRATOR QUEUE PAIR DELETE ERROR" :
            PQIDeviceError pde;     
            pde.fullreg = 0;        //      pde.bytePointer = 0;
                                    //      pde.bitPointer = 0;
                                    //      pde.BPV = 0;
            pde.fields.errorCodeQualifier = 1;
            pde.fields.errorCode = 3;

            pqi_cntrl_write_config(pqiDev, PQI_DEV_ERR, pde.fullreg, DWORD);
            
            pqiDev->sm.state = PQI_DEVICE_STATE_PD4;
            SOP_LOG_ERR("Error %s() deleating the Admin Queue Pair", __func__);

        }

        break;

    default:
        SOP_LOG_NORM(" AQ Function: 0x%02x - unknown", aq_config_reg->function_status_code);

        // Anything else is an error... probably
        register_error(pqiDev, INVALID_PARAM_IN_PQI_REG, 0, 0);
        break;
    }

    SOP_LOG_NORM("%s() exit...", __func__);
}


/*********************************************************************
 Function     :    pqi_mmio_writeb
 Description  :    Write 1 Byte at addr/register
 Return Type  :    void
 Arguments    :    void * : Pointer to PQI device State
 hwaddr : Address (offset address)
 uint32_t : Value to be written
 *********************************************************************/

static void pqi_mmio_writeb(void* opaque, hwaddr addr, uint32_t val) {

    PQIState* n = opaque;

    SOP_LOG_DBG("%s(): addr = 0x%08x, val = 0x%08x", __func__, (unsigned)addr, val);
    SOP_LOG_ERR("%s() writeb is not supported!", __func__);
    
    (void)n;

    SOP_LOG_DBG("%s() exit...", __func__);
}


/*********************************************************************
 Function     :    pqi_mmio_writew
 Description  :    Write 2 Bytes at addr/register
 Return Type  :    void
 Arguments    :    void * : Pointer to PQI device State
 hwaddr : Address (offset address)
 uint32_t : Value to be written
 *********************************************************************/

static void pqi_mmio_writew(void* opaque, hwaddr addr, uint32_t val) {

    PQIState* pqiDev = opaque;
    uint32_t qid;

    SOP_LOG_DBG("%s(): addr = 0x%08x, val = 0x%08x", __func__, (unsigned)addr, val);
    if ((addr >= PQI_IQ_PI_REG(AIQ_ID) &&           // process IQ PI writes (admin and operational)
         addr <  PQI_OQ_CI_REG(AOQ_ID))) {

        pqi_cntrl_write_config(pqiDev, addr, val, WORD);
        qid = (uint32_t)(addr - PQI_IQ_PI_BASE) / 8;
        process_iq_event(pqiDev, qid);

    } else if ((addr >= PQI_OQ_CI_REG(AOQ_ID)) &&   // process OQ CI writes (admin and operational)
               (addr <= PQI_OQ_CI_REG(MAX_Q_ID))) {

        pqi_cntrl_write_config(pqiDev, addr, val, WORD);
        qid = (uint32_t)(addr - PQI_OQ_CI_BASE) / 8;
        process_oq_event(pqiDev, qid);
    
    } else {
        SOP_LOG_ERR("%s() writew is not supported for addr = 0x%08x", __func__,(unsigned)addr);
    }
    SOP_LOG_DBG("%s() exit...", __func__);
}


/*********************************************************************
 Function     :    pqi_mmio_writel
 Description  :    Write 4 Bytes at addr/register
 Return Type  :    void
 Arguments    :    void * : Pointer to PQI device State
 hwaddr : Address (offset address)
 uint32_t : Value to be written
 *********************************************************************/

static void pqi_mmio_writel(void* opaque, hwaddr addr, uint32_t val) {

    PQIState* pqiDev = (PQIState*)opaque;
    //uint64_t var; // Variable to store reg values locally
    bool valid_write = true;
    uint32_t qid;

    SOP_LOG_NORM("%s(): addr = 0x%08x, val = 0x%08x", __func__, (unsigned)addr, val);

    // Do some bounds checking, then check if we can write to this register in our current state

    if (addr > PQI_OQ_CI_REG(MAX_Q_ID)) {

            SOP_LOG_ERR("Error %s() address 0x%08x is outside register range, ignoring", __func__, (unsigned)addr);
            valid_write = false;

    } else {

        if (!(pqiDev->sm.state == PQI_DEVICE_STATE_PD2 || pqiDev->sm.state == PQI_DEVICE_STATE_PD3) && addr != PQI_RESET) {

            // Address is within range, but only the reset reg can be written outside states PD2 and PD3
            SOP_LOG_ERR("Error %s() PQI device not at state PD2 or PD3.  Write not allowed in this state!", __func__);
            valid_write = false;

        } else if ((addr >= PQI_AIQ_EAA) && (addr < PQI_DEV_ERR)) {

            // We need to make sure that the registers aren't locked if the address is in this range

            if (pqiDev->sm.reglock == PQI_REGISTERS_LOCKED) {

                SOP_LOG_DBG("Error %s() PQI Admin Queue registers are locked.  Write not allowed in this state!", __func__);
                valid_write = false;
            }
        }
    }

    // If the write was to a valid location, and is valid for the current state, carry it out

    if (valid_write) {

        switch (addr) {

        case PQI_AQ_CONFIG: // 64-bit register, 1 DWORD at a time
            pqi_cntrl_write_config(pqiDev, PQI_AQ_CONFIG, val, DWORD);
            // this one is called first, and contains the bits we care about, but call
            // the handler in the next one so we know the whole reg has been written
            break;

        case(PQI_AQ_CONFIG + 4):
            pqi_cntrl_write_config(pqiDev, (PQI_AQ_CONFIG + 4), val, DWORD);
            // call the handler here so we know the whole 64-bit reg has been written
            process_aq_config(pqiDev);
            break;

        case PQI_INTMS:
            // Operation not defined if MSI-X is enabled

            if (pqiDev->dev.msix_cap != 0x00 && IS_MSIX(pqiDev)) {

                SOP_LOG_ERR("Error %s() MSI-X is enabled..write to INTMS is undefined", __func__);
          
            } else {

                // MSICAP or PIN based ISR is enabled
                pqi_cntrl_write_config(pqiDev, PQI_INTMS, val, DWORD);
            }

            break;

        case PQI_INTMC:
            // Operation not defined if MSI-X is enabled

            if (pqiDev->dev.msix_cap != 0x00 && IS_MSIX(pqiDev)) {

                SOP_LOG_ERR("Error %s() MSI-X is enabled..write to INTMC is undefined", __func__);

            } else {

                // MSICAP or PIN based ISR is enabled
                pqi_cntrl_write_config(pqiDev, PQI_INTMC, val, DWORD);
            }

            break;

        case PQI_AIQ_EAA: // 64-bit register, 1 DWORD at a time
            pqi_cntrl_write_config(pqiDev, PQI_AIQ_EAA, val, DWORD);
            //*((uint32_t*)&sop_dev->iq[AIQ_ID].ea_addr) = val;
            break;

        case(PQI_AIQ_EAA + 4):
            pqi_cntrl_write_config(pqiDev, (PQI_AIQ_EAA + 4), val, DWORD);
            //*((uint32_t*)(&sop_dev->iq[AIQ_ID].ea_addr) + 1) = val;
            break;

        case PQI_AOQ_EAA: // 64-bit register, 1 DWORD at a time
            pqi_cntrl_write_config(pqiDev, PQI_AOQ_EAA, val, DWORD);
            //*((uint32_t*)&sop_dev->oq[AOQ_ID].ea_addr) = val;
            break;

        case(PQI_AOQ_EAA + 4):
            pqi_cntrl_write_config(pqiDev, (PQI_AOQ_EAA + 4), val, DWORD);
            //*((uint32_t*)(&sop_dev->oq[AOQ_ID].ea_addr) + 1) = val;
            break;
        
        case PQI_AIQ_CIA: // 64-bit register, 1 DWORD at a time
            pqi_cntrl_write_config(pqiDev, PQI_AIQ_CIA, val, DWORD);
            //*((uint32_t*)&sop_dev->iq[AIQ_ID].ci_addr) = val;
            break;

        case(PQI_AIQ_CIA + 4):
            pqi_cntrl_write_config(pqiDev, (PQI_AIQ_CIA + 4), val, DWORD);
            //*((uint32_t*)(&sop_dev->iq[AIQ_ID].ci_addr) + 1) = val;
            break;

        case PQI_AOQ_PIA: // 64-bit register, 1 DWORD at a time
            pqi_cntrl_write_config(pqiDev, PQI_AOQ_PIA, val, DWORD);
            //*((uint32_t*)&sop_dev->oq[AOQ_ID].pi_addr) = val;
            break;

        case(PQI_AOQ_PIA + 4):
            pqi_cntrl_write_config(pqiDev, (PQI_AOQ_PIA + 4), val, DWORD);
            //*((uint32_t*)(&sop_dev->oq[AOQ_ID].pi_addr) + 1) = val;
            break;

        case PQI_AQ_PARM:
            pqi_cntrl_write_config(pqiDev, PQI_AQ_PARM, val, DWORD);
            //PQIAdminQueueParmLayout aqparm;
            //aqparm.fullreg = val;
            //*((uint16_t*)(&sop_dev->iq[AIQ_ID].size)) = aqparm.fields.numaiqelem;
            //*((uint16_t*)(&sop_dev->oq[AOQ_ID].size)) = aqparm.fields.numaoqelem;
            //*((uint16_t*)(&sop_dev->oq[AOQ_ID].msixEntry)) = aqparm.fields.msixEntry;
            break;

        case PQI_RESET:
        	SOP_LOG_NORM("PQI RESET");
            pqi_cntrl_write_config(pqiDev, PQI_RESET, val, DWORD);
            pqi_reset_request(pqiDev, val);
            break;

        case PQI_PWRACT:
            pqi_cntrl_write_config(pqiDev, PQI_PWRACT, val, DWORD);
            break;

        default:
            if ((addr >= PQI_IQ_PI_REG(AIQ_ID) &&           // process IQ PI writes (admin and operational) 
                 addr <  PQI_OQ_CI_REG(AOQ_ID))) {

                pqi_cntrl_write_config(pqiDev, addr, val, DWORD);
                qid = (uint32_t)(addr - PQI_IQ_PI_BASE) / 8;
                process_iq_event(pqiDev, qid);

            } else if ((addr >= PQI_OQ_CI_REG(AOQ_ID)) &&   // process OQ CI writes (admin and operational)
                       (addr <= PQI_OQ_CI_REG(MAX_Q_ID))) {

                pqi_cntrl_write_config(pqiDev, addr, val, DWORD);
                qid = (uint32_t)(addr - PQI_OQ_CI_BASE) / 8;
                process_oq_event(pqiDev, qid);
            
            } else {

                SOP_LOG_ERR("Error %s() invalid addr: 0x%08x", __func__, (unsigned)addr);
            }

            break;
        }
    }

    SOP_LOG_NORM("%s() exit...", __func__);
    return;
}


/*********************************************************************
 Function     :    pqi_cntrl_write_config
 Description  :    Function for PQI Controller space writes
 Return Type  :    void
 Arguments    :    PQIState * : Pointer to PQI device State
 hwaddr : address (offset address)
 uint32_t : Value to write
 uint8_t : Length to be read
 Note: Writes are done to the PQI device in Least Endian Fashion <== BWS - what does this mean?
 *********************************************************************/

void pqi_cntrl_write_config(PQIState* pqiDev, hwaddr addr, uint32_t val, uint8_t len) {

    uint8_t index;
    uint8_t*  intr_vect = (uint8_t*)&pqiDev->intr_vect;

    SOP_LOG_NORM("%s(): pqiDev: 0x%08lu  addr: 0x%04x  val: 0x%08x  len: 0x%02x",
	             __func__, (uint64_t)pqiDev, (uint32_t)addr, val, len);
    val = cpu_to_le32(val);

    if (range_covers_reg(addr, len, PQI_INTMS, DWORD) || range_covers_reg(addr, len, PQI_INTMC, DWORD)) {

        // Check if MSIX is enabled

        if (pqiDev->dev.msix_cap != 0x00 && IS_MSIX(pqiDev)) {

            SOP_LOG_ERR("Error %s() MSI-X is enabled..write to INTMS/INTMC is undefined", __func__);

        } else {

            // Specific case for Interrupt masks

            for (index = 0; index < len && addr + index < PQI_CNTRL_SIZE; val >>= 8, index++) {

                // W1C: Write 1 to Clear
                intr_vect[index] &= ~(val & pqiDev->rwc_mask[addr + index]);
                // W1S: Write 1 to Set
                intr_vect[index] |= (val & pqiDev->rws_mask[addr + index]);
            }
        }

    } else {

    	SOP_LOG_NORM("  Before Write: 0x%02x%02x%02x%02x", (uint8_t)pqiDev->cntrl_reg[addr+3],
		                                                   (uint8_t)pqiDev->cntrl_reg[addr+2],
		                                                   (uint8_t)pqiDev->cntrl_reg[addr+1],
														   (uint8_t)pqiDev->cntrl_reg[addr]);
    	SOP_LOG_NORM("     Used Mask: 0x%02x%02x%02x%02x", (uint8_t)pqiDev->used_mask[addr+3],
		                                                   (uint8_t)pqiDev->used_mask[addr+2],
		                                                   (uint8_t)pqiDev->used_mask[addr+1],
														   (uint8_t)pqiDev->used_mask[addr]);
    	SOP_LOG_NORM("      R/W Mask: 0x%02x%02x%02x%02x", (uint8_t)pqiDev->rw_mask[addr+3],
		                                                   (uint8_t)pqiDev->rw_mask[addr+2],
		                                                   (uint8_t)pqiDev->rw_mask[addr+1],
														   (uint8_t)pqiDev->rw_mask[addr]);
    	SOP_LOG_NORM("     R/WC Mask: 0x%02x%02x%02x%02x", (uint8_t)pqiDev->rwc_mask[addr+3],
		                                                   (uint8_t)pqiDev->rwc_mask[addr+2],
		                                                   (uint8_t)pqiDev->rwc_mask[addr+1],
														   (uint8_t)pqiDev->rwc_mask[addr]);
    	SOP_LOG_NORM("     R/WS Mask: 0x%02x%02x%02x%02x", (uint8_t)pqiDev->rws_mask[addr+3],
		                                                   (uint8_t)pqiDev->rws_mask[addr+2],
		                                                   (uint8_t)pqiDev->rws_mask[addr+1],
														   (uint8_t)pqiDev->rws_mask[addr]);

        for (index = 0; (index < len) && ((addr + index) < PQI_CNTRL_SIZE); val >>= 8, index++) {

            // Setting up RW and RO mask and making reserved bits
            // non writable
            pqiDev->cntrl_reg[addr + index] = (pqiDev->cntrl_reg[addr + index] 
                                               & (~(pqiDev->rw_mask[addr + index]) | ~(pqiDev->used_mask[addr + index]))) 
                                              | (val & pqiDev->rw_mask[addr + index]);
            // W1C: Write 1 to Clear
            pqiDev->cntrl_reg[addr + index] &= ~(val & pqiDev->rwc_mask[addr + index]);
            // W1S: Write 1 to Set
            pqiDev->cntrl_reg[addr + index] |= (val & pqiDev->rws_mask[addr + index]);
        }

        SOP_LOG_NORM("   After Write: 0x%02x%02x%02x%02x", (uint8_t)pqiDev->cntrl_reg[addr+3],
		                                                   (uint8_t)pqiDev->cntrl_reg[addr+2],
		                                                   (uint8_t)pqiDev->cntrl_reg[addr+1],
														   (uint8_t)pqiDev->cntrl_reg[addr]);
    }
    
    SOP_LOG_NORM("%s() exit...", __func__);
}


/*********************************************************************
 Function     :    pqi_cntrl_read_config
 Description  :    Function for PQI Controller space reads
 (except doorbell reads)
 Return Type  :    uint32_t : Value read
 Arguments    :    PQIState * : Pointer to PQI device State
 hwaddr : address (offset address)
 uint8_t : Length to be read
 *********************************************************************/

uint32_t pqi_cntrl_read_config(PQIState* pqiDev, hwaddr addr, uint8_t len) {

    uint32_t val = 0;
    
    SOP_LOG_DBG("%s(): pqiDev: 0x%08lu  addr: 0x%04x  len: 0x%02x", __func__, (uint64_t)pqiDev, (uint32_t)addr, len);
    
    // Prints the assertion and aborts
    // assert(len == 1 || len == 2 || len == 4);
    
	len = MIN(len, PQI_CNTRL_SIZE - addr);

    memcpy((uint8_t*)&val, (uint8_t*)(&pqiDev->cntrl_reg[0] + addr), len);

    // if (range_covers_reg(addr, len, PQI_INTMS, DWORD) || range_covers_reg(addr, len, PQI_INTMC, DWORD)) {
    //  
    //     // Check if MSIX is enabled
    //  
    //     if ((pqiDev->dev.msix_cap != 0x00) && IS_MSIX(pqiDev)) {
    // 
    //         SOP_LOG_ERR("Error %s() MSI-X is enabled..read to INTMS/INTMC is undefined", __func__);
    //         val = 0;
    // 
    //     } else {
    // 
    //         // Read of INTMS or INTMC should return interrupt vector
    //         val = pqiDev->intr_vect;
    //     }
    // }

    switch (len) {
	
	case BYTE:
        SOP_LOG_DBG("%s() val: 0x%02x - exit...", __func__, val);
		break;
	case WORD:
        SOP_LOG_DBG("%s() val: 0x%04x - exit...", __func__, val);
		break;
	case DWORD:
        SOP_LOG_DBG("%s() val: 0x%08x - exit...", __func__, val);
		break;
	case QWORD:
        SOP_LOG_DBG("%s() val: 0x%016x - exit...", __func__, val);
		break;
		
	default:
		// assert(len);
		break;
    }

    return le32_to_cpu(val);
}


/*********************************************************************
 Function     :    pqi_reset_request
 Description  :    Function for BAR reg issued PQI reset request
 Return Type  :    void
 Arguments    :    PQIState * : Pointer to PQI device State
 hwaddr : address (offset address)
 uint8_t : Length to be read
 *********************************************************************/

void pqi_reset_request(PQIState* pqiDev, uint32_t val) {

//    FILE* pqi_config_file;
    PQIReset* pr;
    int i;

    SOP_LOG_NORM("%s(): pqiDev = 0x%08lx", __func__, (uint64_t)pqiDev);

    pr = (PQIReset*)&val;

    if (pr->fields.resetAction == START_RESET) {

        switch (pr->fields.resetType) {

        case NO_RESET:
            if ((pqiDev->sm.state == PQI_DEVICE_STATE_PD1) && !pr->fields.holdInPD1) {

                // TODO: Do the transition from PD1 to PD2 stuff here??
                pqiDev->sm.state = PQI_DEVICE_STATE_PD2;

                // Reply to PQI reset
                pr->fields.resetAction = START_RESET_COMPLETED;
                pqi_cntrl_write_config(pqiDev, PQI_RESET, pr->fullreg, DWORD);
            }
            break;

        case SOFT_RESET:
            SOP_LOG_NORM("%s(): SOFT_RESET pr = 0x%04x", __func__,pr->fullreg);
// Only for this PQI device:
            //  1 - Reset PQI device registers
            //  2 - Reset queuing layer
            //  3 - Reset IU layer content

            // 1 - Reset device registers
//            if ((pqi_config_file = fopen((char*)PQI_CONFIG_FILE, "r")) != NULL) {
//
//                if (pqi_read_config_file(pqi_config_file, pqiDev, PQI_SPACE)) {
//
//                    SOP_LOG_ERR("Error %s() could not read the PQI config file!", __func__);
//                    SOP_LOG_NORM("Defaulting the PQI space..");
//                    pqi_set_registry(pqiDev);
//                }
//
//                fclose(pqi_config_file);
//
//            } else {
//
//                SOP_LOG_ERR("Error %s() could not open the PQI config file!", __func__);
//                SOP_LOG_NORM("Defaulting the PQI space..");
//                pqi_set_registry(pqiDev);
//            }
            pqi_set_registry(pqiDev);


            // 2 - Reset queuing layer

            // initialize the inbound queues
            PQIInboundQueue* ibQp  = &pqiDev->iq[1];
            PQIOutboundQueue* obQp = &pqiDev->oq[1];

            for (i=1; i < MAX_Q_ID; i++, ibQp++, obQp++) {

                // in-bound Queue
                ibQp->id = 0;
                ibQp->pi = 0;
                ibQp->ci_addr = 0;
                ibQp->ci_work = 0;
                ibQp->ci_local = 0;
                ibQp->ea_addr = 0;
                ibQp->size = 0;

                // out-bound Queue
                obQp->id = 0;
                obQp->ci = 0;
                obQp->pi_addr = 0;
                obQp->pi_work = 0;
                obQp->pi_local = 0;
                obQp->ea_addr = 0;
                obQp->size = 0;
                obQp->msixEntry = 0;
            }

            // 3 - Reset IU layer content

            pr->fields.resetAction = START_RESET_COMPLETED;
            pqiDev->sm.state = PQI_DEVICE_STATE_PD2;
            pqi_cntrl_write_config(pqiDev,PQI_RESET,pr->fullreg,4);
            SOP_LOG_NORM("%s(): SOFT_RESET complete. pr = 0x%04x", __func__,pr->fullreg);
            break;

        case FIRM_RESET:
            // Only for this PQI device:
            //  - Reset PQI device registers
            //  - Reset queuing layer
            // For all PQI devices:
            //  - Reset IU layer content
            pr->fields.resetAction = START_RESET_COMPLETED;
            break;

        case HARD_RESET:
            // For all PQI devices:
            //  - Reset PQI device registers
            //  - Reset queuing layer
            //  - Reset IU layer content
            pr->fields.resetAction = START_RESET_COMPLETED;
            break;

        default:
            break;
        }
    }
    
    SOP_LOG_NORM("%s() exit...", __func__);
}


/*********************************************************************
 Function     :    pqi_mmio_readb
 Description  :    Read 1 Bytes at addr/register
 Return Type  :    void
 Arguments    :    void * : Pointer to PQI device State
 hwaddr : Address (offset address)
 Note:- Even though function is readb, return value is uint32_t
 coz, Qemu mapping code does the masking of repective bits
 *********************************************************************/

static uint32_t pqi_mmio_readb(void* opaque, hwaddr addr) {

    uint32_t rd_val = 0;
    PQIState* pqiDev = (PQIState*)opaque;

    SOP_LOG_DBG("%s(pqiDev=0x%08lu, addr=0x%08x)", __func__, (uint64_t)pqiDev, (unsigned)addr);

    // Check if PQI controller Capabilities was written

    if (addr < PQI_END_ASSIGNED_REG) {

        rd_val = pqi_cntrl_read_config(pqiDev, addr, BYTE);

    } else {

        SOP_LOG_ERR("Error %s() illegal address: 0x%08x", __func__, (unsigned)addr);
        rd_val = 0;
    }

    SOP_LOG_DBG("%s() retuning: 0x%02x", __func__, rd_val);

    return rd_val;
}


/*********************************************************************
 Function     :    pqi_mmio_readw
 Description  :    Read 2 Bytes at addr/register
 Return Type  :    void
 Arguments    :    void * : Pointer to PQI device State
 hwaddr : Address (offset address)
 Note:- Even though function is readw, return value is uint32_t
 coz, QEMU mapping code does the masking of repective bits
 *********************************************************************/

static uint32_t pqi_mmio_readw(void* opaque, hwaddr addr) {

    uint32_t rd_val = 0;
    PQIState* pqiDev = (PQIState*)opaque;

    SOP_LOG_DBG("%s(pqiDev=0x%08lu, addr=0x%08x)", __func__, (uint64_t)pqiDev, (unsigned)addr);

    // Check if PQI controller Capabilities was written

    if (addr < PQI_END_ASSIGNED_REG) {

        rd_val = pqi_cntrl_read_config(pqiDev, addr, WORD);

    } else {

        SOP_LOG_ERR("%s() Undefined address: 0x%08x", __func__, (unsigned)addr);
        rd_val = 0;
    }

    SOP_LOG_DBG("%s() returning 0x%04x", __func__, rd_val);

    return rd_val;
}


/*********************************************************************
 Function     :    pqi_mmio_readl
 Description  :    Read 4 Bytes at addr/register
 Return Type  :    void
 Arguments    :    void * : Pointer to PQI device State
 hwaddr : Address (offset address)
 *********************************************************************/

static uint32_t pqi_mmio_readl(void* opaque, hwaddr addr) {

    uint32_t rd_val = 0;
    PQIState* pqiDev = (PQIState*)opaque;

    SOP_LOG_DBG("%s(pqiDev=0x%08lu, addr=0x%08x)", __func__, (uint64_t)pqiDev, (unsigned)addr);

    // Check if PQI controller Capabilities was written

    if (addr < PQI_END_ASSIGNED_REG) {
        
        switch (addr) {

        // Cannot simply return register content for addr: PQI_AQ_CONFIG
        // The "FUNCTION" written differs from the "STATUS CODE" read
        case PQI_AQ_CONFIG: // 64-bit register, 1 DWORD at a time
            rd_val = (uint32_t)pqiDev->adminQueueStatus;
            break;

        case (PQI_AQ_CONFIG + 4):
            rd_val = 0;
            break;

        case PQI_STATUS:
            rd_val = pqiDev->sm.state;
            break;

        case (PQI_STATUS + 4):
            rd_val = 0;
            break;

        default:
            rd_val = pqi_cntrl_read_config(pqiDev, addr, DWORD);
            break;
        }

    } else {

        SOP_LOG_ERR("%s() Illegal address", __func__);
        rd_val = 0;
    }

    SOP_LOG_DBG("%s() returning: 0x%08x", __func__, rd_val);

    return rd_val;
}

/*********************************************************************
 Function     :    range_covers_reg
 Description  :    Checks whether the given range covers a
 particular register completley/partially
 Return Type  :    uint8_t : 1 : covers , 0 : does not cover
 Arguments    :    uint64_t : Start addr to write
 uint64_t : Length to be written
 uint64_t : Register offset in address space
 uint64_t : Size of register
 *********************************************************************/

static inline uint8_t range_covers_reg(uint64_t addr, uint64_t len, uint64_t reg, uint64_t regSize) {

    return (uint8_t)((addr <= range_get_last(reg, regSize)) && 
                   ((range_get_last(reg, regSize) <= range_get_last(addr, len)) || 
                    (range_get_last(reg, BYTE) <= range_get_last(addr, len))));
}


/*********************************************************************
 Function:         pqi_pci_write_config
 Description:      Function for PCI config space writes
 Return Type:      void
 Arguments:        PCIDevice*: Pointer to PCI device state
 uint32_t:   Address (offset address)
 uint32_t:   Value to be written
 uint32_t:        Length to be written
 *********************************************************************/

static void pqi_pci_write_config(PCIDevice* pciDev, uint32_t addr, uint32_t val, int len) {

    switch (len) {
	
	case 1:
        SOP_LOG_DBG("%s(): pciDev = 0x%08lu, addr: 0x%04x, val: 0x%02x, len: %d",
                     __func__, (uint64_t)pciDev, addr, val, len);
		break;
	case 2:
        SOP_LOG_DBG("%s(): pciDev = 0x%08lu, addr: 0x%04x, val: 0x%04x, len: %d",
                     __func__, (uint64_t)pciDev, addr, val, len);
		break;
	case 3:
        SOP_LOG_DBG("%s(): pciDev = 0x%08lu, addr: 0x%04x, val: 0x%06x, len: %d",
                     __func__, (uint64_t)pciDev, addr, val, len);
		break;
	case 4:
        SOP_LOG_DBG("%s(): pciDev = 0x%08lu, addr: 0x%04x, val: 0x%08x, len: %d",
                     __func__, (uint64_t)pciDev, addr, val, len);
		break;
    }

    val = cpu_to_le32(val);

    /* Writing the PCI Config Space */
    pci_default_write_config(pciDev, addr, val, len);

    if (range_covers_reg(addr, len, PCI_BIST, PCI_BIST_LEN) && (!(pciDev->config[PCI_BIST] & PCI_BIST_CAPABLE))) {

        /* Defaulting BIST value to 0x00 */
        pci_set_byte(&pciDev->config[PCI_BIST], (uint8_t)0x00);
    }

    SOP_LOG_DBG("%s() exit...", __func__);
    return;
}


/*********************************************************************
 Function:         pqi_pci_read_config
 Description:      Function for PCI config space reads
 Return Type:      uint32_t:    Value read
 Arguments:        PCIDevice*:  Pointer to PCI device state
 uint32_t:    address (offset address)
 uint32_t:         Length to be read
 *********************************************************************/

static uint32_t pqi_pci_read_config(PCIDevice* pciDev, uint32_t addr, int len) {

    uint32_t val;

    SOP_LOG_DBG("%s(): pciDev = 0x%016lu, addr: 0x%04x, len: %d",
                 __func__, (uint64_t)pciDev, addr, len);

    val = pci_default_read_config(pciDev, addr, len);
    
    switch (len) {
	
	case 1:
		SOP_LOG_DBG("%s() val: 0x%02x exit...", __func__, val);
		break;
	case 2:
		SOP_LOG_DBG("%s() val: 0x%04x exit...", __func__, val);
		break;
	case 3:
		SOP_LOG_DBG("%s() val: 0x%06x exit...", __func__, val);
		break;
	case 4:
		SOP_LOG_DBG("%s() val: 0x%08x exit...", __func__, val);
		break;
    }
	
    return val;
}

/*********************************************************************
 Function     :    pqi_set_registry
 Description  :    Default initialization of PQI Registery (BAR0 regs)
 Return Type  :    void
 Arguments    :    PQIState * : Pointer to PQI device state
 *********************************************************************/

static void pqi_set_registry(PQIState* n) {

    // This is the default initialization sequence when
    // config file is not found
    uint32_t ind;
    uint32_t index;
    uint32_t val_at_reset;
    uint32_t rw_mask;
    uint32_t rws_mask;
    uint32_t rwc_mask;

    SOP_LOG_NORM("%s(): PQIState = 0x%08lu", __func__, (uint64_t)n);
	
	SOP_LOG_NORM(" looping pqi_reg[0] at 0x%08lu", (uint64_t)&pqi_reg[0]);
	SOP_LOG_NORM("    to   pqi_reg[%03x] at0x%08lu", 
	          (unsigned int)((sizeof(pqi_reg) / sizeof(pqi_reg[0])) - 1),
			  (uint64_t)&pqi_reg[(sizeof(pqi_reg) / sizeof(pqi_reg[0])) - 1]);

    for (ind = 0; ind < sizeof(pqi_reg) / sizeof(pqi_reg[0]); ind++) {
	
	    SOP_LOG_DBG("  pqi_reg[%03x].offset-0x%04x len-0x%01x reset-0x%08x",
		             ind, pqi_reg[ind].offset, pqi_reg[ind].len, pqi_reg[ind].reset);

        rw_mask = pqi_reg[ind].rw_mask;
        rwc_mask = pqi_reg[ind].rwc_mask;
        rws_mask = pqi_reg[ind].rws_mask;

        val_at_reset = pqi_reg[ind].reset;

        for (index = 0; index < pqi_reg[ind].len; val_at_reset >>= 8, rw_mask >>= 8, rwc_mask >>= 8, rws_mask >>= 8, index++) {

            n->cntrl_reg[pqi_reg[ind].offset + index] = val_at_reset;
            n->rw_mask[pqi_reg[ind].offset + index] = rw_mask;
            n->rws_mask[pqi_reg[ind].offset + index] = rws_mask;
            n->rwc_mask[pqi_reg[ind].offset + index] = rwc_mask;
            n->used_mask[pqi_reg[ind].offset + index] = (uint8_t)MASK(8, 0);
        }
    }

    // assigned register rw/rws/rwc settings
    // (code below assumes all "assigned" regs are 32-bits)
    uint8_t* crp;
    uint8_t* rwmp;
    uint8_t* rwsmp;
    uint8_t* rwcmp;
    uint8_t* ump;

    crp   = &n->cntrl_reg[ind];
    rwmp  = &n->rw_mask[ind];
    rwsmp = &n->rws_mask[ind];
    rwcmp = &n->rwc_mask[ind];
    ump   = &n->used_mask[ind];

	SOP_LOG_NORM(" looping pqi_reg[%03x] at 0x%08lu -to- pqi_reg[%03x] at0x%08lu", 
	             ind, (uint64_t)&pqi_reg[ind], PQI_CNTRL_SIZE, (uint64_t)&pqi_reg[(sizeof(pqi_reg) / sizeof(pqi_reg[0])) - 1]);

    for ( ; ind < PQI_CNTRL_SIZE; ind++) {

        *crp++   = 0;       // register initial value
        *rwmp++  = 0xFF;    // all bits are r/w
        *rwsmp++ = 0;       // no bits are write 1 to set
        *rwcmp++ = 0;       // no bits are write 1 to clear
        *ump++   = 0xFF;    // all bits are used
    }
    
    SOP_LOG_NORM("%s() exit...", __func__);
}


/*********************************************************************
 Function     :    do_pqi_reset
 Description  :    TODO: Not yet implemented
 Return Type  :    void
 Arguments    :    PQIState * : Pointer to PQI device state
 *********************************************************************/

static void do_pqi_reset(PQIState* n) {

    SOP_LOG_NORM("%s(): PQIState = 0x%08lu", __func__, (uint64_t)n);

    // (void)n;
    
    SOP_LOG_NORM("%s() exit...", __func__);
}


/*********************************************************************
 Function     :    qdev_pqi_reset
 Description  :    Handler for PCI Reset
                   Reset spawned via PCI interface (not via BAR reg)
 Return Type  :    void
 Arguments    :    DeviceState * : Pointer to PQI device state
 *********************************************************************/

static void qdev_pqi_reset(DeviceState* dev) {

    SOP_LOG_NORM("%s(): DeviceState = 0x%08lu", __func__, (uint64_t)dev);

    PQIState* n = DO_UPCAST(PQIState, dev.qdev, dev);
    do_pqi_reset(n);

    SOP_LOG_NORM("%s() exit...", __func__);
}


/*********************************************************************
 Function     :    pqi_pci_space_init
 Description  :    Hardcoded PCI space initialization
 Return Type  :    void
 Arguments    :    PCIDevice * : Pointer to the PCI device
 Note:- RO/RW/RWC masks not supported for default PCI space
 initialization
 *********************************************************************/

static void pqi_pci_space_init(PCIDevice* pciDev) {

    PQIState* n = DO_UPCAST(PQIState, dev, pciDev);
    uint8_t* pci_conf = n->dev.config;

    SOP_LOG_NORM("%s(): pciDev = 0x%08lu", __func__, (uint64_t)pciDev);
    
    // set PCI VID & DID
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_HGST);
    pci_config_set_device_id(pci_conf, PQI_DEV_ID);             // TODO: BWS - find out what the real device ID is going to be
    SOP_LOG_NORM("%s(): VID = 0x%04x  DID = 0x%04x", __func__, PCI_VENDOR_ID_HGST, PQI_DEV_ID);

    // set Command/Status
    // pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_MEMORY);   // Enable response in Memory space
    // pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_CAP_LIST);   // Supports Capability List
    

    // set Rev-ID, Prog IF, class/sub-class
    //    Prior code:
    //     BWS code: pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_EXPRESS >> 8);
    //               pci_config_set_prog_interface(pci_conf, (0xF & PCI_CLASS_STORAGE_EXPRESS));
    // pci_set_byte(pci_conf + PCI_REVISION_ID, 0x03);
    // pci_set_byte(pci_conf + PCI_CLASS_PROG, PQI_PCI_PROG_IF);
    // pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_OTHER);
    
    /* STORAGE EXPRESS is not yet a standard. */
    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_EXPRESS >> 8);

    pci_config_set_prog_interface(pci_conf,
        0xf & PCI_CLASS_STORAGE_EXPRESS);

    // set cache-size, laency, header type, & BIST
    // pci_set_byte(pci_conf + PCI_CACHE_LINE_SIZE, 0);
    // pci_set_byte(pci_conf + PCI_LATENCY_TIMER, 0);
    // pci_set_byte(pci_conf + PCI_HEADER_TYPE, PCI_HEADER_TYPE_NORMAL);
    // pci_set_byte(pci_conf + PCI_BIST, 0);                       // no self-tests
    // 
    // init BARs to zero (disable until memory is allocated for them)
	// pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, 0);	// 64-bit memory BAR 0 (used)
	// pci_set_long(pci_conf + PCI_BASE_ADDRESS_1, 0);
	// 
    // pci_set_long(pci_conf + PCI_BASE_ADDRESS_2, 0); // 64-bit memory BAR 1 (not used)
    // pci_set_long(pci_conf + PCI_BASE_ADDRESS_3, 0);
	// 
    // pci_set_long(pci_conf + PCI_BASE_ADDRESS_4, 0); // 64-bit memory BAR 2 (not used)
    // pci_set_long(pci_conf + PCI_BASE_ADDRESS_5, 0);
    // 
    // set card interface structure (CIS) info (set to zero as it is not supported)
    // pci_set_long(pci_conf + PCI_CARDBUS_CIS, 0);                // no Card Info 
    // 
    // set subsystem vendor & device IDs - RBB commented out so QEMU can set it...
    // pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_HGST);
    // pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, PQI_DEV_ID);

    // set expansion ROM base address (zero disables it)
    // pci_set_long(pci_conf + PCI_ROM_ADDRESS, 0);                // no expansion ROM

    // scratch capabliliy list so it can be built as needed below
    // TODO: For sometime in the future...
    // pci_set_byte(pci_conf + PCI_CAPABILITY_LIST, 0);
    
    // TODO: Build capabilities
    //  The PCIe SSD has these capabilities:
    //      Power Management (PM)
    //      Message Signalled Interupts (MSI)
    //      MSI-X (MSI expanded)
    //      PCI Express Caps
    //      Link Caps
    //      PCI Express Dev Caps 2
    //      Link Caps 2
    //      Advanced Error Reporting (AER) caps
    //  should be done something like below:
    //
    // this device's capabilities list starts at config space offset: 0x80 (with PM caps)
    //
    // Power Management capabilities
    // int pm_offset = 0x80;
    //
    // pci_add_capability(pciDev, PCI_CAP_ID_PM, pm_offset, PCI_PM_SIZEOF);
    //
    // PCI_PM_PMC - 0x5A03 meaning:
    //   5: 0-no D3cold, 1-D3hot, 0-No D2, 1-D1, 
    //   A: 1-D0, 0-No D2, 1-D1, 0-(part of 24-22 max curent)
    //   0: 00-(23-22 max current) 0-No device specificinit 0-reserved-bit
    //   3: 0-Not app to PCIe 000-implements V1.2 of PM Spec.
    // pci_set_word(pci_conf + pm_offset + PCI_PM_PMC, 0x5A03);
    //
    // PCI_PM_CTRL - 0x0008
    //   0: 0-PME messages disabled 000-reserved bits
    //   0: 000-reserved bits 0-PME Enable (disabled)
    //   0: 0000-reserved-bits
    //   8: 1-No soft reset 0-reserved-bit 00-power state (D0)
    // pci_set_word(pci_conf + pm_offset + PCI_PM_CTRL, 0x0008);
    //
    // PCI_PM_PPB_EXTENSIONS - 0x0000
    //   0000-optional register is not supported by this device
    // pci_set_word(pci_conf + pm_offset + PCI_PM_PPB_EXTENSIONS, 0x0000);
    //
    //
    // MSI Capabilities
    // MSI-X Capabilities
    // PCI Express Caps
    // Link Caps
    // PCI Express Dev Caps 2
    // Link Caps 2
    // Advanced Error Reporting (AER) caps


    // set interrupt regs...
    // pci_set_byte(pci_conf + PCI_INTERRUPT_LINE, 0x00);          // No interrupt line or pin yet...
    // SOP_LOG_NORM("%s(): Setting PCI Interrupt PIN A", __func__);
    // pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 0x01);
    // pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 0x00);
    // pci_set_byte(pci_conf + PCI_MIN_GNT, 0x3c);
    // pci_set_byte(pci_conf + PCI_MAX_LAT, 0x00);

    // n->dev.msix_cap = 0;
	
//	uint32_t b, d, f;
//	uint16_t vid, did;
//	
//	for (b=0; b<7; b++) {
//
//	    for (d=0; d<16; d++) {
//		
//		    for (f=0; f<8; f++) {
//			
//			    cpu_outl(0x0CF8, (((b & 0x00FF) << 16) | ((d & 0x000F) << 11) | ((f & 0x07) << 8) | PCI_VENDOR_ID));
//				vid = cpu_inw(0x0CFC);
//			    cpu_outl(0x0CF8, (((b & 0x00FF) << 16) | ((d & 0x000F) << 11) | ((f & 0x07) << 8) | PCI_DEVICE_ID));
//				did = cpu_inw(0x0CFC);
//			    SOP_LOG_NORM(" B-D-F: %02x-%02x-%02x : %04x-%04x", b,d,f, vid, did);
//           }
//		}
//	}

    /*other notation:  pci_config[OFFSET] = 0xff; */

    SOP_LOG_NORM("%s(): Setting PCI Interrupt PIN A", __func__);
    pci_conf[PCI_INTERRUPT_PIN] = 1;
    
    n->nvectors = PQI_MSIX_NVECTORS;
    n->bar0_size = PQI_REG_SIZE;
  
    SOP_LOG_NORM("%s() exit...", __func__);
}

static uint64_t pqi_mmio_read(void *opaque, hwaddr addr,
        unsigned size)
{
	SOP_LOG_DBG("%s: addr: 0x%x length:%d",__func__,(unsigned)addr,size);
	switch (size) {
		case 1:
			return pqi_mmio_readb(opaque,addr);
		case 2:
			return pqi_mmio_readw(opaque,addr);
		case 4:
			return pqi_mmio_readl(opaque,addr);
	}
    return 0;
}

static void pqi_mmio_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
	SOP_LOG_DBG("%s: addr: 0x%x length:%d",__func__,(unsigned)addr,size);
	switch (size) {
		case 1:
			return pqi_mmio_writeb(opaque,addr,val);
		case 2:
			return pqi_mmio_writew(opaque,addr,val);
		case 4:
			return pqi_mmio_writel(opaque,addr,val);
	}
}

static const MemoryRegionOps pqi_mmio_ops = {
    .read = pqi_mmio_read,
    .write = pqi_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*********************************************************************
 Function:         pci_pqi_init
 Description:      PQI initialization
 Return Type:      uint32_t
 Arguments:        PCIDevice*: Pointer to the PCI device
 TODO: Make any initialization here or when controller receives 'enable' bit?
 *********************************************************************/

static int pci_pqi_init(PCIDevice* pciDev) {

    PQIState* n = DO_UPCAST(PQIState, dev, pciDev);
    uint32_t ret = 0;
    static uint32_t instance;
//    FILE* pci_config_file;
//    FILE* pqi_config_file;

    SOP_LOG_NORM("%s(): pciDev = 0x%08lu", __func__, (uint64_t)pciDev);

    n->start_time = time(NULL);

    n->instance = instance++;

    // set up the state machine
    n->sm.state = PQI_DEVICE_STATE_PD0;         // State: Power On And Reset
    n->sm.reglock = PQI_REGISTERS_UNLOCKED;

    // TODO: pci_conf = n->dev.config;
    n->nvectors = PQI_MSIX_NVECTORS;
    n->bar0_size = PQI_REG_SIZE;

    // Reading the PCI space from the file
  
//    if ((pci_config_file = fopen((char*)PCI_CONFIG_FILE, "r")) != NULL) {
//
//        if (pqi_read_config_file(pci_config_file, n, PCI_SPACE)) {
//
//            SOP_LOG_ERR("Error %s() cannot read the PCI config file!", __func__);
//            SOP_LOG_NORM("Defaulting the PCI space..");
//            pqi_pci_space_init(&n->dev);
//        }
//
//        fclose(pci_config_file);
//
//    } else {
//
//        SOP_LOG_ERR("Error %s() cannot open PCI config file!", __func__);
//        SOP_LOG_NORM("Defaulting the PCI space..");
//        pqi_pci_space_init(&n->dev);
//    }
    pqi_pci_space_init(&n->dev);



    //ret = msix_init(&n->dev, n->nvectors, 0, n->bar0_size);
    char *name = g_strdup_printf("%s-msix", n->dev.name);

    memory_region_init(&n->mem_region, name, n->bar0_size*2);
    memory_region_init_io(&n->mem_region_mmio,
                          &pqi_mmio_ops, n,
                          "pqi-mmio", PQI_REG_SIZE);

    memory_region_add_subregion(&n->mem_region,0,&n->mem_region_mmio);

    g_free(name);

    ret = msix_init((struct PCIDevice *)&n->dev, n->nvectors,
    		&n->mem_region, 0, PQI_REG_SIZE,
    		&n->mem_region, 0,
    		PQI_REG_SIZE +( n->nvectors * PCI_MSIX_ENTRY_SIZE),
    		0);

    if (ret) {

        SOP_LOG_NORM("%s(): PCI MSI-X Failed", __func__);

    } else {

        SOP_LOG_NORM("%s(): PCI MSI-X Initialized", __func__);
    }
  
    SOP_LOG_NORM("%s(): Reg0 size %u, nvectors: %hu", __func__, n->bar0_size, n->nvectors);

    // Register BAR 0 (and bar 1 as it is 64bit)
    pci_register_bar(pciDev, 0, (PCI_BASE_ADDRESS_SPACE_MEMORY |
            PCI_BASE_ADDRESS_MEM_TYPE_64),
    		 &n->mem_region);

    SOP_LOG_NORM(" SOP BAR0 info:  region-0x00  size-0x%04x  type-0x%02x  LUNS-0x%02x",
	             n->bar0_size*2,
				 (PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64),
//	             (PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_32),
                 n->num_luns);

    // move the state machine to the next state
    n->sm.state = PQI_DEVICE_STATE_PD1;             // PCIe Configuration Space Ready
    n->sm.reglock = PQI_REGISTERS_UNLOCKED;

    // TODO: BWS - setting max luns to 1 for now!
    // TODO: RBB - setting max luns to 4 for now!
  
    if (n->num_luns == 0 || n->num_luns > SOP_MAX_NUM_LUNS) {

        SOP_LOG_ERR("Error %s() Bad number of LUNS value: %u, must be between 1 and %d", __func__, n->num_luns, SOP_MAX_NUM_LUNS);
        n->num_luns = SOP_MAX_NUM_LUNS;
		// return -1;
    }

    if (n->lun_size == 0 || n->lun_size > SOP_MAX_LUN_SIZE) {

        SOP_LOG_ERR("Error %s() Bad LUN size value: %u, must be between 1 and %d", __func__, n->lun_size, SOP_MAX_LUN_SIZE);
		return -1;
    }
    SOP_LOG_NORM("num_luns = %d. lun_size=%d",n->num_luns,n->lun_size);

    n->disk = (DiskInfo*)g_malloc(sizeof(DiskInfo) * n->num_luns);
	if (n->disk) {
	
        SOP_LOG_NORM(" qemu_malloc(0x%x) returned 0x%lu for n->disk (DiskInfo[%d])", 
		              (unsigned int)(sizeof(DiskInfo) * n->num_luns), 
					  (uint64_t)n->disk,
					  n->num_luns);

	} else {
	
        SOP_LOG_ERR(" Error! 'qemu_malloc(0x%x)' returned zero for n->disk", (unsigned int)(sizeof(DiskInfo) * n->num_luns));
		return -1;
	}

    // TODO: BWS - Initialize the admin queues
    SOP_LOG_NORM("Zeroing the admin IQ & OQ..");
    n->iq[AIQ_ID].pi = 0;
    n->iq[AIQ_ID].ci_addr = 0;
    n->iq[AIQ_ID].ci_work = 0;
    n->iq[AIQ_ID].ci_local = 0;
    n->iq[AIQ_ID].ea_addr = 0;
    n->iq[AIQ_ID].size = 0;
    n->oq[AOQ_ID].pi_addr = 0;
    n->oq[AOQ_ID].pi_work = 0;
    n->oq[AOQ_ID].pi_local = 0;
    n->oq[AOQ_ID].ci = 0;
    n->oq[AOQ_ID].ea_addr = 0;
    n->oq[AOQ_ID].size = 0;

    // Initialize (zero) the operational Queue Data Structures
    SOP_LOG_NORM("Zeroing the operational IQs & OQs..");
    uint32_t i;
    for (i = 1; i < PQI_MAX_QS_ALLOCATED; i++) {

        memset(&(n->oq[i]), 0, sizeof(PQIOutboundQueue));
        memset(&(n->iq[i]), 0, sizeof(PQIInboundQueue));
    }

    // Allocating space for SOP regspace & masks
    n->cntrl_reg = g_malloc0(PQI_CNTRL_SIZE);
	if (n->cntrl_reg) {
	
        SOP_LOG_NORM("qemu_malloc(0x%x) returned 0x%lu for n->cntrl_reg", PQI_CNTRL_SIZE, (uint64_t)n->cntrl_reg);

	} else {

        SOP_LOG_ERR("Error! 'g_malloc0(0x%x)' returned zero for n->cntrl_reg", (unsigned int)PQI_CNTRL_SIZE);

	}

    n->rw_mask = g_malloc0(PQI_CNTRL_SIZE);
	if (n->rw_mask) {
	
        SOP_LOG_NORM("qemu_malloc(0x%x) returned 0x%lu for n->rw_mask", PQI_CNTRL_SIZE, (uint64_t)n->rw_mask);

	} else {
	
        SOP_LOG_ERR("Error! 'g_malloc0(0x%x)' returned zero for n->rw_mask", (unsigned int)PQI_CNTRL_SIZE);
		
	}

    n->rwc_mask = g_malloc0(PQI_CNTRL_SIZE);
	if (n->rwc_mask) {
	
        SOP_LOG_NORM("qemu_malloc(0x%x) returned 0x%lu for n->rwc_mask", PQI_CNTRL_SIZE, (uint64_t)n->rwc_mask);

	} else {
	
        SOP_LOG_ERR("Error! 'g_malloc0(0x%x)' returned zero for n->rwc_mask", (unsigned int)PQI_CNTRL_SIZE);
	
	}

    n->rws_mask = g_malloc0(PQI_CNTRL_SIZE);
	if (n->rws_mask) {
	
        SOP_LOG_NORM("qemu_malloc(0x%x) returned 0x%lu for n->rws_mask", PQI_CNTRL_SIZE, (uint64_t)n->rws_mask);

	} else {
	
        SOP_LOG_ERR("Error! 'g_malloc0(0x%x)' returned zero for n->rws_mask", (unsigned int)PQI_CNTRL_SIZE);
	
	}

    n->used_mask = g_malloc0(PQI_CNTRL_SIZE);
	if (n->used_mask) {
	
        SOP_LOG_NORM("qemu_malloc(0x%x) returned 0x%lu for n->used_mask", PQI_CNTRL_SIZE, (uint64_t)n->used_mask);

	} else {
	
        SOP_LOG_ERR("Error! 'g_malloc0(0x%x)' returned zero for n->used_mask", (unsigned int)PQI_CNTRL_SIZE);
	
	}


    // Update PQI space registry from config file
  
//    if ((pqi_config_file = fopen((char*)PQI_CONFIG_FILE, "r")) != NULL) {
//
//        if (pqi_read_config_file(pqi_config_file, n, PQI_SPACE)) {
//
//            SOP_LOG_ERR("Error %s() could not read the PQI config file!", __func__);
//            SOP_LOG_NORM("Defaulting the PQI space..");
//            pqi_set_registry(n);
//        }
//
//        fclose(pqi_config_file);
//
//    } else {
//
//        SOP_LOG_NORM("Error %s() could not open the PQI config file!", __func__);
//        SOP_LOG_NORM("Defaulting the PQI space..");
//        pqi_set_registry(n);
//    }
    pqi_set_registry(n);

    for (ret = 0; ret < n->nvectors; ret++) {

        msix_vector_use(&n->dev, ret);
    }

    // Create the Storage Disk

    if (pqi_create_storage_disks(n)) {

        SOP_LOG_ERR("Error %s() could not create SOP disk", __func__);
    }

    // we should now be ready to accept PQI admin commands
    n->sm.state = PQI_DEVICE_STATE_PD2;
	
	
	// read some of the BAR space
    uint32_t rd_val[8];
	
	rd_val[0] = pqi_mmio_readl(n, PQI_SIG);
	rd_val[1] = pqi_mmio_readl(n, PQI_SIG+4);
	rd_val[2] = pqi_mmio_readl(n, PQI_AQ_CONFIG);
	rd_val[3] = pqi_mmio_readl(n, PQI_AQ_CONFIG+4);
	rd_val[4] = pqi_mmio_readl(n, PQI_CAP);
	rd_val[5] = pqi_mmio_readl(n, PQI_CAP+4);
	rd_val[6] = pqi_mmio_readl(n, PQI_INTS);
	rd_val[7] = pqi_mmio_readl(n, PQI_INTS+4);
    SOP_LOG_NORM(" 0x%04x: 0x%08x%08x 0x%08x%08x", PQI_SIG, rd_val[1], rd_val[0], rd_val[3], rd_val[2]); 
    SOP_LOG_NORM(" 0x%04x: 0x%08x%08x 0x%08x%08x", PQI_CAP, rd_val[5], rd_val[4], rd_val[7], rd_val[6]); 

    SOP_LOG_NORM("%s() exit...", __func__);
    return 0;
}


/*********************************************************************
 Function     :    pci_pqi_uninit
 Description  :    To unregister the PQI device from QEMU
 Return Type  :    void
 Arguments    :    PCIDevice * : Pointer to the PCI device
 *********************************************************************/

static void pci_pqi_uninit(PCIDevice* pciDev) {

    SOP_LOG_NORM("%s(): pciDev = 0x%08lu", __func__, (uint64_t)pciDev);

    PQIState* n = DO_UPCAST(PQIState, dev, pciDev);

    // Freeing space allocated for SOP regspace masks
    g_free(n->cntrl_reg);
    g_free(n->rw_mask);
    g_free(n->rwc_mask);
    g_free(n->rws_mask);
    g_free(n->used_mask);
    g_free(n->disk);

    pqi_close_storage_disks(n);
    SOP_LOG_NORM("Freed PQI device memory");

    SOP_LOG_NORM("%s() exit...", __func__);
    return;
}


void sop_pci_bus_init(PCIBus *bus)
{
    SOP_LOG_NORM("%s()", __func__);

    pci_create_simple(bus, -1, "soppqi");
    
    SOP_LOG_NORM("%s() exit...", __func__);
}

static Property soppqi_properties[] = {
        DEFINE_PROP_UINT32("luns", PQIState, num_luns, 1),
        DEFINE_PROP_UINT32("size", PQIState, lun_size, 1048576),
        DEFINE_PROP_STRING("wdir", PQIState, working_dir),
        DEFINE_PROP_END_OF_LIST(),
};

static void pqi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = pci_pqi_init;
    k->exit = pci_pqi_uninit;
    //k->romfile = "nvme.rom";
    k->vendor_id = PCI_VENDOR_ID_HGST;
    k->device_id = PQI_DEV_ID;
    k->revision = 0x02;
    k->class_id = (PCI_CLASS_STORAGE_EXPRESS >> 8);
    k->config_read = pqi_pci_read_config;
    k->config_write = pqi_pci_write_config;
    k->is_express = 1; /* ?? */
    dc->desc = "Non-Volatile Memory Express";
    dc->reset = qdev_pqi_reset;
    dc->vmsd = &vmstate_soppqi;
    dc->props = soppqi_properties;
};

static const TypeInfo pqi_info = {
		.name			= "soppqi",
		.parent			= TYPE_PCI_DEVICE,
		.instance_size	= sizeof(PQIState),
		.class_init		= pqi_class_init,
};

static inline void _sop_check_size(void)
{
    BUILD_BUG_ON(sizeof(sopLimitedCommandIU) != 64);
}
/*********************************************************************
 Function     :    sop_register_devices
 Description  :    Registering the SOP Device with Qemu
 Return Type  :    void
 Arguments    :    void
 *********************************************************************/

static void pqi_register_devices(void) {

    type_register_static(&pqi_info); // this function is from <qemu>/hw/pci.h
}


type_init(pqi_register_devices);
