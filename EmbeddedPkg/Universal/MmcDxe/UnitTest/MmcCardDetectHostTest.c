/** @file
  Test truthful media state and one-shot probing per card-presence transition.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Mmc.c"

STATIC BOOLEAN mCardPresent;
STATIC EFI_STATUS mInitStatus;
STATIC UINTN mInitCalls;
STATIC UINTN mReinstallCalls;
STATIC EFI_BOOT_SERVICES mBootServices;
EFI_BOOT_SERVICES *gBS = &mBootServices;
EFI_GUID gEfiBlockIoProtocolGuid = { 0 };

EFI_STATUS
InitializeMmcDevice (IN MMC_HOST_INSTANCE *Instance)
{
  mInitCalls++;
  // Identification can publish provisional geometry before a later failure.
  Instance->BlockIo.Media->MediaPresent = TRUE;
  return mInitStatus;
}

UINTN EFIAPI
Print (IN CONST CHAR16 *Format, ...)
{
  return 0;
}

STATIC BOOLEAN EFIAPI
TestIsCardPresent (IN EFI_MMC_HOST_PROTOCOL *Host)
{
  return mCardPresent;
}

STATIC EFI_STATUS EFIAPI
TestReinstall (IN EFI_HANDLE Handle, IN EFI_GUID *Protocol, IN VOID *Old, IN VOID *New)
{
  assert (Protocol == &gEfiBlockIoProtocolGuid && Old == New);
  mReinstallCalls++;
  return EFI_SUCCESS;
}

int
main (void)
{
  MMC_HOST_INSTANCE Instance = { 0 };
  EFI_MMC_HOST_PROTOCOL Host = { 0 };
  EFI_BLOCK_IO_MEDIA Media = { 0 };
  UINTN Index;

  Instance.Signature = MMC_HOST_INSTANCE_SIGNATURE;
  Instance.MmcHost = &Host;
  Instance.BlockIo.Media = &Media;
  Host.IsCardPresent = TestIsCardPresent;
  mBootServices.ReinstallProtocolInterface = TestReinstall;
  mMmcHostPool.ForwardLink = &Instance.Link;
  mMmcHostPool.BackLink = &Instance.Link;
  Instance.Link.ForwardLink = &mMmcHostPool;
  Instance.Link.BackLink = &mMmcHostPool;

  // Fixed-media/broken-card-detect hosts report present. Failed identification
  // is attempted once for this presence observation, not every 200 ms tick.
  mCardPresent = TRUE;
  mInitStatus = EFI_TIMEOUT;
  CheckCardsCallback (NULL, NULL);
  assert (Instance.Initialized && !Media.MediaPresent);
  assert (Instance.State == MmcHwInitializationState);
  assert (mInitCalls == 1 && mReinstallCalls == 1);
  for (Index = 0; Index < 100; Index++) {
    CheckCardsCallback (NULL, NULL);
  }
  assert (mInitCalls == 1 && mReinstallCalls == 1 && !Media.MediaPresent);

  // Reliable removable-media hosts can retry after an actual removal/insertion.
  mCardPresent = FALSE;
  CheckCardsCallback (NULL, NULL);
  assert (!Instance.Initialized && !Media.MediaPresent && mReinstallCalls == 2);
  mCardPresent = TRUE;
  mInitStatus = EFI_SUCCESS;
  CheckCardsCallback (NULL, NULL);
  assert (Instance.Initialized && Media.MediaPresent);
  assert (mInitCalls == 2 && mReinstallCalls == 3);
  CheckCardsCallback (NULL, NULL);
  assert (mInitCalls == 2);
  mCardPresent = FALSE;
  CheckCardsCallback (NULL, NULL);
  assert (!Instance.Initialized && !Media.MediaPresent);
  puts ("PASS: failed identification stays no-media without timer retry storms;");
  puts ("      removal/reinsertion permits one new probe and successful media publication");
  return 0;
}
