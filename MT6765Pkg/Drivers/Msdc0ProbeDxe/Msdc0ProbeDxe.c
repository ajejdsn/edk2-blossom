/*
Msdc0ProbeDxe.c
Version ???

CMD failure

*/


#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/MmcHost.h>


//
// ============================================================================
// MT6765 MSDC0 register map
// ============================================================================
//

#define MSDC_CFG              0x00
#define MSDC_IOCON            0x04
#define MSDC_PS               0x08
#define MSDC_INT              0x0C
#define MSDC_INTEN            0x10
#define MSDC_FIFOCS           0x14
#define MSDC_TXDATA           0x18
#define MSDC_RXDATA           0x1C

#define SDC_CFG               0x30
#define SDC_CMD               0x34
#define SDC_ARG               0x38
#define SDC_STS               0x3C
#define SDC_RESP0             0x40
#define SDC_RESP1             0x44
#define SDC_RESP2             0x48
#define SDC_RESP3             0x4C
#define SDC_BLK_NUM           0x50

#define SDC_ADV_CFG0          0x64
#define MSDC_NEW_RX_CFG       0x68

#define MSDC_PATCH_BIT        0xB0
#define MSDC_PATCH_BIT1       0xB4
#define MSDC_PATCH_BIT2       0xB8

#define MSDC_PAD_TUNE         0xEC
#define MSDC_PAD_TUNE0        0xF0

#define EMMC50_CFG0           0x208
#define EMMC50_CFG1           0x20C
#define EMMC50_CFG2           0x21C
#define EMMC50_CFG3           0x220

#define SDC_FIFO_CFG          0x228

#define SDC_CFG_BUSWIDTH_MASK (3U << 16)
#define MSDC_CFG_CKDIV_MASK   0x0000FF00
#define MSDC_CFG_CKMOD_MASK   0x00030000
#define MSDC_CFG_CKSTB        BIT7

#define MSDC_IDENT_CLK_DIV    17U   // ~382 kHz @ CKMOD=0, 26MHz src


//
// ============================================================================
// FIFO
// ============================================================================
//

#define MSDC_FIFOCS_RXCNT     0x000000FF
#define MSDC_FIFOCS_TXCNT     0x00FF0000
#define MSDC_FIFOCS_CLR       BIT31


//
// ============================================================================
// Interrupts
// ============================================================================
//

#define MSDC_INT_CMDRDY       BIT8
#define MSDC_INT_CMDTMO       BIT9
#define MSDC_INT_RSPCRCERR    BIT10
#define MSDC_INT_XFER_COMPL   BIT12
#define MSDC_INT_DATTMO       BIT14
#define MSDC_INT_DATCRCERR    BIT15


//
// ============================================================================
// SDC status
// ============================================================================
//

#define SDC_STS_CMDBUSY       BIT1


//
// ============================================================================
// SDC_CMD fields
// ============================================================================
//

#define SDC_CMD_CMD_MASK      0x0000003F

#define SDC_CMD_RSPTYP_MASK   0x00000380
#define SDC_CMD_RSPTYP_SHIFT  7

#define SDC_CMD_DTYPE_MASK    0x00001800
#define SDC_CMD_DTYPE_SHIFT   11

#define SDC_CMD_WR            BIT13
#define SDC_CMD_STOP          BIT14

#define SDC_CMD_BLK_LEN_MASK  0x0FFF0000
#define SDC_CMD_BLK_LEN_SHIFT 16


//
// ============================================================================
// Constants
// ============================================================================
//

#define MSDC_BLOCK_SIZE       512
#define MSDC_TIMEOUT_US       1000000


//
// ============================================================================
// Device path GUID
// ============================================================================
//

STATIC EFI_GUID mMsdc0DevicePathGuid = EFI_CALLER_ID_GUID;


//
// ============================================================================
// Driver state
// ============================================================================
//

STATIC UINTN  mMsdcBase;
STATIC UINT32 mLastCommand;


//
// ============================================================================
// MMIO
// ============================================================================
//

STATIC
UINT32
MsdcRead (
  IN UINTN Offset
  )
{
  return MmioRead32 (mMsdcBase + Offset);
}


