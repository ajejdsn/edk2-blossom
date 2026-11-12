#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/CacheMaintenanceLib.h>

#include <Protocol/MmcHost.h>

#define MSDC0_BASE              0x11230000U

//
// Source clock of MSDC0 (upper bound). If the real one is lower the card
// just runs slower, never faster.
//
#define MSDC_SRC_CLK_HZ         400000000U
#define MSDC_INIT_CLOCK_HZ      400000U
#define MSDC_MAX_CLOCK_HZ       50000000U

#define MSDC_CFG                0x00
#define MSDC_IOCON              0x04
#define MSDC_PS                 0x08
#define MSDC_INT                0x0C
#define MSDC_INTEN              0x10
#define MSDC_FIFOCS             0x14

#define SDC_CFG                 0x30
#define SDC_CMD                 0x34
#define SDC_ARG                 0x38
#define SDC_STS                 0x3C
#define SDC_RESP0               0x40
#define SDC_RESP1               0x44
#define SDC_RESP2               0x48
#define SDC_RESP3               0x4C
#define SDC_BLK_NUM             0x50

#define MSDC_PAD_TUNE           0xEC
#define MSDC_PAD_TUNE0          0xF0
#define MSDC_DMA_SA_HIGH        0x8C
#define MSDC_DMA_SA             0x90
#define MSDC_DMA_CTRL           0x98
#define MSDC_DMA_CFG            0x9C

#define MSDC_CFG_MODE           BIT0
#define MSDC_CFG_CKPDN          BIT1      // comboA: clear keeps the MSDC clock active
#define MSDC_CFG_RST            BIT2
#define MSDC_CFG_PIO            BIT3
#define MSDC_CFG_CKSTB          BIT7
#define MSDC_CFG_CKDIV_SHIFT    8
#define MSDC_CFG_CKDIV          0x000FFF00U
#define MSDC_CFG_CKMOD_SHIFT    20
#define MSDC_CFG_CKMOD          0x00300000U
#define MSDC_CFG_HS400_CK_MODE  BIT22
#define MSDC_CFG_SCLK_STOP_DDR  BIT25

#define MSDC_PS_DAT0            BIT16

#define MSDC_IOCON_SAMPLE_BITS  0x00FF013EU   // RSPL DSPL DDLSEL DDR50CKD DSPLSEL W_DSPL D0..D7SPL
#define MSDC_IOCON_DSPL         BIT2
#define MSDC_PAD_TUNE_DATRRDLY  0x00001F00U
#define MSDC_PAD_TUNE_DATRR_SH  8
#define MSDC_PAD_TUNE_CMDRDLY   0x001F0000U
#define MSDC_PAD_TUNE_CMD_SH    16
#define MSDC_PAD_TUNE_RD_SEL    BIT13
#define MSDC_PAD_TUNE_CMD_SEL   BIT21
#define MSDC_IOCON_RSPL         BIT1

#define MSDC_INT_DMAQ_EMPTY     BIT6      // DMA descriptor queue empty (not a data error)
#define MSDC_INT_CMDRDY         BIT8
#define MSDC_INT_CMDTMO         BIT9
#define MSDC_INT_RSPCRCERR      BIT10
#define MSDC_INT_XFER_COMPL     BIT12
#define MSDC_INT_DXFER_DONE     BIT13
#define MSDC_INT_DATTMO         BIT14
#define MSDC_INT_DATCRCERR      BIT15
#define MSDC_INT_DMA_BDCSERR    BIT17
#define MSDC_INT_DMA_GPDCSERR   BIT18
#define MSDC_INT_DMA_PROTECT    BIT19

#define MSDC_FIFOCS_RXCNT       0x000000FFU
#define MSDC_FIFOCS_TXCNT       0x00FF0000U
#define MSDC_FIFOCS_CLR         BIT31

#define SDC_CFG_BUSWIDTH_SHIFT  16
#define SDC_CFG_BUSWIDTH        0x00030000U
#define SDC_CFG_SDIO            BIT19
#define SDC_CFG_SDIOIDE         BIT20
#define SDC_CFG_DTOC_SHIFT      24
#define SDC_CFG_DTOC            0xFF000000U

#define SDC_CMD_OPC             0x0000003FU
#define SDC_CMD_RSPTYP_SHIFT    7
#define SDC_CMD_DTYPE_SINGLE    BIT11
#define SDC_CMD_DTYPE_MULTI     BIT12
#define SDC_CMD_WR              BIT13
#define SDC_CMD_STOP            BIT14
#define SDC_CMD_BLKLEN_SHIFT    16
#define SDC_CMD_BLKLEN          0x0FFF0000U

#define SDC_STS_SDCBUSY         BIT0
#define SDC_STS_CMDBUSY         BIT1

#define MSDC_DMA_CTRL_START     BIT0
#define MSDC_DMA_CTRL_STOP      BIT1
#define MSDC_DMA_CTRL_MODE      BIT8      // 1 = descriptor mode
#define MSDC_DMA_CTRL_BURST_64B (6U << 12)
#define MSDC_DMA_CTRL_BURST     (7U << 12)
#define MSDC_DMA_CFG_STS        BIT0
#define MSDC_DMA_CFG_DECSEN     BIT1

#define GPDMA_DESC_HWO          BIT0
#define GPDMA_DESC_BDP          BIT1
#define BDMA_DESC_EOL           BIT0

#define MSDC_BLOCK_SIZE         512U
#define MSDC_BD_MAX_LEN         0x8000U
#define MSDC_BD_MAX_NUM         16U
#define MSDC_BOUNCE_PAGES       128U

#define MSDC_DESC_GPD2_OFF      0x40U
#define MSDC_DESC_BD_OFF        0x80U

#define MSDC_CMD_TIMEOUT_US     200000U
#define MSDC_BUSY_TIMEOUT_US    3000000U
#define MSDC_DAT0_TIMEOUT_US    500000U
#define MSDC_DATA_TIMEOUT_US    10000000U

#define MSDC_CMD_INT_MASK       (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR)
#define MSDC_DMA_ERR_MASK       (MSDC_INT_DMA_BDCSERR | MSDC_INT_DMA_GPDCSERR | MSDC_INT_DMA_PROTECT)
#define MSDC_DATA_INT_MASK      (MSDC_INT_XFER_COMPL | MSDC_INT_DATTMO | MSDC_INT_DATCRCERR | MSDC_DMA_ERR_MASK)
#define MSDC_ALL_INT_MASK       (MSDC_CMD_INT_MASK | MSDC_DATA_INT_MASK)

//
// Card status R1 value reported for commands that are executed lazily
// (READY_FOR_DATA, state = tran).
//
#define MSDC_FAKE_R1_READY      0x00000900U

typedef struct {
  UINT32 GpdInfo;
  UINT32 Next;
  UINT32 Ptr;
  UINT32 GpdDataLen;
  UINT32 Arg;
  UINT32 BlkNum;
  UINT32 Cmd;
} MT_GPDMA_DESC;

typedef struct {
  UINT32 BdInfo;
  UINT32 Next;
  UINT32 Ptr;
  UINT32 BdDataLen;
} MT_BDMA_DESC;

