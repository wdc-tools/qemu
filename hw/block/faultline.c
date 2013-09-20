/*
 * QEMU Faultline Simulator Controller
 *
 * Copyright (c) 2013, HGST Corporation
 *
 */

/**
 * Usage: add options:
 *      -device faultline,host=<host>,port=<port>
 *
 *      i.e. -device faultline,host=192.168.0.100,port=1890
 *
  */

#include <hw/block/block.h>
#include <hw/hw.h>
#include <hw/pci/msix.h>
#include <hw/pci/msi.h>
#include <hw/pci/pci.h>
#include <qemu/bitops.h>
#include <qemu/bitmap.h>
#include <qemu/thread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "faultline.h"

static size_t readFromConnection(FaultlineCtrl *f, uint8_t *data,
		int length, uint32_t *ec)
{
	size_t offset=0;
	size_t dataRxd=0;
	//fprintf(stderr,"(%s): is_connected:%d\n",__func__,f->is_connected);
	if (!f->is_connected) {
		return 0;
	}
	while (offset < length) {
		dataRxd = recv(f->faultline_socket,&data[offset],length-offset,0);
		offset+=dataRxd;
		//fprintf(stderr,"(%s): %d bytes received\n",__func__,(int)dataRxd);
		dataRxd=0;
	}
	//fprintf(stderr,"(%s): total %d bytes received\n",__func__,(int)offset);
	return offset;
}
static int readComplete(FaultlineCtrl *f)
{
	int err=0;
	uint16_t length;
	//fprintf(stderr,"(%s):\n",__func__);
	size_t bytes_read = readFromConnection(f,(uint8_t *)&length,
			sizeof(uint16_t),&f->error_code);
	//fprintf(stderr,"(%s): %d bytes read (%d)\n",__func__,(int)bytes_read,
	//		bytes_read == sizeof(length));

	if (bytes_read == sizeof(length)) {
		qemu_mutex_lock(&f->read_stream_mutex);
		f->read_buffer_length = readFromConnection(f,f->read_buffer,
				f->read_buffer_length,&f->error_code);
		qemu_cond_broadcast(&f->read_stream_condition);
		qemu_mutex_unlock(&f->read_stream_mutex);
	}
	return err;
}
static int readMem(FaultlineCtrl *f)
{
	int err=0;
	uint16_t length;
	size_t bytes_read = readFromConnection(f, (uint8_t*)&length,sizeof(length),
			&f->error_code);
	if (bytes_read == sizeof(length)) {
		mem_read * memread_cmd = (mem_read *)malloc(sizeof(mem_read) +
				length - sizeof(uint8_t));
        memread_cmd->length = length;

        // read remaining data- 32 bit address low and high fields
        bytes_read = readFromConnection(f, (uint8_t*)memread_cmd+3,
        		2 * sizeof(uint32_t), &f->error_code);

        if (bytes_read == 2 * sizeof(uint32_t)) {

            // setup read completion
            read_completion * data = (read_completion*)malloc(
            		sizeof(read_completion) + length - sizeof(uint8_t));
            data->opcode = READ_COMPLETION_OPCODE;
            data->length = memread_cmd->length;

            uint64_t read_address = (((uint64_t)memread_cmd->addr_high) << 32)
            		| memread_cmd->addr_low;

            // copy memory to data field of read completion from address specified by command read from socket
            //memcpy(&(data->data), (char*) read_address, memread_cmd->length);
            pci_dma_read(&f->parent_obj, read_address,
            		&(data->data), memread_cmd->length);

            // write to socket
            send(f->faultline_socket, (char*)data,
                sizeof(read_completion) + memread_cmd->length -
                sizeof(uint8_t), 0);

            free(data);
        }
        free(memread_cmd);
	}
	return err;
}
static int writeMem(FaultlineCtrl *f)
{


    int err = 0;

    // get length of write command
    uint16_t length;
    size_t bytes_read = readFromConnection(f, (uint8_t*)(&length),
    		sizeof(uint16_t), &f->error_code);

    if (bytes_read == sizeof(length)) {

        // last byte of mem_write struct includes data specified by length
        mem_write * memwrite_cmd = (mem_write *) malloc(sizeof(mem_write) +
        		length - sizeof(uint8_t));
        memset(memwrite_cmd, 0, sizeof(mem_write) + length - sizeof(uint8_t));
        memwrite_cmd->length = length;

        // read remaining data
        bytes_read = readFromConnection(f, (uint8_t*)(&memwrite_cmd->addr_low),
        		length + (sizeof(uint32_t) * 2),
        		&f->error_code);

        if (bytes_read == length + (sizeof(uint32_t) * 2)) {
            uint64_t write_address = (((uint64_t)memwrite_cmd->addr_high)
            		<< 32) | memwrite_cmd->addr_low;

            //write data to address as specified by command
            //memcpy((char*) write_address, &(memwrite_cmd->data),
            //		memwrite_cmd->length);
            pci_dma_write(&f->parent_obj, write_address,
            		&(memwrite_cmd->data), memwrite_cmd->length);
        }
        free(memwrite_cmd);
    }
    return err;
}
static int sendToTCP(FaultlineCtrl *f,uint8_t *command, int size,
		uint8_t *data, uint16_t length)
{

    int err_code = 0;
    //int i=0;

    if (size && command && f->is_connected) {

        //first byte of command is opcode
        switch (*command) {
        case MEM_READ_OPCODE:
        case CFG_READ_OPCODE:
            {
                // write a read command to socket
            	qemu_mutex_lock(&f->read_stream_mutex);
                if(size != send(f->faultline_socket,(char*) command, size, 0)) {
                    f->is_connected = false;
                    err_code = ERR_DEVICE_INACTIVE;
                    break;
                }

                // wait on read completion stream condition
                f->read_buffer = data;
                f->read_buffer_length = length;

                //fprintf(stderr,"(%s): about to wait\n",__func__);

                qemu_cond_wait(&f->read_stream_condition,
                		&f->read_stream_mutex);

                //fprintf(stderr,"(%s): wait complete\n",__func__);
            	//for (i=0;i<f->read_buffer_length;i++)
            	//	fprintf(stderr,"0x%x ",f->read_buffer[i]);

                // check if all characters have been read
                if (f->read_buffer_length != length) {
                    err_code = ERR_BUFFER_UNDERRUN;
                }

                // reset the read buffer
                f->read_buffer = NULL;
                f->read_buffer_length = length;
                qemu_mutex_unlock(&f->read_stream_mutex);
                break;
            }
        case MEM_WRITE_OPCODE:
        case CFG_WRITE_OPCODE:
            if (size != send(f->faultline_socket, command, size, 0)) {
                f->is_connected = false;
            }
            break;
        default:
            return ERR_DRIVER_INTERNAL; //error, should have opcode for either read or write
        }
    }

    if (f->error_code) {
        f->is_connected = 0;
        err_code = ERR_DEVICE_INACTIVE;
    }

    return err_code;
}


