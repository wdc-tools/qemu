#ifndef HW_FAULTLINE_H
#define HW_FAULTLINE_H

typedef struct FaultlineBar {
    uint64_t    cap;
} FaultlineBar;



#define TYPE_FAULTLINE "faultline"
#define FAULTLINE(obj) \
        OBJECT_CHECK(FaultlineCtrl, (obj), TYPE_FAULTLINE)
#define ERR_DEVICE_INACTIVE 0xf001
#define ERR_BUFFER_UNDERRUN 0xf002
#define ERR_DRIVER_INTERNAL 0xf003
#define MEM_READ_OPCODE				0x20
#define MEM_WRITE_OPCODE			0x61
#define CFG_READ_OPCODE				0x02
#define CFG_WRITE_OPCODE			0x42
#define READ_COMPLETION_OPCODE		0x4A
#define FIRE_INTERRUPT_OPCODE		0xF4

typedef struct FaultlineCtrl {
    PCIDevice    parent_obj;
    MemoryRegion iomem;
    FaultlineBar bar;
    QemuThread	ioThread;
    QemuCond	read_stream_condition;
    QemuMutex	read_stream_mutex;

    uint16_t    page_size;
    uint16_t    page_bits;
    int32_t     num_msi;
    int32_t     num_msix;
    uint32_t	is_connected;
    uint32_t	stopping;
    uint32_t	error_code;
    char		*faultline_host;
    char		*faultline_port;
    struct addrinfo *faultline_addr;
    int			faultline_socket;
    uint8_t 	*read_buffer;
    size_t		read_buffer_length;


} FaultlineCtrl;

#pragma pack(push, 1)

typedef struct mem_read {
	uint8_t		opcode; //0x20
	uint16_t	length;
	uint32_t	addr_low;
	uint32_t	addr_high;
} mem_read;

typedef struct mem_write {
	uint8_t		opcode; //0x61
	uint16_t	length;
	uint32_t	addr_low;
	uint32_t	addr_high;
	uint8_t		data;
} mem_write;

typedef struct cfg_read {
	uint8_t		opcode; //0x02
	uint16_t	length;
	uint32_t	reg;
} cfg_read;

typedef struct cfg_write {
	uint8_t		opcode; //0x42
	uint16_t	length;
	uint32_t	reg;
	uint8_t		data;
} cfg_write;

typedef struct read_completion {
	uint8_t		opcode; //0x4A
	uint16_t	length;
	uint8_t		data;
} read_completion;

typedef struct interrupt_fire {
	uint8_t		opcode; //0xF4
	uint16_t	interrupt_id;
} interrupt_fire;

#pragma pack(pop)  //PCITCP_STRUCTS

static size_t readFromConnection(FaultlineCtrl *f,uint8_t *data, int length, uint32_t *ec);
static int readComplete(FaultlineCtrl *f);
static int readMem(FaultlineCtrl *f);
static int writeMem(FaultlineCtrl *f);
static int sendToTCP(FaultlineCtrl *f,uint8_t *command, int size, uint8_t *data, uint16_t length);

#endif /* HW_FAULTLINE_H */
