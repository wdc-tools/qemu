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
#include "sop.h"


// host has written to the IQ's PI
// (possibly adding an element to the inbound queue)


// Administratior function dispatcher
// (T10/2240-D, PQI specification, section 9.2.1)

void sop_execute_admin_command(PQIState* pqiDev, PQIInboundQueue* iq, uint16_t ci) {

    uint8_t iu_type;
    uint8_t compatible_features;
    uint16_t iu_length;
    uint8_t function_code;

    SOP_LOG_DBG("%s(): called", __func__);

    uint8_t iu [ADM_IQ_ELEMENT_LENGTH];
    pqi_dma_mem_read(iq->ea_addr + (ci * ADM_IQ_ELEMENT_LENGTH), iu, ADM_IQ_ELEMENT_LENGTH);

    iu_type = (uint8_t)(iu)[0];
    compatible_features = (uint8_t)(iu)[1];
    iu_length = (uint16_t)(iu)[1];
    
    if ((iu_type == 0) && (compatible_features == 0) && (iu_length == 0)) {

        // NULL IU
        return;
    }

    if ((iu_type == ADMIN_IU_REQUEST) && (compatible_features == 0) && (iu_length == 0)) {

        function_code = (uint8_t)(iu)[10];
        SOP_LOG_DBG("%s(): function code = %x", __func__, function_code);
        switch ( function_code )
        {
        case REPORT_PQI_DEV_CAPABLIITY:
            admin_report_caps(pqiDev, (reportPqiDevCapReq*)iu);
            break;
        case REPORT_MANUFACTURING_INFO:
            admin_report_man_info(pqiDev, (reportManInfoReq*)iu);
            break;
        case CREATE_OPERATIONAL_IQ:
            admin_create_op_iq(pqiDev, (createOpIqReq*)iu);
            break;
        case CREATE_OPERATIONAL_OQ:
            admin_create_op_oq(pqiDev, (createOpOqReq*)iu);
            break;
        case DELETE_OPERATIONAL_IQ:   
            admin_delete_op_iq(pqiDev, (deleteOpIqReq*)iu);
            break;
        case DELETE_OPERATIONAL_OQ:   
            admin_delete_op_oq(pqiDev, (deleteOpOqReq*)iu);
            break;
        case CHANGE_OPERATIONAL_IQ_PROP:   
            admin_change_op_iq_props(pqiDev, (changeOpIqPropReq*)iu);
            break;
        case CHANGE_OPERATIONAL_OQ_PROP:   
            admin_change_op_oq_props(pqiDev, (changeOpOqPropReq*)iu);
            break;
        case REPORT_OPERATIONAL_IQ_LIST:   
            admin_report_op_iq_list(pqiDev, (reportOpIqListReq*)iu);
            break;
        case REPORT_OPERATIONAL_OQ_LIST:   
            admin_report_op_oq_list(pqiDev, (reportOpOqListReq*)iu);
            break;

        default:
            SOP_LOG_ERR("Error. Invalid IQ request function code: 0x%x", function_code);
            break;
        }
        return;
    }
}


// Report PQI Device Caability
// (T10/2240-D, PQI specification, section 9.2.2)

void admin_report_caps(PQIState* pqiDev, reportPqiDevCapReq* iu) {

    uint32_t rVal = SUCCESS;
    reportPqiDevCapParmData capParam;

    SOP_LOG_DBG("%s(): called", __func__);

    // capabilities to be retuned are held in this structure (capParam)
    // which need to be written into the data buffer(s) of the SGL(s)

    // first scrub the caps struct...
    memset(&capParam, 0, sizeof(reportPqiDevCapParmData));

    // then put valid info into it...
    capParam.length = 0x3E;                             // hard-code what "should" be ???
    capParam.maxOpIqs = PQI_MAX_QS_ALLOCATED -1;
    capParam.maxOpIqElements = pqiDev->iq[AIQ_ID].size;
    capParam.maxOpIqElementLength = 0xFF;               // 0xFF means 4080
    capParam.minOpIqElementLength = 0x04;               // 0x04 means 64
    capParam.maxOpOqs = PQI_MAX_QS_ALLOCATED - 1;
    capParam.maxOpOqElements = 4;                       // 4 means 64
    capParam.CIC = 0;                                   // no int coalescing?
    // capParam.intCoalescingTimeGran = ??;
    capParam.maxOpOqElementLength = 0xFF;               // 0xFF means 4080
    capParam.minOpOqElementLengty = 0x04;               // 0x04 means 64
    capParam.opIqElementArrayAddrAlignmentExp = 2;      // 4 byte boundaries ?
    capParam.opOqElementArrayAddrAlignmentExp = 2;
    capParam.opIqCiAddrAlignmentExp = 2;
    capParam.opOqPiAddrAlignmentExp = 2;
    capParam.opQueProtocolSupportBitmask = 0;           // SOP Supported
    capParam.adminSglDescTypeSupportBitmask = 0x0F;     // Support only manditory types

    // put Info inside the SGL...
    rVal = copy_to_sgl(&iu->sglDescriptor, (uint8_t*)&capParam, iu->dataInBufferSize);

    // build the response 
    reportPqiDevCapRsp capRsp;
    memset(&capRsp, 0, sizeof(reportPqiDevCapRsp));

    capRsp.header.type = 0xE0;
    capRsp.header.feat = 0x00;
    capRsp.header.length = 0x003C;
    capRsp.functionCode = 0x00;

    if ( rVal == SUCCESS ) {

        capRsp.status = ADM_STAT_GOOD;

    } else {

        capRsp.status = ADM_STAT_DATA_BUF_ERROR;
    } 

    // send it
    post_to_oq(pqiDev, AOQ_ID, (void*)&capRsp, (int)sizeof(reportPqiDevCapRsp));
}


