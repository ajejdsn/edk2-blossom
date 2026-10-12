/** @file
  SMBIOS tables for Xiaomi Redmi 9A (dandelion, MT6762G Helio G25)

  Copyright (c) 2015, ARM Limited. All rights reserved.
  Copyright (c) 2026, Emil Ermekov

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include <ArmPlatform.h>
#include <IndustryStandard/SmBios.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <PiDxe.h>
#include <Protocol/Smbios.h>

#define TYPE0_STRINGS                                    \
  "EDK II\0"                           /* Vendor */      \
  "EDK II\0"                           /* BiosVersion */ \
  __DATE__"\0"                         /* BiosReleaseDate */

#define TYPE1_STRINGS                                   \
  "Xiaomi\0"                         /* Manufacturer */ \
  "Redmi 9A\0"                       /* Product Name */ \
  "dandelion\0"                      /* Version */      \
  "0123456789\0"                     /* Serial (placeholder) */

#define TYPE2_STRINGS                                     \
  "Xiaomi\0"                         /* Manufacturer */   \
  "dandelion\0"                      /* Product Name */   \
  "1.0\0"                            /* Version */        \
  "0123456789\0"                     /* Serial */         \
  "Base of Chassis\0"                /* board location */

#define TYPE3_STRINGS                                   \
  "Xiaomi\0"                         /* Manufacturer */ \
  "1.0\0"                            /* Version */      \
  "0123456789\0"                     /* Serial  */

#define TYPE4_STRINGS                                               \
  "BGA\0"                            /* socket type */              \
  "MediaTek\0"                       /* manufacturer */             \
  "Cortex-A53\0"                     /* processor description */    \
  "0xd03\0"                          /* A53 part number */

#define TYPE7_STRINGS                              \
  "L1 Instruction\0"                 /* L1I  */    \
  "L1 Data\0"                        /* L1D  */    \
  "L2\0"                             /* L2   */

#define TYPE16_STRINGS                             \
  "\0"                               /* nothing...... */

#define TYPE17_STRINGS                                       \
  "RIGHT SIDE\0"                     /* location */          \
  "BANK 0\0"                         /* bank description */

#define TYPE32_STRINGS                             \
  "\0"                               /* nothing... again */

#pragma pack(1)
typedef struct {
  SMBIOS_TABLE_TYPE0 Base;
  INT8              Strings[sizeof(TYPE0_STRINGS)];
} ARM_TYPE0;

typedef struct {
  SMBIOS_TABLE_TYPE1 Base;
  UINT8              Strings[sizeof(TYPE1_STRINGS)];
} ARM_TYPE1;

typedef struct {
  SMBIOS_TABLE_TYPE2 Base;
  UINT8              Strings[sizeof(TYPE2_STRINGS)];
} ARM_TYPE2;

typedef struct {
  SMBIOS_TABLE_TYPE3 Base;
  UINT8              Strings[sizeof(TYPE3_STRINGS)];
} ARM_TYPE3;

typedef struct {
  SMBIOS_TABLE_TYPE4 Base;
  UINT8              Strings[sizeof(TYPE4_STRINGS)];
} ARM_TYPE4;

typedef struct {
  SMBIOS_TABLE_TYPE7 Base;
  UINT8              Strings[sizeof(TYPE7_STRINGS)];
} ARM_TYPE7;

typedef struct {
  SMBIOS_TABLE_TYPE16 Base;
  UINT8              Strings[sizeof(TYPE16_STRINGS)];
} ARM_TYPE16;

typedef struct {
  SMBIOS_TABLE_TYPE17 Base;
  UINT8              Strings[sizeof(TYPE17_STRINGS)];
} ARM_TYPE17;

typedef struct {
  SMBIOS_TABLE_TYPE19 Base;
  UINT8              Strings[sizeof("\0")];
} ARM_TYPE19;

typedef struct {
  SMBIOS_TABLE_TYPE32 Base;
  UINT8              Strings[sizeof(TYPE32_STRINGS)];
} ARM_TYPE32;

