#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include "Library/FrameBufferBltLib.h"
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <PiDxe.h>
#include <Protocol/GraphicsOutput.h>
#include <Uefi.h>

/// Defines
#define VNBYTES(bpix) (1 << (bpix)) / 8
#define VNBITS(bpix) (1 << (bpix))

#define FB_BITS_PER_PIXEL (32)
#define FB_BYTES_PER_PIXEL (FB_BITS_PER_PIXEL / 8)

// MT6765 DISP_OVL0 layer-0 pitch reg; BIT28 enables const blend so panel pipeline ignores framebuffers byt3 as a pixel alphachan
#define MT6765_DISP_OVL0_PITCH0  (0x1400B000U + 0x44U)
#define MT6765_OVL_CONST_BLEND   (1U << 28)

enum video_log2_bpp {
  VIDEO_BPP1 = 0,
  VIDEO_BPP2,
  VIDEO_BPP4,
  VIDEO_BPP8,
  VIDEO_BPP16,
  VIDEO_BPP32,
};

typedef struct {
  VENDOR_DEVICE_PATH DisplayDevicePath;
  EFI_DEVICE_PATH    EndDevicePath;
} DISPLAY_DEVICE_PATH;

DISPLAY_DEVICE_PATH mDisplayDevicePath = {
    {{HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
          (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
          (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8),
      }},
     EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID},
    {END_DEVICE_PATH_TYPE,
     END_ENTIRE_DEVICE_PATH_SUBTYPE,
     {sizeof(EFI_DEVICE_PATH_PROTOCOL), 0}}};

/// Declares

STATIC FRAME_BUFFER_CONFIGURE *mFrameBufferBltLibConfigure;
STATIC UINTN                   mFrameBufferBltLibConfigureSize;
STATIC UINT32                  mFrameBufferPixelsPerScanLine;

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber,
    OUT UINTN *SizeOfInfo, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber);

STATIC
EFI_STATUS
EFIAPI
DisplayBlt(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *            BltBuffer,
    OPTIONAL IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
    IN UINTN SourceX, IN UINTN SourceY, IN UINTN DestinationX,
    IN UINTN DestinationY, IN UINTN Width, IN UINTN Height,
    IN UINTN Delta OPTIONAL);

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mDisplay = {
    DisplayQueryMode, DisplaySetMode, DisplayBlt, NULL};