// Report Manufacturer Information
// (T10/2240-D, PQI specification, section 9.2.3)

void admin_report_man_info(PQIState* pqiDev, reportManInfoReq* iu) {

    uint32_t rVal = SUCCESS;
    uint32_t i = 0;
    reportManInfoPrmData manInfoParam;

    // Manufacturing info to be retuned is held in "manInfoParam"
    // which need to be written into the data buffer(s) of the SGL(s)

    // first scrub the info struct...
    memset(&manInfoParam, 0, sizeof(reportManInfoPrmData));          

    // then put valid info into it...
    manInfoParam.length = 0x7E;                                         
    manInfoParam.pciVendorId = PCI_VENDOR_ID_HGST;
    manInfoParam.pciDeviceId = PQI_DEV_ID;
    manInfoParam.pciRevisionId = 0x03;
    manInfoParam.pciProgInterface = (0xf & PCI_CLASS_STORAGE_EXPRESS);  // TODO: RBB what is it for PQI/SOP?
    manInfoParam.pciClassCode = PCI_CLASS_STORAGE_EXPRESS >> 8;
    manInfoParam.pciSubsystemVendorId = 0x1AF4;                         // TODO: RBB put HGST VID/DID? QEMU puts these values: 1AF4/1100
    manInfoParam.pciSubsystemId = 0x1100;

    for ( i=0; i < sizeof(manInfoParam.productSerialNumber); i++ )      // TODO: RBB what's the drive SN?
    {
        manInfoParam.productSerialNumber[i] = ' ';
    }

    manInfoParam.T10VendorId[0] = 'H';  // "HGST"
    manInfoParam.T10VendorId[1] = 'G';
    manInfoParam.T10VendorId[2] = 'S';
    manInfoParam.T10VendorId[3] = 'T';
    for ( i=4; i < sizeof(manInfoParam.T10VendorId); i++ )
    {
        manInfoParam.T10VendorId[i] = ' ';
    }

    manInfoParam.productId[0] = 'S';    // "SOP-DEV-A"
    manInfoParam.productId[1] = 'O';
    manInfoParam.productId[2] = 'P';
    manInfoParam.productId[3] = '-';
    manInfoParam.productId[4] = 'D';
    manInfoParam.productId[5] = 'E';
    manInfoParam.productId[6] = 'V';
    manInfoParam.productId[7] = '-';
    manInfoParam.productId[8] = 'A';
    for ( i=9; i < sizeof(manInfoParam.productId); i++ )
    {
        manInfoParam.productId[i] = ' ';
    }

    manInfoParam.productRevLevel[0] = '0';  // "0.01"
    manInfoParam.productRevLevel[1] = '.';
    manInfoParam.productRevLevel[2] = '0';
    manInfoParam.productRevLevel[3] = '1';
    for ( i=4; i < sizeof(manInfoParam.productRevLevel); i++ )
    {
        manInfoParam.productRevLevel[i] = ' ';
    }

    // put Info inside the SGL...
    rVal = copy_to_sgl(&iu->sglDescriptor, (uint8_t*)&manInfoParam, iu->dataInBufferSize);

    // build the response 
    reportManInfoRsp manInfoRsp;
    memset(&manInfoRsp, 0, sizeof(reportManInfoRsp));

    manInfoRsp.header.type = 0xE0;
    manInfoRsp.header.feat = 0x00;
    manInfoRsp.header.length = 0x003C;
    manInfoRsp.functionCode = 0x01;

    if ( rVal == SUCCESS ) {

        manInfoRsp.status = ADM_STAT_GOOD;

    } else {

        manInfoRsp.status = ADM_STAT_DATA_BUF_ERROR;
    } 

    // send it
    post_to_oq(pqiDev, AOQ_ID, (void*)&manInfoRsp, (int)sizeof(reportManInfoRsp));
}


// Create OP IQ
// (T10/2240-D, PQI specification, section 9.2.4)

void admin_create_op_iq(PQIState* pqiDev, createOpIqReq* iu) {

    SOP_LOG_NORM("%s(): called for qid:%d", __func__,iu->iqId);

    if ( iu->iqId == AIQ_ID ) {

        // error - cannot create OP IQ with Admin queie ID
        admin_create_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( iu->iqId >= PQI_MAX_QS_ALLOCATED ) {

        // error - queue id out of range
        admin_create_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( pqiDev->iq[iu->iqId].id || pqiDev->iq[iu->iqId].ea_addr ) {

        // error - queue already created (or seems to be)
        admin_create_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ((iu->numberOfElements < 2) || (iu->numberOfElements > 256)) {

        // error - illegal number of elements
        admin_create_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 32);

    } else if ((iu->elementLength < 0x04) || (iu->elementLength > 0xFF)) {

        // error - illegal element size
        admin_create_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 34);

    } else if (iu->opQueueProtocol != 0x00) {

          // error - illegal element size
          admin_create_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 36);

    } else {

        PQIInboundQueue* ibQ = &pqiDev->iq[iu->iqId];

        // initialize the queue array
        ibQ->id = iu->iqId;
        ibQ->pi = 0;
        ibQ->ci_addr = iu->iqCiAddress;
        ibQ->ci_work = 0;
        ibQ->ci_local = 0;
        ibQ->ea_addr = iu->iqElementArrayAddress & ELEMENT_ARRAY_ADDR_MASK;
        ibQ->length = iu->elementLength;
        ibQ->size = iu->numberOfElements;

        SOP_LOG_NORM("%s(): created op_iq id:%d elem_len=%d, elem_ct=%d, ci_addr=0x%lx",
        		__func__,ibQ->id,ibQ->length,ibQ->size,ibQ->ci_addr);

        admin_create_op_iq_response(pqiDev, iu, ADM_STAT_GOOD, 0x00);
    }
}