enum SMBIOS_REFRENCE_HANDLES {
  SMBIOS_HANDLE_A53_L1I = 0x1000,
  SMBIOS_HANDLE_A53_L1D,
  SMBIOS_HANDLE_A53_L2,
  SMBIOS_HANDLE_MOTHERBOARD,
  SMBIOS_HANDLE_CHASSIS,
  SMBIOS_HANDLE_A53_CLUSTER,
  SMBIOS_HANDLE_MEMORY,
  SMBIOS_HANDLE_DIMM
};

#pragma pack()

// BIOS information (section 7.1)
STATIC ARM_TYPE0 mArmDefaultType0 = {
  {
    {
      EFI_SMBIOS_TYPE_BIOS_INFORMATION,
      sizeof (SMBIOS_TABLE_TYPE0),
      SMBIOS_HANDLE_PI_RESERVED,
    },
    1,     // Vendor
    2,     // BiosVersion
    0xE800,
    3,     // BiosReleaseDate
    0,
    {
      0,0,0,0,0,0,
      0, // PCI not supported
      0,
      0, // PNP not supported
      0,
      1, // BIOS upgradable
      0,0,0,
      0, // no boot from CD
      1, // selectable boot
    },
    {
      0x0,
      0x0,
    },
    0,
    0,
    0xFF,
    0xFF
  },
  TYPE0_STRINGS
};

// System information (section 7.2)
STATIC CONST ARM_TYPE1 mArmDefaultType1 = {
  {
    {
      EFI_SMBIOS_TYPE_SYSTEM_INFORMATION,
      sizeof(SMBIOS_TABLE_TYPE1),
      SMBIOS_HANDLE_PI_RESERVED,
    },
    1,     // Manufacturer
    2,     // Product Name
    3,     // Version
    4,     // Serial
    { 0x2555d305, 0x5bb8, 0x4e7d,
      { 0xa5, 0xb0, 0x7c, 0xf4, 0x4f, 0x76, 0xb2, 0x72 }}, // UUID
    6,     // PS
    0,     // SKU
    0,     // Family
  },
  TYPE1_STRINGS
};

// Baseboard (section 7.3)
STATIC ARM_TYPE2 mArmDefaultType2 = {
  {
    {
      EFI_SMBIOS_TYPE_BASEBOARD_INFORMATION,
      sizeof (SMBIOS_TABLE_TYPE2),
      SMBIOS_HANDLE_MOTHERBOARD,
    },
    1,    // Manufacturer
    2,    // Product Name
    3,    // Version
    4,    // Serial
    0,    // Asset tag
    {1},  // motherboard, not replaceable
    5,    // location of board
    SMBIOS_HANDLE_CHASSIS,
    BaseBoardTypeMotherBoard,
    1,
    {SMBIOS_HANDLE_A53_CLUSTER},
  },
  TYPE2_STRINGS
};

// Enclosure
STATIC CONST ARM_TYPE3 mArmDefaultType3 = {
  {
    {
      EFI_SMBIOS_TYPE_SYSTEM_ENCLOSURE,
      sizeof (SMBIOS_TABLE_TYPE3),
      SMBIOS_HANDLE_CHASSIS,
    },
    1,   // Manufacturer
    ChassisTypeHandHeld,   // handheld
    2,   // version
    3,   // serial
    0,   // asset tag
    ChassisStateUnknown,
    ChassisStateSafe,
    ChassisStateSafe,
    ChassisSecurityStatusNone,
    {0,0,0,0},
    1,  // height
    1,  // power cords
    0,  // contained elements
  },
  TYPE3_STRINGS
};

// CPU: 8x Cortex-A53 @ 2.0 GHz
STATIC CONST ARM_TYPE4 mArmDefaultType4_a53 = {
  {
    {
      EFI_SMBIOS_TYPE_PROCESSOR_INFORMATION,
      sizeof (SMBIOS_TABLE_TYPE4),
      SMBIOS_HANDLE_A53_CLUSTER,
    },
    1, // socket type
    3, // processor type CPU
    ProcessorFamilyIndicatorFamily2,
    2, // manufacturer
    {{0,},{0.}},
    3, // version -> TYPE4_STRINGS[3] = "Cortex-A53"
    {0,0,0,0,0,1}, // voltage
    0,
    2000, // max speed MHz
    2000, // current speed MHz
    0x41, // status
    ProcessorUpgradeOther,
    SMBIOS_HANDLE_A53_L1I,
    SMBIOS_HANDLE_A53_L2,
    0xFFFF,
    0,
    0,
    4, // part number -> TYPE4_STRINGS[4] = "0xd03"
    8, // core count
    8, // enabled core count
    8, // threads per socket (A53 = 1 thread per core, but SMBIOS quirk)
    0xEC,
    ProcessorFamilyARM,
  },
  TYPE4_STRINGS
};