STATIC MT_GPDMA_DESC         *mDmaGpd;
STATIC MT_BDMA_DESC          *mDmaBd;
STATIC EFI_PHYSICAL_ADDRESS   mDescPhys;
STATIC UINT8                 *mBounce;
STATIC EFI_PHYSICAL_ADDRESS   mBouncePhys;
STATIC UINT32                 mBounceBlocks;

STATIC UINT32                 mPadTuneReg = MSDC_PAD_TUNE0;
STATIC BOOLEAN                mTuneDone;
STATIC BOOLEAN                mCmdTuneDone;
STATIC BOOLEAN                mCmdTuneActive;
STATIC UINT32                 mLastCmdInt;
STATIC UINT32                 mLastCmdSts;
STATIC UINT32                 mLastCmdPs;
STATIC UINT32                 mLastCmdReg;
STATIC UINT32                 mLastCmdArg;
STATIC UINT32                 mLastCmdCfg;
STATIC UINT32                 mLastCmdIocon;
STATIC UINT32                 mLastCmdPadEc;
STATIC UINT32                 mLastCmdPadF0;
STATIC UINT32                 mLastCmdDmaCfg;
STATIC UINT32                 mLastCmdDmaCtrl;
STATIC UINT32                 mLastCmdFifo;
STATIC UINT32                 mCurrentClockHz;
STATIC UINT8                  mTuneBuf[MSDC_BLOCK_SIZE];
STATIC BOOLEAN                mPendingValid;
STATIC UINT32                 mPendingOpcode;
STATIC UINT32                 mPendingArg;
STATIC UINT32                 mPredefBlocks;
STATIC BOOLEAN                mStopHandled;
STATIC BOOLEAN                mUseCachedResp;
STATIC UINT32                 mCachedResp[4];

STATIC UINT32 MsdcRead (IN UINTN Offset)
{
  return MmioRead32 (MSDC0_BASE + Offset);
}

STATIC VOID MsdcWrite (IN UINTN Offset, IN UINT32 Value)
{
  MmioWrite32 (MSDC0_BASE + Offset, Value);
}

STATIC VOID MsdcW1c (IN UINT32 Mask)
{
  MmioWrite32 (MSDC0_BASE + MSDC_INT, Mask);
}

STATIC EFI_STATUS MsdcWaitMask (IN UINTN Offset, IN UINT32 Mask, IN BOOLEAN Set, IN UINT32 TimeoutUs)
{
  UINT32 Value;

  if (TimeoutUs == 0) {
    TimeoutUs = 1;
  }
  while (TimeoutUs-- != 0) {
    Value = MsdcRead (Offset);
    if (Set) {
      if ((Value & Mask) == Mask) {
        return EFI_SUCCESS;
      }
    } else {
      if ((Value & Mask) == 0) {
        return EFI_SUCCESS;
      }
    }
    MicroSecondDelay (1);
  }
  return EFI_TIMEOUT;
}

STATIC UINT8 MsdcDmaChecksum (IN CONST UINT8 *Buf, IN UINT32 Len)
{
  UINT32 Sum = 0;
  UINT32 Index;

  for (Index = 0; Index < Len; Index++) {
    Sum += Buf[Index];
  }
  return (UINT8)(0xFF - (UINT8)Sum);
}

STATIC EFI_STATUS MsdcClearFifo (VOID)
{
  MsdcWrite (MSDC_FIFOCS, MSDC_FIFOCS_CLR);
  return MsdcWaitMask (MSDC_FIFOCS, MSDC_FIFOCS_CLR, FALSE, 10000);
}

//
// Same as mtk-sd msdc_reset_hw(): reset state machines, clear FIFO and
// pending interrupts. Register contents (clock, bus width) are retained.
//
STATIC EFI_STATUS MsdcResetController (VOID)
{
  UINT32 Cfg = MsdcRead (MSDC_CFG);

  MsdcWrite (MSDC_CFG, Cfg | MSDC_CFG_RST);
  if (EFI_ERROR (MsdcWaitMask (MSDC_CFG, MSDC_CFG_RST, FALSE, 10000))) {
    DEBUG ((DEBUG_ERROR, "MSDC0: reset timeout\n"));
    return EFI_TIMEOUT;
  }
  if (EFI_ERROR (MsdcClearFifo ())) {
    DEBUG ((DEBUG_ERROR, "MSDC0: FIFO clear timeout\n"));
    return EFI_TIMEOUT;
  }
  MsdcW1c (MsdcRead (MSDC_INT));
  MsdcWrite (MSDC_INTEN, MSDC_ALL_INT_MASK);
  return EFI_SUCCESS;
}

STATIC VOID MsdcStopDma (VOID)
{
  UINT32 Ctrl = MsdcRead (MSDC_DMA_CTRL);

  MsdcWrite (MSDC_DMA_CTRL, Ctrl | MSDC_DMA_CTRL_STOP);
  if (EFI_ERROR (MsdcWaitMask (MSDC_DMA_CFG, MSDC_DMA_CFG_STS, FALSE, 20000))) {
    DEBUG ((DEBUG_ERROR, "MSDC0: DMA stop timeout\n"));
  }
  MsdcWrite (MSDC_DMA_CTRL, Ctrl & ~(MSDC_DMA_CTRL_START | MSDC_DMA_CTRL_STOP));
}

STATIC EFI_STATUS MsdcSetClock (IN UINT32 Hz)
{
  UINT32      Cfg;
  UINT32      Div;
  UINT32      Mode;
  UINT32      SafeDiv;
  UINT64      Denom;
  EFI_STATUS  Status;

  if (Hz == 0) {
    return EFI_INVALID_PARAMETER;
  }

  if (Hz > MSDC_MAX_CLOCK_HZ) {
    Hz = MSDC_MAX_CLOCK_HZ;
  }

  // MT6765 comboA clock tree: 400 MHz MSDCPLL parent.
  // CKMOD=0: f = source / (4 * div), div=0 gives source / 2.
  // CKMOD=1: direct source clock. DDR CKMOD=2 is deliberately not used here.
  if (Hz >= MSDC_SRC_CLK_HZ) {
    Mode = 1;
    Div  = 0;
  } else if (Hz >= (MSDC_SRC_CLK_HZ / 2U)) {
    Mode = 0;
    Div  = 0;
  } else {
    Mode  = 0;
    Denom = (UINT64)Hz * 4U;
    Div   = (UINT32)(((UINT64)MSDC_SRC_CLK_HZ + Denom - 1U) / Denom);
    if (Div == 0) {
      Div = 1;
    }
    if (Div > 0xFFFU) {
      Div = 0xFFFU;
    }
  }

  Cfg = MsdcRead (MSDC_CFG);
  Cfg &= ~(MSDC_CFG_CKDIV |
           MSDC_CFG_CKMOD |
           MSDC_CFG_HS400_CK_MODE |
           MSDC_CFG_SCLK_STOP_DDR |
           MSDC_CFG_CKPDN |
           MSDC_CFG_PIO);
  Cfg |= MSDC_CFG_MODE;
  Cfg |= ((Div << MSDC_CFG_CKDIV_SHIFT) & MSDC_CFG_CKDIV);
  Cfg |= ((Mode << MSDC_CFG_CKMOD_SHIFT) & MSDC_CFG_CKMOD);

  // Match the MT6765 vendor sequence: move through a neighboring divider
  // before applying the requested divider, without power-cycling the clock.
  SafeDiv = (Div == 0) ? 1U : (Div + 1U);
  if (SafeDiv > 0xFFFU) {
    SafeDiv = 0xFFFU;
  }

  MsdcWrite (MSDC_CFG,
             (Cfg & ~MSDC_CFG_CKDIV) |
             ((SafeDiv << MSDC_CFG_CKDIV_SHIFT) & MSDC_CFG_CKDIV));
  Status = MsdcWaitMask (MSDC_CFG, MSDC_CFG_CKSTB, TRUE, 100000);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MSDC0: clock safe-divider %u not stable CFG=%08x\n",
      SafeDiv, MsdcRead (MSDC_CFG)));
    return Status;
  }

  MsdcWrite (MSDC_CFG, Cfg);
  Status = MsdcWaitMask (MSDC_CFG, MSDC_CFG_CKSTB, TRUE, 100000);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MSDC0: clock divider %u not stable CFG=%08x\n",
      Div, MsdcRead (MSDC_CFG)));
    return Status;
  }

  mCurrentClockHz = Hz;
  DEBUG ((DEBUG_INFO,
    "MSDC0: clock req=%u src=%u mode=%u div=%u safe=%u CFG=%08x\n",
    Hz, MSDC_SRC_CLK_HZ, Mode, Div, SafeDiv, MsdcRead (MSDC_CFG)));
  return EFI_SUCCESS;
}

