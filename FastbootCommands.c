#include "EFIDroidUi.h"
#include <Protocol/DevicePath.h>
#include <Protocol/RamDisk.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LKDisplay.h>
#include <IndustryStandard/PeImage.h>
#include <Library/DevicePathLib.h>

#define SIDELOAD_FILENAME L"Sideload.efi"

STATIC VOID
CommandRebootInternal (
  CONST CHAR16 *Reason
)
{
  FastbootOkay("");
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, Reason?StrLen(Reason):0, (CHAR16*)Reason);
}

STATIC VOID
CommandReboot (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  CommandRebootInternal(NULL);
}

STATIC VOID
CommandRebootRecovery (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  CommandRebootInternal(L"recovery");
}

STATIC VOID
CommandRebootBootloader (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  CommandRebootInternal(L"bootloader");
}

STATIC VOID
CommandRebootDownload (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  CommandRebootInternal(L"download");
}

STATIC VOID
CommandPowerOff (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  FastbootInfo("You have 5s to unplug your USB cable :)");
  FastbootOkay("");
  gBS->Stall(5*1000000);
  gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

STATIC
BOOLEAN
IsEfiFile (
  VOID* Buffer
)
{
  EFI_IMAGE_DOS_HEADER                *DosHdr;
  EFI_IMAGE_OPTIONAL_HEADER_PTR_UNION Hdr;

  DosHdr = (EFI_IMAGE_DOS_HEADER *)Buffer;

  if (DosHdr->e_magic == EFI_IMAGE_DOS_SIGNATURE) {
    //
    // Valid DOS header so get address of PE header
    //
    Hdr.Pe32 = (EFI_IMAGE_NT_HEADERS32 *)(((CHAR8 *)DosHdr) + DosHdr->e_lfanew);
  } else {
    //
    // No Dos header so assume image starts with PE header.
    //
    Hdr.Pe32 = (EFI_IMAGE_NT_HEADERS32 *)Buffer;
  }

  if (Hdr.Pe32->Signature != EFI_IMAGE_NT_SIGNATURE) {
    //
    // Not a valid PE image so Exit
    //
    return FALSE;
  }

  return TRUE;
}

STATIC VOID
CommandBoot (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  EFI_RAM_DISK_PROTOCOL     *RamDiskProtocol;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  boot_img_hdr_t            *AndroidHdr;
  UINT64                    DataAddr;
  EFI_STATUS                Status;
  EFI_GUID                  DiskGuid = gEfiVirtualDiskGuid;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *Volume;
  EFI_FILE_PROTOCOL                 *Root;
  EFI_FILE_PROTOCOL                 *EfiFile;
  UINTN                     WaitIndex;
  EFI_INPUT_KEY             Key;

  AndroidHdr = Data;
  DataAddr = (UINT64)(UINTN)Data;
  DevicePath = NULL;

  // verify Android header
  Status = AndroidVerify(AndroidHdr);
  if (EFI_ERROR(Status)) {
    FastbootFail("Not a boot image");
    return;
  }

  // calculate offsets
  UINTN KernelOffset  = AndroidHdr->page_size;

  // EFI file without a ramdisk
  if(IsEfiFile((VOID*)(UINTN)(DataAddr+KernelOffset))) {
    // get ramdisk protocol
    Status = gBS->LocateProtocol (&gEfiRamDiskProtocolGuid, NULL, (VOID **) &RamDiskProtocol);
    if (EFI_ERROR (Status)) {
      FastbootFail("Can't locate ramdisk protocol");
      return;
    }

    // Allocate ramdisk
    UINT64 RamDiskSize = MAX(AndroidHdr->kernel_size + SIZE_1MB, SIZE_1MB);
    VOID* RamDisk = AllocatePool(RamDiskSize);
    if (RamDisk==NULL) {
      FastbootFail("Can't allocate ramdisk memory");
      return;
    }

    // mkfs.vfat
    Status = MakeDosFs(RamDisk, RamDiskSize);
    if (EFI_ERROR (Status)) {
      FastbootFail("Can't format ramdisk to FAT");
      goto ERROR_FREE_RAMDISK;
    }

    // register ramdisk
    Status = RamDiskProtocol->Register((UINTN)RamDisk, RamDiskSize, &DiskGuid, NULL, &DevicePath);
    if (EFI_ERROR (Status)) {
      FastbootFail("Can't register ramdisk");
      goto ERROR_FREE_RAMDISK;
    }

    // get handle
    EFI_DEVICE_PATH_PROTOCOL* Protocol = DevicePath;
    EFI_HANDLE FSHandle;
    Status = gBS->LocateDevicePath (
                    &gEfiSimpleFileSystemProtocolGuid,
                    &Protocol,
                    &FSHandle
                    );
    if (EFI_ERROR (Status)) {
      FastbootFail("Can't get FS handle");
      goto ERROR_UNREGISTER_RAMDISK;
    }

    // get the SimpleFilesystem protocol on that handle
    Volume = NULL;
    Status = gBS->HandleProtocol (
                    FSHandle,
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID **)&Volume
                    );
    if (EFI_ERROR (Status)) {
      FastbootFail("Can't get FS protocol");
      goto ERROR_UNREGISTER_RAMDISK;
    }

    // Open the root directory of the volume
    Root = NULL;
    Status = Volume->OpenVolume (
                       Volume,
                       &Root
                       );
    if (EFI_ERROR (Status) || Root==NULL) {
      FastbootFail("Can't open volume");
      goto ERROR_UNREGISTER_RAMDISK;
    }

    // Create EFI file
    EfiFile = NULL;
    Status = Root->Open (
                     Root,
                     &EfiFile,
                     SIDELOAD_FILENAME,
                     EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,
                     0
                     );
    if (EFI_ERROR (Status)) {
      FastbootFail("Can't create file");
      goto ERROR_UNREGISTER_RAMDISK;
    }

    // write kernel to efi file
    UINTN WriteSize = AndroidHdr->kernel_size;
    Status = EfiFile->Write(EfiFile, &WriteSize, (VOID*)(UINTN)(DataAddr+KernelOffset));
    if (EFI_ERROR (Status)) {
      FastbootFail("Can't write to file");
      goto ERROR_UNREGISTER_RAMDISK;
    }

    // build device path
    EFI_DEVICE_PATH_PROTOCOL *LoaderDevicePath;
    LoaderDevicePath = FileDevicePath(FSHandle, L"Sideload.efi");
    if (LoaderDevicePath==NULL) {
      FastbootFail("Can't build file device path");
      goto ERROR_UNREGISTER_RAMDISK;
    }

    // build arguments
    CONST CHAR16* Args = L"";
    UINTN LoadOptionsSize = (UINT32)StrSize (Args);
    VOID *LoadOptions     = AllocatePool (LoadOptionsSize);
    StrCpy (LoadOptions, Args);

    // send OKAY
    FastbootOkay("");

    // stop fastboot
    FastbootStopNow();

    // shut down menu
    MenuPreBoot();

    // start efi application
    Status = UtilStartEfiApplication (LoaderDevicePath, LoadOptionsSize, LoadOptions);

    // wait for user input
    Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &WaitIndex);
    if(!EFI_ERROR(Status)) {
        gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    }

    // restart menu
    MenuPostBoot();

    // start fastboot
    FastbootInit();

ERROR_UNREGISTER_RAMDISK:
    // unregister ramdisk
    Status = RamDiskProtocol->Unregister(DevicePath);
    if (EFI_ERROR (Status)) {
      ASSERT(FALSE);
    }

ERROR_FREE_RAMDISK:
    // free ramdisk memory
    FreePool(RamDisk);
  }

  // Android
  else {
    // send OKAY
    FastbootOkay("");
    FastbootStopNow();

    BOOLEAN DisablePatching = TRUE;
    CHAR8* Var = UtilGetEFIDroidVariable("fastboot-enable-boot-patch");
    if (Var && !AsciiStrCmp(Var, "1")) {
      DisablePatching = FALSE;
      FreePool(Var);
    }

    // boot Android
    AndroidBootFromBuffer(Data, Size, NULL, DisablePatching, NULL);
  }
}

