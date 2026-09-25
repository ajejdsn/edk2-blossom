/*
 * Msdc0ProbeDxe.c
 *
 * https://github.com/ajejdsn/edk2-blossom
 *
 * https://github.com/ajejdsn/edk2-blossom/blob/master/MT6765Pkg/Drivers/Msdc0ProbeDxe/Msdc0ProbeDxe.c
 *
 * this shit almost works
 * ima newbie >.<
 * 
 */

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/MmcHost.h>

#define MSDC0_BASE              0x11230000U

#define MSDC_CFG                0x00
#define MSDC_IOCON              0x04
#define MSDC_PS                 0x08
#define MSDC_INT                0x0C
#define MSDC_INTEN              0x10
#define MSDC_FIFOCS             0x14
#define MSDC_TXDATA             0x18
#define MSDC_RXDATA             0x1C

#define SDC_CFG                 0x30
#define SDC_CMD                 0x34
#define SDC_ARG                 0x38
#define SDC_STS                 0x3C
#define SDC_RESP0               0x40
#define SDC_RESP1               0x44
#define SDC_RESP2               0x48
#define SDC_RESP3               0x4C
#define SDC_BLK_NUM             0x50

#define MSDC_PAD_TUNE0          0xF0
#define MSDC_PATCH_BIT0         0xB0
#define MSDC_PATCH_BIT1         0xB4
#define MSDC_PATCH_BIT2         0xB8

#define MSDC_CFG_MODE           BIT0
#define MSDC_CFG_CKPDN          BIT1
#define MSDC_CFG_RST            BIT2
#define MSDC_CFG_PIO            BIT3
#define MSDC_CFG_CKSTB          BIT7
#define MSDC_CFG_CKDIV          0x000FFF00U
#define MSDC_CFG_CKMOD          0x00300000U
#define MSDC_CFG_CKMOD_SHIFT    20
#define MSDC_CFG_SCLK_STOP_DDR  BIT25

#define MSDC_IOCON_DDR50CKD     BIT4

#define MSDC_INT_CMDRDY         BIT8
#define MSDC_INT_CMDTMO         BIT9
#define MSDC_INT_RSPCRCERR      BIT10
#define MSDC_INT_XFER_COMPL     BIT12
#define MSDC_INT_DATTMO         BIT14
#define MSDC_INT_DATCRCERR      BIT15

#define MSDC_FIFOCS_CLR         BIT31
#define MSDC_FIFOCS_RXCNT       0x000000FFU
#define MSDC_FIFOCS_TXCNT       0x00FF0000U

#define SDC_CFG_BUSWIDTH        0x00030000U
#define SDC_CFG_SDIO            BIT19
#define SDC_CFG_SDIOIDE         BIT20
#define SDC_CFG_DTOC            0xFF000000U

#define SDC_CMD_OPC             0x0000003FU
#define SDC_CMD_RSPTYP          0x00000380U
#define SDC_CMD_DTYPE           0x00001800U
#define SDC_CMD_DTYPE_SINGLE    BIT11
#define SDC_CMD_DTYPE_MULTI     BIT12
#define SDC_CMD_WR              BIT13
#define SDC_CMD_STOP            BIT14
#define SDC_CMD_BLKLEN          0x0FFF0000U

#define SDC_STS_SDCBUSY         BIT0
#define SDC_STS_CMDBUSY         BIT1

// comboA MT6765 vendor defaults used by msdc_init_tune_setting()
#define MSDC_PB0_DEFAULT_VAL        0x403C0007U
#define MSDC_PB1_DEFAULT_VAL        0xFFFA0349U
#define MSDC_PB1_DDR_CMD_FIX_SEL    BIT14
#define MSDC_PB2_RESPWAITCNT        (0x3U << 2)
#define MSDC_PB2_RESPSTENSEL        (0x7U << 16)
#define MSDC_PB2_DDR50SEL           BIT19
#define MSDC_PB2_CRCSTSENSEL        (0x7U << 29)