// Create OP OQ
// (T10/2240-D, PQI specification, section 9.2.5)

void admin_create_op_oq(PQIState* pqiDev, createOpOqReq* iu) {

    SOP_LOG_NORM("%s(): called for qid:%d", __func__,iu->oqId);

    if ( iu->oqId == AOQ_ID ) {

        // error - cannot create OP IQ with Admin queue ID
        admin_create_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( iu->oqId >= PQI_MAX_QS_ALLOCATED ) {

        // error - queue id out of range
        admin_create_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( pqiDev->oq[iu->oqId].id || pqiDev->oq[iu->oqId].ea_addr ) {

        // error - queue already created (or seems to be)
        admin_create_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ((iu->numberOfElements < 2) || (iu->numberOfElements > 256)) {

        // error - illegal number of elements
        admin_create_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 32);

    } else if ((iu->elementLength < 0x04) || (iu->elementLength > 0xFF)) {

        // error - illegal element size
        admin_create_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 34);

    } else if (iu->opQueueProtocol != 0x00) {

          // error - illegal element size
          admin_create_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 46);

    } else {

        PQIOutboundQueue* obQ = &pqiDev->oq[iu->oqId];

        // initialize the queue array
        obQ->id = iu->oqId;
        obQ->ci = 0;
        obQ->pi_addr = iu->oqPiAddress;
        obQ->pi_work = 0;
        obQ->pi_local = 0;
        obQ->ea_addr = iu->oqElementArrayAddress & ELEMENT_ARRAY_ADDR_MASK;
        obQ->size = iu->numberOfElements;
        obQ->msixEntry = iu->intMsgNumber & OP_OQ_INT_MESSAGE_NUMBER_MASK;
        obQ->ea_addr = iu->oqElementArrayAddress;
        obQ->length = iu->elementLength;
        obQ->protocol = iu->opQueueProtocol;
        obQ->size = iu->numberOfElements;
        obQ->msixEntry = iu->intMsgNumber;
        obQ->waitForRearm = iu->wairForRearm;
        obQ->coCount = iu->coalescingCount;
        obQ->minCoTime = iu->minCoalescingTime;
        obQ->maxCoTime = iu->maxCoalescingTime;

        SOP_LOG_NORM("%s(): created op_iq id:%d elem_len=%d, elem_ct=%d, ci_addr=0x%lx",
        		__func__,obQ->id,obQ->length,obQ->size,obQ->pi_addr);

        admin_create_op_oq_response(pqiDev, iu, ADM_STAT_GOOD, 0x00);
    }
}


// Delete OP IQ
// (T10/2240-D, PQI specification, section 9.2.6)

