#include "EFIDroidUi.h"

lkapi_t *mLKApi = NULL;

MENU_OPTION                 *mBootMenuMain = NULL;
MENU_OPTION                 *mPowerMenu = NULL;
EFI_DEVICE_PATH_TO_TEXT_PROTOCOL   *gEfiDevicePathToTextProtocol = NULL;
EFI_DEVICE_PATH_FROM_TEXT_PROTOCOL *gEfiDevicePathFromTextProtocol = NULL;

STATIC MENU_ENTRY *FileExplorerEntry = NULL;
STATIC EFI_GUID mUefiShellFileGuid = {0x7C04A583, 0x9E3E, 0x4f1c, {0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 }};

EFI_STATUS
BootOptionEfiOption (
  IN MENU_ENTRY* This
)
{
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOption = This->Private;

  EfiBootManagerBoot(BootOption);
  if(EFI_ERROR(BootOption->Status)) {
    CHAR8 Buf[100];
    AsciiSPrint(Buf, 100, "%r", BootOption->Status);
    MenuShowMessage("Error", Buf);
  }

  return BootOption->Status;
}

STATIC VOID
AddEfiBootOptions (
  VOID
)
{
  UINTN                         Index;
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOption;
  UINTN                         BootOptionCount;
  BOOLEAN                       First = TRUE;
  MENU_ENTRY                    *Entry;
  EFI_DEVICE_PATH*              DevicePathNode;

  BootOption = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);

  for (Index = 0; Index < BootOptionCount; Index++) {
    //
    // Don't display the hidden/inactive boot option
    //
    if (((BootOption[Index].Attributes & LOAD_OPTION_HIDDEN) != 0) || ((BootOption[Index].Attributes & LOAD_OPTION_ACTIVE) == 0)) {
      continue;
    }

    if (First) {
      // GROUP: UEFI
      Entry = MenuCreateGroupEntry();
      Entry->Name = AsciiStrDup("UEFI");
      MenuAddEntry(mBootMenuMain, Entry);
      First = FALSE;
    }

    Entry = MenuCreateEntry();
    if(Entry == NULL) {
      break;
    }

    CONST CHAR8* IconPath = "icons/uefi.png";

    DevicePathNode = BootOption[Index].FilePath;
    while ((DevicePathNode != NULL) && !IsDevicePathEnd (DevicePathNode)) {

      // detect shell
      if (IS_DEVICE_PATH_NODE (DevicePathNode, MEDIA_DEVICE_PATH, MEDIA_PIWG_FW_FILE_DP)) {
        CONST MEDIA_FW_VOL_FILEPATH_DEVICE_PATH* FvDevicePathNode =  ((CONST MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)DevicePathNode);
        if (FvDevicePathNode != NULL && CompareGuid (&FvDevicePathNode->FvFileName, &mUefiShellFileGuid)) {
          IconPath = "icons/efi_shell.png";
          break;
        }
      }

      // next
      DevicePathNode     = NextDevicePathNode (DevicePathNode);
    }

    Entry->Icon = libaroma_stream_ramdisk(IconPath);
    Entry->Name = Unicode2Ascii(BootOption[Index].Description);
    Entry->Callback = BootOptionEfiOption;
    Entry->Private = &BootOption[Index];
    Entry->ResetGop = TRUE;
    MenuAddEntry(mBootMenuMain, Entry);
  }
}

#if defined (MDE_CPU_ARM)
EFI_STATUS
FastbootCallback (
  IN MENU_ENTRY* This
)
{
  FastbootInit();
  return EFI_SUCCESS;
}
#endif

EFI_STATUS
RebootCallback (
  IN MENU_ENTRY* This
)
{
  CHAR16* Reason = This->Private;
  UINTN Len = Reason?StrLen(Reason):0;

  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, Len, Reason);

  return EFI_DEVICE_ERROR;
}

EFI_STATUS
PowerOffCallback (
  IN MENU_ENTRY* This
)
{
  gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
  return EFI_DEVICE_ERROR;
}

EFI_STATUS
RebootLongPressCallback (
  IN MENU_ENTRY* This
)
{
  MenuShowSelectionDialog(mPowerMenu);
  return EFI_SUCCESS;
}

EFI_STATUS
PowerMenuBackCallback (
  MENU_OPTION* This
)
{
  return EFI_ABORTED;
}

EFI_STATUS
MainMenuActionCallback (
  MENU_OPTION* This
)
{
  return SettingsMenuShow();
}

EFI_STATUS
MainMenuUpdateUi (
  VOID
)
{
  if (FileExplorerEntry) {
    FileExplorerEntry->Hidden = !SettingBoolGet("ui-show-file-explorer");
  }

  InvalidateMenu(mBootMenuMain);

  return EFI_SUCCESS;
}