STATIC
VOID
MsdcWrite (
  IN UINTN  Offset,
  IN UINT32 Value
  )
{
  MmioWrite32 (mMsdcBase + Offset, Value);
}


//
// ============================================================================
// Wait for register condition
//
// NOTE: I teleported bread.
//
// ============================================================================
//

STATIC
BOOLEAN
MsdcWaitMask (
  IN UINTN  Offset,
  IN UINT32 Mask,
  IN UINT32 Value,
  IN UINTN  TimeoutUs
  )
{
  while (TimeoutUs-- > 0) {

    if ((MsdcRead (Offset) & Mask) == Value) {
      return TRUE;
    }

    MicroSecondDelay (1);
  }

  return FALSE;
}


//
// ============================================================================
// Wait for interrupt
// ============================================================================
//

STATIC
EFI_STATUS
MsdcWaitInterrupt (
  IN  UINT32 Wanted,
  IN  UINT32 ErrorMask,
  OUT UINT32 *InterruptStatus
  )
{
  UINT32 IntStatus;
  UINTN  Timeout;

  for (Timeout = 0; Timeout < MSDC_TIMEOUT_US; Timeout++) {

    IntStatus = MsdcRead (MSDC_INT);

    if ((IntStatus & (Wanted | ErrorMask)) != 0) {

      //
      // Clear only the bits we observed.
      //
      MsdcWrite (MSDC_INT, IntStatus);

      if (InterruptStatus != NULL) {
        *InterruptStatus = IntStatus;
      }

      if ((IntStatus & ErrorMask) != 0) {

        if ((IntStatus & (MSDC_INT_CMDTMO | MSDC_INT_DATTMO)) != 0) {
          return EFI_TIMEOUT;
        }

        return EFI_DEVICE_ERROR;
      }

      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  if (InterruptStatus != NULL) {
    *InterruptStatus = 0;
  }

  return EFI_TIMEOUT;
}


//
// ============================================================================
// Debug state
// ============================================================================
//

STATIC
VOID
MsdcDumpState (
  VOID
  )
{
  DEBUG ((
    DEBUG_ERROR,
    "MSDC0: CFG=%08x INT=%08x INTEN=%08x FIFO=%08x "
    "SDC_CFG=%08x SDC_STS=%08x BLK=%08x\n",
    MsdcRead (MSDC_CFG),
    MsdcRead (MSDC_INT),
    MsdcRead (MSDC_INTEN),
    MsdcRead (MSDC_FIFOCS),
    MsdcRead (SDC_CFG),
    MsdcRead (SDC_STS),
    MsdcRead (SDC_BLK_NUM)
    ));
}


//
// ============================================================================
// MTK response encoding
//
// 0 = no response
// 1 = R1/R5/R6/R7
// 2 = R2
// 3 = R3/R4
// 7 = R1b
//
// IMPORTANT:
// MMC_RESPONSE_TYPE_R1 == MMC_RESPONSE_TYPE_R1b in EDK2,
// so R1b is identified from the command opcode here.
// ============================================================================
//

STATIC
UINT32
MsdcResponseType (
  IN MMC_CMD Cmd
  )
{
  UINT32 Opcode;

  Opcode = MMC_GET_INDX (Cmd);

  if ((Cmd & MMC_CMD_WAIT_RESPONSE) == 0) {
    return 0;
  }

  if ((Cmd & MMC_CMD_LONG_RESPONSE) != 0) {
    return 2;
  }

  if ((Cmd & MMC_CMD_NO_CRC_RESPONSE) != 0) {
    return 3;
  }

  //
  // R1b commands.
  //
  if (Opcode == 6 ||
	  Opcode == 7 ||
      Opcode == 12 ||
      Opcode == 28 ||
      Opcode == 29
	  ) {
    return 7;
  }

  return 1;
}


STATIC
EFI_STATUS
MsdcApplyClock (
  IN UINT32 BusClockHz
  )
{
  UINT32 Cfg;
  UINT32 Div;

  Cfg = MsdcRead (MSDC_CFG);
  Cfg &= ~(MSDC_CFG_CKMOD_MASK | MSDC_CFG_CKDIV_MASK);

  if (BusClockHz == 0 || BusClockHz <= 400000U) {
    Div = MSDC_IDENT_CLK_DIV;
  } else {
    Div = (26000000U + (BusClockHz * 4U) - 1U) / (BusClockHz * 4U);
    if (Div == 0) {
      Div = 1;
    }
    if (Div > 255U) {
      Div = 255U;
    }
  }

  Cfg |= (Div << 8);

  MsdcWrite (MSDC_CFG, Cfg);

  if (!MsdcWaitMask (MSDC_CFG, MSDC_CFG_CKSTB, MSDC_CFG_CKSTB, MSDC_TIMEOUT_US)) {
    DEBUG ((DEBUG_ERROR, "MSDC0: CKSTB timeout, target=%u Hz Div=%u\n", BusClockHz, Div));
    return EFI_TIMEOUT;
  }

  DEBUG ((DEBUG_INFO, "MSDC0: clock set target=%u Hz Div=%u CFG=%08x\n",
    BusClockHz, Div, MsdcRead (MSDC_CFG)));

  return EFI_SUCCESS;
}

//
// ============================================================================
// Does this command have data?
// ============================================================================
//

STATIC
BOOLEAN
MsdcCommandHasData (
  IN MMC_CMD Cmd
  )
{
  switch (MMC_GET_INDX (Cmd)) {

    case 8:
    case 17:
    case 18:
    case 19:
    case 21:
    case 24:
    case 25:
    case 30:
    case 51:
      return TRUE;

    default:
      return FALSE;
  }
}


//
// ============================================================================
// Is write command?
// ============================================================================
//

STATIC
BOOLEAN
MsdcCommandIsWrite (
  IN MMC_CMD Cmd
  )
{
  switch (MMC_GET_INDX (Cmd)) {

    case 24:
    case 25:
      return TRUE;

    default:
      return FALSE;
  }
}


//
// ============================================================================
// Build MTK SDC_CMD
// ============================================================================
//

STATIC
UINT32
MsdcBuildRawCommand (
  IN MMC_CMD Cmd
  )
{
  UINT32 Opcode;
  UINT32 RawCommand;

  Opcode = MMC_GET_INDX (Cmd);

  RawCommand =
    Opcode |
    (MsdcResponseType (Cmd) << SDC_CMD_RSPTYP_SHIFT);

  //
  // Data command.
  //
  if (MsdcCommandHasData (Cmd)) {

    RawCommand |=
      (MSDC_BLOCK_SIZE << SDC_CMD_BLK_LEN_SHIFT);

    if (Opcode == 18 || Opcode == 25) {

      RawCommand |=
        (2 << SDC_CMD_DTYPE_SHIFT);

    } else {

      RawCommand |=
        (1 << SDC_CMD_DTYPE_SHIFT);
    }

    if (MsdcCommandIsWrite (Cmd)) {
      RawCommand |= SDC_CMD_WR;
    }
  }

  //
  // CMD12.
  //
  if (Opcode == 12) {
    RawCommand |= SDC_CMD_STOP;
  }

  return RawCommand;
}


//
// ============================================================================
// Send command
//
// NO CMD1 emulation.
// NO CMD2 emulation.
// NO fake RCA.
// NO fake CMD13.
//
// MmcDxe owns MMC state.
// MSDC only talks to hardware.
// ============================================================================
//

STATIC
EFI_STATUS
EFIAPI
MsdcSendCommand (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_CMD               Cmd,
  IN UINT32                Argument
  )
{
  UINT32     Opcode;
  UINT32     BlockCount;
  UINT32     RawCommand;
  UINT32     IntStatus;
  EFI_STATUS Status;

  Opcode = MMC_GET_INDX (Cmd);

  DEBUG ((
    DEBUG_INFO,
    "MSDC0: CMD%u ARG=%08x MMC_CMD=%08x\n",
    Opcode,
    Argument,
    Cmd
    ));

  //
  // Clear stale interrupt status.
  //
  MsdcWrite (
    MSDC_INT,
    0xFFFFFFFF
    );

  //
  // Wait for previous command.
  //
  if (!MsdcWaitMask (
        SDC_STS,
        SDC_STS_CMDBUSY,
        0,
        MSDC_TIMEOUT_US
        )) {

    DEBUG ((
      DEBUG_ERROR,
      "MSDC0: SDC_STS CMDBUSY timeout prior to CMD%u\n", // cmd2 is a REAL naughty boy
      Opcode
      ));
// 	Default CMD* commands using R1 or R3.
//  However, CMD2 Uses R2, and sends 136-bit CID.
//  This shit causes buffer overflow, and FIFO fucked up.
//  How? idk -_-
//
    MsdcDumpState ();

    //
    // Clear stale FIFO only if controller is stuck.
    //
    MsdcWrite (
      MSDC_FIFOCS,
      MSDC_FIFOCS_CLR
      );

    //
    // Try one more time.
    //
    if (!MsdcWaitMask (
           SDC_STS,
           SDC_STS_CMDBUSY,
           0,
           MSDC_TIMEOUT_US
           )) {

      return EFI_TIMEOUT;
    }
  }

  //
  // IMPORTANT:
  // Always write SDC_BLK_NUM.
  //
  if (MsdcCommandHasData (Cmd)) {
    BlockCount = 1;
  } else {
    BlockCount = 0;
  }

  MsdcWrite (
    SDC_BLK_NUM,
    BlockCount
    );

  //
  // Clear FIFO before a data transfer.
  //
  if (BlockCount != 0) {
    MsdcWrite (
      MSDC_FIFOCS,
      MSDC_FIFOCS_CLR
      );
  }

  RawCommand = MsdcBuildRawCommand (Cmd);

  mLastCommand = Opcode;

  DEBUG ((
    DEBUG_INFO,
    "MSDC0: CMD%u RAW=%08x BLK=%u CFG=%08x SDC_CFG=%08x\n",
    Opcode,
    RawCommand,
    BlockCount,
    MsdcRead (MSDC_CFG),
    MsdcRead (SDC_CFG)
    ));

  //
  // Argument.
  //
  MsdcWrite (
    SDC_ARG,
    Argument
    );

  //
  // Command.
  //
  MsdcWrite (
    SDC_CMD,
    RawCommand
    );

  //
  // Commands without response.
  //
  if ((Cmd & MMC_CMD_WAIT_RESPONSE) == 0) {

    if (!MsdcWaitMask (
           SDC_STS,
           SDC_STS_CMDBUSY,
           0,
           MSDC_TIMEOUT_US
           )) {

      DEBUG ((
        DEBUG_ERROR,
        "MSDC0: CMD%u no-response timeout\n",
        Opcode
        ));

      MsdcDumpState ();

      return EFI_TIMEOUT;
    }

    return EFI_SUCCESS;
  }

  //
  // Commands with response.
  //
  Status = MsdcWaitInterrupt (
             MSDC_INT_CMDRDY,
             MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR,
             &IntStatus
             );

  if (EFI_ERROR (Status)) {

IntStatus = MsdcRead (MSDC_INT);

if ((IntStatus & (MSDC_INT_CMDTMO |
                  MSDC_INT_RSPCRCERR |
                  MSDC_INT_CMDRDY)) != 0) {

  DEBUG ((
    DEBUG_ERROR,
    "MSDC0: CMD%u INT=%08x STATUS=%r\n",
    Opcode,
    IntStatus,
    (IntStatus & MSDC_INT_CMDTMO) ?
      EFI_TIMEOUT :
      ((IntStatus & MSDC_INT_RSPCRCERR) ?
        EFI_CRC_ERROR : EFI_SUCCESS)
    ));

  DEBUG ((
    DEBUG_ERROR,
    "MSDC0: RESP RAW CMD%u: "
    "R0=%08x R1=%08x R2=%08x R3=%08x\n",
    Opcode,
    MsdcRead (SDC_RESP0),
    MsdcRead (SDC_RESP1),
    MsdcRead (SDC_RESP2),
    MsdcRead (SDC_RESP3)
    ));
}
 

  MsdcDumpState ();

  return Status;
}
  //
  // CMD6 needs a little time for card state transition.
  //
  if (Opcode == 6) {
    MicroSecondDelay (1000);
  }

  return EFI_SUCCESS;
}



STATIC
EFI_STATUS
EFIAPI
MsdcReceiveResponse (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_RESPONSE_TYPE     Type,
  OUT UINT32               *Buffer
  )
{
  UINT32 R0;
  UINT32 R1;
  UINT32 R2;
  UINT32 R3;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  R0 = MsdcRead (SDC_RESP0);
  R1 = MsdcRead (SDC_RESP1);
  R2 = MsdcRead (SDC_RESP2);
  R3 = MsdcRead (SDC_RESP3);

  DEBUG ((
    DEBUG_ERROR,
    "MSDC0: RESP RAW "
    "R0=%08x R1=%08x R2=%08x R3=%08x type=%u cmd=%u\n",
    R0,
    R1,
    R2,
    R3,
    Type,
    mLastCommand
    ));

  if (Type == MMC_RESPONSE_TYPE_R2) {

    //
    // MTK response register order.
    //
    Buffer[0] = R3;
    Buffer[1] = R2;
    Buffer[2] = R1;
    Buffer[3] = R0;

    DEBUG ((
      DEBUG_ERROR,
      "MSDC0: RESP R2 "
      "%08x %08x %08x %08x\n",
      Buffer[0],
      Buffer[1],
      Buffer[2],
      Buffer[3]
      ));

  } else {

    Buffer[0] = R0;
  }

  return EFI_SUCCESS;
}

//
// ============================================================================
// Read block data
// ============================================================================
//

STATIC
EFI_STATUS
EFIAPI
MsdcReadBlockData (
  IN  EFI_MMC_HOST_PROTOCOL *This,
  IN  EFI_LBA               Lba,
  IN  UINTN                 Length,
  OUT UINT32                *Buffer
  )
{
  UINT8      *Bytes;
  UINT32     IntStatus;
  UINTN      Timeout;
  EFI_STATUS Status;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Length == 0) {
    return EFI_SUCCESS;
  }

  Bytes = (UINT8 *)Buffer;

  while (Length > 0) {

    Timeout = MSDC_TIMEOUT_US;

    while (
      ((MsdcRead (MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) == 0) &&
      ((MsdcRead (MSDC_INT) &
        (MSDC_INT_XFER_COMPL |
         MSDC_INT_DATTMO |
         MSDC_INT_DATCRCERR)) == 0)
      ) {

      if (Timeout-- == 0) {
        return EFI_TIMEOUT;
      }

      MicroSecondDelay (1);
    }

    while (
      ((MsdcRead (MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) != 0) &&
      (Length > 0)
      ) {

      if (Length >= sizeof (UINT32)) {

        *(UINT32 *)Bytes = MsdcRead (MSDC_RXDATA);

        Bytes += sizeof (UINT32);
        Length -= sizeof (UINT32);

      } else {

        UINT32 Word;

        Word = MsdcRead (MSDC_RXDATA);

        CopyMem (
          Bytes,
          &Word,
          Length
          );

        Length = 0;
      }
    }
  }

  Status = MsdcWaitInterrupt (
             MSDC_INT_XFER_COMPL,
             MSDC_INT_DATTMO | MSDC_INT_DATCRCERR,
             &IntStatus
             );

  if (EFI_ERROR (Status)) {

    DEBUG ((
      DEBUG_ERROR,
      "MSDC0: CMD%u read failed INT=%08x STATUS=%r\n",
      mLastCommand,
      IntStatus,
      Status
      ));
  }

  return Status;
}


//
// ============================================================================
// Write block data
// ============================================================================
//

STATIC
EFI_STATUS
EFIAPI
MsdcWriteBlockData (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN EFI_LBA               Lba,
  IN UINTN                 Length,
  IN UINT32                *Buffer
  )
{
  UINT8      *Bytes;
  UINT32     IntStatus;
  UINTN      Timeout;
  EFI_STATUS Status;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Length == 0) {
    return EFI_SUCCESS;
  }

  Bytes = (UINT8 *)Buffer;

  while (Length > 0) {

    Timeout = MSDC_TIMEOUT_US;

    while (
      (((MsdcRead (MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16) >= 128) &&
      ((MsdcRead (MSDC_INT) &
        (MSDC_INT_XFER_COMPL |
         MSDC_INT_DATTMO |
         MSDC_INT_DATCRCERR)) == 0)
      ) {

      if (Timeout-- == 0) {
        return EFI_TIMEOUT;
      }

      MicroSecondDelay (1);
    }

    if (Length >= sizeof (UINT32)) {

      MsdcWrite (
        MSDC_TXDATA,
        *(UINT32 *)Bytes
        );

      Bytes += sizeof (UINT32);
      Length -= sizeof (UINT32);

    } else {

      UINT32 Word;

      Word = 0;

      CopyMem (
        &Word,
        Bytes,
        Length
        );

      MsdcWrite (
        MSDC_TXDATA,
        Word
        );

      Length = 0;
    }
  }

  Status = MsdcWaitInterrupt (
             MSDC_INT_XFER_COMPL,
             MSDC_INT_DATTMO | MSDC_INT_DATCRCERR,
             &IntStatus
             );

  if (EFI_ERROR (Status)) {

    DEBUG ((
      DEBUG_ERROR,
      "MSDC0: CMD%u write failed INT=%08x STATUS=%r\n",
      mLastCommand,
      IntStatus,
      Status
      ));
  }

  return Status;
}


//
// ============================================================================
// Card presence
// ============================================================================
//

STATIC
BOOLEAN
EFIAPI
MsdcIsCardPresent (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  //
  // eMMC is soldered down.
  //
  return TRUE;
}


//
// ============================================================================
// Read-only
// ============================================================================
//

STATIC
BOOLEAN
EFIAPI
MsdcIsReadOnly (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  return FALSE;
}


//
// ============================================================================
// Device path
// ============================================================================
//

STATIC
EFI_STATUS
EFIAPI
MsdcBuildDevicePath (
  IN  EFI_MMC_HOST_PROTOCOL    *This,
  OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath
  )
{
  VENDOR_DEVICE_PATH *Node;

  if (DevicePath == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Node = (VENDOR_DEVICE_PATH *)CreateDeviceNode (
                                 HARDWARE_DEVICE_PATH,
                                 HW_VENDOR_DP,
                                 sizeof (VENDOR_DEVICE_PATH)
                                 );

  if (Node == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyGuid (
    &Node->Guid,
    &mMsdc0DevicePathGuid
    );

  *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)Node;

  return EFI_SUCCESS;
}


//
// ============================================================================
// NotifyState
//
// IMPORTANT:
// 1)Do not reset MSDC here.
// 2)Do not touch clock.
// 3)Do not touch SDC_CFG.
// 4)Do not touch MSDC_CFG.
// 5)Do not talk about Fight Club. 
// 6)DO NOT talk about Fight Club.
// 
//
// LK/preloader handoff is preserved.
// ============================================================================
//

STATIC
EFI_STATUS
EFIAPI
MsdcNotifyState (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_STATE             State
  )
{
  if (State == MmcHwInitializationState) {

    UINT32 Cfg;

    //
    // CMD0 (issued right after this state) forces the card back to
    // Idle State / open-drain identification mode, regardless of
    // whatever high-speed configuration LK left the bus in.  The
    // host controller must match that or CRC-checked / timed
    // responses (CMD2, CMD13, ...) will simply never come back.
    //

    MsdcApplyClock (0);   // 0 => identification clock

    Cfg  = MsdcRead (SDC_CFG);
    Cfg &= ~SDC_CFG_BUSWIDTH_MASK;   // back to 1-bit for identification
    MsdcWrite (SDC_CFG, Cfg);

    DEBUG ((DEBUG_INFO,
      "MSDC0: forced identification mode CFG=%08x SDC_CFG=%08x\n",
      MsdcRead (MSDC_CFG), MsdcRead (SDC_CFG)));
  }

  return EFI_SUCCESS;
}




STATIC
EFI_STATUS
EFIAPI
MsdcSetIos (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN UINT32                BusClockFreq,
  IN UINT32                BusWidth,
  IN UINT32                TimingMode
  )
{
  UINT32 Cfg;

  DEBUG ((DEBUG_INFO, "MSDC0: SetIos freq=%u width=%u timing=%u\n",
    BusClockFreq, BusWidth, TimingMode));

  Cfg = MsdcRead (SDC_CFG);
  Cfg &= ~SDC_CFG_BUSWIDTH_MASK;

  switch (BusWidth) {
    case 1: break;
    case 4: Cfg |= (1U << 16); break;
    case 8: Cfg |= (2U << 16); break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  MsdcWrite (SDC_CFG, Cfg);

  if (BusClockFreq > 26000000U) {
  DEBUG ((DEBUG_ERROR, "MSDC0: SetIos freq=%u clamp to 26MHz (HS200/400 not implemented)\n", BusClockFreq));
  BusClockFreq = 26000000U; // purr~
}

  return MsdcApplyClock (BusClockFreq);
}


//
// ============================================================================
// Multiblock
//
// FALSE deliberately.
// This makes stock MmcDxe BlockIo prefer CMD17/CMD24.
// ============================================================================
//

STATIC
BOOLEAN
EFIAPI
MsdcIsMultiBlock (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  return FALSE;
}


//
// ============================================================================
// EFI_MMC_HOST_PROTOCOL
// ============================================================================
//

STATIC EFI_MMC_HOST_PROTOCOL mMsdcHost = {
  MMC_HOST_PROTOCOL_REVISION,

  MsdcIsCardPresent,
  MsdcIsReadOnly,
  MsdcBuildDevicePath,

  MsdcNotifyState,

  MsdcSendCommand,
  MsdcReceiveResponse,

  MsdcReadBlockData,
  MsdcWriteBlockData,

  MsdcSetIos,
  MsdcIsMultiBlock
};


// 
// ============================================================================
// Entry point
// ============================================================================
//

EFI_STATUS
EFIAPI
Msdc0ProbeEntry (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;

  //
  // FIRST LOG.
  // If this does not appear, the driver itself is not dispatched.
  //
  DEBUG ((
    DEBUG_ERROR,
    "MSDC0: >>> ENTRY <<<\n"
    ));

  //
  // This is the PCD form already used by your original driver.
  //
  mMsdcBase = (UINTN)FixedPcdGet64 (
                         PcdMsdc0Base
                         );

  DEBUG ((
    DEBUG_ERROR,
    "MSDC0: BASE=%lx\n",
    (UINT64)mMsdcBase
    ));

  if (mMsdcBase == 0) {

    DEBUG ((
      DEBUG_ERROR,
      "MSDC0: BASE IS ZERO\n"
      ));

    return EFI_NOT_FOUND;
  }

  //
  // Read-only diagnostic.
  // No controller initialization.
  //
  DEBUG ((
    DEBUG_ERROR,
    "MSDC0: BEFORE INSTALL "
    "CFG=%08x "
    "SDC_CFG=%08x "
    "INT=%08x "
    "BLK=%08x\n",
    MsdcRead (MSDC_CFG),
    MsdcRead (SDC_CFG),
    MsdcRead (MSDC_INT),
    MsdcRead (SDC_BLK_NUM)
    ));

  //
  // Install ONLY the MMC host protocol.
  //
  // MmcDxe will consume this protocol.
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEmbeddedMmcHostProtocolGuid,
                  &mMsdcHost,
                  NULL
                  );

  if (EFI_ERROR (Status)) {

    DEBUG ((
      DEBUG_ERROR,
      "MSDC0: failed to install MMC host protocol: %r\n",
      Status
      ));

    return Status;
  }

  DEBUG ((
    DEBUG_ERROR,
    "MSDC0: >>> HOST READY <<<\n"
    ));

  DEBUG ((
    DEBUG_ERROR,
    "MSDC0: AFTER INSTALL "
    "CFG=%08x "
    "SDC_CFG=%08x "
    "INT=%08x "
    "BLK=%08x\n",
    MsdcRead (MSDC_CFG),
    MsdcRead (SDC_CFG),
    MsdcRead (MSDC_INT),
    MsdcRead (SDC_BLK_NUM)
    ));

  return EFI_SUCCESS;
}



// WAIT!!!
// 1300+ CODE LINES?!! @.@