static void faultline_isr_notify(FaultlineCtrl *n, uint32_t vector)
{
	if (msix_enabled(&(n->parent_obj))) {
	   	//fprintf(stderr,"(%s) msix: %0x\n",__func__,vector);
		msix_notify(&(n->parent_obj), vector);
	} else if (msi_enabled(&(n->parent_obj))) {
	   	//fprintf(stderr,"(%s) msi: %0x\n",__func__,vector);
			msi_notify(&(n->parent_obj), vector);
	} else {
	   	//fprintf(stderr,"(%s) intx: %0x\n",__func__,vector);
		qemu_irq_pulse(n->parent_obj.irq[0]);
	}
}

static uint64_t faultline_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    FaultlineCtrl *f = (FaultlineCtrl *)opaque;
    uint64_t val = 0;
    mem_read msg;
	//fprintf(stderr,"mmio_read:%lu,%u",addr,size);

	memset(&msg,0,sizeof(msg));
	msg.opcode = MEM_READ_OPCODE;
	msg.addr_low = addr & 0xffffffff;
	msg.addr_high = (addr >> 32) & 0xffffffff;
	msg.length = size;
	sendToTCP(f,(uint8_t *)&msg,sizeof(msg),(uint8_t *)&val, size);
	//fprintf(stderr,"\n");

    return val;
}


static void faultline_mmio_write(void *opaque, hwaddr addr, uint64_t data,
    unsigned size)
{
	FaultlineCtrl *f = (FaultlineCtrl *)opaque;
	mem_write *msg = (mem_write *)malloc(sizeof(mem_write)+size-sizeof(uint8_t));

	// forward write over tcp connection to faultline
	memset(msg,0,sizeof(msg) + size - sizeof(uint8_t));
	msg->opcode = MEM_WRITE_OPCODE;
	msg->addr_low = addr & 0xffffffff;
	msg->addr_high = (addr >> 32) & 0xffffffff;
	msg->length = size;
	memcpy(&(msg->data),(const void *)&data,size);
	sendToTCP(f,(uint8_t *)msg,sizeof(struct mem_write)+size-sizeof(uint8_t),
			(uint8_t *)&data,size);
	free(msg);
}

static const MemoryRegionOps faultline_mmio_ops = {
    .read = faultline_mmio_read,
    .write = faultline_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 8,
    },
};

static void connect_to_faultline(FaultlineCtrl *n)
{
	int32_t nrc=0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((nrc = getaddrinfo(n->faultline_host,
    				n->faultline_port,
    				&hints,
    				&n->faultline_addr)) != 0) {
    	fprintf(stderr,"getaddrinfo error: %s\n",gai_strerror(nrc));
    } else {
    	n->faultline_socket = socket(n->faultline_addr->ai_family,
    			n->faultline_addr->ai_socktype,
    			n->faultline_addr->ai_protocol);
    	if (n->faultline_socket < 0)
        	fprintf(stderr,"socket error\n");
    	if (connect(n->faultline_socket,
    			n->faultline_addr->ai_addr,
    			n->faultline_addr->ai_addrlen))
    		fprintf(stderr,"Connection failed\n");
    	else {
    		fprintf(stderr,"Connected..\n");
    		n->is_connected=1;
    	}
    }
}