// L1 Instruction cache
STATIC CONST ARM_TYPE7 mArmDefaultType7_a53_l1i = {
  {
    {
      EFI_SMBIOS_TYPE_CACHE_INFORMATION,
      sizeof (SMBIOS_TABLE_TYPE7),
      SMBIOS_HANDLE_A53_L1I,
    },
    1,
    0x380, // L1 enabled, unknown WB
    32, // 32K
    32,
    {0,1},
    {0,1},
    0,
    CacheErrorParity,
    CacheTypeInstruction,
    CacheAssociativity2Way,
  },
  TYPE7_STRINGS
};

// L1 Data cache
STATIC CONST ARM_TYPE7 mArmDefaultType7_a53_l1d = {
  {
    {
      EFI_SMBIOS_TYPE_CACHE_INFORMATION,
      sizeof (SMBIOS_TABLE_TYPE7),
      SMBIOS_HANDLE_A53_L1D,
    },
    2,
    0x180, // L1 enabled, WB
    32, // 32K
    32,
    {0,1},
    {0,1},
    0,
    CacheErrorSingleBit,
    CacheTypeData,
    CacheAssociativity4Way,
  },
  TYPE7_STRINGS
};

// L2 cache (unified, 1 MB per cluster)
STATIC CONST ARM_TYPE7 mArmDefaultType7_a53_l2 = {
  {
    {
      EFI_SMBIOS_TYPE_CACHE_INFORMATION,
      sizeof (SMBIOS_TABLE_TYPE7),
      SMBIOS_HANDLE_A53_L2,
    },
    3,
    0x181, // L2 enabled, WB
    1024, // 1M
    1024,
    {0,1},
    {0,1},
    0,
    CacheErrorSingleBit,
    CacheTypeUnified,
    CacheAssociativity16Way,
  },
  TYPE7_STRINGS
};

// Memory array
STATIC CONST ARM_TYPE16 mArmDefaultType16 = {
  {
    {
      EFI_SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY,
      sizeof (SMBIOS_TABLE_TYPE16),
      SMBIOS_HANDLE_MEMORY,
    },
    MemoryArrayLocationSystemBoard,
    MemoryArrayUseSystemMemory,
    MemoryErrorCorrectionNone,
    0x200000, // 2GB (KB)
    0xFFFE,
    0x1,      // soldered
  },
  TYPE16_STRINGS
};
// Memory device (LPDDR4X 2GB @ 1600 MHz)
STATIC CONST ARM_TYPE17 mArmDefaultType17 = {
  {
    {
      EFI_SMBIOS_TYPE_MEMORY_DEVICE,
      sizeof (SMBIOS_TABLE_TYPE17),
      SMBIOS_HANDLE_DIMM,
    },
    SMBIOS_HANDLE_MEMORY, // array handle
    0xFFFE,               // no errors
    32,                   // total width (2x16)
    32,                   // data width
    0x2000,               // 2GB (MB) = 2048
    0x0B,                 // row of chips (soldered)
    0,                    // not part of a set
    1,                    // right side
    2,                    // bank 0
    MemoryTypeLpddr4,     // SMBIOS 0x1E, no LPDDR4X code
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}, // unbuffered
    1600,                 // speed MHz
    0,                    // manufacturer
    0,                    // serial
    0,                    // asset tag
    0,                    // part number
    0,                    // rank
  },
  TYPE17_STRINGS
};