STATIC VOID MsdcSetBusWidth (IN UINT32 BusWidth)
{
  UINT32 SdcCfg = MsdcRead (SDC_CFG);

  SdcCfg &= ~(SDC_CFG_BUSWIDTH | SDC_CFG_SDIO | SDC_CFG_SDIOIDE);
  if (BusWidth == 8) {
    SdcCfg |= (2U << SDC_CFG_BUSWIDTH_SHIFT);
  } else if (BusWidth == 4) {
    SdcCfg |= (1U << SDC_CFG_BUSWIDTH_SHIFT);
  }
  MsdcWrite (SDC_CFG, SdcCfg);
  DEBUG ((DEBUG_INFO, "MSDC0: bus width requested=%u encoded=%u SDC_CFG=%08x\n",
    BusWidth, (MsdcRead (SDC_CFG) & SDC_CFG_BUSWIDTH) >> SDC_CFG_BUSWIDTH_SHIFT,
    MsdcRead (SDC_CFG)));
}

//
// The delay field lives in PAD_TUNE0 (0xF0) on newer MSDC and PAD_TUNE (0xEC)
// on older ones. Use whichever register keeps the written bits.
//
STATIC VOID MsdcDetectPadTune (VOID)
{
  STATIC CONST UINT32  Candidates[] = { MSDC_PAD_TUNE0, MSDC_PAD_TUNE };
  UINT32               Index;
  UINT32               Saved;
  UINT32               Readback;

  for (Index = 0; Index < ARRAY_SIZE (Candidates); Index++) {
    Saved = MsdcRead (Candidates[Index]);
    MsdcWrite (Candidates[Index], Saved | MSDC_PAD_TUNE_DATRRDLY);
    Readback = MsdcRead (Candidates[Index]);
    MsdcWrite (Candidates[Index], Saved);
    if ((Readback & MSDC_PAD_TUNE_DATRRDLY) == MSDC_PAD_TUNE_DATRRDLY) {
      mPadTuneReg = Candidates[Index];
      break;
    }
  }
  DEBUG ((DEBUG_INFO, "MSDC0: pad tune reg %02x (E0=%08x F0=%08x)\n",
    mPadTuneReg, MsdcRead (MSDC_PAD_TUNE), MsdcRead (MSDC_PAD_TUNE0)));
}

STATIC EFI_STATUS MsdcInitHw (VOID)
{
  UINT32 Cfg;
  UINT32 SdcCfg;
  EFI_STATUS Status;

  Cfg = MsdcRead (MSDC_CFG);
  Cfg |= MSDC_CFG_MODE;
  Cfg &= ~(MSDC_CFG_CKPDN | MSDC_CFG_PIO | MSDC_CFG_SCLK_STOP_DDR);
  MsdcWrite (MSDC_CFG, Cfg);

  Status = MsdcResetController ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Cfg = MsdcRead (MSDC_CFG);
  Cfg |= MSDC_CFG_MODE;
  Cfg &= ~(MSDC_CFG_CKPDN | MSDC_CFG_PIO | MSDC_CFG_SCLK_STOP_DDR);
  MsdcWrite (MSDC_CFG, Cfg);

  MsdcDetectPadTune ();

  // sampling setup (IOCON / PAD_TUNE0) left by the bootloader for 8-bit SDR50 is kept
  DEBUG ((DEBUG_INFO, "MSDC0: LK state CFG=%08x SDC_CFG=%08x IOCON=%08x TUNE0=%08x\n",
    MsdcRead (MSDC_CFG), MsdcRead (SDC_CFG), MsdcRead (MSDC_IOCON), MsdcRead (mPadTuneReg)));

  SdcCfg  = MsdcRead (SDC_CFG);
  SdcCfg &= ~(SDC_CFG_BUSWIDTH | SDC_CFG_SDIO | SDC_CFG_SDIOIDE | SDC_CFG_DTOC);
  SdcCfg |= (0xFFU << SDC_CFG_DTOC_SHIFT);
  MsdcWrite (SDC_CFG, SdcCfg);

  Status = MsdcSetClock (MSDC_INIT_CLOCK_HZ);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MsdcWrite (MSDC_INTEN, MSDC_ALL_INT_MASK);
  MsdcW1c (MsdcRead (MSDC_INT));
  return EFI_SUCCESS;
}

