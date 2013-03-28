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


// Process IQ event (Host writing to IQ PI)

void process_iq_event(PQIState* pqiDev, uint32_t qid) {
    
    uint16_t ci;
    uint16_t pi;

    SOP_LOG_NORM("%s(): called. qid=%d", __func__,qid);

    if(qid == AIQ_ID) {
        
        PQIInboundQueue* iq = &pqiDev->iq[AIQ_ID];

        // nab IQ CI & IQ PI
        SOP_LOG_NORM("%s(): qid:%d before getting ci", __func__,qid);
        ci = get_iq_ci(pqiDev, qid);
        SOP_LOG_NORM("%s(): qid:%d ci = %i", __func__, qid, ci);
        pi = get_iq_pi(pqiDev, qid);

        SOP_LOG_NORM("%s(): qid:%d pi = %i", __func__, qid, pi);

        while ( ci != pi ) {

            //  process admin queue while inbound IUs exist

            SOP_LOG_NORM("%s(): qid = %d, ci = %d", __func__, qid, ci);

            sop_execute_admin_command(pqiDev, iq, ci);

            if(++ci >= iq->size) {

                ci = 0;     // queue wrap condition
            }

            iq->ci_work = iq->ci_local = ci;
            set_iq_ci(pqiDev, qid, ci);
        }

        // TODO: send interrupt to host here?
    
    } else {

        // process operational queues    
        SOP_LOG_NORM("Warning: Processing Operational IQ %d is not supported yet", qid);
        PQIInboundQueue* iq = &pqiDev->iq[qid];

         // nab IQ CI & IQ PI
         SOP_LOG_NORM("%s(): qid:%d before getting ci", __func__,qid);
         ci = get_iq_ci(pqiDev, qid);
         SOP_LOG_NORM("%s(): qid:%d ci = %i", __func__, qid, ci);
         pi = get_iq_pi(pqiDev, qid);

         SOP_LOG_NORM("%s(): qid:%d pi = %i", __func__, qid, pi);

         while ( ci != pi ) {

             //  process admin queue while inbound IUs exist

             SOP_LOG_NORM("%s(): qid = %d, ci = %d", __func__, qid, ci);

             sop_execute_sop_command(pqiDev, qid, ci);

             if(++ci >= iq->size) {

                 ci = 0;     // queue wrap condition
             }

             iq->ci_work = iq->ci_local = ci;
             set_iq_ci(pqiDev, qid, ci);
         }

    }
}


// Process OQ evenet (Host writing to OQ CI)
//  - the host has removed 1 or more IUs from the device's outbound queue

void process_oq_event(PQIState* pqiDev, uint32_t qid) {

    SOP_LOG_NORM("%s(): called", __func__);

    if(qid == AOQ_ID) {

        // nab OQ CI (actual) & copy to device's local OQ CI
        pqiDev->oq[AOQ_ID].ci = get_oq_ci(pqiDev, qid);

        // TODO: call to process inbound queue
        //        (possibly waiting for this queue to have room...)

    } else {

        // process operational queues    
        //SOP_LOG_ERR("Processing Operational OQ %d not supported yet", qid);
        // nab OQ CI (actual) & copy to device's local OQ CI
        pqiDev->oq[qid].ci = get_oq_ci(pqiDev, qid);
    }
}


// Gets the IQ PI (which is written by the host - and never by the device)

uint16_t get_iq_pi(PQIState* pqiDev, uint16_t qid) {

    uint16_t piVal = 0;

    if(qid <= MAX_Q_ID) {
        
        piVal = (uint16_t)pqi_cntrl_read_config(pqiDev, PQI_IQ_PI_REG(qid), DWORD);
    
    } else {

        // process operational queues    
        SOP_LOG_ERR("OQ %d not supported yet", qid);
    }

    return piVal;
}


// Gets the IQ CI (which is written by the device - and never by the host)

uint16_t get_iq_ci(PQIState* pqiDev, uint16_t qid) {

    return pqiDev->iq[qid].ci_local;
}


// Sets the IQ CI (which is written by the device - and never by the host)