#define MSDC_BLOCK_SIZE         512U
#define MSDC_FIFO_SIZE          128U
#define MSDC_CMD_TIMEOUT_US     1000000U
#define MSDC_DATA_TIMEOUT_US    5000000U

// Set to 1 only for a diagnostic boot; normal operation keeps DDR enabled
#define MSDC_FORCE_SDR_FALLBACK 1

// comboA  MSDC0 uses the 400MHz MSDCPLL parent
#define MSDC0_SOURCE_HZ         400000000U

#define MSDC_DATA_INT_MASK      (MSDC_INT_XFER_COMPL | MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)
#define MSDC_CMD_INT_MASK       (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)

STATIC EFI_MMC_HOST_PROTOCOL  mMsdcHost;
STATIC UINT32                 mLastCommand;
STATIC UINT32                 mCommandRetryDepth;

STATIC
UINT32
MsdcRead (
  IN UINTN Offset
  )
{
  return MmioRead32 (MSDC0_BASE + Offset);
}

STATIC
VOID
MsdcWrite (
  IN UINTN  Offset,
  IN UINT32 Value
  )
{
  MmioWrite32 (MSDC0_BASE + Offset, Value);
}

STATIC
VOID
MsdcW1c (
  IN UINT32 Mask
  )
{
  MmioWrite32 (MSDC0_BASE + MSDC_INT, Mask);
}

STATIC
VOID
MsdcDebugState (
  IN CONST CHAR8 *Tag
  )
{
  DEBUG ((
    DEBUG_INFO,
    "MSDC0 %a: CFG=%08x IOCON=%08x PS=%08x INT=%08x INTEN=%08x FIFO=%08x SDC_CFG=%08x SDC_STS=%08x PB0=%08x PB1=%08x PB2=%08x CMD=%08x ARG=%08x\n",
    Tag,
    MsdcRead (MSDC_CFG),
    MsdcRead (MSDC_IOCON),
    MsdcRead (MSDC_PS),
    MsdcRead (MSDC_INT),
    MsdcRead (MSDC_INTEN),
    MsdcRead (MSDC_FIFOCS),
    MsdcRead (SDC_CFG),
    MsdcRead (SDC_STS),
    MsdcRead (MSDC_PATCH_BIT0),
    MsdcRead (MSDC_PATCH_BIT1),
    MsdcRead (MSDC_PATCH_BIT2),
    MsdcRead (SDC_CMD),
    MsdcRead (SDC_ARG)
    ));
}