#if 0
STATIC EFI_TEXT_STRING mOutputStringOrig = NULL;
STATIC EFI_TEXT_STRING mErrOutputStringOrig = NULL;
STATIC CHAR8 mOutputBuffer[FASTBOOT_COMMAND_MAX_LENGTH];
STATIC UINTN mOutputBufferPos = 0;

STATIC
EFI_STATUS
EFIAPI
OutputStringHook (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
  IN CHAR16                          *String
  )
{
  CHAR8* AsciiString = Unicode2Ascii(String);
  if(AsciiString) {
    UINTN i;
    for(i=0; i<AsciiStrLen(AsciiString); i++) {
      CHAR8 c = AsciiString[i];

      mOutputBuffer[mOutputBufferPos++] = c;

      if(mOutputBufferPos==sizeof(mOutputBuffer)-1-4 || c=='\n' || c=='\r') {
        mOutputBuffer[mOutputBufferPos] = 0;
        FastbootInfo(mOutputBuffer);
        mOutputBufferPos = 0;
      }
    }

    FreePool(AsciiString);
  }

  return EFI_SUCCESS;
}

STATIC VOID
CommandShell (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  EFI_STATUS Status;
  EFI_STATUS CommandStatus;
  CHAR8 Buffer[59];

  // convert to unicode
  CHAR16* UnicodeCommand = Ascii2Unicode(Arg);
  if(UnicodeCommand==NULL) {
    FastbootFail("Memory error");
    return;
  }

  // initialize console hook
  mOutputBufferPos = 0;
  mOutputStringOrig = gST->ConOut->OutputString;
  mErrOutputStringOrig = gST->StdErr->OutputString;
  gST->ConOut->OutputString = OutputStringHook;
  gST->StdErr->OutputString = OutputStringHook;

  // run shell command
  Status = ShellExecute (&gImageHandle, UnicodeCommand, FALSE, NULL, &CommandStatus);

  // flush output buffer
  if(mOutputBufferPos>0)
      FastbootInfo(mOutputBuffer);

  // restore console
  gST->StdErr->OutputString = mErrOutputStringOrig;
  gST->ConOut->OutputString = mOutputStringOrig;

  // print status
  if(EFI_ERROR(Status)) {
    AsciiSPrint(Buffer, 59, "Error: %r", Status);
    FastbootFail(Buffer);
  }
  else {
    FastbootOkay("");
  }
}
#endif

