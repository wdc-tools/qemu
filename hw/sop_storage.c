/*
 * Copyright (c) 2011 Intel Corporation
 * Copyright (C) 2012-2013 HGST, Inc.
 *
 * by
 *    Maciej Patelczyk <mpatelcz@gkslx007.igk.intel.com>
 *    Krzysztof Wierzbicki <krzysztof.wierzbicki@intel.com>
 *    Patrick Porlan <patrick.porlan@intel.com>
 *    Nisheeth Bhat <nisheeth.bhat@intel.com>
 *    Sravan Kumar Thokala <sravan.kumar.thokala@intel.com>
 *    Keith Busch <keith.busch@intel.com>
 *    Robert Bennett <Robert.Bennett@hgst.com>
 *    David Darrington <David.Darrington@hgst.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "sop.h"
#include <sys/mman.h>
#include <assert.h>
#include "qemu-common.h"

#define MASK_AD         0x4
#define MASK_IDW        0x2
#define MASK_IDR        0x1

void pqi_dma_mem_read(hwaddr addr, uint8_t *buf, int len)
{
  cpu_physical_memory_rw(addr, buf, len, 0);
}

void pqi_dma_mem_write(hwaddr addr, uint8_t *buf, int len)
{
  cpu_physical_memory_rw(addr, buf, len, 1);
}

/*********************************************************************
    Function     :    pqi_create_storage_disk
    Description  :    Creates a PQI Storage Disk and the namespaces within
    Return Type  :    int (0:1 Success:Failure)

    Arguments    :    uint32_t : instance number of the sop device
                      uint32_t : namespace id
                      DiskInfo * : PQI disk to create storage for
*********************************************************************/
int pqi_create_storage_disk(uint32_t instance, uint32_t lunid, DiskInfo *disk, PQIState *n)
{
    uint64_t size;
    char str[64];

    SOP_LOG_NORM(" %s() instance: %d, lunid: %d, ", __func__, instance, lunid);

    if (n->working_dir) {
    	snprintf(str, sizeof(str), "%s/sop_disk%d_n%d.img", n->working_dir, instance, lunid);
    } else {
    	snprintf(str, sizeof(str), "sop_disk%d_n%d.img", instance, lunid);
    }

    SOP_LOG_NORM(" %s() backing file: %s, ", __func__, str);

    disk->lunid = lunid;

//    disk->fd = open(str, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
    disk->fd = open(str, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
    if(disk->fd < 0)
    {
        SOP_LOG_ERR("  Error cannot open %s  code: 0x%x", str, disk->fd);
        SOP_LOG_ERR("  errno: '%s'\n", strerror(errno));
        return FAIL;
    }

    size = (n->lun_size * 512);

    if(posix_fallocate(disk->fd, 0, size) != 0)
    {
        SOP_LOG_ERR("  Error while allocating space for LUN");
        return FAIL;
    }

    disk->mapping_addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, disk->fd, 0);
    if(disk->mapping_addr == NULL)
    {
        SOP_LOG_ERR("  Error while mapping disk addr: %d", lunid);
        return FAIL;
    }
    disk->mapping_size = size;
    
    SOP_LOG_NORM(" %s() LUN: %d  size: %d ", __func__, lunid, (int)size);

    return SUCCESS;
}

/*********************************************************************
    Function     :    pqi_create_storage_disks
    Description  :    Creates a PQI Storage Disks and the
                      namespaces within
    Return Type  :    int (0:1 Success:Failure)

    Arguments    :    PQIState * : Pointer to PQI device State
*********************************************************************/
int pqi_create_storage_disks(PQIState *n)
{
    uint32_t i;
    int ret = SUCCESS;

    SOP_LOG_NORM("%s(): instance: %d for NLUNS: %d", __func__, n->instance, n->num_luns);

    for (i = 0; i < n->num_luns; i++) {
        ret |= pqi_create_storage_disk(n->instance, i + 1, &n->disk[i], n);
    }

    SOP_LOG_NORM("%s():Backing store created for instance %d", __func__, n->instance);

    return ret;
}


/*********************************************************************
    Function     :    pqi_close_storage_disk
    Description  :    Deletes PQI Storage Disk
    Return Type  :    int (0:1 Success:Failure)

    Arguments    :    DiskInfo * : Pointer to PQI disk
*********************************************************************/
int pqi_close_storage_disk(DiskInfo *disk)
{

    if (disk->mapping_addr != NULL) {

        if (munmap(disk->mapping_addr, disk->mapping_size) < 0) {

            SOP_LOG_ERR("Error while closing LUN: %d", disk->lunid);
            return FAIL;

        } else {

            disk->mapping_addr = NULL;
            disk->mapping_size = 0;

            if (close(disk->fd) < 0) {

                SOP_LOG_ERR("Unable to close the pqi disk");
                return FAIL;
            }
        }
    }

    return SUCCESS;
}


/*********************************************************************
    Function     :    pqi_close_storage_disks
    Description  :    Closes the PQI Storage Disks and the
                      associated namespaces
    Return Type  :    int (0:1 Success:Failure)

    Arguments    :    PQIState * : Pointer to PQI device State
*********************************************************************/
int pqi_close_storage_disks(PQIState *n)
{
    uint32_t i;
    int ret = SUCCESS;

    for (i = 0; i < n->num_luns; i++) {

        ret = pqi_close_storage_disk(&n->disk[i]);
    }

    return ret;
}