STATIC
VOID
DisplayDisablePixelAlpha (
    VOID
    )
{
  MmioOr32 (MT6765_DISP_OVL0_PITCH0, MT6765_OVL_CONST_BLEND);
  DEBUG ((EFI_D_INFO,
          "SimpleFbDxe: OVL0 constant blend enabled at 0x%08x\n",
          MT6765_DISP_OVL0_PITCH0));
}

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber,
    OUT UINTN *SizeOfInfo, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
  EFI_STATUS Status;

  if (This == NULL || This->Mode == NULL || SizeOfInfo == NULL || Info == NULL ||
      ModeNumber >= This->Mode->MaxMode) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->AllocatePool(
      EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
      (VOID **)Info);

  if (EFI_ERROR(Status)) {
    return Status;
  }

  *SizeOfInfo                   = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  (*Info)->Version              = This->Mode->Info->Version;
  (*Info)->HorizontalResolution = This->Mode->Info->HorizontalResolution;
  (*Info)->VerticalResolution   = This->Mode->Info->VerticalResolution;
  (*Info)->PixelFormat          = This->Mode->Info->PixelFormat;
  CopyMem (&(*Info)->PixelInformation,
           &This->Mode->Info->PixelInformation,
           sizeof (EFI_PIXEL_BITMASK));
  (*Info)->PixelsPerScanLine    = This->Mode->Info->PixelsPerScanLine;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber)
{
  if (This == NULL || This->Mode == NULL || ModeNumber >= This->Mode->MaxMode) {
    return EFI_INVALID_PARAMETER;
  }

  This->Mode->Mode = ModeNumber;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplayBlt(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *            BltBuffer,
    OPTIONAL IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
    IN UINTN SourceX, IN UINTN SourceY, IN UINTN DestinationX,
    IN UINTN DestinationY, IN UINTN Width, IN UINTN Height,
    IN UINTN Delta OPTIONAL)
{
  RETURN_STATUS Status;
  EFI_TPL       Tpl;

  // Do not modify BltBuffer, the Reserved byte is not an alpha channel in GOP pixel format and changing it here corrupts callers buffers
  if (mFrameBufferBltLibConfigure == NULL) {
    return EFI_NOT_READY;
  }

  //
  // We have to raise to TPL_NOTIFY, so we make an atomic write to the frame
  // buffer. We would not want a timer based event (Cursor, ...) to come in
  // while we are doing this operation.
  //
  Tpl    = gBS->RaiseTPL(TPL_NOTIFY);
  Status = FrameBufferBlt(
      mFrameBufferBltLibConfigure, BltBuffer, BltOperation, SourceX, SourceY,
      DestinationX, DestinationY, Width, Height, Delta);

  gBS->RestoreTPL(Tpl);

  return RETURN_ERROR(Status) ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SimpleFbDxeInitialize(
    IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status             = EFI_SUCCESS;
  EFI_HANDLE hUEFIDisplayHandle = NULL;

  DEBUG((EFI_D_ERROR, "SimpleFbDxe: Initializing FrameBuffer...\n"));

  UINT32 MipiFrameBufferAddr   = FixedPcdGet32(PcdMipiFrameBufferAddress);
  UINT32 MipiFrameBufferWidth  = FixedPcdGet32(PcdMipiFrameBufferWidth);
  UINT32 MipiFrameBufferHeight = FixedPcdGet32(PcdMipiFrameBufferHeight);

  if (MipiFrameBufferAddr == 0 || MipiFrameBufferWidth == 0 || MipiFrameBufferHeight == 0) {
    DEBUG((EFI_D_ERROR, "SimpleFbDxe: Invalid FrameBuffer parameters\n"));
    return EFI_DEVICE_ERROR;
  }

  if (mDisplay.Mode == NULL) {
    Status = gBS->AllocatePool(
        EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE),
        (VOID **)&mDisplay.Mode);
    ASSERT_EFI_ERROR(Status);
    if (EFI_ERROR(Status)) return Status;
    ZeroMem(mDisplay.Mode, sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE));
  }

  if (mDisplay.Mode->Info == NULL) {
    Status = gBS->AllocatePool(
        EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
        (VOID **)&mDisplay.Mode->Info);
    ASSERT_EFI_ERROR(Status);
    if (EFI_ERROR(Status)) return Status;
    ZeroMem(mDisplay.Mode->Info, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));
  }

  mDisplay.Mode->MaxMode       = 1;
  mDisplay.Mode->Mode          = 0;
  mDisplay.Mode->Info->Version = 0;

  mDisplay.Mode->Info->HorizontalResolution = MipiFrameBufferWidth;
  mDisplay.Mode->Info->VerticalResolution   = MipiFrameBufferHeight;

  UINT32 LineLength = FixedPcdGet32(PcdMipiFrameBufferStride);
  UINT64 MinimumLineLength = (UINT64)MipiFrameBufferWidth * FB_BYTES_PER_PIXEL;

  if (LineLength == 0 || (UINT64)LineLength < MinimumLineLength) {
    DEBUG((EFI_D_ERROR,
           "SimpleFbDxe: Invalid stride %u for width %u\n",
           LineLength,
           MipiFrameBufferWidth));
    return EFI_DEVICE_ERROR;
  }

  UINTN FrameBufferSize = (UINTN)LineLength * MipiFrameBufferHeight;

  mFrameBufferPixelsPerScanLine = LineLength / FB_BYTES_PER_PIXEL;
  mDisplay.Mode->Info->PixelsPerScanLine = mFrameBufferPixelsPerScanLine;

  // platform fb is laid out as BGRA8888 in a memory
  mDisplay.Mode->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;

  mDisplay.Mode->SizeOfInfo      = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  mDisplay.Mode->FrameBufferBase = (EFI_PHYSICAL_ADDRESS)MipiFrameBufferAddr;
  mDisplay.Mode->FrameBufferSize = FrameBufferSize;

  // config blt
  Status = FrameBufferBltConfigure(
      (VOID *)(UINTN)mDisplay.Mode->FrameBufferBase, mDisplay.Mode->Info,
      mFrameBufferBltLibConfigure, &mFrameBufferBltLibConfigureSize);
  if (Status == RETURN_BUFFER_TOO_SMALL) {
    mFrameBufferBltLibConfigure = AllocatePool(mFrameBufferBltLibConfigureSize);
    if (mFrameBufferBltLibConfigure == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = FrameBufferBltConfigure(
        (VOID *)(UINTN)mDisplay.Mode->FrameBufferBase, mDisplay.Mode->Info,
        mFrameBufferBltLibConfigure, &mFrameBufferBltLibConfigureSize);
  }
  if (RETURN_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "SimpleFbDxe: FrameBufferBltConfigure failed: %r\n", Status));
    return EFI_DEVICE_ERROR;
  }

  // garden uses DISP_OVL0 for fb layer; if pix alpha blending remains enabled, Win10/11 can be rendered transparent by alpha byte even though the GOP format is otherwise correct
  DisplayDisablePixelAlpha ();

  /* Register handle */
  Status = gBS->InstallMultipleProtocolInterfaces(
      &hUEFIDisplayHandle, &gEfiDevicePathProtocolGuid, &mDisplayDevicePath,
      &gEfiGraphicsOutputProtocolGuid, &mDisplay, NULL);

  ASSERT_EFI_ERROR(Status);

  return Status;
}
