/*
Msdc0ProbeDxe.c
modified
Peace of log:
InitializeMmcDevice(): Error in Identification Mode. Status=Device Error
MmcTransferBlock(MMC_CMD65553): Error  Time Out
MmcIoBlocks(): Failed to transfer block and Status: Time Out
MmcIoBlocks(): Failed to transfer block and Status: Device Error

purr~
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


// reg map

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

#define MSDC_IDENT_CLK_DIV    17U   


// fifo


#define MSDC_FIFOCS_RXCNT     0x000000FF
#define MSDC_FIFOCS_TXCNT     0x00FF0000
#define MSDC_FIFOCS_CLR       BIT31


// ints

#define MSDC_INT_CMDRDY       BIT8
#define MSDC_INT_CMDTMO       BIT9
#define MSDC_INT_RSPCRCERR    BIT10
#define MSDC_INT_XFER_COMPL   BIT12
#define MSDC_INT_DATTMO       BIT14
#define MSDC_INT_DATCRCERR    BIT15


// sdc

#define SDC_STS_CMDBUSY       BIT1


// sdc_cmd

#define SDC_CMD_CMD_MASK      0x0000003F
#define SDC_CMD_RSPTYP_MASK   0x00000380
#define SDC_CMD_RSPTYP_SHIFT  7
#define SDC_CMD_DTYPE_MASK    0x00001800
#define SDC_CMD_DTYPE_SHIFT   11
#define SDC_CMD_WR            BIT13
#define SDC_CMD_STOP          BIT14
#define SDC_CMD_BLK_LEN_MASK  0x0FFF0000
#define SDC_CMD_BLK_LEN_SHIFT 16


// consts

#define MSDC_BLOCK_SIZE       512
#define MSDC_TIMEOUT_US       1000000


// guid

STATIC EFI_GUID mMsdc0DevicePathGuid = EFI_CALLER_ID_GUID;


// state


STATIC UINTN  mMsdcBase;
STATIC UINT32 mLastCommand;
STATIC UINT32  mBlockLength = 512;
STATIC BOOLEAN mEmmcReady = FALSE;
STATIC UINT32  mEmmcCid[4] = {0};
STATIC UINT32  mEmmcCsd[4] = {0};


// mmio


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


// wait for reg cond.


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


// wait for int

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


// debug

/*STATIC
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
}*/


// resp encoding

STATIC
UINT32
MsdcResponseType (
  IN MMC_CMD Cmd
  )
{
  UINT32 Opcode = MMC_GET_INDX (Cmd);

  if ((Cmd & MMC_CMD_WAIT_RESPONSE) == 0) return 0;
  if ((Cmd & MMC_CMD_LONG_RESPONSE) != 0) return 2;
  if ((Cmd & MMC_CMD_NO_CRC_RESPONSE) != 0) return 3;

  if (Opcode == 6 || Opcode == 7 || Opcode == 12 || Opcode == 28 || Opcode == 29) {
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
    if (Div == 0) Div = 1;
    if (Div > 255U) Div = 255U;
  }

  Cfg |= (Div << 8);
  MsdcWrite (MSDC_CFG, Cfg);

  if (!MsdcWaitMask (MSDC_CFG, MSDC_CFG_CKSTB, MSDC_CFG_CKSTB, MSDC_TIMEOUT_US)) {
    return EFI_TIMEOUT;
  }
  return EFI_SUCCESS;
}


// comand has data?