STATIC VOID
CommandDisplayInfo (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  CHAR8                                Buffer[59];
  EFI_GRAPHICS_OUTPUT_PROTOCOL         *Gop;
  EFI_LK_DISPLAY_PROTOCOL              *LKDisplay;
  UINT32                               ModeIndex;
  UINTN                                MaxMode;
  UINTN                                SizeOfInfo;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  EFI_STATUS                           Status;

  // get graphics protocol
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);
  if (EFI_ERROR (Status)) {
    AsciiSPrint(Buffer, 59, "Error: %r", Status);
    FastbootFail(Buffer);
    return;
  }

  MaxMode = Gop->Mode->MaxMode;

  AsciiSPrint(Buffer, 59, "%u mode%a", MaxMode, MaxMode>1?"s":"");
  FastbootInfo(Buffer);

  AsciiSPrint(Buffer, 59, "buffer: 0x%08x-0x%08x",
    (UINTN)Gop->Mode->FrameBufferBase,
    (UINTN)Gop->Mode->FrameBufferBase+Gop->Mode->FrameBufferSize
  );
  FastbootInfo(Buffer);

  // get LKDisplay protocol
  Status = gBS->LocateProtocol (&gEfiLKDisplayProtocolGuid, NULL, (VOID **) &LKDisplay);
  if (!EFI_ERROR (Status)) {
    AsciiSPrint(Buffer, 59, "density: %u", LKDisplay->GetDensity(LKDisplay));
    FastbootInfo(Buffer);

    LK_DISPLAY_FLUSH_MODE FlushMode = LKDisplay->GetFlushMode(LKDisplay);
    if (FlushMode==LK_DISPLAY_FLUSH_MODE_AUTO)
      AsciiSPrint(Buffer, 59, "flush_mode: auto");
    else if (FlushMode==LK_DISPLAY_FLUSH_MODE_MANUAL)
      AsciiSPrint(Buffer, 59, "flush_mode: manual");
    else
      AsciiSPrint(Buffer, 59, "flush_mode: invalid");
    FastbootInfo(Buffer);

    AsciiSPrint(Buffer, 59, "portrait mode: %u", LKDisplay->GetPortraitMode());
    FastbootInfo(Buffer);

    AsciiSPrint(Buffer, 59, "landscape mode: %u", LKDisplay->GetLandscapeMode());
    FastbootInfo(Buffer);
  }

  for (ModeIndex = 0; ModeIndex < MaxMode; ModeIndex++) {
    Status = Gop->QueryMode (
                       Gop,
                       ModeIndex,
                       &SizeOfInfo,
                       &Info
                       );
    if (!EFI_ERROR (Status)) {

      AsciiSPrint(Buffer, 59, "mode %u:%a", ModeIndex, ModeIndex==Gop->Mode->Mode?" (active)":"");
      FastbootInfo(Buffer);

      AsciiSPrint(Buffer, 59, "  res: %ux%u", Info->HorizontalResolution, Info->VerticalResolution);
      FastbootInfo(Buffer);

      AsciiSPrint(Buffer, 59, "  scanline: %upx", Info->PixelsPerScanLine);
      FastbootInfo(Buffer);

      if (Info->PixelFormat==PixelRedGreenBlueReserved8BitPerColor)
        AsciiSPrint(Buffer, 59, "  format: RGB888");
      else if (Info->PixelFormat==PixelBlueGreenRedReserved8BitPerColor)
        AsciiSPrint(Buffer, 59, "  format: BGR888");
      else if (Info->PixelFormat==PixelBitMask) {
        AsciiSPrint(Buffer, 59, "  format: mask %08x/%08x/%08x",
          Info->PixelInformation.RedMask,
          Info->PixelInformation.GreenMask,
          Info->PixelInformation.BlueMask
        );
      }
      else if (Info->PixelFormat==PixelBltOnly)
        AsciiSPrint(Buffer, 59, "  format: unknown");
      else
        AsciiSPrint(Buffer, 59, "  format: invalid");


      FastbootInfo(Buffer);

      FreePool (Info);
    }
  }

  FastbootOkay("");
}