void set_iq_ci(PQIState* pqiDev, uint16_t qid, uint16_t ciVal) {

   hwaddr addr;
        
    if(qid == AIQ_ID) {

        addr = (hwaddr)(pqiDev->iq[qid].ci_addr & ADMIN_CIA_PIA_MASK);
        
    
    } else {

        // set CI for inbound operational (non-admin) queues    
        addr = (hwaddr)(pqiDev->iq[qid].ci_addr & OP_CIA_PIA_QC_MASK);
    }

    pqi_dma_mem_write(addr, (uint8_t*)&ciVal, 2);
}


// Gets the OQ CI
// OQ CI is written by the host - read only by the device
// (exception: the device zeros it at queue creation)

uint16_t get_oq_ci(PQIState* pqiDev, uint16_t qid) {

    uint16_t ciVal;

    if(qid == AOQ_ID) {
        
        // get CI for admin queue
        ciVal = (uint16_t)pqi_cntrl_read_config(pqiDev, PQI_OQ_CI_REG(qid), DWORD);
    
    } else {

        // get CI for outbound operational (non-admin) queues    
        ciVal = (uint16_t)pqiDev->oq[qid].ci;
    }

    return ciVal;
}


// Gets the device's OQ PI (from the BAR)
// OQ PI is written by the device - read only by the host

uint16_t get_oq_pi(PQIState* pqiDev, uint16_t qid) {

    return pqiDev->oq[qid].pi_local;
}


// Sets the OQ PI 
// OQ_PI is written by the device - read-only by the host

void set_oq_pi(PQIState* pqiDev, uint16_t qid, uint16_t piVal) {

    hwaddr addr;

    SOP_LOG_DBG("%s(): called", __func__);

    if(qid == AOQ_ID) {
        
        addr = (hwaddr)(pqiDev->oq[qid].pi_addr & ADMIN_CIA_PIA_MASK);
    
    } else {

        // set CI for outbound operational (non-admin) queues    
        addr = (hwaddr)(pqiDev->oq[qid].pi_addr & OP_CIA_PIA_QC_MASK);
    }

    pqi_dma_mem_write(addr, (uint8_t*)&piVal, 2);

}


// post the response 
void post_to_oq (PQIState* pqiDev, int qid, void *iu, int length) {

    uint16_t ci;
    uint16_t pi;
    hwaddr addr;
    PQIOutboundQueue* oq;


    oq = &pqiDev->oq[qid];

    SOP_LOG_DBG("%s(): called. msixEntry=%d", __func__,oq->msixEntry);

    // nab OQ PI & IQ CI
    pi = get_oq_pi(pqiDev, qid);
    ci = get_oq_ci(pqiDev, qid);

    // TODO: what to do when the outbound queue is full ?? (start timer)
    //
    // if (pi == ci) { ??? }

    SOP_LOG_DBG("%s(): qid = %d, pi = %d, ci = %d", __func__, qid, pi, ci);

    addr = (hwaddr)(oq->ea_addr + (pi * ADM_OQ_ELEMENT_LENGTH));
    
    pqi_dma_mem_write(addr, (uint8_t*)iu, length);

    SOP_LOG_DBG("%s(): %s", __func__, "Wrote entry");
    if(++pi >= oq->size) {

        pi = 0;     // queue wrap condition
    }

    oq->pi_work = oq->pi_local = pi;
    SOP_LOG_DBG("%s(): %s", __func__, "Going to write pi");
    set_oq_pi(pqiDev, qid, pi);

    //if (oq->irq_enabled) {
        msix_notify(&(pqiDev->dev), oq->msixEntry);
    //}
}