STATIC UINT32 MsdcResponseType (IN UINT32 Opcode)
{
  switch (Opcode & SDC_CMD_OPC) {
    case 0:
    case 4:
    case 15:
      return 0;
    case 2:
    case 9:
    case 10:
      return 2;
    case 1:
    case 41:
    case 58:
      return 3;
    case 5:
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

STATIC BOOLEAN MsdcCommandHasData (IN UINT32 Opcode, IN UINT32 Argument)
{
  switch (Opcode & SDC_CMD_OPC) {
    case 8:
      return (BOOLEAN)(Argument == 0);       // eMMC SEND_EXT_CSD
    case 17:
    case 18:
    case 24:
    case 25:
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC EFI_STATUS MsdcWaitCommand (VOID)
{
  UINT32 Timeout;
  UINT32 Int;

  for (Timeout = 0; Timeout < MSDC_CMD_TIMEOUT_US; Timeout++) {
    Int = MsdcRead (MSDC_INT);
    if ((Int & MSDC_INT_CMDTMO) != 0) {
      mLastCmdInt     = Int;
      mLastCmdSts     = MsdcRead (SDC_STS);
      mLastCmdPs      = MsdcRead (MSDC_PS);
      mLastCmdReg     = MsdcRead (SDC_CMD);
      mLastCmdArg     = MsdcRead (SDC_ARG);
      mLastCmdCfg     = MsdcRead (MSDC_CFG);
      mLastCmdIocon   = MsdcRead (MSDC_IOCON);
      mLastCmdPadEc   = MsdcRead (MSDC_PAD_TUNE);
      mLastCmdPadF0   = MsdcRead (MSDC_PAD_TUNE0);
      mLastCmdDmaCfg  = MsdcRead (MSDC_DMA_CFG);
      mLastCmdDmaCtrl = MsdcRead (MSDC_DMA_CTRL);
      mLastCmdFifo    = MsdcRead (MSDC_FIFOCS);
      DEBUG ((DEBUG_ERROR,
        "MSDC0: CMD HW timeout INT=%08x SDC_STS=%08x PS=%08x SDC_CMD=%08x SDC_ARG=%08x CFG=%08x IOCON=%08x PAD_EC=%08x PAD_F0=%08x DMA_CFG=%08x DMA_CTRL=%08x FIFO=%08x\n",
        mLastCmdInt, mLastCmdSts, mLastCmdPs, mLastCmdReg, mLastCmdArg,
        mLastCmdCfg, mLastCmdIocon, mLastCmdPadEc, mLastCmdPadF0,
        mLastCmdDmaCfg, mLastCmdDmaCtrl, mLastCmdFifo));
      MsdcW1c (Int & MSDC_CMD_INT_MASK);
      MsdcResetController ();
      return EFI_TIMEOUT;
    }
    if ((Int & MSDC_INT_RSPCRCERR) != 0) {
      mLastCmdInt = Int;
      mLastCmdSts = MsdcRead (SDC_STS);
      mLastCmdPs  = MsdcRead (MSDC_PS);
      mLastCmdReg = MsdcRead (SDC_CMD);
      mLastCmdArg = MsdcRead (SDC_ARG);
      MsdcW1c (Int & MSDC_CMD_INT_MASK);
      MsdcResetController ();
      return EFI_CRC_ERROR;
    }
    if ((Int & MSDC_INT_CMDRDY) != 0) {
      mLastCmdInt = Int;
      MsdcW1c (MSDC_INT_CMDRDY);
      return EFI_SUCCESS;
    }
    MicroSecondDelay (1);
  }
  mLastCmdInt = MsdcRead (MSDC_INT);
  DEBUG ((DEBUG_ERROR,
    "MSDC0: CMD wait timeout INT=%08x STS=%08x PS=%08x CMD=%08x ARG=%08x CFG=%08x IOCON=%08x PAD=%08x INTEN=%08x\n",
    mLastCmdInt, MsdcRead (SDC_STS), MsdcRead (MSDC_PS),
    MsdcRead (SDC_CMD), MsdcRead (SDC_ARG), MsdcRead (MSDC_CFG),
    MsdcRead (MSDC_IOCON), MsdcRead (mPadTuneReg), MsdcRead (MSDC_INTEN)));
  MsdcResetController ();
  return EFI_TIMEOUT;
}

//
// Blocks == 0 -> command without data phase.
//
STATIC EFI_STATUS MsdcIssueCommand (
  IN UINT32   Opcode,
  IN UINT32   Argument,
  IN UINT32   Blocks,
  IN BOOLEAN  IsWrite
  )
{
  UINT32      Rsp     = MsdcResponseType (Opcode);
  BOOLEAN     HasData = (BOOLEAN)(Blocks != 0);
  UINT32      Raw;
  UINT32      BusyMask;
  UINT32      Try;
  EFI_STATUS  Status = EFI_SUCCESS;

  Opcode &= SDC_CMD_OPC;
  Raw = Opcode | (Rsp << SDC_CMD_RSPTYP_SHIFT);
  if (Opcode == 12) {
    Raw |= SDC_CMD_STOP;
  }
  if (HasData) {
    Raw |= (Blocks > 1) ? SDC_CMD_DTYPE_MULTI : SDC_CMD_DTYPE_SINGLE;
    Raw |= (MSDC_BLOCK_SIZE << SDC_CMD_BLKLEN_SHIFT) & SDC_CMD_BLKLEN;
    if (IsWrite) {
      Raw |= SDC_CMD_WR;
    }
  }

  // Match the upstream MediaTek host rule:
  // always wait for CMDBUSY; also wait for SDCBUSY for data and R1B commands.
  // CMD12 is R1B, so it must not be launched while the controller data FSM
  // is still busy after the preceding multi-block transfer.
  BusyMask = SDC_STS_CMDBUSY;
  if (HasData || (Rsp == 7)) {
    BusyMask |= SDC_STS_SDCBUSY;
  }

  mUseCachedResp = FALSE;
  mLastCmdInt = 0;
  mLastCmdSts = 0;
  mLastCmdPs = 0;
  mLastCmdReg = 0;
  mLastCmdArg = 0;
  mLastCmdCfg = 0;
  mLastCmdIocon = 0;
  mLastCmdPadEc = 0;
  mLastCmdPadF0 = 0;
  mLastCmdDmaCfg = 0;
  mLastCmdDmaCtrl = 0;
  mLastCmdFifo = 0;

  for (Try = 0; Try < 3; Try++) {
    Status = MsdcWaitMask (SDC_STS, BusyMask, FALSE, MSDC_BUSY_TIMEOUT_US);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR,
        "MSDC0: busy before CMD%u mask=%08x STS=%08x PS=%08x INT=%08x DMA_CFG=%08x DMA_CTRL=%08x FIFO=%08x\n",
        Opcode, BusyMask, MsdcRead (SDC_STS), MsdcRead (MSDC_PS),
        MsdcRead (MSDC_INT), MsdcRead (MSDC_DMA_CFG),
        MsdcRead (MSDC_DMA_CTRL), MsdcRead (MSDC_FIFOCS)));
      MsdcResetController ();
      return Status;
    }

    if (HasData) {
      if ((MsdcRead (MSDC_FIFOCS) & (MSDC_FIFOCS_RXCNT | MSDC_FIFOCS_TXCNT)) != 0) {
        MsdcResetController ();
      }
      MsdcWrite (SDC_BLK_NUM, Blocks);
    }

    MsdcW1c (MsdcRead (MSDC_INT));
    MsdcWrite (SDC_ARG, Argument);
    MsdcWrite (SDC_CMD, Raw);

    Status = MsdcWaitCommand ();
    if (Status != EFI_CRC_ERROR || HasData || mCmdTuneActive) {
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_VERBOSE, "MSDC0: CMD%u arg=%08x failed: %r\n", Opcode, Argument, Status));
    return Status;
  }

  if (Rsp == 7) {
    Status = MsdcWaitMask (SDC_STS, SDC_STS_CMDBUSY | SDC_STS_SDCBUSY, FALSE, MSDC_BUSY_TIMEOUT_US);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR,
        "MSDC0: CMD%u response received but controller busy timeout STS=%08x PS=%08x INT=%08x DMA_CFG=%08x DMA_CTRL=%08x\n",
        Opcode, MsdcRead (SDC_STS), MsdcRead (MSDC_PS), MsdcRead (MSDC_INT),
        MsdcRead (MSDC_DMA_CFG), MsdcRead (MSDC_DMA_CTRL)));
      return Status;
    }
    // DAT0 low == card busy. Log a stuck DAT0 separately from controller FSM.
    Status = MsdcWaitMask (MSDC_PS, MSDC_PS_DAT0, TRUE, MSDC_DAT0_TIMEOUT_US);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN,
        "MSDC0: CMD%u controller idle but DAT0 still low PS=%08x STS=%08x\n",
        Opcode, MsdcRead (MSDC_PS), MsdcRead (SDC_STS)));
    }
  }

  return EFI_SUCCESS;
}