STATIC
BOOLEAN
MsdcCommandHasData (
  IN MMC_CMD Cmd
  )
{
  switch (MMC_GET_INDX (Cmd)) {
    // CMD14 and CMD19 added
    // bus width error without them
    case 8: case 11: case 14: case 17: case 18: case 19: case 21:
    case 24: case 25: case 30: case 51:
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC
BOOLEAN
MsdcCommandIsWrite (
  IN MMC_CMD Cmd
  )
{
  switch (MMC_GET_INDX (Cmd)) {
    case 19: case 20: case 24: case 25: case 27:
      return TRUE;
    default:
      return FALSE;
  }
}


// build sdc_cmd
STATIC
UINT32
MsdcBuildRawCommand (
  IN MMC_CMD Cmd
  )
{
  UINT32 Opcode = MMC_GET_INDX (Cmd);
  UINT32 RawCommand = Opcode | (MsdcResponseType (Cmd) << SDC_CMD_RSPTYP_SHIFT);

  if (MsdcCommandHasData (Cmd)) {
    UINT32 BlkLen = mBlockLength;
    if (Opcode == 14 || Opcode == 19) {
      UINT32 BusWidthCfg = (MsdcRead (SDC_CFG) & SDC_CFG_BUSWIDTH_MASK) >> 16;
      if (BusWidthCfg == 1) BlkLen = 4;      // 4-bit mode
      else if (BusWidthCfg == 2) BlkLen = 8; // 8-bit mode
      else BlkLen = 1;                       // 1-bit mode
    }

    RawCommand |= (BlkLen << SDC_CMD_BLK_LEN_SHIFT);
    
    if (Opcode == 18 || Opcode == 25) {
      RawCommand |= (2 << SDC_CMD_DTYPE_SHIFT);
    } else {
      RawCommand |= (1 << SDC_CMD_DTYPE_SHIFT);
    }
    if (MsdcCommandIsWrite (Cmd)) {
      RawCommand |= SDC_CMD_WR;
    }
  }

  if (Opcode == 12) {
    RawCommand |= SDC_CMD_STOP;
  }
  return RawCommand;
}


// hw emmc init
STATIC
EFI_STATUS
MsdcForceEmmcInit (VOID)
{
  UINT32 IntStatus, R0;
  UINT32 Timeout = 1000;


  DEBUG ((DEBUG_ERROR, "MSDC0: Hard resetting controller IP...\n"));

  // controller rst
  MsdcWrite (MSDC_CFG, MsdcRead (MSDC_CFG) | BIT2);
  while ((MsdcRead (MSDC_CFG) & BIT2) != 0) {
    MicroSecondDelay (10);
  }
  MsdcWrite (MSDC_PAD_TUNE, 0);
  MsdcWrite (MSDC_PAD_TUNE0, 0);
  MsdcWrite (MSDC_NEW_RX_CFG, 0);
  // MsdcWrite (MSDC_IOCON, 0);
  // buffer cleanup
  MsdcWrite (MSDC_FIFOCS, MSDC_FIFOCS_CLR);
  MsdcWrite (MSDC_INT, 0xFFFFFFFF);

  MsdcApplyClock (400000);
  MsdcWrite (SDC_CFG, MsdcRead (SDC_CFG) & ~SDC_CFG_BUSWIDTH_MASK);
  MicroSecondDelay (10000);
  #define SAFE_CMD(Arg, CmdRaw) \
    MsdcWaitMask (SDC_STS, SDC_STS_CMDBUSY, 0, MSDC_TIMEOUT_US); \
    MsdcWrite (MSDC_INT, 0xFFFFFFFF); \
    MsdcWrite (SDC_ARG, Arg); \
    MsdcWrite (SDC_CMD, CmdRaw);

  // double warm reset
  SAFE_CMD(0, 0 | (0 << SDC_CMD_RSPTYP_SHIFT));
  MicroSecondDelay (2000);
  SAFE_CMD(0, 0 | (0 << SDC_CMD_RSPTYP_SHIFT));
  MicroSecondDelay (2000);

  // CMD1
  while (Timeout-- > 0) {
    SAFE_CMD(0x40FF8080, 1 | (3 << SDC_CMD_RSPTYP_SHIFT)); // R3
    MsdcWaitInterrupt (MSDC_INT_CMDRDY, MSDC_INT_CMDTMO, &IntStatus);
    R0 = MsdcRead (SDC_RESP0);
    if (R0 & BIT31) break; 
    MicroSecondDelay (1000);
  }

  // get cid
  SAFE_CMD(0, 2 | (2 << SDC_CMD_RSPTYP_SHIFT)); // R2
  MsdcWaitInterrupt (MSDC_INT_CMDRDY, MSDC_INT_CMDTMO, &IntStatus);
  mEmmcCid[0] = MsdcRead (SDC_RESP3);
  mEmmcCid[1] = MsdcRead (SDC_RESP2);
  mEmmcCid[2] = MsdcRead (SDC_RESP1);
  mEmmcCid[3] = MsdcRead (SDC_RESP0);

  // rca =1
  SAFE_CMD(0x00010000, 3 | (1 << SDC_CMD_RSPTYP_SHIFT)); // R1
  MsdcWaitInterrupt (MSDC_INT_CMDRDY, MSDC_INT_CMDTMO, &IntStatus);

  // csd read
  SAFE_CMD(0x00010000, 9 | (2 << SDC_CMD_RSPTYP_SHIFT)); // R2
  MsdcWaitInterrupt (MSDC_INT_CMDRDY, MSDC_INT_CMDTMO, &IntStatus);
  mEmmcCsd[0] = MsdcRead (SDC_RESP3);
  mEmmcCsd[1] = MsdcRead (SDC_RESP2);
  mEmmcCsd[2] = MsdcRead (SDC_RESP1);
  mEmmcCsd[3] = MsdcRead (SDC_RESP0);

  // TRAN
  SAFE_CMD(0x00010000, 7 | (7 << SDC_CMD_RSPTYP_SHIFT)); // R1b
  MsdcWaitInterrupt (MSDC_INT_CMDRDY, MSDC_INT_CMDTMO, &IntStatus);
  
  MsdcWaitMask (SDC_STS, SDC_STS_CMDBUSY, 0, MSDC_TIMEOUT_US);

  #undef SAFE_CMD

  mEmmcReady = TRUE;
  DEBUG ((DEBUG_ERROR, "MSDC0: Hardware eMMC init complete by ProbeDxe! UwU\n"));
  return EFI_SUCCESS;
}


// send cmd
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
  UINT32     ErrorMask;
  EFI_STATUS Status;

  Opcode = MMC_GET_INDX (Cmd);
  mLastCommand = Opcode;

  if (Opcode == 16) {
    mBlockLength = Argument;
  }

  if (mEmmcReady) {
    // sd cmds block
    if (Opcode == 5 || Opcode == 51 || Opcode == 52 || Opcode == 53 || Opcode == 55 || Opcode == 41) {
      return EFI_TIMEOUT;
    }
    if (Opcode == 8 && Argument != 0) {
      return EFI_TIMEOUT;
    }

    // bus test block
    if (Opcode == 14 || Opcode == 19) {
      return EFI_UNSUPPORTED;
    }

    // fake resp
    switch (Opcode) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 9:
      case 7:
        return EFI_SUCCESS; 
    }
  }

  // bus was stuck? fuck it
  if (!MsdcWaitMask (SDC_STS, SDC_STS_CMDBUSY | SDC_STS_CMDBUSY, 0, MSDC_TIMEOUT_US)) {
    MsdcWrite (MSDC_CFG, MsdcRead (MSDC_CFG) | BIT2);
    while ((MsdcRead (MSDC_CFG) & BIT2) != 0) { MicroSecondDelay(10); }
    MsdcApplyClock (400000);
  }

  MsdcWrite (MSDC_INT, 0xFFFFFFFF);

  if (MsdcCommandHasData (Cmd)) {
    BlockCount = 1;
  } else {
    BlockCount = 0;
  }

  MsdcWrite (SDC_BLK_NUM, BlockCount);
  MsdcWrite (MSDC_FIFOCS, MSDC_FIFOCS_CLR);

  RawCommand = MsdcBuildRawCommand (Cmd);

  MsdcWrite (SDC_ARG, Argument);
  MsdcWrite (SDC_CMD, RawCommand);

  if ((Cmd & MMC_CMD_WAIT_RESPONSE) == 0) {
    MsdcWaitMask (SDC_STS, SDC_STS_CMDBUSY, 0, MSDC_TIMEOUT_US);
    return EFI_SUCCESS;
  }

  // MSDC_INT_RSPCRCERR remove
  ErrorMask = MSDC_INT_CMDTMO; 

  Status = MsdcWaitInterrupt (MSDC_INT_CMDRDY, ErrorMask, &IntStatus);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Opcode == 6 || Opcode == 7 || Opcode == 28 || Opcode == 29) {
    MsdcWaitMask (SDC_STS, SDC_STS_CMDBUSY, 0, MSDC_TIMEOUT_US * 2);
  }

  return EFI_SUCCESS;
}
// recv resp
STATIC
EFI_STATUS
EFIAPI
MsdcReceiveResponse (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_RESPONSE_TYPE     Type,
  OUT UINT32               *Buffer
  )
{
  UINT32 R0, R1, R2, R3;

  if (Buffer == NULL) return EFI_INVALID_PARAMETER;

  if (mEmmcReady) {
    switch (mLastCommand) {
      case 1:
        Buffer[0] = 0xC0FF8080;
        return EFI_SUCCESS;
      case 2:
        Buffer[0] = mEmmcCid[0]; Buffer[1] = mEmmcCid[1];
        Buffer[2] = mEmmcCid[2]; Buffer[3] = mEmmcCid[3];
        return EFI_SUCCESS;
      case 3:
        Buffer[0] = 0x00010000;
        return EFI_SUCCESS;
      case 9:
        Buffer[0] = mEmmcCsd[0]; Buffer[1] = mEmmcCsd[1];
        Buffer[2] = mEmmcCsd[2]; Buffer[3] = mEmmcCsd[3];
        return EFI_SUCCESS;
      case 7:
        Buffer[0] = (4 << 9); 
        return EFI_SUCCESS;
      case 0:
        return EFI_SUCCESS;
    }
  }

  R0 = MsdcRead (SDC_RESP0);
  R1 = MsdcRead (SDC_RESP1);
  R2 = MsdcRead (SDC_RESP2);
  R3 = MsdcRead (SDC_RESP3);

  if (Type == MMC_RESPONSE_TYPE_R2) {
    Buffer[0] = R3;
    Buffer[1] = R2;
    Buffer[2] = R1;
    Buffer[3] = R0;
  } else {
    Buffer[0] = R0;
  }

  return EFI_SUCCESS;
}