void sop_execute_sop_command(PQIState* pqiDev, uint32_t qid, uint16_t ci) {

    uint8_t iu_type;
    uint8_t compatible_features;
    uint16_t iu_length;
    uint8_t* cdb;

    PQIInboundQueue* iq = &pqiDev->iq[qid];

    SOP_LOG_NORM("%s(): called", __func__);

    uint8_t iu [ADM_IQ_ELEMENT_LENGTH];
    pqi_dma_mem_read(iq->ea_addr + (ci * ADM_IQ_ELEMENT_LENGTH), iu, ADM_IQ_ELEMENT_LENGTH);

    iu_type = (uint8_t)(iu)[0];
    compatible_features = (uint8_t)(iu)[1];
    iu_length = (uint16_t)(iu)[2];

    SOP_LOG_NORM("%s(): iutype = %x, iu_length = %x", __func__, iu_type,iu_length);

    if ((iu_type == 0) && (compatible_features == 0) && (iu_length == 0)) {

        // NULL IU
        return;
    }

    if ((iu_type == SOP_LIMITED_CMD_IU)) {
    	// TBD: LIMITED_OMMAND_IU implies lun 0 is targeted, need to add
    	// support for COMMAND_IU, which includes a 64-bit LOGICAL_UNIT_NUMBER
        cdb = &iu[16];
        SOP_LOG_NORM("%s(): cdb_type=%d",__func__,cdb[0]);
        switch ( cdb[0] )
        {
        case OP_INQUIRY:
            sop_cdb_inquiry(pqiDev, qid, (sopLimitedCommandIU *)iu);
            break;
        case OP_TEST_UNIT_READY:
            sop_cdb_test_unit_ready(pqiDev, qid, (sopLimitedCommandIU *)iu);
            break;
        case OP_READ_CAPACITY:
            sop_cdb_read_capacity(pqiDev, qid, (sopLimitedCommandIU *)iu);
            break;
        case OP_READ_10:
        	sop_cdb_read_10(pqiDev, qid, (sopLimitedCommandIU *)iu);
        	break;
        case OP_WRITE_10:
        	sop_cdb_write_10(pqiDev, qid, (sopLimitedCommandIU *)iu);
        	break;

        default:
            SOP_LOG_ERR("Error. Invalid/unsupported: 0x%x", cdb[0]);
            break;
        }
        return;
    }
}

void sop_cdb_inquiry(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r) {

	uint32_t rVal = SUCCESS;
	sopSuccessRsp inqRsp;
	inquiryCDB * c = (inquiryCDB *)r->cdb;
	uint16_t len = be16_to_cpu(c->alloc_len);
	uint8_t buffer[len];
	int i=0;

    SOP_LOG_NORM("%s(): called", __func__);

    for(i=0;i<len;i++) {
    	buffer[i]=0x00;
    }

    // put Info inside the SGL...
	rVal = copy_to_sgl(r->sg, (uint8_t*)buffer, len);

	if (rVal != 0) {
		SOP_LOG_NORM("%s(): bad rc 0x%x from admin_build_sgl", __func__, rVal);
	}

	inqRsp.header.type=0x90;
	inqRsp.header.feat=0;
	inqRsp.header.length=0x000c;
	inqRsp.requestIdentifier=r->request_id;
	inqRsp.qisd = qid;

	post_to_oq(pqiDev, qid, (void *)&inqRsp, sizeof(sopSuccessRsp));

}

void sop_cdb_test_unit_ready(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r) {

	sopSuccessRsp turRsp;
    SOP_LOG_NORM("%s(): called", __func__);

    turRsp.header.type=0x90;
    turRsp.header.feat=0;
    turRsp.header.length=0x000c;
    turRsp.requestIdentifier=r->request_id;
    turRsp.qisd = qid;

	post_to_oq(pqiDev, qid, (void *)&turRsp, sizeof(sopSuccessRsp));

}

void sop_cdb_read_capacity(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r) {

	uint32_t rVal = SUCCESS;
	uint32_t buffer[2];
	sopSuccessRsp rsp;
    SOP_LOG_NORM("%s(): called", __func__);

	buffer[0]=cpu_to_be32(pqiDev->lun_size-1); //last lba on device
	buffer[1]=cpu_to_be32(512); // block size

	SOP_LOG_NORM("(%s(): --sgl type:0x%x sgl addr:0x%llx , sgl length:%x",
			__func__,r->sg[0].type,
			(unsigned long long)le64_to_cpu(r->sg[0].desc.std.address),
			le32_to_cpu(r->sg[0].desc.std.length));

    // put Info inside the SGL...
	rVal = copy_to_sgl(r->sg, (uint8_t*)buffer, r->xfer_size);

	if (rVal != 0) {
		SOP_LOG_NORM("%s(): bad rc 0x%x from admin_build_sgl", __func__, rVal);
	}

	rsp.header.type=0x90;
	rsp.header.feat=0;
	rsp.header.length=0x000c;
	rsp.requestIdentifier=r->request_id;
	rsp.qisd = qid;

	post_to_oq(pqiDev, qid, (void *)&rsp, sizeof(sopSuccessRsp));

}