STATIC VOID MsdcSetupDescriptors (IN UINT32 Addr, IN UINT32 Bytes)
{
  UINT32 BdCount = (Bytes + MSDC_BD_MAX_LEN - 1) / MSDC_BD_MAX_LEN;
  UINT32 BdPhys  = (UINT32)mDescPhys + MSDC_DESC_BD_OFF;
  UINT32 Region  = MSDC_DESC_BD_OFF + (UINT32)sizeof (MT_BDMA_DESC) * MSDC_BD_MAX_NUM;
  UINT32 Index;
  UINT32 Len;

  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)mDescPhys, Region);
  ZeroMem ((VOID *)(UINTN)mDescPhys, Region);

  for (Index = 0; Index < BdCount; Index++) {
    Len = (Bytes > MSDC_BD_MAX_LEN) ? MSDC_BD_MAX_LEN : Bytes;

    mDmaBd[Index].BdInfo    = (Index == BdCount - 1) ? BDMA_DESC_EOL : 0;
    mDmaBd[Index].Next      = (Index == BdCount - 1) ? 0 : (BdPhys + (Index + 1) * (UINT32)sizeof (MT_BDMA_DESC));
    mDmaBd[Index].Ptr       = Addr;
    mDmaBd[Index].BdDataLen = Len;
    mDmaBd[Index].BdInfo   |= ((UINT32)MsdcDmaChecksum ((UINT8 *)&mDmaBd[Index], 16)) << 8;

    Addr  += Len;
    Bytes -= Len;
  }

  mDmaGpd->GpdInfo = GPDMA_DESC_HWO | GPDMA_DESC_BDP;
  mDmaGpd->Next    = (UINT32)mDescPhys + MSDC_DESC_GPD2_OFF;   // second (empty) GPD is mandatory
  mDmaGpd->Ptr     = BdPhys;
  mDmaGpd->GpdInfo |= ((UINT32)MsdcDmaChecksum ((UINT8 *)mDmaGpd, 16)) << 8;

  WriteBackDataCacheRange ((VOID *)(UINTN)mDescPhys, Region);
}

STATIC EFI_STATUS MsdcWaitDataDone (VOID)
{
  UINT32 Timeout;
  UINT32 Int;

  for (Timeout = 0; Timeout < MSDC_DATA_TIMEOUT_US; Timeout++) {
    Int = MsdcRead (MSDC_INT);

    if ((Int & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR | MSDC_DMA_ERR_MASK)) != 0) {
      MsdcW1c (Int & (MSDC_INT_DMAQ_EMPTY | MSDC_INT_DXFER_DONE |
                       MSDC_INT_DATTMO | MSDC_INT_DATCRCERR | MSDC_DMA_ERR_MASK));
      DEBUG ((DEBUG_ERROR, "MSDC0: data error INT=%08x DMA_CFG=%08x DMA_CTRL=%08x DMA_SA=%08x GPD_INFO=%08x BD_INFO=%08x BD_LEN=%08x SDC_CFG=%08x PS=%08x IOCON=%08x TUNE=%08x CFG=%08x\n",
        Int, MsdcRead (MSDC_DMA_CFG), MsdcRead (MSDC_DMA_CTRL), MsdcRead (MSDC_DMA_SA),
        mDmaGpd->GpdInfo, mDmaBd[0].BdInfo, mDmaBd[0].BdDataLen,
        MsdcRead (SDC_CFG), MsdcRead (MSDC_PS), MsdcRead (MSDC_IOCON),
        MsdcRead (mPadTuneReg), MsdcRead (MSDC_CFG)));
      if ((Int & MSDC_INT_DATTMO) != 0) {
        return EFI_TIMEOUT;
      }
      if ((Int & MSDC_INT_DATCRCERR) != 0) {
        return EFI_CRC_ERROR;
      }
      return EFI_DEVICE_ERROR;
    }

    if ((Int & (MSDC_INT_DMAQ_EMPTY | MSDC_INT_DXFER_DONE)) != 0) {
      MsdcW1c (Int & (MSDC_INT_DMAQ_EMPTY | MSDC_INT_DXFER_DONE));
    }
    if ((Int & MSDC_INT_XFER_COMPL) != 0) {
      MsdcW1c (MSDC_INT_XFER_COMPL);
      return EFI_SUCCESS;
    }
    MicroSecondDelay (1);
  }

  DEBUG ((DEBUG_ERROR, "MSDC0: data wait timeout INT=%08x DMA_CFG=%08x\n", MsdcRead (MSDC_INT), MsdcRead (MSDC_DMA_CFG)));
  return EFI_TIMEOUT;
}

//
// One command + one DMA run through the bounce buffer (<4GB, page aligned).
//
STATIC EFI_STATUS MsdcDataTransfer (
  IN UINT32   Opcode,
  IN UINT32   Argument,
  IN BOOLEAN  IsWrite,
  IN UINT32   Blocks,
  IN UINT8    *UserBuf
  )
{
  UINT32      Bytes = Blocks * MSDC_BLOCK_SIZE;
  UINT32      DmaCfg;
  UINT32      DmaCtrl;
  EFI_STATUS  Status;

  if (IsWrite) {
    CopyMem (mBounce, UserBuf, Bytes);
    WriteBackDataCacheRange (mBounce, Bytes);
  } else {
    WriteBackInvalidateDataCacheRange (mBounce, Bytes);
  }

  MsdcSetupDescriptors ((UINT32)mBouncePhys, Bytes);

  DmaCfg = MsdcRead (MSDC_DMA_CFG);
  MsdcWrite (MSDC_DMA_CFG, DmaCfg | MSDC_DMA_CFG_DECSEN);

  DmaCtrl = MsdcRead (MSDC_DMA_CTRL);
  DmaCtrl &= ~(MSDC_DMA_CTRL_MODE | (7U << 12) | MSDC_DMA_CTRL_START |
               MSDC_DMA_CTRL_STOP);
  DmaCtrl |= MSDC_DMA_CTRL_BURST_64B | MSDC_DMA_CTRL_MODE;
  MsdcWrite (MSDC_DMA_CTRL, DmaCtrl);
  MsdcWrite (MSDC_DMA_SA_HIGH, 0);
  MsdcWrite (MSDC_DMA_SA, (UINT32)mDescPhys);
  MsdcWrite (MSDC_INTEN, MSDC_ALL_INT_MASK);

  Status = MsdcIssueCommand (Opcode, Argument, Blocks, IsWrite);
  if (EFI_ERROR (Status)) {
    MsdcStopDma ();
    return Status;
  }

  MsdcWrite (MSDC_DMA_CTRL, DmaCtrl | MSDC_DMA_CTRL_START);

  Status = MsdcWaitDataDone ();
  if (EFI_ERROR (Status)) {
    MsdcStopDma ();
    MsdcResetController ();
    return Status;
  }

  if (EFI_ERROR (MsdcWaitMask (MSDC_DMA_CFG, MSDC_DMA_CFG_STS, FALSE, 20000))) {
    DEBUG ((DEBUG_WARN, "MSDC0: DMA still active after XFER_COMPL\n"));
    MsdcStopDma ();
  } else {
    MsdcWrite (MSDC_DMA_CTRL, DmaCtrl);
  }

  if (!IsWrite) {
    InvalidateDataCacheRange (mBounce, Bytes);
    CopyMem (UserBuf, mBounce, Bytes);
  }
  return EFI_SUCCESS;
}