STATIC VOID
CommandExit (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  FastbootOkay("");
  FastbootRequestStop();
}

STATIC
VOID
CommandScreenShot (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  SCREENSHOT *ScreenShot;
  CHAR8 Response[128];
  UINTN Index;

  // show list of screenshots
  if(Arg[0]==0) {
    FastbootInfo("available screenshots:");

    for (ScreenShot = gScreenShotList,Index=0; ScreenShot; ScreenShot = ScreenShot->Next,Index++) {
      AsciiSPrint(Response, sizeof(Response), "\t%d: %u bytes", Index, ScreenShot->Len);
      FastbootInfo(Response);
    }

    FastbootOkay("");
  }

  else {
    UINT8 ScreenShotIndex = atoi (Arg);
    for (ScreenShot = gScreenShotList,Index=0; ScreenShot; ScreenShot = ScreenShot->Next,Index++) {
      if(Index!=ScreenShotIndex)
        continue;

      FastbootSendBuf(ScreenShot->Data, ScreenShot->Len);
      FastbootOkay("");

      return;
    }

    FastbootFail("screenshot not found");
  }
}

STATIC
RETURN_STATUS
EFIAPI
IterateVariablesCallbackPrint (
  IN  VOID                         *Context,
  IN  CHAR16                       *VariableName,
  IN  EFI_GUID                     *VendorGuid,
  IN  UINT32                       Attributes,
  IN  UINTN                        DataSize,
  IN  VOID                         *Data
  )
{
  EFI_STATUS          Status;
  CONST CHAR16        *Arg;

  Status = EFI_SUCCESS;
  Arg = Context;

  // show our variables only
  if (!CompareGuid(VendorGuid, &gEFIDroidVariableGuid))
    return Status;

  // show specific variable only
  if (Arg && StrCmp(VariableName, Arg))
    return Status;

  UINTN BufSize = StrLen(VariableName) + DataSize + 10 + 1;
  CHAR8 *Buf = AllocatePool(BufSize);
  if (Buf == NULL)
    return EFI_OUT_OF_RESOURCES;

  AsciiSPrint(Buf, BufSize, "%s: %a\n", VariableName, (CONST CHAR8*)Data);
  FastbootSendString(Buf, AsciiStrLen(Buf));

  FreePool(Buf);

  return Status;
}

