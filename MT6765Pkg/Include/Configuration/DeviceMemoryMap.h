#ifndef _DEVICE_MEMORY_MAP_H_
#define _DEVICE_MEMORY_MAP_H_

#include <Library/ArmLib.h>

#define MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT 128

/* sys mem att cap */
#define SYSTEM_MEMORY_RESOURCE_ATTR_CAPABILITIES                               \
  EFI_RESOURCE_ATTRIBUTE_PRESENT | EFI_RESOURCE_ATTRIBUTE_INITIALIZED |        \
  EFI_RESOURCE_ATTRIBUTE_TESTED | EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |         \
  EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |                                   \
  EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |                             \
  EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |                                \
  EFI_RESOURCE_ATTRIBUTE_EXECUTION_PROTECTABLE

typedef enum { NoHob, AddMem, AddDev, HobOnlyNoCacheSetting, MaxMem } DeviceMemoryAddHob;

#define MEMORY_REGION_NAME_MAX_LENGTH 32

typedef struct {
  CHAR8                        Name[MEMORY_REGION_NAME_MAX_LENGTH];
  EFI_PHYSICAL_ADDRESS         Address;
  UINT64                       Length;
  DeviceMemoryAddHob           HobOption;
  EFI_RESOURCE_TYPE            ResourceType;
  EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttribute;
  EFI_MEMORY_TYPE              MemoryType;
  ARM_MEMORY_REGION_ATTRIBUTES ArmAttributes;
} ARM_MEMORY_REGION_DESCRIPTOR_EX, *PARM_MEMORY_REGION_DESCRIPTOR_EX;

// shrtctz
#define MEM_RES   EFI_RESOURCE_MEMORY_RESERVED
#define MMAP_IO   EFI_RESOURCE_MEMORY_MAPPED_IO
#define SYS_MEM   EFI_RESOURCE_SYSTEM_MEMORY


#define SYS_MEM_CAP SYSTEM_MEMORY_RESOURCE_ATTR_CAPABILITIES
#define INITIALIZED EFI_RESOURCE_ATTRIBUTE_INITIALIZED
#define WRITE_COMBINEABLE EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE
#define UNCACHEABLE EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE

#define Reserv    EfiReservedMemoryType
#define Conv      EfiConventionalMemory
#define BsData    EfiBootServicesData
#define BsCode    EfiBootServicesCode
#define RtData    EfiRuntimeServicesData
#define RtCode    EfiRuntimeServicesCode
#define MmIO      EfiMemoryMappedIO

/* 
 * NOTE: WRITE_*_XN are mapped to their non-XN cparts;
 * see ArmLib.h
 * for now, all of them resolve to plain WRITE_BACK or WRITE_THROUGH
 */
#define NS_DEVICE                ARM_MEMORY_REGION_ATTRIBUTE_DEVICE
#define DEVICE                   ARM_MEMORY_REGION_ATTRIBUTE_DEVICE
#define WRITE_THROUGH            ARM_MEMORY_REGION_ATTRIBUTE_WRITE_THROUGH
#define WRITE_THROUGH_XN         ARM_MEMORY_REGION_ATTRIBUTE_WRITE_THROUGH
#define WRITE_BACK               ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK
#define WRITE_BACK_XN            ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK
#define UNCACHED_UNBUFFERED      ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED
#define UNCACHED_UNBUFFERED_XN   ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED


static ARM_MEMORY_REGION_DESCRIPTOR_EX gDeviceMemoryDescriptorEx[] = {


    // mmio/periphs
    // anything below 0x40000000 is periph regs (GIC, UART, MSDC,
    // GPIO, PMIC, MIPI DSI/CSI, audio, etc) - no RAM here =3

    { "MMIO Peripherals",      0x00000000, 0x40000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE          },

    // ddr 2gb mapped at 0x40000000 - 0xC0000000
	// dandelion 2gb tested only, angelicain/cattail or 3-4gb garden devices are not recommended

    // bl handover & low ddr
    { "HLOS Low",              0x40000000, 0x00C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN   }, 

    // --- uefi rt workspace ---
    { "UEFI Stack",            0x40C00000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN   }, // 256kb
    { "CPU Vectors",           0x40C40000, 0x00010000, AddMem, SYS_MEM, SYS_MEM_CAP, BsCode, WRITE_BACK      }, // 64kb (can I fit the BL drama inside ts?)

    // --- hlos handover (mid) ---
    { "HLOS Mid",              0x40C50000, 0x06B30000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN   }, 

    // --- mtk UwU reserved (SCP,SSPM,MCUPM,DSP) ---
    { "MTK Reserved",          0x47780000, 0x00600000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN   }, 

    // --- atf (el3, psci, smc handlers) ---
    { "ATF Reserved",          0x47D80000, 0x00050000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN   }, 

    // --- hlos handover (main) ---
    { "HLOS Main",             0x47DD0000, 0x08230000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN   }, 

    // --- efi fv vol (DXECore decompresses here, must be exec) ---
    { "UEFI FD",               0x50000000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP, BsCode, WRITE_BACK      }, 

    // --- hlos handover (high) ---
    { "HLOS High",             0x50200000, 0x2EA50000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN   }, 

    // --- fb (address fixed by bootloader,PcdMipiFrameBufferAddress)
    { "Display Reserved",      0x7EC50000, 0x013B0000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN   },

    // --- upper 1g
    { "HLOS Extended",         0x80000000, 0x40000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN   }, // 1g

    { "Terminator", 0, 0, 0, 0, 0, 0, 0 }
};

#endif