STATIC VOID MsdcSetReadDelay (IN UINT32 Dspl, IN UINT32 Delay)
{
  UINT32 Iocon = MsdcRead (MSDC_IOCON);
  UINT32 Pad   = MsdcRead (mPadTuneReg);

  if (Dspl != 0) {
    Iocon |= MSDC_IOCON_DSPL;
  } else {
    Iocon &= ~MSDC_IOCON_DSPL;
  }
  Pad &= ~MSDC_PAD_TUNE_DATRRDLY;
  Pad |= (Delay << MSDC_PAD_TUNE_DATRR_SH) | MSDC_PAD_TUNE_RD_SEL;
  MsdcWrite (MSDC_IOCON, Iocon);
  MsdcWrite (mPadTuneReg, Pad);
}

//
// Read-sampling sweep: reads block 0 for every (edge, delay) pair and picks
// the middle of the longest passing window.
//
STATIC VOID MsdcSetCommandDelay (IN UINT32 Rspl, IN UINT32 Delay)
{
  UINT32 Iocon = MsdcRead (MSDC_IOCON);
  UINT32 Pad   = MsdcRead (mPadTuneReg);

  if (Rspl != 0) {
    Iocon |= MSDC_IOCON_RSPL;
  } else {
    Iocon &= ~MSDC_IOCON_RSPL;
  }

  Pad &= ~(MSDC_PAD_TUNE_CMDRDLY | MSDC_PAD_TUNE_CMD_SEL);
  Pad |= (Delay << MSDC_PAD_TUNE_CMD_SH) | MSDC_PAD_TUNE_CMD_SEL;

  MsdcWrite (MSDC_IOCON, Iocon);
  MsdcWrite (mPadTuneReg, Pad);
}

//
// CMD16 is idempotent. Sweep the response sampling edge and command delay
// only after a response-CRC failure, then keep the centre of the widest
// consecutive passing window.
//
STATIC EFI_STATUS MsdcTuneCommand (IN UINT32 Argument)
{
  UINT32   Rspl;
  UINT32   Delay;
  UINT32   Run;
  UINT32   Start = 0;
  UINT32   BestRun = 0;
  UINT32   BestStart = 0;
  UINT32   BestRspl = 0;
  BOOLEAN  Ok;
  UINT32   SavedIocon = MsdcRead (MSDC_IOCON);
  UINT32   SavedPad   = MsdcRead (mPadTuneReg);
  EFI_STATUS Status = EFI_CRC_ERROR;

  mCmdTuneActive = TRUE;

  for (Rspl = 0; Rspl < 2; Rspl++) {
    Run = 0;
    for (Delay = 0; Delay < 32; Delay++) {
      MsdcSetCommandDelay (Rspl, Delay);
      Ok = (BOOLEAN)!EFI_ERROR (MsdcIssueCommand (16, Argument, 0, FALSE));

      if (Ok) {
        if (Run == 0) {
          Start = Delay;
        }
        Run++;
      }

      if ((!Ok || Delay == 31) && Run > BestRun) {
        BestRun   = Run;
        BestStart = Start;
        BestRspl  = Rspl;
      }

      if (!Ok) {
        Run = 0;
      }
    }
  }

  mCmdTuneActive = FALSE;

  if (BestRun == 0) {
    MsdcWrite (MSDC_IOCON, SavedIocon);
    MsdcWrite (mPadTuneReg, SavedPad);
    DEBUG ((DEBUG_ERROR, "MSDC0: CMD response tuning found no window\n"));
    return EFI_CRC_ERROR;
  }

  MsdcSetCommandDelay (BestRspl, BestStart + BestRun / 2);
  Status = MsdcIssueCommand (16, Argument, 0, FALSE);
  if (EFI_ERROR (Status)) {
    MsdcWrite (MSDC_IOCON, SavedIocon);
    MsdcWrite (mPadTuneReg, SavedPad);
    return Status;
  }

  mCmdTuneDone = TRUE;
  DEBUG ((DEBUG_INFO, "MSDC0: CMD response tune rspl=%u delay=%u (window %u)\n",
    BestRspl, BestStart + BestRun / 2, BestRun));
  return EFI_SUCCESS;
}

STATIC VOID MsdcTuneRead (VOID)
{
  UINT32   Dspl;
  UINT32   Delay;
  UINT32   Run;
  UINT32   Start = 0;
  UINT32   BestRun = 0;
  UINT32   BestStart = 0;
  UINT32   BestDspl = 0;
  BOOLEAN  Ok;
  UINT32   SavedIocon = MsdcRead (MSDC_IOCON);
  UINT32   SavedPad   = MsdcRead (mPadTuneReg);

  mTuneDone = TRUE;

  for (Dspl = 0; Dspl < 2; Dspl++) {
    Run = 0;
    for (Delay = 0; Delay < 32; Delay++) {
      MsdcSetReadDelay (Dspl, Delay);
      Ok = (BOOLEAN)(!EFI_ERROR (MsdcDataTransfer (17, 0, FALSE, 1, mTuneBuf)) &&
                     !EFI_ERROR (MsdcDataTransfer (17, 0, FALSE, 1, mTuneBuf)));
      if (Ok) {
        if (Run == 0) {
          Start = Delay;
        }
        Run++;
      }
      if ((!Ok || Delay == 31) && Run > BestRun) {
        BestRun   = Run;
        BestStart = Start;
        BestDspl  = Dspl;
      }
      if (!Ok) {
        Run = 0;
      }
    }
  }

  if (BestRun == 0) {
    DEBUG ((DEBUG_ERROR, "MSDC0: read tuning found no window\n"));
    MsdcWrite (MSDC_IOCON, SavedIocon);
    MsdcWrite (mPadTuneReg, SavedPad);
    return;
  }
  MsdcSetReadDelay (BestDspl, BestStart + BestRun / 2);
  DEBUG ((DEBUG_INFO, "MSDC0: read tune dspl=%u delay=%u (window %u)\n", BestDspl, BestStart + BestRun / 2, BestRun));
}

