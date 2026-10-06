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
    Device (CPU1) { Name (_HID, "ACPI0007") Name (_UID, 1) }
    Device (CPU2) { Name (_HID, "ACPI0007") Name (_UID, 2) }
    Device (CPU3) { Name (_HID, "ACPI0007") Name (_UID, 3) }
    Device (CPU4) { Name (_HID, "ACPI0007") Name (_UID, 4) }
    Device (CPU5) { Name (_HID, "ACPI0007") Name (_UID, 5) }
    Device (CPU6) { Name (_HID, "ACPI0007") Name (_UID, 6) }
    Device (CPU7) { Name (_HID, "ACPI0007") Name (_UID, 7) }
  }
}