// mem array mapped address - from pcd
STATIC CONST ARM_TYPE19 mArmDefaultType19 = {
  {
    {
      EFI_SMBIOS_TYPE_MEMORY_ARRAY_MAPPED_ADDRESS,
      sizeof (SMBIOS_TABLE_TYPE19),
      SMBIOS_HANDLE_PI_RESERVED,
    },
    0xFFFFFFFF,
    0xFFFFFFFF,
    SMBIOS_HANDLE_DIMM,
    1,
    0x080000000,
    0x100000000,
  },
  "\0"
};

// System boot info
STATIC CONST ARM_TYPE32 mArmDefaultType32 = {
  {
    {
      EFI_SMBIOS_TYPE_SYSTEM_BOOT_INFORMATION,
      sizeof (SMBIOS_TABLE_TYPE32),
      SMBIOS_HANDLE_PI_RESERVED,
    },
    {0,0,0,0,0,0},
    BootInformationStatusNoError,
  },
  TYPE32_STRINGS
};

STATIC CONST VOID *DefaultCommonTables[]=
{
  &mArmDefaultType0,
  &mArmDefaultType1,
  &mArmDefaultType2,
  &mArmDefaultType3,
  &mArmDefaultType7_a53_l1i,
  &mArmDefaultType7_a53_l1d,
  &mArmDefaultType7_a53_l2,
  &mArmDefaultType4_a53,
  &mArmDefaultType16,
  &mArmDefaultType17,
  &mArmDefaultType32,
  NULL
};

EFI_STATUS
InstallMemoryStructure (
  IN EFI_SMBIOS_PROTOCOL       *Smbios,
  IN UINT64                    StartingAddress,
  IN UINT64                    RegionLength
  )
{
  EFI_SMBIOS_HANDLE         SmbiosHandle;
  ARM_TYPE19                MemoryDescriptor;
  EFI_STATUS                Status = EFI_SUCCESS;

  CopyMem( &MemoryDescriptor, &mArmDefaultType19, sizeof(ARM_TYPE19));

  MemoryDescriptor.Base.ExtendedStartingAddress = StartingAddress;
  MemoryDescriptor.Base.ExtendedEndingAddress = StartingAddress+RegionLength;
  SmbiosHandle = MemoryDescriptor.Base.Hdr.Handle;

  Status = Smbios->Add (
    Smbios,
    NULL,
    &SmbiosHandle,
    (EFI_SMBIOS_TABLE_HEADER*) &MemoryDescriptor
    );
  return Status;
}

EFI_STATUS
InstallStructures (
   IN EFI_SMBIOS_PROTOCOL       *Smbios,
   IN CONST VOID *DefaultTables[]
   )
{
    EFI_STATUS                Status = EFI_SUCCESS;
    EFI_SMBIOS_HANDLE         SmbiosHandle;

    int TableEntry;
    for ( TableEntry=0; DefaultTables[TableEntry] != NULL; TableEntry++)
    {
	SmbiosHandle = ((EFI_SMBIOS_TABLE_HEADER*)DefaultTables[TableEntry])->Handle;
	Status = Smbios->Add (
	    Smbios,
	    NULL,
	    &SmbiosHandle,
	    (EFI_SMBIOS_TABLE_HEADER*) DefaultTables[TableEntry]
	    );
	if (EFI_ERROR(Status))
	    break;
    }
    return Status;
}

EFI_STATUS
InstallAllStructures (
   IN EFI_SMBIOS_PROTOCOL       *Smbios
   )
{
  EFI_STATUS                Status = EFI_SUCCESS;

  Status=InstallStructures (Smbios,DefaultCommonTables);
  ASSERT_EFI_ERROR (Status);

  Status = InstallMemoryStructure ( Smbios, PcdGet64 (PcdSystemMemoryBase), PcdGet64 (PcdSystemMemorySize));
  ASSERT_EFI_ERROR (Status);

  return Status;
}

EFI_STATUS
EFIAPI
SmbiosTablePublishEntry (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  EFI_STATUS                Status;
  EFI_SMBIOS_PROTOCOL       *Smbios;

  Status = gBS->LocateProtocol (
    &gEfiSmbiosProtocolGuid,
    NULL,
    (VOID**)&Smbios
    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = InstallAllStructures (Smbios);

  return Status;
}