STATIC EFI_STATUS MsdcTransfer (
  IN BOOLEAN  IsWrite,
  IN EFI_LBA  Lba,
  IN UINTN    Length,
  IN UINT8    *Buffer
  )
{
  UINT32      Remaining = (UINT32)(Length / MSDC_BLOCK_SIZE);
  UINT32      Argument;
  UINT32      FirstOpcode = 0;
  UINT32      Chunk;
  UINT32      Opcode;
  BOOLEAN     AutoStop;
  BOOLEAN     LastWasMulti = FALSE;
  BOOLEAN     StopCompleted = FALSE;
  BOOLEAN     MoreChunks;
  UINT32      Attempt;
  EFI_STATUS  Status = EFI_SUCCESS;

  if (mPendingValid) {
    Argument      = mPendingArg;
    FirstOpcode   = mPendingOpcode;
  } else {
    Argument      = (UINT32)Lba;
  }
  mPendingValid  = FALSE;
  mStopHandled   = FALSE;

  while (Remaining != 0) {
    Chunk = (Remaining > mBounceBlocks) ? mBounceBlocks : Remaining;

    if (FirstOpcode == 8) {
      Opcode = 8;
      Chunk  = 1;
    } else if (Chunk == 1) {
      Opcode = IsWrite ? 24 : 17;
    } else {
      Opcode = IsWrite ? 25 : 18;
    }

    AutoStop      = (BOOLEAN)(mPredefBlocks != 0 && mPredefBlocks == Chunk);
    mPredefBlocks = 0;
    MoreChunks    = (BOOLEAN)(Remaining > Chunk);
    StopCompleted = FALSE;

    for (Attempt = 0; Attempt < 2; Attempt++) {
      Status = MsdcDataTransfer (Opcode, Argument, IsWrite, Chunk, Buffer);

      if (!EFI_ERROR (Status) && Chunk > 1) {
        if (AutoStop) {
          // CMD23 predefined block count terminates this transfer on-card.
          StopCompleted = TRUE;
        } else if (MoreChunks) {
          // Only intermediate chunks need a driver-issued STOP so the next
          // chunk can start. The final STOP belongs to MmcDxe's CMD12 callback.
          EFI_STATUS StopStatus = MsdcIssueCommand (12, 0, 0, FALSE);
          if (EFI_ERROR (StopStatus)) {
            DEBUG ((DEBUG_WARN,
              "MSDC0: intermediate CMD12 failed: %r STS=%08x INT=%08x CMDINT=%08x\n",
              StopStatus, MsdcRead (SDC_STS), MsdcRead (MSDC_INT), mLastCmdInt));
            mStopHandled = FALSE;
            return StopStatus;
          }
          StopCompleted = TRUE;
        }
        // Final non-CMD23 multi-block transfer: leave mStopHandled FALSE so
        // MmcDxe issues exactly one real CMD12 through MsdcSendCommand().
      }

      if (!EFI_ERROR (Status) || IsWrite || mTuneDone || (Status != EFI_CRC_ERROR && Status != EFI_TIMEOUT)) {
        break;
      }
      DEBUG ((DEBUG_WARN, "MSDC0: read error %r, tuning sampling\n", Status));
      MsdcTuneRead ();
      if (mCurrentClockHz > 25000000U) {
        EFI_STATUS ClockStatus;

        DEBUG ((DEBUG_WARN, "MSDC0: retry read at 25MHz after tuning\n"));
        ClockStatus = MsdcSetClock (25000000U);
        if (EFI_ERROR (ClockStatus)) {
          Status = ClockStatus;
          break;
        }
      }
    }
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MSDC0: %a CMD%u arg=%08x blocks=%u failed: %r\n",
        IsWrite ? "write" : "read", Opcode, Argument, Chunk, Status));
      mStopHandled = FALSE;
      return Status;
    }

    LastWasMulti = (BOOLEAN)(Chunk > 1);
    FirstOpcode  = 0;
    Argument    += Chunk;
    Buffer      += (UINTN)Chunk * MSDC_BLOCK_SIZE;
    Remaining   -= Chunk;
  }

  // Suppress MmcDxe's follow-up CMD12 only when this driver really stopped
  // the stream (or CMD23 terminated it). On a failed internal STOP, leave the
  // host callback live so it can issue a real CMD12 instead of a fake response.
  mStopHandled = (BOOLEAN)(LastWasMulti && StopCompleted);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MsdcSendCommand (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN MMC_CMD                MmcCmd,
  IN UINT32                 Argument
  )
{
  UINT32      Opcode = MmcCmd & SDC_CMD_OPC;
  EFI_STATUS  Status;

  (VOID)This;

  DEBUG ((DEBUG_VERBOSE, "MSDC0: CMD%u ARG=%08x\n", Opcode, Argument));

  if (Opcode == 12 && mStopHandled) {
    mStopHandled   = FALSE;
    mUseCachedResp = TRUE;
    ZeroMem (mCachedResp, sizeof (mCachedResp));
    mCachedResp[0] = MSDC_FAKE_R1_READY;
    return EFI_SUCCESS;
  }
  mStopHandled = FALSE;

  //
  // Data commands cannot be started here: the DMA buffer and length are only
  // known in Read/WriteBlockData(). Remember the command and run it there.
  //
  if (MsdcCommandHasData (Opcode, Argument)) {
    mPendingValid  = TRUE;
    mPendingOpcode = Opcode;
    mPendingArg    = Argument;
    mUseCachedResp = TRUE;
    ZeroMem (mCachedResp, sizeof (mCachedResp));
    mCachedResp[0] = MSDC_FAKE_R1_READY;
    return EFI_SUCCESS;
  }

  mPendingValid = FALSE;
  if (Opcode == 23) {
    mPredefBlocks = Argument & 0xFFFF;
  }

  Status = MsdcIssueCommand (Opcode, Argument, 0, FALSE);

  if (Opcode == 16 && Status == EFI_CRC_ERROR && !mCmdTuneDone &&
      (mLastCmdInt & MSDC_INT_RSPCRCERR) != 0) {
    DEBUG ((DEBUG_WARN, "MSDC0: CMD16 response CRC, tuning CMD path (INT=%08x PAD=%08x IOCON=%08x)\n",
      mLastCmdInt, MsdcRead (mPadTuneReg), MsdcRead (MSDC_IOCON)));
    Status = MsdcTuneCommand (Argument);
  }

  if (Opcode == 16 && EFI_ERROR (Status) && mCurrentClockHz > 25000000U) {
    DEBUG ((DEBUG_WARN, "MSDC0: CMD16 retry at 25MHz (status=%r INT=%08x)\n",
      Status, mLastCmdInt));
    Status = MsdcSetClock (25000000U);
    if (!EFI_ERROR (Status)) {
      Status = MsdcIssueCommand (Opcode, Argument, 0, FALSE);
    }
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MSDC0: CMD%u arg=%08x failed: %r POST_STS=%08x POST_INT=%08x CMDINT=%08x PRE_STS=%08x PRE_PS=%08x PRE_CMD=%08x PRE_ARG=%08x PRE_DMA_CFG=%08x PRE_DMA_CTRL=%08x\n",
      Opcode, Argument, Status, MsdcRead (SDC_STS), MsdcRead (MSDC_INT),
      mLastCmdInt, mLastCmdSts, mLastCmdPs, mLastCmdReg, mLastCmdArg,
      mLastCmdDmaCfg, mLastCmdDmaCtrl));
  }
  return Status;
}