INT32
main (
  IN INT32  Argc,
  IN CHAR8  **Argv
  )
{
  EFI_STATUS                          Status;
  MENU_ENTRY                          *Entry;

  // init libboot
  libboot_init();

  Status = gBS->LocateProtocol (
                  &gEfiDevicePathToTextProtocolGuid,
                  NULL,
                  (VOID **)&gEfiDevicePathToTextProtocol
                  );
  if (EFI_ERROR (Status)) {
    return -1;
  }

  Status = gBS->LocateProtocol (
                  &gEfiDevicePathFromTextProtocolGuid,
                  NULL,
                  (VOID **)&gEfiDevicePathFromTextProtocol
                  );
  if (EFI_ERROR (Status)) {
    return -1;
  }

  // set default values
  if(!UtilVariableExists(L"multiboot-debuglevel", &gEFIDroidVariableGuid))
    UtilSetEFIDroidVariable("multiboot-debuglevel", "4");
  if(!UtilVariableExists(L"fastboot-enable-boot-patch", &gEFIDroidVariableGuid))
    UtilSetEFIDroidVariable("fastboot-enable-boot-patch", "0");
  if(!UtilVariableExists(L"ui-show-file-explorer", &gEFIDroidVariableGuid))
    SettingBoolSet("ui-show-file-explorer", TRUE);

  // create menus
  mBootMenuMain = MenuCreate();

  mPowerMenu = MenuCreate();
  mPowerMenu->BackCallback = PowerMenuBackCallback;
  mPowerMenu->HideBackIcon = TRUE;

  mBootMenuMain->Title = AsciiStrDup("Please Select OS");
  mBootMenuMain->ActionIcon = libaroma_stream_ramdisk("icons/ic_settings_black_24dp.png");
  mBootMenuMain->ActionCallback = MainMenuActionCallback;
  mBootMenuMain->ItemFlags = MENU_ITEM_FLAG_SEPARATOR_ALIGN_TEXT;

#if defined (MDE_CPU_ARM)
  // add android options
  AndroidLocatorInit();
  AndroidLocatorAddItems();
#endif

  // add default EFI options
  AddEfiBootOptions();

  // GROUP: Tools
  Entry = MenuCreateGroupEntry();
  Entry->Name = AsciiStrDup("Tools");
  MenuAddEntry(mBootMenuMain, Entry);

  // add file explorer option
  Entry = MenuCreateEntry();
  Entry->Icon = libaroma_stream_ramdisk("icons/fileexplorer.png");
  Entry->Name = AsciiStrDup("File Explorer");
  Entry->Callback = FileExplorerCallback;
  Entry->HideBootMessage = TRUE;
  Entry->Hidden = !SettingBoolGet("ui-show-file-explorer");
  MenuAddEntry(mBootMenuMain, Entry);
  FileExplorerEntry = Entry;

#if defined (MDE_CPU_ARM)
  FastbootCommandsAdd();

  // add fastboot option
  Entry = MenuCreateEntry();
  Entry->Icon = libaroma_stream_ramdisk("icons/android.png");
  Entry->Name = AsciiStrDup("Fastboot");
  Entry->Callback = FastbootCallback;
  Entry->HideBootMessage = TRUE;
  MenuAddEntry(mBootMenuMain, Entry);
#endif

  // add reboot option
  Entry = MenuCreateEntry();
  Entry->Icon = libaroma_stream_ramdisk("icons/reboot.png");
  Entry->Name = AsciiStrDup("Reboot");
  Entry->Callback = RebootCallback;
  Entry->Private = NULL;
  Entry->LongPressCallback = RebootLongPressCallback;
  MenuAddEntry(mBootMenuMain, Entry);

  Entry = MenuCreateEntry();
  Entry->Icon = libaroma_stream_ramdisk("icons/power_off.png");
  Entry->Name = AsciiStrDup("Power Off");
  Entry->Callback = PowerOffCallback;
  MenuAddEntry(mPowerMenu, Entry);

  Entry = MenuCreateEntry();
  Entry->Icon = libaroma_stream_ramdisk("icons/reboot.png");
  Entry->Name = AsciiStrDup("Reboot");
  Entry->Callback = RebootCallback;
  Entry->Private = NULL;
  MenuAddEntry(mPowerMenu, Entry);

  Entry = MenuCreateEntry();
  Entry->Icon = libaroma_stream_ramdisk("icons/reboot_recovery.png");
  Entry->Name = AsciiStrDup("Reboot to Recovery");
  Entry->Callback = RebootCallback;
  Entry->Private = UnicodeStrDup(L"recovery");
  MenuAddEntry(mPowerMenu, Entry);

  Entry = MenuCreateEntry();
  Entry->Icon = libaroma_stream_ramdisk("icons/reboot_bootloader.png");
  Entry->Name = AsciiStrDup("Reboot to Bootloader");
  Entry->Callback = RebootCallback;
  Entry->Private = UnicodeStrDup(L"bootloader");
  MenuAddEntry(mPowerMenu, Entry);

  Entry = MenuCreateEntry();
  Entry->Icon = libaroma_stream_ramdisk("icons/download_mode.png");
  Entry->Name = AsciiStrDup("Enter Download Mode");
  Entry->Callback = RebootCallback;
  Entry->Private = UnicodeStrDup(L"download");
  MenuAddEntry(mPowerMenu, Entry);

  MenuInit();

  // show previous boot error
  CHAR8* EFIDroidErrorStr = UtilGetEFIDroidVariable("EFIDroidErrorStr");
  if (EFIDroidErrorStr != NULL) {
    MenuShowMessage("Previous boot failed", EFIDroidErrorStr);

    // delete variable
    UtilSetEFIDroidVariable("EFIDroidErrorStr", NULL);

    // backup variable
    UtilSetEFIDroidVariable("EFIDroidErrorStrPrev", EFIDroidErrorStr);

    // free pool
    FreePool(EFIDroidErrorStr);
  }

  // get last boot entry
  LAST_BOOT_ENTRY* LastBootEntry = UtilGetEFIDroidDataVariable(L"LastBootEntry");
  if(LastBootEntry)
   UtilSetEFIDroidDataVariable(L"LastBootEntry", NULL, 0);

  // run recovery mode handler
  mLKApi = GetLKApi();
  if (mLKApi && mLKApi->platform_get_uefi_bootmode()==LKAPI_UEFI_BM_RECOVERY) {
    AndroidLocatorHandleRecoveryMode(LastBootEntry);
  }

  // free last boot entry
  if(LastBootEntry)
    FreePool(LastBootEntry);

  // clear the watchdog timer
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  // show main menu
  MenuStackPush(mBootMenuMain);
  MenuEnter (0, TRUE);
  MenuDeInit();

  return 0;
}
