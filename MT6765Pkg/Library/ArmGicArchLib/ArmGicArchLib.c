#include <Uefi.h>
#include <Library/ArmLib.h>
#include <Library/ArmGicLib.h>

ARM_GIC_ARCH_REVISION
EFIAPI
ArmGicGetSupportedArchRevision (
  VOID
  )
{
  return ARM_GIC_ARCH_REVISION_2;
}