void admin_delete_op_iq(PQIState* pqiDev, deleteOpIqReq* iu) {

    SOP_LOG_NORM("%s(): called", __func__);

    if ( iu->iqId == AIQ_ID ) {

        // error - cannot delete Admin queie ID via this message
        admin_delete_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( iu->iqId >= PQI_MAX_QS_ALLOCATED ) {

        // error - queue id out of range
        admin_delete_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( (pqiDev->iq[iu->iqId].id == 0) || (pqiDev->iq[iu->iqId].ea_addr == 0)) {

        // error - queue already deleted (or seems to be)
        admin_delete_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else {

        PQIInboundQueue* ibQ = &pqiDev->iq[iu->iqId];

        // zap the queue status
        ibQ->id = 0;
        ibQ->pi = 0;
        ibQ->ci_addr = 0;
        ibQ->ci_work = 0;
        ibQ->ci_local = 0;
        ibQ->ea_addr = 0;
        ibQ->size = 0;

        admin_delete_op_iq_response(pqiDev, iu, ADM_STAT_GOOD, 0x00);
    }
}


// Delete OP OQ
// (T10/2240-D, PQI specification, section 9.2.7)

void admin_delete_op_oq(PQIState* pqiDev, deleteOpOqReq* iu) {

    SOP_LOG_NORM("%s(): called", __func__);

    if ( iu->oqId == AOQ_ID ) {

        // error - cannot delete Admin queie with this message
        admin_delete_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( iu->oqId >= PQI_MAX_QS_ALLOCATED ) {

        // error - queue id out of range
        admin_delete_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( pqiDev->oq[iu->oqId].id || pqiDev->oq[iu->oqId].ea_addr ) {

        // error - queue already created (or seems to be)
        admin_delete_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else {

        admin_delete_op_oq_response(pqiDev, iu, ADM_STAT_GOOD, 0x00);

        PQIOutboundQueue* obQ = &pqiDev->oq[iu->oqId];

        // zap the queue
        obQ->id = 0;
        obQ->ci = 0;
        obQ->pi_addr = 0;
        obQ->pi_work = 0;
        obQ->pi_local = 0;
        obQ->ea_addr = 0;
        obQ->size = 0;
        obQ->msixEntry = 0;
    }
}


// Change OP IQ properties
// (T10/2240-D, PQI specification, section 9.2.8)

void admin_change_op_iq_props(PQIState* pqiDev, changeOpIqPropReq* iu) {

    SOP_LOG_NORM("%s(): called", __func__);

    if ( iu->iqId == AIQ_ID ) {

        // error - cannot change OP IQ with Admin queie ID
        admin_change_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( iu->iqId >= PQI_MAX_QS_ALLOCATED ) {

        // error - queue id out of range
        admin_change_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( !pqiDev->iq[iu->iqId].id || !pqiDev->iq[iu->iqId].ea_addr ) {

        // error - queue is not assigned
        admin_change_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else {

        // TODO: What is to be "changed" ?????

        admin_change_op_iq_response(pqiDev, iu, ADM_STAT_GOOD, 0x00);
    }
}


// Change OP OQ properties
// (T10/2240-D, PQI specification, section 9.2.9)

void admin_change_op_oq_props(PQIState* pqiDev, changeOpOqPropReq* iu) {

    SOP_LOG_NORM("%s(): called", __func__);

    if ( iu->oqId == AOQ_ID ) {

        // error - cannot change OP OQ with Admin queie ID
        admin_change_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( iu->oqId >= PQI_MAX_QS_ALLOCATED ) {

        // error - queue id out of range
        admin_change_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else if ( !pqiDev->oq[iu->oqId].id || !pqiDev->oq[iu->oqId].ea_addr ) {

        // error - queue is not assigned
        admin_change_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 12);

    } else {

        // TODO: What is to be "changed" ?????

        admin_change_op_oq_response(pqiDev, iu, ADM_STAT_GOOD, 0x00);
    }
}


// Report OP IQ List 
// (T10/2240-D, PQI specification, section 9.2.10)

void admin_report_op_iq_list(PQIState* pqiDev, reportOpIqListReq* iu) {

    uint32_t rVal = SUCCESS;
    uint32_t listSize, i = 0;
    uint16_t numberOfIqProps = 0;
    uint16_t firstIq = 0;
    uint8_t* pBuff;
    reportOpIqListParmDataHeader* pOpIqListHeader;
    reportOpIqPropDescriptor* pOpIqPropDesc;
    PQIInboundQueue* pIq;

    SOP_LOG_NORM("%s(): called", __func__);

    if ( (iu->dataInBufferSize == 0x00) || 
         (iu->dataInBufferSize > sizeof(reportOpIqListParmDataHeader)) ) {

        // warning - zero data-in-buffer size -OR- not enough to put even 1 descriptor inside...
        SOP_LOG_ERR("Error, data-in-buffer size is zero");
        admin_report_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 44);  // TODO: FIX FIX FIX

        return;
    }
    
    // determine how many OP IQ props there are 
    for ( i=1; i < PQI_MAX_QS_ALLOCATED; i++ )
    {
        if ( pqiDev->iq[i].id || pqiDev->iq[i].ea_addr ) {

            if ( !firstIq ) {

                firstIq = i;
            }

            numberOfIqProps += 1;
        }
    }

    if ( numberOfIqProps ) {

        listSize = sizeof(reportOpIqListParmDataHeader) +
                   (sizeof(reportOpIqPropDescriptor) * numberOfIqProps);

        // Adjust how many props can be returned (to fit data-in-buffer size)
        while ( listSize > iu->dataInBufferSize ) {

            listSize -= sizeof(reportOpIqListParmDataHeader);
        }

        pBuff = (uint8_t*)g_malloc0(listSize);

        if (!pBuff) {
    
            SOP_LOG_ERR("Error. could not allocate buffer for report OP IQ list, %d props", numberOfIqProps);

            // error - cannot report back (cannot get enough memory)
            admin_report_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 44);  // TODO: FIX FIX FIX

            return;
        }

    } else {

        // warning - no OP IQs to send back
        SOP_LOG_ERR("Error, no OP IQs in the list to report");
        admin_report_op_iq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 44);
        
        return;
    }

    // build the response prop list
    pOpIqListHeader = (reportOpIqListParmDataHeader*)&pBuff[0];
    pOpIqListHeader->number = numberOfIqProps;
    
    pIq = &pqiDev->iq[firstIq];
    pOpIqPropDesc = (reportOpIqPropDescriptor*)&pBuff[sizeof(reportOpIqListParmDataHeader)];
    
    while ( numberOfIqProps ) {

        pOpIqPropDesc->iqId                  = pIq->id;
        pOpIqPropDesc->iqError               = 0;
        pOpIqPropDesc->iqElementArrayAddress = pIq->ea_addr;
        pOpIqPropDesc->iqCiAddress           = pIq->ci_addr;
        pOpIqPropDesc->numberOfElements      = pIq->size;
        pOpIqPropDesc->elementLength         = pIq->length;
        pOpIqPropDesc->protocol              = pIq->protocol;
        pOpIqPropDesc->vendorSpecific        = pIq->vendor;
        pOpIqPropDesc->iqPiOffset            = PQI_IQ_PI_BASE + (pIq->id * 8);

        pOpIqPropDesc++;
	numberOfIqProps--;
    }

    // put Info inside the SGL & release scratch buffer
    rVal = copy_to_sgl(&iu->sglDescriptor, pBuff, listSize);
    g_free(pBuff);

    if ( rVal == SUCCESS ) {

        admin_report_op_iq_response(pqiDev, iu, ADM_STAT_GOOD, 0x00);

    } else {

        admin_report_op_iq_response(pqiDev, iu, ADM_STAT_DATA_BUF_ERROR, 0x00);
    
    }
}


// Report OP OQ List 
// (T10/2240-D, PQI specification, section 9.2.11)

void admin_report_op_oq_list(PQIState* pqiDev, reportOpOqListReq* iu) {

    uint32_t rVal = SUCCESS;
    uint32_t listSize, i = 0;
    uint16_t numberOfOqProps = 0;
    uint16_t firstOq = 0;
    uint8_t* pBuff;
    reportOpOqListParmDataHeader* pOpOqListHeader;
    reportOpOqPropDescriptor* pOpOqPropDesc;
    PQIOutboundQueue* pOq;

    SOP_LOG_NORM("%s(): called", __func__);

    if ( (iu->dataInBufferSize == 0x00) || 
         (iu->dataInBufferSize > sizeof(reportOpOqListParmDataHeader)) ) {

        // warning - zero data-in-buffer size -OR- not enough to put even 1 descriptor inside...
        SOP_LOG_ERR("Error, data-in-buffer size is zero");
        admin_report_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 44);  // TODO: FIX FIX FIX

        return;
    }
    
    // determine how many OP IQ props there are 
    for ( i=1; i < PQI_MAX_QS_ALLOCATED; i++ )
    {
        if ( pqiDev->iq[i].id || pqiDev->iq[i].ea_addr ) {

            if ( !firstOq ) {

                firstOq = i;
            }

            numberOfOqProps += 1;
        }
    }

    if ( numberOfOqProps ) {

        listSize = sizeof(reportOpOqListParmDataHeader) +
                   (sizeof(reportOpOqPropDescriptor) * numberOfOqProps);

        // Adjust how many props can be returned (to fit data-in-buffer size)
        while ( listSize > iu->dataInBufferSize ) {

            listSize -= sizeof(reportOpOqListParmDataHeader);
        }

        pBuff = (uint8_t*)g_malloc0(listSize);

        if (!pBuff) {
    
            SOP_LOG_ERR("Error. could not allocate buffer for report OP IQ list, %d props", numberOfOqProps);

            // error - cannot report back (cannot get enough memory)
            admin_report_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 44);  // TODO: FIX FIX FIX

            return;
        }

    } else {

        // warning - no OP IQs to send back
        SOP_LOG_ERR("Error, no OP IQs in the list to report");
        admin_report_op_oq_response(pqiDev, iu, ADM_STAT_INVALID_FIELD_IN_REQ_IU, 44);
        
        return;
    }

    // build the response prop list
    pOpOqListHeader = (reportOpOqListParmDataHeader*)&pBuff[0];
    pOpOqListHeader->number = numberOfOqProps;

    pOpOqPropDesc = (reportOpOqPropDescriptor*)&pBuff[sizeof(reportOpOqListParmDataHeader)];
    
    pOq = &pqiDev->oq[firstOq];
    while ( numberOfOqProps-- ) {

        pOpOqPropDesc->oqId                   = pOq->id;
        pOpOqPropDesc->oqError                = 0;       	
        pOpOqPropDesc->oqElementArrayAddress  = pOq->ea_addr;
        pOpOqPropDesc->oqPiAddress            = pOq->pi_addr;
        pOpOqPropDesc->numberOfElements       = pOq->size;
        pOpOqPropDesc->elementLength          = pOq->length;
        pOpOqPropDesc->intMessageNumber       = pOq->msixEntry;
        pOpOqPropDesc->waitForRearm           = pOq->waitForRearm;
        pOpOqPropDesc->coalescingCount        = pOq->coCount;
        pOpOqPropDesc->minCoalescingTime      = pOq->minCoTime;
        pOpOqPropDesc->maxCoalescingTime      = pOq->maxCoTime;
        pOpOqPropDesc->protocol               = pOq->protocol;
        pOpOqPropDesc->vendorSpecific         = pOq->vendor;
        pOpOqPropDesc->oqCiOffset             = PQI_OQ_CI_BASE + (pOq->id * 8);

        pOpOqPropDesc++;
    }

    // put Info inside the SGL & release scratch buffer
    rVal = copy_to_sgl(&iu->sglDescriptor, pBuff, listSize);
    g_free(pBuff);

    if ( rVal == SUCCESS ) {

        admin_report_op_oq_response(pqiDev, iu, ADM_STAT_GOOD, 0x00);

    } else {

        admin_report_op_oq_response(pqiDev, iu, ADM_STAT_DATA_BUF_ERROR, 0x00);
    
    }
}


// send response for create OP IQ queue good/error conditions
// (T10/2240-D, PQI specification, section 9.2.4)

void admin_create_op_iq_response(PQIState* pqiDev, createOpIqReq* iu, uint32_t  status, uint32_t  add_status) {

    // build response
    createOpIqRsp opIqRsp;
    memset(&opIqRsp, 0, sizeof(reportPqiDevCapRsp));

    opIqRsp.header.type = 0xE0;
    opIqRsp.header.feat = 0x00;
    opIqRsp.header.length = 0x003C;
    // opIqRsp.qisd = iu->qisd;
    opIqRsp.requestIdentifier = iu->requestIdentifier;
    opIqRsp.functionCode = 0x10;
    opIqRsp.status = status;

    if (add_status) {

        opIqRsp.additionalStatusDescriptor = add_status;
    }

    if (status == ADM_STAT_GOOD) {

        opIqRsp.iqPiOffset = (uint64_t)(PQI_IQ_PI_REG(iu->iqId));
    }

    post_to_oq(pqiDev, AOQ_ID, (void*)&opIqRsp, (int)sizeof(createOpIqRsp));
}


// send response for create OP OQ queue good/error conditions
// (T10/2240-D, PQI specification, section 9.2.5)

void admin_create_op_oq_response(PQIState* pqiDev, createOpOqReq* iu, uint32_t  status, uint32_t  add_status) {

    // build response
    createOpOqRsp opOqRsp;
    memset(&opOqRsp, 0, sizeof(createOpOqRsp));

    opOqRsp.header.type = 0xE0;
    opOqRsp.header.feat = 0x00;
    opOqRsp.header.length = 0x003C;
    // opOqRsp.qisd = iu->qisd;
    opOqRsp.requestIdentifier = iu->requestIdentifier;
    opOqRsp.functionCode = 0x11;
    opOqRsp.status = status;

    if (add_status) {

        opOqRsp.additionalStatusDescriptor = add_status;
    }

    if (status == ADM_STAT_GOOD) {

        opOqRsp.oqCiOffset = (uint64_t)(PQI_OQ_CI_REG(iu->oqId));
    }

    post_to_oq(pqiDev, AOQ_ID, (void*)&opOqRsp, (int)sizeof(createOpOqRsp));

}


// send response for delete OP IQ queue good/error conditions
// (T10/2240-D, PQI specification, section 9.2.6)

void admin_delete_op_iq_response(PQIState* pqiDev, deleteOpIqReq* iu, uint32_t  status, uint32_t  add_status) {

    // build response
    deleteOpIqRsp opIqRsp;
    memset(&opIqRsp, 0, sizeof(deleteOpIqRsp));

    opIqRsp.header.type = 0xE0;
    opIqRsp.header.feat = 0x00;
    opIqRsp.header.length = 0x003C;
    // opIqRsp.qisd = iu->qisd;
    opIqRsp.requestIdentifier = iu->requestIdentifier;
    opIqRsp.functionCode = 0x12;
    opIqRsp.status = status;

    if (add_status) {

        opIqRsp.additionalStatusDescriptor = add_status;
    }

    post_to_oq(pqiDev, AOQ_ID, (void*)&opIqRsp, (int)sizeof(deleteOpIqRsp));

}


// send response for delete OP OQ queue good/error conditions
// (T10/2240-D, PQI specification, section 9.2.7)

void admin_delete_op_oq_response(PQIState* pqiDev, deleteOpOqReq* iu, uint32_t  status, uint32_t  add_status) {

    // build response
    deleteOpOqRsp opOqRsp;
    memset(&opOqRsp, 0, sizeof(deleteOpOqRsp));

    opOqRsp.header.type = 0xE0;
    opOqRsp.header.feat = 0x00;
    opOqRsp.header.length = 0x003C;
    // opOqRsp.qisd = iu->qisd;
    opOqRsp.requestIdentifier = iu->requestIdentifier;
    opOqRsp.functionCode = 0x13;
    opOqRsp.status = status;

    if (add_status) {

        opOqRsp.additionalStatusDescriptor = add_status;
    }

    post_to_oq(pqiDev, AOQ_ID, (void*)&opOqRsp, (int)sizeof(deleteOpOqRsp));

}


// send response for change OP IQ queue good/error conditions
// (T10/2240-D, PQI specification, section 9.2.8)

void admin_change_op_iq_response(PQIState* pqiDev, changeOpIqPropReq* iu, uint32_t  status, uint32_t  add_status) {

    // build response
    changeOpIqPropRsp opIqRsp;
    memset(&opIqRsp, 0, sizeof(reportPqiDevCapRsp));

    opIqRsp.header.type = 0xE0;
    opIqRsp.header.feat = 0x00;
    opIqRsp.header.length = 0x003C;
    // opIqRsp.qisd = iu->qisd;
    opIqRsp.requestIdentifier = iu->requestIdentifier;
    opIqRsp.functionCode = 0x14;
    opIqRsp.status = status;

    if (add_status) {

        opIqRsp.additionalStatusDescriptor = add_status;
    }

    post_to_oq(pqiDev, AOQ_ID, (void*)&opIqRsp, (int)sizeof(changeOpIqPropRsp));

}


// send response for change OP OQ queue good/error conditions
// (T10/2240-D, PQI specification, section 9.2.9)

void admin_change_op_oq_response(PQIState* pqiDev, changeOpOqPropReq* iu, uint32_t  status, uint32_t  add_status) {

    // build response
    changeOpOqPropRsp opOqRsp;
    memset(&opOqRsp, 0, sizeof(changeOpOqPropRsp));

    opOqRsp.header.type = 0xE0;
    opOqRsp.header.feat = 0x00;
    opOqRsp.header.length = 0x003C;
    // opOqRsp.qisd = iu->qisd;
    opOqRsp.requestIdentifier = iu->requestIdentifier;
    opOqRsp.functionCode = 0x15;
    opOqRsp.status = status;

    if (add_status) {

        opOqRsp.additionalStatusDescriptor = add_status;
    }

    post_to_oq(pqiDev, AOQ_ID, (void*)&opOqRsp, (int)sizeof(changeOpOqPropRsp));

}


// send response for report OP IQ list props (good/error conditions)
// (T10/2240-D, PQI specification, section 9.2.10)

void admin_report_op_iq_response(PQIState* pqiDev, reportOpIqListReq* iu, uint32_t status, uint32_t  add_status) {

    // build the response 
    reportOpIqListRsp listRsp;
    memset(&listRsp, 0, sizeof(reportOpIqListRsp));

    listRsp.header.type = 0xE0;
    listRsp.header.feat = 0x00;
    listRsp.header.length = 0x003C;
    // listRsp.qisd = iu->qisd;
    listRsp.functionCode = 0x16;
    listRsp.status = status;

    if (add_status) {

        listRsp.additionalStatusDescriptor = add_status;
    }

    // send it
    post_to_oq(pqiDev, AOQ_ID, (void*)&listRsp, (int)sizeof(reportOpIqListRsp));
}


// send response for report OP OQ list props good/error conditions
// (T10/2240-D, PQI specification, section 9.2.11)

void admin_report_op_oq_response(PQIState* pqiDev, reportOpOqListReq* iu, uint32_t status, uint32_t  add_status) {

    // build the response 
    reportOpOqListRsp listRsp;
    memset(&listRsp, 0, sizeof(reportOpOqListRsp));

    listRsp.header.type = 0xE0;
    listRsp.header.feat = 0x00;
    listRsp.header.length = 0x003C;
    // listRsp.qisd = iu->qisd;
    listRsp.functionCode = 0x16;
    listRsp.status = status;

    if (add_status) {

        listRsp.additionalStatusDescriptor = add_status;
    }

    // send it
    post_to_oq(pqiDev, AOQ_ID, (void*)&listRsp, (int)sizeof(reportOpOqListRsp));
}


/*****************************************************************************
 Function:         copy_to_sgl
 Description:      common function to put/take data to/from an SGL
 Return Type:      uint32_t - 0: SUCCESS
                              1: FAIL ---- sgl_bit_bucket_zero_byte_error
                              2: FAIL +1 - sgl_std_last_seg_zero_byte_error
                              3: FAIL +2 - sgl_std_seg_zero_byte_error
                              4: FAIL +3 - sgl_data_block_zero_byte_error
                              5: FAIL +4 - data_length_error
                              6: FAIL +5 - Vendor-Specific SGL not supported
                              7: FAIL +6 - unsupported SGL type
                              8: FAIL +7 - bad last segment
                              9: FAIL +8 - buffer to small
                             10: FAIL +9 - bad segment
                              
 Arguments:        sglDesc* sgl     SGL to put data into
                   uint8_t* pData   data to put into SGL
                   uint32_t len     number of bytes to put into the SGL

 Reference: T10/2240-D, PQI specification, section 7.3 and Annex A
 ****************************************************************************/
uint32_t copy_to_sgl(sglDesc* sgl, uint8_t* pData, uint32_t len) {
	return copy_sgl(sgl,pData,len,1);
}
uint32_t copy_from_sgl(sglDesc* sgl, uint8_t* pData, uint32_t len) {
	return copy_sgl(sgl,pData,len,0);
}
uint32_t copy_sgl(sglDesc* sgl, uint8_t* pData, uint32_t len,uint8_t direction) {

    hwaddr sglAddr;
    uint32_t sglSegmentCount=2;
    uint32_t sglDescSize=16;

    SOP_LOG_DBG("%s()", __func__);

    int last = false;
    sglDesc* currentSgl = sgl;
    int sgl_seg_index = 0;

    // TODO: add error handling...
    //       The following code assumes no errors (assumes properly build SGL lists)

    while (len) {
    	if (sgl_seg_index >= sglSegmentCount) {
    		// ran out of sgl segments with more data to xfer?
    		goto sgl_too_small_error;
    	}

        SOP_LOG_DBG("%s():sgl desc = 0x%08x 0x%08x 0x%08x 0x%08x", __func__,
        		((uint32_t*)&(currentSgl[sgl_seg_index]))[0],
        		((uint32_t*)&(currentSgl[sgl_seg_index]))[1],
        		((uint32_t*)&(currentSgl[sgl_seg_index]))[2],
        		((uint32_t*)&(currentSgl[sgl_seg_index]))[3]);
        SOP_LOG_DBG("type = %hhx, 'zero' = %hhx", currentSgl[sgl_seg_index].type, currentSgl[sgl_seg_index].zero);

        switch ( currentSgl[sgl_seg_index].type )
        {
        case SGL_DATA_BLOCK:                        // SGL type that transfers data
            if (currentSgl[sgl_seg_index].zero == 0) {

                sglAddr = currentSgl[sgl_seg_index].desc.data.address;   // Data Address

                // transfer data (some or all) to destination buffer
                SOP_LOG_DBG("%s():writing 0x%08x bytes to 0x%16llx", __func__, currentSgl[sgl_seg_index].desc.data.length, (long long unsigned)sglAddr);
                if (direction) {
                	pqi_dma_mem_write(sglAddr, pData, (int)currentSgl[sgl_seg_index].desc.data.length);
                } else {
                	pqi_dma_mem_read(sglAddr, pData, (int)currentSgl[sgl_seg_index].desc.data.length);
                }
                pData += currentSgl[sgl_seg_index].desc.data.length;

                if (len >= currentSgl[sgl_seg_index].desc.data.length) {

                    len -= currentSgl[sgl_seg_index].desc.data.length;

                } else {

                    goto data_length_error;
                }

                ++sgl_seg_index;             // next SGL

            } else {

                goto sgl_data_block_zero_byte_error;
            }
            break;

        case SGL_STANDARD_SEGMENT:                  // SGL type that points to another SGL segment
        	if (last) {
        		// This type of descriptor is not allowed in the SGL_STANDARD_LAST_SEGMENT
        		goto bad_last_sgl_error;
        	}
            if (currentSgl[sgl_seg_index].zero == 0) {                   // (points to a list of SGLs)

                if (currentSgl != sgl)
                    free(currentSgl);

                SOP_LOG_DBG("std download sgl segment");

                sglSegmentCount = currentSgl[sgl_seg_index].desc.std.length / sglDescSize;

                currentSgl = download_sgl_segment(currentSgl[sgl_seg_index].desc.std.address,
                		currentSgl[sgl_seg_index].desc.std.length); // point to the next descriptor segment

                if (currentSgl == NULL) {
                	goto sgl_segment_error;
                }

                sgl_seg_index = 0; // Reset our position in the SGL segment

                continue;

            } else {

                goto sgl_std_seg_zero_byte_error;
            }
            break;

        case SGL_STANDARD_LAST_SEGMENT:             // SGL type that determines the next SGL is the last one...
        	if (last) {
        		// This type of descriptor is not allowed in the SGL_STANDARD_LAST_SEGMENT
        		goto bad_last_sgl_error;
        	}
            if (currentSgl[sgl_seg_index].zero == 0) {

                if (currentSgl != sgl)
                    free(currentSgl);

                SOP_LOG_DBG("stdlast download sgl segment");

                sglSegmentCount = currentSgl[sgl_seg_index].desc.std.length / sglDescSize;

                currentSgl = download_sgl_segment(currentSgl[sgl_seg_index].desc.std.address,
                		currentSgl[sgl_seg_index].desc.std.length); // point to the next descriptor segment

                if (currentSgl == NULL) {
                	goto sgl_segment_error;
                }

                sgl_seg_index = 0; // Reset our position in the SGL segment

                last = true;                  // tell the processing for the next descriptor to end it all...

            } else {

                goto sgl_std_last_seg_zero_byte_error;
            }
            break;

        case SGL_ALTERNATIVE_LAST_SEGMENT:
        	if (last) {
        		goto bad_last_sgl_error;
        	}
            sglAddr = sgl->desc.altLast.address;
            break;

        case SGL_BIT_BUCKET:
            if (currentSgl[sgl_seg_index].zero == 0) {
                if (direction) {
                	// to sgl
                    pData += currentSgl[sgl_seg_index].desc.data.length;
                } else {
                	// from sgl -- ignore
                }
            	++sgl_seg_index;
            } else {

                goto sgl_bit_bucket_zero_byte_error;
            }
            break;

        case VENDOR_SPECIFIC:
            // TODO: put in Vendor-specific & illegal SGL type handling
            goto vendor_sgl_error;
            break;
        default:
            // TODO: put in Vendor-specific & illegal SGL type handling
            // goto vendor_sgl_error;
            goto sgl_type_error;
            break;
        }
    }

    return (SUCCESS);

    sgl_bit_bucket_zero_byte_error:
        SOP_LOG_ERR("Error. BitBucket sgl type, zero field is not zero.");
        return (FAIL);
    sgl_std_last_seg_zero_byte_error:
        SOP_LOG_ERR("Error. Standard Last Segment sgl type, zero field is not zero.");
        return (FAIL + 1);
    sgl_std_seg_zero_byte_error:
        SOP_LOG_ERR("Error. Standard Segment sgl type, zero field is not zero.");
        return (FAIL + 2);
    sgl_data_block_zero_byte_error:
        SOP_LOG_ERR("Error. Data Block sgl type, zero field is not zero.");
        return (FAIL + 3);
    data_length_error:
        SOP_LOG_ERR("Error. sgl length error.");
        return (FAIL + 4);
    vendor_sgl_error:
        SOP_LOG_ERR("Error. vendor-specific sgl is not yet supported.");
        return (FAIL + 5);
    sgl_type_error:
        SOP_LOG_ERR("Error. unrecognized sgl type.");
        return (FAIL + 6);
	bad_last_sgl_error:
		SOP_LOG_ERR("Error. bad last sgl.");
		return (FAIL + 7);
	sgl_too_small_error:
		SOP_LOG_ERR("Error. sgl describes buffer thats too small.");
		return (FAIL + 8);
	sgl_segment_error:
		SOP_LOG_ERR("Error. bad sgl segment.");
		return (FAIL + 9);

}

sglDesc * download_sgl_segment(hwaddr address, uint32_t len) {
    // Return null if the length is bad (not a multiple of sglDesc size)
	SOP_LOG_DBG("%s():downloading from 0x%16llx (0x%08x bytes)", __func__, (long long unsigned)address, len);
    if (len % sizeof(sglDesc) != 0)
        return NULL;

    // Otherwise download it from the host
    sglDesc * sgl_buffer = (sglDesc *)malloc(len);
    pqi_dma_mem_read(address, (uint8_t*)sgl_buffer, len);
    
    //print_bytes("new sgl segment",sgl_buffer,len);

    return sgl_buffer;
}