STATIC
EFI_STATUS
MsdcWaitMask (
  IN UINTN   Offset,
  IN UINT32  Mask,
  IN BOOLEAN Set,
  IN UINT32  TimeoutUs
  )
{
  if (TimeoutUs == 0) {
    TimeoutUs = 1;
  }

  while (TimeoutUs-- != 0) {
    UINT32 Value;

    Value = MsdcRead (Offset);
    if (Set) {
      if ((Value & Mask) == Mask) {
        return EFI_SUCCESS;
      }
    } else if ((Value & Mask) == 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
MsdcResetController (
  VOID
  )
{
  UINT32 Cfg;
  UINT32 SdcCfg;
  UINT32 Int;
  BOOLEAN ClockEnabled;
  EFI_STATUS Status;

  // keep the neg bus width across a hostonly reset
  Cfg    = MsdcRead (MSDC_CFG);
  SdcCfg = MsdcRead (SDC_CFG);
  ClockEnabled = (BOOLEAN)((Cfg & MSDC_CFG_CKPDN) != 0);

  Cfg |= MSDC_CFG_MODE | MSDC_CFG_PIO | MSDC_CFG_RST;
  MsdcWrite (MSDC_CFG, Cfg);
  Status = MsdcWaitMask (MSDC_CFG, MSDC_CFG_RST, FALSE, 10000);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Cfg = MsdcRead (MSDC_CFG);
  Cfg |= MSDC_CFG_MODE | MSDC_CFG_PIO;
  if (ClockEnabled) {
    Cfg |= MSDC_CFG_CKPDN;
  } else {
    Cfg &= ~MSDC_CFG_CKPDN;
  }
  MsdcWrite (MSDC_CFG, Cfg);

  MsdcWrite (MSDC_FIFOCS, MSDC_FIFOCS_CLR);
  Status = MsdcWaitMask (MSDC_FIFOCS, MSDC_FIFOCS_CLR, FALSE, 10000);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MsdcWrite (SDC_CFG, SdcCfg);
  MsdcWrite (MSDC_INTEN, MSDC_CMD_INT_MASK | MSDC_DATA_INT_MASK);

  Int = MsdcRead (MSDC_INT);
  if (Int != 0) {
    MsdcW1c (Int);
  }

  return EFI_SUCCESS;
}

STATIC
VOID
MsdcConfigureKnownGoodState (
  VOID
  )
{
  UINT32 Cfg;
  UINT32 SdcCfg;
  UINT32 Iocon;

  Cfg = MsdcRead (MSDC_CFG);
  Cfg |= MSDC_CFG_MODE | MSDC_CFG_PIO;
  Cfg &= ~MSDC_CFG_CKPDN;
  MsdcWrite (MSDC_CFG, Cfg);

  SdcCfg = MsdcRead (SDC_CFG);
  SdcCfg &= ~(SDC_CFG_BUSWIDTH | SDC_CFG_SDIO | SDC_CFG_SDIOIDE | SDC_CFG_DTOC);
  SdcCfg |= (9U << 24); // 1 bit emmc
  MsdcWrite (SDC_CFG, SdcCfg);

  // keep only ComboA DDR50 clock disable bit, as the vendor driver does
  Iocon = MsdcRead (MSDC_IOCON) & MSDC_IOCON_DDR50CKD;
  MsdcWrite (MSDC_IOCON, Iocon);
  MsdcWrite (MSDC_PAD_TUNE0, 0);

  // enable status srcs; this driver still consumes them by polling
  MsdcWrite (MSDC_INTEN, MSDC_CMD_INT_MASK | MSDC_DATA_INT_MASK);
}

STATIC
VOID
MsdcConfigureDdrTuning (
  VOID
  )
{
  UINT32 Pb2;

  // match ComboAs default DDR50 tuning before the first DDR command
  MsdcWrite (MSDC_PAD_TUNE0, 0);
  MsdcWrite (MSDC_PATCH_BIT0, MSDC_PB0_DEFAULT_VAL);
  MsdcWrite (MSDC_PATCH_BIT1, MSDC_PB1_DEFAULT_VAL | MSDC_PB1_DDR_CMD_FIX_SEL);

  Pb2 = MsdcRead (MSDC_PATCH_BIT2);
  Pb2 &= ~(MSDC_PB2_RESPWAITCNT | MSDC_PB2_RESPSTENSEL | MSDC_PB2_CRCSTSENSEL);
  Pb2 |= (3U << 2) | (1U << 16) | (1U << 29) | MSDC_PB2_DDR50SEL;
  MsdcWrite (MSDC_PATCH_BIT2, Pb2);
}

STATIC
EFI_STATUS
MsdcApplyClock (
  IN UINT32  RequestedHz,
  IN BOOLEAN Ddr
  )
{
  UINT32 Div;
  UINT32 Mode;
  UINT32 Cfg;
  UINT32 SafeDiv;
  EFI_STATUS Status;

  if (RequestedHz == 0) {
    return EFI_INVALID_PARAMETER;
  }

  if (Ddr) {
    Mode = 2;
    if (RequestedHz >= (MSDC0_SOURCE_HZ / 4U)) {
      Div = 0;
    } else {
      Div = (UINT32)(((UINT64)MSDC0_SOURCE_HZ + ((UINT64)RequestedHz * 4U) - 1U) /
                     ((UINT64)RequestedHz * 4U));
      Div >>= 1;
      if (Div == 0) {
        Div = 1;
      }
    }
  } else if (RequestedHz >= MSDC0_SOURCE_HZ) {
    Mode = 1; // no div
    Div  = 0;
  } else if (RequestedHz >= (MSDC0_SOURCE_HZ / 2U)) {
    Mode = 0;
    Div  = 0; /* src / 2 */
  } else {
    Mode = 0;
    Div  = (UINT32)(((UINT64)MSDC0_SOURCE_HZ + ((UINT64)RequestedHz * 4U) - 1U) /
                    ((UINT64)RequestedHz * 4U));
    if (Div == 0) {
      Div = 1;
    }
    if (Div > 0xFFFU) {
      Div = 0xFFFU;
    }
  }

  Cfg = MsdcRead (MSDC_CFG);
  Cfg &= ~(MSDC_CFG_CKDIV | MSDC_CFG_CKMOD);
  Cfg |= ((Div << 8) & MSDC_CFG_CKDIV);
  Cfg |= ((Mode << MSDC_CFG_CKMOD_SHIFT) & MSDC_CFG_CKMOD);
  Cfg |= MSDC_CFG_MODE | MSDC_CFG_PIO;
  if (Ddr) {
    Cfg |= MSDC_CFG_SCLK_STOP_DDR;
  } else {
    Cfg &= ~MSDC_CFG_SCLK_STOP_DDR;
  }
  Cfg &= ~MSDC_CFG_CKPDN;

  // avoid clk glitches
  SafeDiv = (Div == 0) ? 1 : Div + 1;
  if (SafeDiv > 0xFFFU) {
    SafeDiv = 0xFFFU;
  }
  MsdcWrite (MSDC_CFG, (Cfg & ~MSDC_CFG_CKDIV) | ((SafeDiv << 8) & MSDC_CFG_CKDIV));
  Status = MsdcWaitMask (MSDC_CFG, MSDC_CFG_CKSTB, TRUE, 100000);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MsdcWrite (MSDC_CFG, Cfg);
  Status = MsdcWaitMask (MSDC_CFG, MSDC_CFG_CKSTB, TRUE, 100000);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //keeps CKPDN clear CFG=02200199
  return EFI_SUCCESS;
}

STATIC
UINT32
MsdcResponseType (
  IN UINT32 Opcode
  )
{
  Opcode &= SDC_CMD_OPC;

  switch (Opcode) {
    case 0:
    case 4:
      return 0;
    case 2:
    case 9:
      return 2;
    case 1:
    case 41:
      return 3;
    case 5:
      return 4;
    case 6:
    case 7:
    case 12:
    case 28:
    case 29:
    case 38:
      return 7;
    default:
      return 1;
  }
}

STATIC
BOOLEAN
MsdcCommandHasData (
  IN UINT32 Opcode,
  IN UINT32 Argument
  )
{
  Opcode &= SDC_CMD_OPC;

  // MMC CMD8 with arg 0 is SEND_EXT_CSD; otherwise it is SD CMD8
  if (Opcode == 8) {
    return Argument == 0;
  }

  return (Opcode == 17) || (Opcode == 18) || (Opcode == 21) ||
         (Opcode == 24) || (Opcode == 25) || (Opcode == 26) ||
         (Opcode == 27) || (Opcode == 30) || (Opcode == 46) ||
         (Opcode == 47) || (Opcode == 51);
}

STATIC
BOOLEAN
MsdcCommandIsWrite (
  IN UINT32 Opcode
  )
{
  Opcode &= SDC_CMD_OPC;
  return (Opcode == 24) || (Opcode == 25) || (Opcode == 26) || (Opcode == 27);
}

STATIC
UINT32
MsdcBuildRawCommand (
  IN UINT32  Opcode,
  IN UINT32  Argument,
  IN UINT32  BlockSize,
  IN BOOLEAN IsWrite
  )
{
  UINT32 Raw;

  Opcode = Opcode & SDC_CMD_OPC;
  Raw    = Opcode | ((MsdcResponseType (Opcode) << 7) & SDC_CMD_RSPTYP);

  if (Opcode == 12) {
    Raw |= SDC_CMD_STOP;
  }

  if (MsdcCommandHasData (Opcode, Argument)) {
    // DTYPE is bits 12:11;  BIT10 is ACMD and is not a data flag
    Raw |= SDC_CMD_DTYPE_SINGLE;
    Raw |= (BlockSize << 16) & SDC_CMD_BLKLEN;
    if (IsWrite) {
      Raw |= SDC_CMD_WR;
    }
  }

  return Raw;
}

STATIC
EFI_STATUS
MsdcWaitCommand (
  IN UINT32 Opcode
  )
{
  UINT32 Int;

  for (UINT32 Timeout = 0; Timeout < MSDC_CMD_TIMEOUT_US; ++Timeout) {
    Int = MsdcRead (MSDC_INT);

    if ((Int & MSDC_INT_CMDTMO) != 0) {
      DEBUG ((DEBUG_WARN, "MSDC0: CMD%u timeout\n", Opcode));
      MsdcDebugState ("cmd-timeout-pending");
      MsdcW1c (Int & MSDC_CMD_INT_MASK);
      MsdcResetController ();
      return EFI_TIMEOUT;
    }

    if ((Int & MSDC_INT_RSPCRCERR) != 0) {
      DEBUG ((DEBUG_WARN, "MSDC0: CMD%u response CRC error\n", Opcode));
      MsdcDebugState ("cmd-crc-pending");
      MsdcW1c (Int & MSDC_CMD_INT_MASK);
      MsdcResetController ();
      return EFI_CRC_ERROR;
    }

    if ((Int & MSDC_INT_CMDRDY) != 0) {
      MsdcW1c (MSDC_INT_CMDRDY);
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  DEBUG ((DEBUG_WARN, "MSDC0: CMD%u polling timeout\n", Opcode));
  MsdcDebugState ("cmd-poll-timeout");
  MsdcResetController ();
  return EFI_TIMEOUT;
}

STATIC
BOOLEAN
MsdcCommandMayRetry (
  IN UINT32 Opcode
  )
{
  switch (Opcode & SDC_CMD_OPC) {
    case 2:  // ALL_SEND_CID
    case 3:  // SET_RELATIVE_ADDR
    case 7:  // SELECT_CARD
    case 8:  // SEND_EXT_CSD
    case 9:  // SEND_CSD
    case 13: // SEND_STATUS 
    case 17: // READ_SINGLE_BLOCK 
    case 18: // READ_MULTIPLE_BLOCK 
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC
EFI_STATUS
EFIAPI
MsdcSendCommand (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_CMD                MmcCmd,
  IN UINT32                 Argument
  )
{
  UINT32 Opcode;
  UINT32 Raw;
  UINT32 Int;
  EFI_STATUS Status;

  (VOID)This;
  Opcode       = MmcCmd & SDC_CMD_OPC;
  mLastCommand = Opcode;

  Status = MsdcWaitMask (
             SDC_STS,
             SDC_STS_CMDBUSY | SDC_STS_SDCBUSY,
             FALSE,
             MSDC_CMD_TIMEOUT_US
             );
  if (EFI_ERROR (Status)) {
    MsdcResetController ();
    return Status;
  }

  Int = MsdcRead (MSDC_INT);
  if (Int != 0) {
    MsdcW1c (Int);
  }

  if (MsdcCommandHasData (Opcode, Argument)) {
    MsdcWrite (MSDC_FIFOCS, MSDC_FIFOCS_CLR);
    Status = MsdcWaitMask (MSDC_FIFOCS, MSDC_FIFOCS_CLR, FALSE, 10000);
    if (EFI_ERROR (Status)) {
      MsdcResetController ();
      return Status;
    }
    MsdcWrite (SDC_BLK_NUM, 1);
  } else {
    MsdcWrite (SDC_BLK_NUM, 0);
  }

  MsdcWrite (SDC_ARG, Argument);
  Raw = MsdcBuildRawCommand (
          Opcode,
          Argument,
          MSDC_BLOCK_SIZE,
          MsdcCommandIsWrite (Opcode)
          );
  DEBUG ((DEBUG_INFO, "MSDC0: CMD%u ARG=%08x RAW=%08x\n", Opcode, Argument, Raw));
  MsdcWrite (SDC_CMD, Raw);

  Status = MsdcWaitCommand (Opcode);
  if (EFI_ERROR (Status)) {
    if ((Status == EFI_CRC_ERROR) &&
        MsdcCommandMayRetry (Opcode) &&
        (mCommandRetryDepth < 2))
    {
      ++mCommandRetryDepth;
      DEBUG ((DEBUG_WARN, "MSDC0: retry CMD%u after response CRC error\n", Opcode));
      Status = MsdcSendCommand (This, MmcCmd, Argument);
      --mCommandRetryDepth;
    }
    return Status;
  }

  if (MsdcResponseType (Opcode) == 7) {
    Status = MsdcWaitMask (
               SDC_STS,
               SDC_STS_CMDBUSY | SDC_STS_SDCBUSY,
               FALSE,
               MSDC_CMD_TIMEOUT_US
               );
    if (EFI_ERROR (Status)) {
      MsdcResetController ();
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MsdcReceiveResponse (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_RESPONSE_TYPE      Type,
  OUT UINT32               *Buffer
  )
{
  (VOID)This;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Type == MMC_RESPONSE_TYPE_R2) {
    Buffer[0] = MsdcRead (SDC_RESP3);
    Buffer[1] = MsdcRead (SDC_RESP2);
    Buffer[2] = MsdcRead (SDC_RESP1);
    Buffer[3] = MsdcRead (SDC_RESP0);
  } else {
    Buffer[0] = MsdcRead (SDC_RESP0);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MsdcCheckDataStatus (
  IN UINT32 Int
  )
{
  if ((Int & MSDC_INT_DATTMO) != 0) {
    MsdcW1c (Int & MSDC_DATA_INT_MASK);
    DEBUG ((DEBUG_WARN, "MSDC0: CMD%u data timeout\n", mLastCommand));
    MsdcDebugState ("data-timeout");
    MsdcResetController ();
    return EFI_TIMEOUT;
  }

  if ((Int & MSDC_INT_DATCRCERR) != 0) {
    MsdcW1c (Int & MSDC_DATA_INT_MASK);
    DEBUG ((DEBUG_WARN, "MSDC0: CMD%u data CRC error\n", mLastCommand));
    MsdcDebugState ("data-crc");
    MsdcResetController ();
    return EFI_CRC_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MsdcReadBlockData (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN EFI_LBA                Lba,
  IN UINTN                  Length,
  OUT UINT32               *Buffer
  )
{
  UINT8     *BytePtr;
  UINTN      Remaining;
  BOOLEAN    TransferComplete;
  EFI_STATUS Status;

  (VOID)This;
  (VOID)Lba;

  DEBUG ((
    DEBUG_INFO,
    "MSDC0: ReadBlockData LBA=%Lu Length=%Lu Buffer=%p\n",
    (UINT64)Lba,
    (UINT64)Length,
    Buffer
    ));

  if (Buffer == NULL) {
    DEBUG ((DEBUG_ERROR, "MSDC0: ReadBlockData invalid buffer\n"));
    return EFI_INVALID_PARAMETER;
  }
  if (Length == 0) {
    return EFI_SUCCESS;
  }
  if (Length != MSDC_BLOCK_SIZE) {
    DEBUG ((DEBUG_ERROR, "MSDC0: ReadBlockData unsupported length=%Lu\n", (UINT64)Length));
    return EFI_UNSUPPORTED;
  }

  BytePtr          = (UINT8 *)Buffer;
  Remaining        = Length;
  TransferComplete = FALSE;

  for (UINT32 Timeout = 0; Timeout < MSDC_DATA_TIMEOUT_US; ++Timeout) {
    UINT32 Fifo;
    UINT32 Count;
    UINT32 Int;

    Fifo  = MsdcRead (MSDC_FIFOCS);
    Count = Fifo & MSDC_FIFOCS_RXCNT;

    // read every available complete word, do not wait for a 64 byte threshold
    while ((Count >= 4) && (Remaining >= 4)) {
      *(UINT32 *)BytePtr = MsdcRead (MSDC_RXDATA);
      BytePtr += 4;
      Remaining -= 4;
      Count -= 4;
    }

    // byte accesses are reserved for the final 1..3 bytes only
    if ((Remaining < 4) && (Count >= Remaining) && (Remaining != 0)) {
      while (Remaining != 0) {
        *BytePtr++ = MmioRead8 (MSDC0_BASE + MSDC_RXDATA);
        --Remaining;
        --Count;
      }
    }

    Int = MsdcRead (MSDC_INT);
    Status = MsdcCheckDataStatus (Int);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if ((Int & MSDC_INT_XFER_COMPL) != 0) {
      TransferComplete = TRUE;
    }

    if (TransferComplete && (Remaining == 0)) {
      MsdcW1c (MSDC_INT_XFER_COMPL);
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  MsdcResetController ();
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
EFIAPI
MsdcWriteBlockData (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN EFI_LBA                Lba,
  IN UINTN                  Length,
  IN UINT32                *Buffer
  )
{
  UINT8     *BytePtr;
  UINTN      Remaining;
  BOOLEAN    TransferComplete;
  EFI_STATUS Status;

  (VOID)This;
  (VOID)Lba;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (Length == 0) {
    return EFI_SUCCESS;
  }
  if (Length != MSDC_BLOCK_SIZE) {
    return EFI_UNSUPPORTED;
  }

  BytePtr          = (UINT8 *)Buffer;
  Remaining        = Length;
  TransferComplete = FALSE;

  for (UINT32 Timeout = 0; Timeout < MSDC_DATA_TIMEOUT_US; ++Timeout) {
    UINT32 Fifo;
    UINT32 Used;
    UINT32 Chunk;
    UINT32 Int;

    Fifo      = MsdcRead (MSDC_FIFOCS);
    Used      = (Fifo & MSDC_FIFOCS_TXCNT) >> 16;

    // comboA PIO path is fed only when FIFO is empty
    if ((Used == 0) && (Remaining != 0)) {
      Chunk = (Remaining >= MSDC_FIFO_SIZE) ? MSDC_FIFO_SIZE : (UINT32)Remaining;

      for (UINT32 Words = Chunk / 4; Words != 0; --Words) {
        MsdcWrite (MSDC_TXDATA, *(UINT32 *)BytePtr);
        BytePtr += 4;
        Remaining -= 4;
      }
      for (UINT32 Bytes = Chunk % 4; Bytes != 0; --Bytes) {
        MmioWrite8 (MSDC0_BASE + MSDC_TXDATA, *BytePtr++);
        --Remaining;
      }
    }

    Int = MsdcRead (MSDC_INT);
    Status = MsdcCheckDataStatus (Int);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if ((Int & MSDC_INT_XFER_COMPL) != 0) {
      TransferComplete = TRUE;
    }

    Used = (MsdcRead (MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16;
    if (TransferComplete && (Remaining == 0) && (Used == 0)) {
      MsdcW1c (MSDC_INT_XFER_COMPL);
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  MsdcResetController ();
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
EFIAPI
MsdcSetIos (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN UINT32                 BusClockFreq,
  IN UINT32                 BusWidth,
  IN UINT32                 TimingMode
  )
{
  UINT32 SdcCfg;
  UINT32 Width;
  UINT32 Iocon;
  BOOLEAN Ddr;

  (VOID)This;
  Ddr = FALSE;

#if MSDC_FORCE_SDR_FALLBACK
  if ((TimingMode == EMMCHS52DDR1V2) || (TimingMode == EMMCHS52DDR1V8)) {
    DEBUG ((DEBUG_WARN, "MSDC0: rejecting DDR timing for SDR fallback probe\n"));
    return EFI_UNSUPPORTED;
  }
#endif

  // dont let MmcDxe switch the card to a mode this PIO host doesnt drive
  switch (TimingMode) {
    case EMMCHS52DDR1V8:
      Ddr = TRUE;
      break;
    case EMMCHS52DDR1V2:
    case EMMCHS200SDR1V8:
    case EMMCHS200SDR1V2:
    case EMMCHS400DDR1V8:
    case EMMCHS400DDR1V2:
      return EFI_UNSUPPORTED;
    default:
      break;
  }

  if ((BusWidth != 1) && (BusWidth != 4) && (BusWidth != 8)) {
    return EFI_UNSUPPORTED;
  }
  if (BusClockFreq == 0) {
    BusClockFreq = 400000;
  }

  switch (BusWidth) {
    case 1:
      Width = 0;
      break;
    case 4:
      Width = 1;
      break;
    case 8:
      Width = 2;
      break;
    default:
      return EFI_UNSUPPORTED;
  }

  SdcCfg = MsdcRead (SDC_CFG);
  SdcCfg &= ~SDC_CFG_BUSWIDTH;
  SdcCfg &= ~SDC_CFG_SDIO;
  SdcCfg &= ~SDC_CFG_SDIOIDE;
  SdcCfg |= Width << 16;
  MsdcWrite (SDC_CFG, SdcCfg);

  Iocon = MsdcRead (MSDC_IOCON);
  if (Ddr) {
    Iocon |= MSDC_IOCON_DDR50CKD;
    MsdcConfigureDdrTuning ();
  }
  MsdcWrite (MSDC_IOCON, Iocon);

  return MsdcApplyClock (BusClockFreq, Ddr);
}

STATIC
BOOLEAN
EFIAPI
MsdcIsCardPresent (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  (VOID)This;
  return TRUE;
}

STATIC
BOOLEAN
EFIAPI
MsdcIsReadOnly (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  (VOID)This;
  return FALSE;
}

STATIC
EFI_STATUS
EFIAPI
MsdcBuildDevicePath (
  IN EFI_MMC_HOST_PROTOCOL     *This,
  OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath
  )
{
  VENDOR_DEVICE_PATH *VendorNode;

  (VOID)This;

  if (DevicePath == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  VendorNode = (VENDOR_DEVICE_PATH *)CreateDeviceNode (
                                       HARDWARE_DEVICE_PATH,
                                       HW_VENDOR_DP,
                                       sizeof (VENDOR_DEVICE_PATH)
                                       );
  if (VendorNode == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (&VendorNode->Guid, &gEfiCallerIdGuid, sizeof (EFI_GUID));
  *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)VendorNode;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MsdcNotifyState (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_STATE              State
  )
{
  (VOID)This;
  (VOID)State;
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
EFIAPI
MsdcIsMultiBlock (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  (VOID)This;
  return FALSE;
}

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

EFI_STATUS
EFIAPI
Msdc0EntryPoint (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_HANDLE Handle;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Status = MsdcResetController ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MsdcConfigureKnownGoodState ();
  Status = MsdcSetIos (&mMsdcHost, 400000, 1, EMMCBACKWARD);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MsdcDebugState ("ready");

  Handle = NULL;
  return gBS->InstallMultipleProtocolInterfaces (
               &Handle,
               &gEmbeddedMmcHostProtocolGuid,
               &mMsdcHost,
               NULL
               );
}
