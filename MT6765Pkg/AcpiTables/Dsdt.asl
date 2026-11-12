/** @file
  Minimal MT6765 DSDT.
**/

#include "ArmPlatform.h"

DefinitionBlock("DsdtTable.aml", "DSDT", 1, "MTK   ", "MT6765", EFI_ACPI_ARM_OEM_REVISION) {
  Scope(_SB) {
    Method (_OSC, 4, Serialized) {
      CreateDWordField (Arg3, 0x00, STS0)
      CreateDWordField (Arg3, 0x04, CAP0)
      Return (Arg3)
    }

    Device (CPU0) { Name (_HID, "ACPI0007") Name (_UID, 0) }
  }
}
