/** @file
  Regression coverage for single-block fallback and block I/O failure handling.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../MmcBlockIo.c"

STATIC BOOLEAN mMultiBlock;
STATIC UINT32 mLastCommand;
STATIC UINT32 mTransferCount;
STATIC UINT32 mStopCount;
STATIC UINT32 mStatusCount;
STATIC UINT32 mFailTransfer;
STATIC UINT32 mCardStatus;
STATIC UINT32 mCardStatusAfterTransfer;
STATIC EFI_STATUS mSendStatus;
STATIC EFI_STATUS mResponseStatus;
STATIC EFI_STATUS mStopStatus;
STATIC UINTN mDelays;
STATIC UINTN mExpectedLength;
STATIC EFI_LBA mExpectedLba;
STATIC VOID *mExpectedBuffer;

UINT64 EFIAPI
MultU64x32 (IN UINT64 Multiplicand, IN UINT32 Multiplier)
{
  return Multiplicand * Multiplier;
}

UINTN EFIAPI
MicroSecondDelay (IN UINTN Microseconds)
{
  mDelays += Microseconds;
  return Microseconds;
}

STATIC BOOLEAN EFIAPI
TestIsMultiBlock (IN EFI_MMC_HOST_PROTOCOL *This)
{
  return mMultiBlock;
}

STATIC EFI_STATUS EFIAPI
TestNotifyState (IN EFI_MMC_HOST_PROTOCOL *This, IN MMC_STATE State)
{
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
TestSendCommand (IN EFI_MMC_HOST_PROTOCOL *This, IN MMC_CMD Cmd, IN UINT32 Argument)
{
  mLastCommand = Cmd;
  if (Cmd == MMC_CMD13) {
    mStatusCount++;
    return mSendStatus;
  }

  if (Cmd == MMC_CMD12) {
    mStopCount++;
    return mStopStatus;
  }

  if (mMultiBlock && mExpectedLength > 512) {
    assert (Cmd == MMC_CMD18 || Cmd == MMC_CMD25);
  } else {
    assert (Cmd == MMC_CMD17 || Cmd == MMC_CMD24);
  }

  assert (Argument == mExpectedLba);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
TestReceiveResponse (IN EFI_MMC_HOST_PROTOCOL *This, IN MMC_RESPONSE_TYPE Type, IN UINT32 *Buffer)
{
  Buffer[0] = mTransferCount == 0 ? mCardStatus : mCardStatusAfterTransfer;
  return mResponseStatus;
}

STATIC EFI_STATUS EFIAPI
TestBlockData (IN EFI_MMC_HOST_PROTOCOL *This, IN EFI_LBA Lba, IN UINTN Length, IN UINT32 *Buffer)
{
  assert (Lba == mExpectedLba);
  assert (Length == mExpectedLength);
  assert (Buffer == mExpectedBuffer);
  mTransferCount++;
  mExpectedLba += Length / 512;
  mExpectedBuffer = (UINT8 *)Buffer + Length;
  return mTransferCount == mFailTransfer ? EFI_CRC_ERROR : EFI_SUCCESS;
}

STATIC EFI_MMC_HOST_PROTOCOL mHost;
STATIC EFI_BLOCK_IO_MEDIA mMedia;
STATIC MMC_HOST_INSTANCE mInstance;
STATIC UINT32 mBuffer[512];

STATIC VOID
ResetTest (VOID)
{
  memset (&mHost, 0, sizeof (mHost));
  memset (&mMedia, 0, sizeof (mMedia));
  memset (&mInstance, 0, sizeof (mInstance));
  mHost.Revision        = MMC_HOST_PROTOCOL_REVISION_SD_CMD;
  mHost.NotifyState     = TestNotifyState;
  mHost.IsMultiBlock    = TestIsMultiBlock;
  mHost.SendCommand     = TestSendCommand;
  mHost.ReceiveResponse = TestReceiveResponse;
  mHost.ReadBlockData   = TestBlockData;
  mHost.WriteBlockData  = TestBlockData;
  mMedia.BlockSize      = 512;
  mMedia.MediaId        = 1;
  mMedia.MediaPresent   = TRUE;
  mMedia.LastBlock      = 1023;
  mInstance.Signature   = MMC_HOST_INSTANCE_SIGNATURE;
  mInstance.MmcHost     = &mHost;
  mInstance.BlockIo.Media = &mMedia;
  mInstance.CardInfo.CardType = EMMC_CARD;
  mInstance.CardInfo.OCRData.AccessMode = MMC_OCR_ACCESS_SECTOR;
  mInstance.CardInfo.RCA = 1;
  mMultiBlock          = FALSE;
  mTransferCount       = 0;
  mStopCount           = 0;
  mStatusCount         = 0;
  mFailTransfer        = 0;
  mCardStatus          = MMC_R0_READY_FOR_DATA | (MMC_R0_STATE_TRAN << 9);
  mCardStatusAfterTransfer = mCardStatus;
  mSendStatus          = EFI_SUCCESS;
  mResponseStatus      = EFI_SUCCESS;
  mStopStatus          = EFI_SUCCESS;
  mDelays              = 0;
  mExpectedLength      = 512;
  mExpectedLba         = 3;
  mExpectedBuffer      = mBuffer;
}

int
main (void)
{
  UINT32 Transfer;
  UINT32 BusyStates[] = { 0, MMC_R0_READY_FOR_DATA, MMC_R0_STATE_TRAN << 9 };
  UINT32 Index;

  for (Transfer = 0; Transfer <= 1; Transfer++) {
    ResetTest ();
    assert (MmcIoBlocks (&mInstance.BlockIo, Transfer, 1, 3, 2048, mBuffer) == EFI_SUCCESS);
    assert (mTransferCount == 4 && mStopCount == 0);
    assert (mExpectedLba == 7);

    ResetTest ();
    mFailTransfer = 2;
    assert (MmcIoBlocks (&mInstance.BlockIo, Transfer, 1, 3, 2048, mBuffer) == EFI_CRC_ERROR);
    assert (mTransferCount == 2 && mStopCount == 1);

    ResetTest ();
    mMultiBlock = TRUE;
    mExpectedLength = 2048;
    assert (MmcIoBlocks (&mInstance.BlockIo, Transfer, 1, 3, 2048, mBuffer) == EFI_SUCCESS);
    assert (mTransferCount == 1 && mStopCount == 1);
  }

  ResetTest ();
  mMultiBlock = TRUE;
  mExpectedLength = 2048;
  mStopStatus = EFI_TIMEOUT;
  assert (MmcIoBlocks (&mInstance.BlockIo, 0, 1, 3, 2048, mBuffer) == EFI_TIMEOUT);

  for (Index = 0; Index < ARRAY_SIZE (BusyStates); Index++) {
    ResetTest ();
    mCardStatus = BusyStates[Index];
    assert (MmcIoBlocks (&mInstance.BlockIo, 0, 1, 3, 512, mBuffer) == EFI_NOT_READY);
    assert (mTransferCount == 0 && mStatusCount == 20 && mDelays == 20000);
  }

  ResetTest ();
  mCardStatusAfterTransfer = 0;
  assert (MmcIoBlocks (&mInstance.BlockIo, 0, 1, 3, 512, mBuffer) == EFI_TIMEOUT);
  assert (mDelays == MMCI0_TIMEOUT * 1000);
  ResetTest ();
  mSendStatus = EFI_TIMEOUT;
  assert (MmcIoBlocks (&mInstance.BlockIo, 0, 1, 3, 512, mBuffer) == EFI_TIMEOUT);
  assert (mTransferCount == 0);
  ResetTest ();
  mResponseStatus = EFI_DEVICE_ERROR;
  assert (MmcIoBlocks (&mInstance.BlockIo, 0, 1, 3, 512, mBuffer) == EFI_DEVICE_ERROR);
  assert (mTransferCount == 0);
  ResetTest ();
  assert (MmcIoBlocks (&mInstance.BlockIo, 0, 1, MAX_UINT64, 1024, mBuffer) == EFI_INVALID_PARAMETER);
  assert (mTransferCount == 0);
  puts ("PASS: single-block fallback, multi-block STOP order, transfer error propagation,");
  puts ("      ready-state timeouts, response errors, and overflow-safe LBA validation");
  return 0;
}