// r/w?...
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

  if (Buffer == NULL) return EFI_INVALID_PARAMETER;
  if (Length == 0) return EFI_SUCCESS;

  Bytes = (UINT8 *)Buffer;

  while (Length > 0) {
    Timeout = MSDC_TIMEOUT_US;
    while (((MsdcRead (MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) == 0) &&
           ((MsdcRead (MSDC_INT) & (MSDC_INT_XFER_COMPL | MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) == 0)) {
      if (Timeout-- == 0) return EFI_TIMEOUT;
      MicroSecondDelay (1);
    }

    while (((MsdcRead (MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) != 0) && (Length > 0)) {
      if (Length >= sizeof (UINT32)) {
        *(UINT32 *)Bytes = MsdcRead (MSDC_RXDATA);
        Bytes += sizeof (UINT32);
        Length -= sizeof (UINT32);
      } else {
        UINT32 Word = MsdcRead (MSDC_RXDATA);
        CopyMem (Bytes, &Word, Length);
        Length = 0;
      }
    }
  }

  Status = MsdcWaitInterrupt (MSDC_INT_XFER_COMPL, MSDC_INT_DATTMO | MSDC_INT_DATCRCERR, &IntStatus);
  return Status;
}

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

  if (Buffer == NULL) return EFI_INVALID_PARAMETER;
  if (Length == 0) return EFI_SUCCESS;

  Bytes = (UINT8 *)Buffer;

  while (Length > 0) {
    Timeout = MSDC_TIMEOUT_US;
    while ((((MsdcRead (MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16) >= 128) &&
           ((MsdcRead (MSDC_INT) & (MSDC_INT_XFER_COMPL | MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) == 0)) {
      if (Timeout-- == 0) return EFI_TIMEOUT;
      MicroSecondDelay (1);
    }

    if (Length >= sizeof (UINT32)) {
      MsdcWrite (MSDC_TXDATA, *(UINT32 *)Bytes);
      Bytes += sizeof (UINT32);
      Length -= sizeof (UINT32);
    } else {
      UINT32 Word = 0;
      CopyMem (&Word, Bytes, Length);
      MsdcWrite (MSDC_TXDATA, Word);
      Length = 0;
    }
  }

  Status = MsdcWaitInterrupt (MSDC_INT_XFER_COMPL, MSDC_INT_DATTMO | MSDC_INT_DATCRCERR, &IntStatus);
  return Status;
}


// r/o
STATIC
BOOLEAN
EFIAPI
MsdcIsCardPresent (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  return TRUE;
}

STATIC
BOOLEAN
EFIAPI
MsdcIsReadOnly (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  return FALSE;
}


// device path
STATIC
EFI_STATUS
EFIAPI
MsdcBuildDevicePath (
  IN  EFI_MMC_HOST_PROTOCOL    *This,
  OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath
  )
{
  VENDOR_DEVICE_PATH *Node;

  if (DevicePath == NULL) return EFI_INVALID_PARAMETER;

  Node = (VENDOR_DEVICE_PATH *)CreateDeviceNode (HARDWARE_DEVICE_PATH, HW_VENDOR_DP, sizeof (VENDOR_DEVICE_PATH));
  if (Node == NULL) return EFI_OUT_OF_RESOURCES;

  CopyGuid (&Node->Guid, &mMsdc0DevicePathGuid);
  *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)Node;

  return EFI_SUCCESS;
}


// notify state
STATIC
EFI_STATUS
EFIAPI
MsdcNotifyState (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_STATE             State
  )
{
  // make MmcDxe to not reset emmc
  return EFI_SUCCESS;
}


// setios
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

  Cfg = MsdcRead (SDC_CFG);
  Cfg &= ~SDC_CFG_BUSWIDTH_MASK;

  switch (BusWidth) {
    case 1: break;
    case 4: Cfg |= (1U << 16); break;
    case 8: Cfg |= (2U << 16); break;
    default: return EFI_INVALID_PARAMETER;
  }

  MsdcWrite (SDC_CFG, Cfg);

  if (BusClockFreq > 26000000U) {
    BusClockFreq = 26000000U;
  }

  return MsdcApplyClock (BusClockFreq);
}


STATIC
BOOLEAN
EFIAPI
MsdcIsMultiBlock (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  return FALSE;
}


//efi_emmc_host_protocol
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


// entry
EFI_STATUS
EFIAPI
Msdc0ProbeEntry (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  DEBUG ((DEBUG_ERROR, "MSDC0: >>> ENTRY >=D <<<\n"));
  mMsdcBase = (UINTN)FixedPcdGet64 (PcdMsdc0Base);
  DEBUG ((DEBUG_ERROR, "MSDC0: BASE=%lx\n", (UINT64)mMsdcBase));
  if (mMsdcBase == 0) {
    DEBUG ((DEBUG_ERROR, "MSDC0: BASE IS ZERO\n"));
    return EFI_NOT_FOUND;
  }
  // hard init, bypass MmcDxe
  MsdcForceEmmcInit ();

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEmbeddedMmcHostProtocolGuid,
                  &mMsdcHost,
                  NULL
                  );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MSDC0: failed to install MMC host protocol: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_ERROR, "MSDC0: >>> HOST READY <<<\n"));

  return EFI_SUCCESS;
}