STATIC
VOID
CommandGetNvVar (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  EFI_STATUS Status;
  CHAR8      Buf[100];
  CHAR16     *Arg16;

  Arg16 = NULL;
  if(AsciiStrLen(Arg)>0) {
    Arg16 = Ascii2Unicode(Arg);
    ASSERT(Arg16);
  }

  Status = UtilIterateVariables(IterateVariablesCallbackPrint, Arg16);

  if (Arg16)
    FreePool(Arg16);

  if(EFI_ERROR(Status)) {
    AsciiSPrint(Buf, sizeof(Buf), "%r", Status);
    FastbootFail(Buf);
  }
  else {
    FastbootOkay("");
  }
}

STATIC
VOID
CommandSetNvVar (
  CHAR8 *Arg,
  VOID *Data,
  UINT32 Size
)
{
  EFI_STATUS  Status;
  CHAR8       Buf[100];
  CONST CHAR8 *Name;
  CONST CHAR8 *Value;
  CHAR8       *Ptr;

  Status = EFI_SUCCESS;

  Name = Arg;
  Value = NULL;
  for (Ptr=Arg; *Ptr; Ptr++) {
    if(Ptr[0]==' ') {
      Ptr[0] = '\0';
      Value = &Ptr[1];
      break;
    }
  }

  Status = UtilSetEFIDroidVariable(Name, Value);
  if(EFI_ERROR(Status)) {
    AsciiSPrint(Buf, sizeof(Buf), "%r", Status);
    FastbootFail(Buf);
  }
  else {
    FastbootOkay("");
  }
}

VOID
FastbootCommandsAdd (
  VOID
)
{
  FastbootRegister("reboot", CommandReboot);
  FastbootRegister("reboot-bootloader", CommandRebootBootloader);
  FastbootRegister("oem reboot-recovery", CommandRebootRecovery);
  FastbootRegister("oem reboot-download", CommandRebootDownload);
  FastbootRegister("oem poweroff", CommandPowerOff);
  FastbootRegister("boot", CommandBoot);

  //FastbootRegister("oem shell", CommandShell);
  FastbootRegister("oem displayinfo", CommandDisplayInfo);
  FastbootRegister("oem exit", CommandExit);
  FastbootRegister("oem screenshot", CommandScreenShot);
  FastbootRegister("oem getnvvar", CommandGetNvVar);
  FastbootRegister("oem setnvvar", CommandSetNvVar);
}