static void faultline_init_pci(FaultlineCtrl *n)
{
    uint8_t *pci_conf = n->parent_obj.config;
    int32_t nr_msi=32,nr_msix=32;

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_config_set_prog_interface(pci_conf, 0x2);
    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_EXPRESS);
    pcie_endpoint_cap_init(&n->parent_obj, 0x80);

    memory_region_init_io(&n->iomem, &faultline_mmio_ops, n, "faultline", 0x2000);
    pci_register_bar(&n->parent_obj, 0,
        PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64,
        &n->iomem);

    if (n->num_msix>=0)
        nr_msix = n->num_msix;

    if ((n->num_msi==0) || (n->num_msi >0 &&
                            is_power_of_2(n->num_msi) && n->num_msi<=32)) {
        nr_msi = n->num_msi;
    }
   
    if (nr_msix)
        msix_init_exclusive_bar(&n->parent_obj, nr_msix, 4);

    if (nr_msi)
        msi_init(&n->parent_obj, 0x50, nr_msi, true, false);

    connect_to_faultline(n);

}
static void *io_thread(void *opaque)
{
    FaultlineCtrl *f = opaque;
    uint8_t opcode;
	uint16_t interrupt_id;
    do {
        //
    	opcode = 99;
    	//fprintf(stderr,"(%s) 1: %0x\n",__func__,opcode);
    	readFromConnection(f,&opcode, sizeof(opcode), &f->error_code);
    	//fprintf(stderr,"(%s) 2: %0x\n",__func__,opcode);

    	switch (opcode) {
			case READ_COMPLETION_OPCODE:
				readComplete(f);
				break;
			case FIRE_INTERRUPT_OPCODE:
				readFromConnection(f,(uint8_t *)&interrupt_id,
						sizeof(uint16_t),&f->error_code);
				faultline_isr_notify(f,interrupt_id);
				break;
			case MEM_READ_OPCODE:
				readMem(f);
				break;
			case MEM_WRITE_OPCODE:
				writeMem(f);
				break;
			default:
				f->is_connected=0;
				break;
		}
    } while (!f->stopping);
    return NULL;
}

static int faultline_init(PCIDevice *pci_dev)
{
    FaultlineCtrl *n = FAULTLINE(pci_dev);

    faultline_init_pci(n);

    qemu_cond_init(&n->read_stream_condition);
    qemu_mutex_init(&n->read_stream_mutex);
    qemu_thread_create(&n->ioThread, io_thread,
                       n, QEMU_THREAD_JOINABLE);

    return 0;
}

static void faultline_exit(PCIDevice *pci_dev)
{
    FaultlineCtrl *n = FAULTLINE(pci_dev);

    // kill polling thread
    n->stopping=1;
    qemu_thread_join(&n->ioThread);

    // drop tcp connection
    if (n->faultline_socket)
    	close(n->faultline_socket);
    if (n->faultline_addr)
    	freeaddrinfo(n->faultline_addr);
    qemu_cond_destroy(&n->read_stream_condition);
    qemu_mutex_destroy(&n->read_stream_mutex);
    //g_free(n->features.int_vector_config);
    msix_uninit_exclusive_bar(pci_dev);
    memory_region_destroy(&n->iomem);
}

static Property faultline_props[] = {
    //DEFINE_BLOCK_PROPERTIES(FaultlineCtrl, conf),
    DEFINE_PROP_INT32("num_msix", FaultlineCtrl, num_msix, -1),
    DEFINE_PROP_INT32("num_msi", FaultlineCtrl, num_msi, -1),
    DEFINE_PROP_STRING("host", FaultlineCtrl, faultline_host),
    DEFINE_PROP_STRING("port", FaultlineCtrl, faultline_port),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription faultline_vmstate = {
    .name = "faultline",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, FaultlineCtrl),
        VMSTATE_UINT64(bar.cap, FaultlineCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static void faultline_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->init = faultline_init;
    pc->exit = faultline_exit;
    pc->class_id = PCI_CLASS_STORAGE_EXPRESS;
    pc->vendor_id = PCI_VENDOR_ID_HGST;
    pc->device_id = 0x0100;
    pc->subsystem_vendor_id = PCI_VENDOR_ID_HGST;
    pc->subsystem_id = 0x1234;
    pc->revision = 1;
    pc->is_express = 1;

    dc->desc = "Faultline simulator proxy device";
    dc->props = faultline_props;
    dc->vmsd = &faultline_vmstate;
}

static const TypeInfo faultline_info = {
    .name          = "faultline",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(FaultlineCtrl),
    .class_init    = faultline_class_init,
};

static void register_types(void)
{
    type_register_static(&faultline_info);
}

type_init(register_types)