void sop_cdb_read_10(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r) {

	uint32_t rVal = SUCCESS;
	read10CDB * c = (read10CDB *)r->cdb;
	uint32_t lba = be32_to_cpu(c->lba);
	sopSuccessRsp rsp;

	SOP_LOG_DBG("%s(): called. op=0x%x length=0x%x", __func__,r->header.type,r->header.length);

    //print_bytes("read10",r,r->header.length+4);
	SOP_LOG_DBG("r xfer size=%d, partial=%x",r->xfer_size, (r->partial?1:0));

    //print_cdb(r);
	SOP_LOG_DBG("r cdb: lba=%d ", lba);
	SOP_LOG_DBG("r cdb: xfer_len=%d ", be16_to_cpu(c->xfer_len));


    DiskInfo *disk;
    disk = &pqiDev->disk[0];

    if (lba < pqiDev->lun_size) {

    	// put Info inside the SGL...
    	rVal = copy_to_sgl(r->sg, disk->mapping_addr+(lba*512), be16_to_cpu(c->xfer_len)*512);

    	if (rVal != 0) {
    		SOP_LOG_ERR("%s(): bad rc 0x%x from admin_build_sgl", __func__, rVal);
    		sopCommandRspIU ersp;
    		ersp.header.type=SOP_CMD_RSP_IU;
    		ersp.header.feat=0;
    		ersp.header.length=0x0010;
    		ersp.request_id=r->request_id;
    		ersp.queue_id = qid;
    		ersp.status = SOP_CHECK_CONDITION;
    		ersp.qualifier = SOP_ILLEGAL_REQUEST;

    		post_to_oq(pqiDev, qid, (void *)&ersp, sizeof(sopCommandRspIU));
    		return;    	}
    } else {
    	SOP_LOG_ERR("%s(): lba %u", __func__, (uint32_t)lba);
    }

	rsp.header.type=0x90;
	rsp.header.feat=0;
	rsp.header.length=0x000c;
	rsp.requestIdentifier=r->request_id;
	rsp.qisd = qid;

	post_to_oq(pqiDev, qid, (void *)&rsp, sizeof(sopSuccessRsp));

}

void sop_cdb_write_10(PQIState* pqiDev, uint32_t qid, sopLimitedCommandIU *r) {

	uint32_t rVal = SUCCESS;
	write10CDB * c = (write10CDB *)r->cdb;
	uint32_t lba = be32_to_cpu(c->lba);
	sopSuccessRsp rsp;

	SOP_LOG_DBG("%s(): called. op=0x%x length=0x%x", __func__,r->header.type,r->header.length);

    //print_bytes("write10",r,r->header.length+4);
	SOP_LOG_DBG("w xfer size=%d, partial=%x",r->xfer_size, (r->partial?1:0));
    SOP_LOG_DBG("wxfer size=%d, ",r->xfer_size);

    //print_cdb(r);
    SOP_LOG_DBG("w cdb: lba=%d ", lba);
    SOP_LOG_DBG("w cdb: xfer_len=%d ", be16_to_cpu(c->xfer_len));


    DiskInfo *disk;
    disk = &pqiDev->disk[0];

    if (lba < pqiDev->lun_size) {
    	// put Info inside the SGL...
    	rVal = copy_from_sgl(r->sg, disk->mapping_addr+(lba*512), be16_to_cpu(c->xfer_len)*512);

    	if (rVal != 0) {
    		SOP_LOG_ERR("%s(): bad rc 0x%x from admin_build_sgl", __func__, rVal);
    		sopCommandRspIU ersp;
    		ersp.header.type=SOP_CMD_RSP_IU;
    		ersp.header.feat=0;
    		ersp.header.length=0x0010;
    		ersp.request_id=r->request_id;
    		ersp.queue_id = qid;
    		ersp.status = SOP_CHECK_CONDITION;
    		ersp.qualifier = SOP_ILLEGAL_REQUEST;

    		post_to_oq(pqiDev, qid, (void *)&ersp, sizeof(sopCommandRspIU));
    		return;
    	}
    } else {
    	SOP_LOG_ERR("%s(): lba %u", __func__, (uint32_t)lba);
    }

	rsp.header.type=0x90;
	rsp.header.feat=0;
	rsp.header.length=0x000c;
	rsp.requestIdentifier=r->request_id;
	rsp.qisd = qid;

	post_to_oq(pqiDev, qid, (void *)&rsp, sizeof(sopSuccessRsp));

}