STATIC EFI_STATUS EFIAPI MsdcReceiveResponse (
  IN  EFI_MMC_HOST_PROTOCOL  *This,
  IN  MMC_RESPONSE_TYPE      Type,
  OUT UINT32                 *Buffer
  )
{
  (VOID)This;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (mUseCachedResp) {
    if (Type == MMC_RESPONSE_TYPE_R2) {
      CopyMem (Buffer, mCachedResp, sizeof (mCachedResp));
    } else {
      Buffer[0] = mCachedResp[0];
    }
    return EFI_SUCCESS;
  }

  if (Type == MMC_RESPONSE_TYPE_R2) {
    // EDK2 expects the least significant word first
    Buffer[0] = MsdcRead (SDC_RESP0);
    Buffer[1] = MsdcRead (SDC_RESP1);
    Buffer[2] = MsdcRead (SDC_RESP2);
    Buffer[3] = MsdcRead (SDC_RESP3);
  } else {
    Buffer[0] = MsdcRead (SDC_RESP0);
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MsdcReadBlockData (
  IN  EFI_MMC_HOST_PROTOCOL  *This,
  IN  EFI_LBA                Lba,
  IN  UINTN                  Length,
  OUT UINT32                 *Buffer
  )
{
  (VOID)This;

  if (Buffer == NULL || Length == 0 || (Length % MSDC_BLOCK_SIZE) != 0) {
    return EFI_INVALID_PARAMETER;
  }
  DEBUG ((DEBUG_VERBOSE, "MSDC0: Read LBA=%Lu Len=%Lu\n", (UINT64)Lba, (UINT64)Length));
  return MsdcTransfer (FALSE, Lba, Length, (UINT8 *)Buffer);
}

STATIC EFI_STATUS EFIAPI MsdcWriteBlockData (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN EFI_LBA                Lba,
  IN UINTN                  Length,
  IN UINT32                 *Buffer
  )
{
  (VOID)This;

  if (Buffer == NULL || Length == 0 || (Length % MSDC_BLOCK_SIZE) != 0) {
    return EFI_INVALID_PARAMETER;
  }
  DEBUG ((DEBUG_VERBOSE, "MSDC0: Write LBA=%Lu Len=%Lu\n", (UINT64)Lba, (UINT64)Length));
  return MsdcTransfer (TRUE, Lba, Length, (UINT8 *)Buffer);
}

STATIC EFI_STATUS EFIAPI MsdcSetIos (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN UINT32                 BusClockFreq,
  IN UINT32                 BusWidth,
  IN UINT32                 TimingMode
  )
{
  (VOID)This;

  DEBUG ((DEBUG_INFO, "MSDC0: SetIos freq=%u width=%u timing=%u\n", BusClockFreq, BusWidth, TimingMode));

  // Keep this host in the legacy/SDR modes supported by the DMA path.
  // The comboA PIO reference explicitly rejects these timing modes before
  // touching SDC_CFG or the clock. Merely warning and continuing leaves the
  // card/host potentially in different DDR/HS200/HS400 states.
  if ((TimingMode == EMMCHS52DDR1V2) ||
      (TimingMode == EMMCHS52DDR1V8) ||
      (TimingMode == EMMCHS200SDR1V8) ||
      (TimingMode == EMMCHS200SDR1V2) ||
      (TimingMode == EMMCHS400DDR1V8) ||
      (TimingMode == EMMCHS400DDR1V2)) {
    DEBUG ((DEBUG_WARN, "MSDC0: rejecting unsupported timing %u (SDR DMA only)\n", TimingMode));
    return EFI_UNSUPPORTED;
  }

  if ((BusWidth != 1) && (BusWidth != 4) && (BusWidth != 8)) {
    DEBUG ((DEBUG_ERROR, "MSDC0: unsupported bus width %u\n", BusWidth));
    return EFI_UNSUPPORTED;
  }

  MsdcSetBusWidth (BusWidth);

  if (BusClockFreq != 0) {
    return MsdcSetClock (BusClockFreq);
  }
  return EFI_SUCCESS;
}

STATIC BOOLEAN EFIAPI MsdcIsCardPresent (IN EFI_MMC_HOST_PROTOCOL *This)
{
  (VOID)This;
  return TRUE;
}

STATIC BOOLEAN EFIAPI MsdcIsReadOnly (IN EFI_MMC_HOST_PROTOCOL *This)
{
  (VOID)This;
  return FALSE;
}

STATIC BOOLEAN EFIAPI MsdcIsMultiBlock (IN EFI_MMC_HOST_PROTOCOL *This)
{
  (VOID)This;
  return TRUE;
}

STATIC EFI_STATUS EFIAPI MsdcBuildDevicePath (
  IN  EFI_MMC_HOST_PROTOCOL   *This,
  OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath
  )
{
  VENDOR_DEVICE_PATH *VendorNode;

  (VOID)This;
  if (DevicePath == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  VendorNode = (VENDOR_DEVICE_PATH *)CreateDeviceNode (HARDWARE_DEVICE_PATH, HW_VENDOR_DP, sizeof (VENDOR_DEVICE_PATH));
  if (VendorNode == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (&VendorNode->Guid, &gEfiCallerIdGuid, sizeof (EFI_GUID));
  *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)VendorNode;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI MsdcNotifyState (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN MMC_STATE              State
  )
{
  (VOID)This;
  (VOID)State;
  return EFI_SUCCESS;
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

EFI_STATUS EFIAPI Msdc0EntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_HANDLE            Handle = NULL;
  EFI_PHYSICAL_ADDRESS  Addr;
  UINTN                 Pages;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  // Descriptors: one page below 4GB (GPD0 @0x00, GPD1 @0x40, BDs @0x80)
  Addr   = 0xFFFFFFFFULL;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, 1, &Addr);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  mDescPhys = Addr;
  ZeroMem ((VOID *)(UINTN)mDescPhys, EFI_PAGE_SIZE);
  mDmaGpd = (MT_GPDMA_DESC *)(UINTN)mDescPhys;
  mDmaBd  = (MT_BDMA_DESC *)(UINTN)(mDescPhys + MSDC_DESC_BD_OFF);

  // Bounce buffer below 4GB: the controller only gets 32-bit addresses
  for (Pages = MSDC_BOUNCE_PAGES; Pages >= 8; Pages >>= 1) {
    Addr   = 0xFFFFFFFFULL;
    Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, Pages, &Addr);
    if (!EFI_ERROR (Status)) {
      break;
    }
  }
  if (EFI_ERROR (Status)) {
    gBS->FreePages (mDescPhys, 1);
    return Status;
  }
  mBouncePhys   = Addr;
  mBounce       = (UINT8 *)(UINTN)mBouncePhys;
  mBounceBlocks = (UINT32)((Pages * EFI_PAGE_SIZE) / MSDC_BLOCK_SIZE);
  mTuneDone = FALSE;
  mCmdTuneDone = FALSE;
  mCmdTuneActive = FALSE;
  mLastCmdInt = 0;
  mCurrentClockHz = 0;
  if (mBounceBlocks > (MSDC_BD_MAX_LEN * MSDC_BD_MAX_NUM) / MSDC_BLOCK_SIZE) {
    mBounceBlocks = (MSDC_BD_MAX_LEN * MSDC_BD_MAX_NUM) / MSDC_BLOCK_SIZE;
  }

  Status = MsdcInitHw ();
  if (EFI_ERROR (Status)) {
    gBS->FreePages (mBouncePhys, Pages);
    gBS->FreePages (mDescPhys, 1);
    return Status;
  }

  MsdcSetBusWidth (1);

  DEBUG ((DEBUG_INFO, "MSDC0: init done, desc=%08x bounce=%08x blocks=%u\n",
    (UINT32)mDescPhys, (UINT32)mBouncePhys, mBounceBlocks));

  return gBS->InstallMultipleProtocolInterfaces (&Handle, &gEmbeddedMmcHostProtocolGuid, &mMsdcHost, NULL);
}
