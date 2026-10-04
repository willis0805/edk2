/** @file
  Mock-host regression tests for SD switch status and command dispatch.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Include the production implementation to exercise its private entry points.
// The standalone runner discards unrelated identification functions at link time.
#include "../MmcIdentification.c"

STATIC MMC_CMD  mLastCommand;
STATIC UINT32   mLastArgument;
STATIC UINT32   mClock;
STATIC UINT32   mBusWidth;
STATIC UINT32   mSwitchCount;
STATIC UINT32   mBusWidthCount;
STATIC UINT32   mSetIosCount;
STATIC UINT8    mSupportByte13;
STATIC UINT8    mSupportByte14;
STATIC UINT8    mSelectionByte16;
STATIC UINT8    mSelectionByte19;
STATIC UINT8    mScrBusWidths;
STATIC BOOLEAN mCccSwitch;
STATIC BOOLEAN mExpectTag;
STATIC UINT32  mFailSendSwitch;
STATIC UINT32  mFailReadSwitch;
STATIC BOOLEAN mFailSetIos;
STATIC UINT32  mDeviceState;
STATIC UINT32  mStateAfterRead;
STATIC BOOLEAN mExtCsdRead;
STATIC UINTN   mDelays;
STATIC UINTN   mAllocations;
STATIC UINT64  mCounterTicks;
STATIC UINT64  mCounterOrigin;
STATIC BOOLEAN mCounterDown;
STATIC BOOLEAN mProbeMode;
STATIC UINTN   mProbeCommands;
STATIC UINTN   mCommandCost;

UINT64 EFIAPI
GetPerformanceCounter (VOID)
{
  return (mCounterDown ? mCounterOrigin - mCounterTicks : mCounterOrigin + mCounterTicks) & MAX_UINT32;
}

UINT64 EFIAPI
GetPerformanceCounterProperties (OUT UINT64 *Start, OUT UINT64 *End)
{
  *Start = mCounterDown ? MAX_UINT32 : 0;
  *End   = mCounterDown ? 0 : MAX_UINT32;
  return 1000000;
}

UINT64 EFIAPI
GetTimeInNanoSecond (IN UINT64 Ticks)
{
  return Ticks * 1000;
}

EFI_STATUS
MmcNotifyState (IN MMC_HOST_INSTANCE *Instance, IN MMC_STATE State)
{
  Instance->State = State;
  return EFI_SUCCESS;
}

VOID PrintRCA (IN UINT32 Value) { (void)Value; }
VOID PrintOCR (IN UINT32 Value) { (void)Value; }
VOID PrintResponseR1 (IN UINT32 Value) { (void)Value; }
VOID PrintCID (IN UINT32 *Value) { (void)Value; }


UINTN EFIAPI
MicroSecondDelay (IN UINTN Microseconds)
{
  mDelays += Microseconds;
  mCounterTicks += Microseconds;
  return Microseconds;
}

VOID *EFIAPI
AllocatePages (IN UINTN Pages)
{
  mAllocations++;
  return calloc (Pages, EFI_PAGE_SIZE);
}

VOID EFIAPI
FreePages (IN VOID *Buffer, IN UINTN Pages)
{
  (void)Pages;
  mAllocations--;
  free (Buffer);
}


VOID *EFIAPI
CopyMem (
  OUT VOID       *Destination,
  IN  CONST VOID *Source,
  IN  UINTN      Length
  )
{
  return memcpy (Destination, Source, Length);
}

UINT64 EFIAPI
MultU64x32 (
  IN UINT64 Multiplicand,
  IN UINT32 Multiplier
  )
{
  return Multiplicand * Multiplier;
}

VOID
PrintCSD (
  IN UINT32 *Csd
  )
{
  (void)Csd;
}

STATIC BOOLEAN EFIAPI
TestIsReadOnly (
  IN EFI_MMC_HOST_PROTOCOL *This
  )
{
  (void)This;
  return FALSE;
}

STATIC EFI_STATUS EFIAPI
TestSendCommand (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_CMD               Cmd,
  IN UINT32                Argument
  )
{
  (void)This;
  if (mProbeMode) {
    mCounterTicks += mCommandCost;
    mProbeCommands++;
    return Cmd == MMC_CMD0 ? EFI_SUCCESS : EFI_TIMEOUT;
  }
  mLastCommand  = Cmd;
  mLastArgument = Argument;
  if ((MMC_GET_INDX (Cmd) == 6) && ((Argument & 0x00FFFFF0) == 0x00FFFFF0)) {
    assert (Cmd == (mExpectTag ? SD_CMD6 : MMC_CMD6));
    mSwitchCount++;
    assert (Argument == ((mSwitchCount == 1) ? 0x00FFFFF0 : 0x80FFFFF1));
    if (mSwitchCount == mFailSendSwitch) {
      return EFI_TIMEOUT;
    }
  } else if ((MMC_GET_INDX (Cmd) == 6) && (Argument == 2)) {
    assert (Cmd == MMC_CMD6);
    mBusWidthCount++;
  } else if (MMC_GET_INDX (Cmd) == 6) {
    assert (Cmd == MMC_CMD6);
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
TestReceiveResponse (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN MMC_RESPONSE_TYPE     Type,
  IN UINT32                *Buffer
  )
{
  (void)This;
  (void)Type;
  Buffer[0] = 0;
  if (mLastCommand == MMC_CMD9) {
    memset (Buffer, 0, 4 * sizeof (*Buffer));
    Buffer[2] = (9 << 16) | (mCccSwitch ? (SD_CCC_SWITCH << 20) : 0);
  } else if (mLastCommand == MMC_CMD13) {
    Buffer[0] = (mExtCsdRead ? mStateAfterRead : mDeviceState) << 9;
  } else if (mLastCommand == MMC_CMD2) {
    memset (Buffer, 0, 4 * sizeof (*Buffer));
  } else {
    assert (mLastCommand == MMC_CMD55);
    Buffer[0] = MMC_STATUS_APP_CMD;
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
TestReadBlockData (
  IN  EFI_MMC_HOST_PROTOCOL *This,
  IN  EFI_LBA               Lba,
  IN  UINTN                 Length,
  OUT UINT32                *Buffer
  )
{
  UINT8 *Bytes;

  (void)This;
  assert (Lba == 0);
  memset (Buffer, 0, Length);
  Bytes = (UINT8 *)Buffer;
  if (mLastCommand == MMC_ACMD51) {
    assert (Length == 8);
    Bytes[0] = 2;
    Bytes[1] = mScrBusWidths;
  } else if (mLastCommand == MMC_CMD8) {
    assert (Length == 512);
    ((ECSD *)Buffer)->SECTOR_COUNT = 1024;
    mExtCsdRead = TRUE;
  } else {
    assert (MMC_GET_INDX (mLastCommand) == 6);
    assert (Length == SWITCH_CMD_DATA_LENGTH);
    if (mSwitchCount == mFailReadSwitch) {
      return EFI_CRC_ERROR;
    }

    Bytes[13] = mSupportByte13;
    Bytes[14] = mSupportByte14;
    Bytes[16] = mSelectionByte16;
    Bytes[19] = mSelectionByte19;
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
TestSetIos (
  IN EFI_MMC_HOST_PROTOCOL *This,
  IN UINT32                BusClockFreq,
  IN UINT32                BusWidth,
  IN UINT32                TimingMode
  )
{
  (void)This;
  assert (TimingMode == EMMCBACKWARD);
  mClock    = BusClockFreq;
  mBusWidth = BusWidth;
  mSetIosCount++;
  return mFailSetIos ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC EFI_MMC_HOST_PROTOCOL mHost;
STATIC EFI_BLOCK_IO_MEDIA    mMedia;
STATIC MMC_HOST_INSTANCE     mInstance;

STATIC VOID
ResetTest (
  IN BOOLEAN TaggedCommands
  )
{
  memset (&mHost, 0, sizeof (mHost));
  memset (&mMedia, 0, sizeof (mMedia));
  memset (&mInstance, 0, sizeof (mInstance));
  mHost.Revision        = TaggedCommands ? MMC_HOST_PROTOCOL_REVISION_SD_CMD : MMC_HOST_PROTOCOL_REVISION;
  mHost.IsReadOnly      = TestIsReadOnly;
  mHost.SendCommand     = TestSendCommand;
  mHost.ReceiveResponse = TestReceiveResponse;
  mHost.ReadBlockData   = TestReadBlockData;
  mHost.SetIos          = TestSetIos;
  mInstance.MmcHost     = &mHost;
  mInstance.BlockIo.Media = &mMedia;
  mInstance.CardInfo.CardType = SD_CARD_2_HIGH;
  mInstance.CardInfo.RCA = 1;
  mLastCommand          = 0;
  mLastArgument         = 0;
  mClock                = 0;
  mBusWidth             = 0;
  mSwitchCount          = 0;
  mBusWidthCount        = 0;
  mSetIosCount          = 0;
  mSupportByte13        = 2;
  mSupportByte14        = 0;
  mSelectionByte16      = 1;
  mSelectionByte19      = 0;
  mScrBusWidths         = SD_BUS_WIDTH_1BIT | SD_BUS_WIDTH_4BIT;
  mCccSwitch            = TRUE;
  mExpectTag            = TaggedCommands;
  mFailSendSwitch       = 0;
  mFailReadSwitch       = 0;
  mFailSetIos           = FALSE;
  mDeviceState          = EMMC_TRAN_STATE;
  mStateAfterRead       = EMMC_TRAN_STATE;
  mExtCsdRead           = FALSE;
  mDelays               = 0;
  assert (mAllocations == 0);
  mCounterTicks = 0;
  mCounterOrigin = 0;
  mCounterDown = FALSE;
  mProbeMode = FALSE;
  mProbeCommands = 0;
  mCommandCost = 0;
}

int
main (void)
{
  UINT32 Index;

  assert (MMC_GET_INDX (SD_CMD6) == 6);
  assert ((SD_CMD6 & ~SD_CMD) == MMC_CMD6);

  // New hosts receive the explicit SD tag; old hosts retain the original ABI.
  for (Index = 0; Index < 2; Index++) {
    ResetTest ((BOOLEAN)Index);
    assert (InitializeSdMmcDevice (&mInstance) == EFI_SUCCESS);
    assert (mSwitchCount == 2);
    assert (mBusWidthCount == 1);
    assert (mClock == SD_HIGH_SPEED);
    assert (mBusWidth == BUSWIDTH_4);
    assert (mMedia.BlockSize == 512);
  }

  // A bit in the adjacent byte must not be mistaken for high-speed support.
  ResetTest (TRUE);
  mSupportByte13 = 0;
  mSupportByte14 = 2;
  assert (InitializeSdMmcDevice (&mInstance) == EFI_SUCCESS);
  assert (mSwitchCount == 1);
  assert (mClock == SD_DEFAULT_SPEED);

  // Only the low nibble of byte 16 encodes the selected function in group 1.
  ResetTest (TRUE);
  mSelectionByte16 = 0xA1;
  assert (InitializeSdMmcDevice (&mInstance) == EFI_SUCCESS);
  assert (mClock == SD_HIGH_SPEED);
  for (Index = 0; Index < 16; Index++) {
    if (Index == 1) {
      continue;
    }

    ResetTest (TRUE);
    mSelectionByte16 = (UINT8)Index;
    mSelectionByte19 = 1;
    assert (InitializeSdMmcDevice (&mInstance) == EFI_DEVICE_ERROR);
    assert (mSetIosCount == 0);
  }

  // Cards without switch support stay at default speed.
  ResetTest (TRUE);
  mCccSwitch = FALSE;
  assert (InitializeSdMmcDevice (&mInstance) == EFI_SUCCESS);
  assert (mSwitchCount == 0);
  assert (mClock == SD_DEFAULT_SPEED);

  // A one-bit-only card must not receive ACMD6 or a four-bit host setting.
  ResetTest (TRUE);
  mScrBusWidths = SD_BUS_WIDTH_1BIT;
  assert (InitializeSdMmcDevice (&mInstance) == EFI_SUCCESS);
  assert (mBusWidthCount == 0);
  assert (mBusWidth == 1);

  for (Index = 1; Index <= 2; Index++) {
    ResetTest (TRUE);
    mFailSendSwitch = Index;
    assert (InitializeSdMmcDevice (&mInstance) == EFI_TIMEOUT);
    assert (mSetIosCount == 0);
    ResetTest (TRUE);
    mFailReadSwitch = Index;
    assert (InitializeSdMmcDevice (&mInstance) == EFI_CRC_ERROR);
    assert (mSetIosCount == 0);
  }

  ResetTest (TRUE);
  mFailSetIos = TRUE;
  assert (InitializeSdMmcDevice (&mInstance) == EFI_DEVICE_ERROR);

  // The eMMC switch operation must remain untagged, even with a new host.
  ResetTest (TRUE);
  assert (EmmcSetEXTCSD (&mInstance, EXTCSD_HS_TIMING, EMMC_TIMING_HS) == EFI_SUCCESS);
  assert (mSwitchCount == 0);
  assert (mLastCommand == MMC_CMD13);
  ResetTest (TRUE);
  mDeviceState = EMMC_PRG_STATE;
  assert (EmmcSetEXTCSD (&mInstance, EXTCSD_HS_TIMING, EMMC_TIMING_HS) == EFI_TIMEOUT);
  assert (mDelays == MAX_RETRY_COUNT * 1000);

  ResetTest (TRUE);
  mStateAfterRead = EMMC_DATA_STATE;
  assert (EmmcIdentificationMode (&mInstance, (OCR_RESPONSE){ 0 }) == EFI_TIMEOUT);
  assert (mDelays == MAX_RETRY_COUNT * 1000);
  assert (mAllocations == 0);
  // Slow absent-card failures cannot multiply a per-command timeout by 1000.
  ResetTest (TRUE);
  mProbeMode = TRUE;
  mCommandCost = 1000000;
  mInstance.State = MmcHwInitializationState;
  assert (MmcIdentificationMode (&mInstance) == EFI_TIMEOUT);
  assert (mProbeCommands == 5);
  assert (mCounterTicks <= 6000000);

  // Timer arithmetic also handles down counters and a wrap in either direction.
  for (Index = 0; Index < 2; Index++) {
    MMC_IDENTIFICATION_TIMER Timer;
    ResetTest (TRUE);
    mCounterDown = (BOOLEAN)Index;
    mCounterOrigin = mCounterDown ? 2000000 : MAX_UINT32 - 2000000;
    GetPerformanceCounterProperties (&Timer.CounterStart, &Timer.CounterEnd);
    Timer.InitialCount = GetPerformanceCounter ();
    mCounterTicks = 4999999;
    assert (!MmcIdentificationTimedOut (&Timer));
    mCounterTicks = 5000000;
    assert (MmcIdentificationTimedOut (&Timer));
  }
  puts ("PASS: five-second aggregate no-card budget and up/down counter rollover");
  puts ("PASS: SD CMD6 tagging, legacy compatibility, wire-order status, rejection,");
  puts ("      one-bit fallback, error propagation, untagged eMMC switching, and bounded eMMC busy states");
  return 0;
}
