Unicode true
!ifdef ISOLATED_TEST_BUILD
RequestExecutionLevel user
!else
RequestExecutionLevel admin
!endif
SetCompressor /SOLID lzma
SetCompressorDictSize 32
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!define STARTUP_RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define TEST_STARTUP_RUN_KEY "Software\UshaLabs\Downshift.PackagingTest\Run"
!define STARTUP_VALUE "UshaLabs.Downshift"

Name "Downshift ${VERSION}"
Caption "Downshift ${VERSION} Setup - UshaLabs"
OutFile "${SETUP_OUTPUT}"
InstallDir "$PROGRAMFILES64\Downshift"
InstallDirRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift" "InstallLocation"
BrandingText "Developed by UshaLabs"
Icon "${PROJECT_ROOT}\assets\downshift.ico"
UninstallIcon "${PROJECT_ROOT}\assets\downshift.ico"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Downshift"
VIAddVersionKey "CompanyName" "UshaLabs"
VIAddVersionKey "FileDescription" "Downshift Windows Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026 UshaLabs and Downshift contributors"
ShowInstDetails show
ShowUninstDetails show
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Downshift by UshaLabs"
!define MUI_WELCOMEPAGE_TEXT "Developed by UshaLabs.$\r$\n$\r$\nThis setup installs Downshift. The app downloads and verifies its video tools automatically when first opened.$\r$\n$\r$\nYou can set up the included Chrome extension while the video tools download.$\r$\n$\r$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PROJECT_ROOT}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT "Downshift will open when you sign in to Windows. You can change this in Settings > Apps > Startup.$\r$\n$\r$\nOpen Downshift to download its video tools and set up Chrome.$\r$\n$\r$\nThis local preview is unsigned. Downloads and history are kept when you uninstall."
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var Isolated
Var RegistryKey
Var StartupKey
Var Shortcut
Var DesktopShortcut
Var Arguments
Var Result
Var Output

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Downshift requires 64-bit Windows."
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "Downshift requires Windows 10 or newer."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  StrCpy $Isolated "0"
  ${GetParameters} $Arguments
  ClearErrors
  ${GetOptions} $Arguments "/ISOLATEDTEST" $Result
  ${IfNot} ${Errors}
    StrCpy $Isolated "1"
    SetShellVarContext current
  ${EndIf}
!ifdef ISOLATED_TEST_BUILD
  ${If} $Isolated != "1"
    Abort "This private verification build requires /ISOLATEDTEST."
  ${EndIf}
!endif
FunctionEnd

Section "Downshift" Main
  SetOutPath "$INSTDIR"
  SetOverwrite on
  !include "${INSTALL_FILES}"
  nsExec::ExecToLog '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\Open-ChromeSetup.ps1" -PrepareOnly'
  Pop $Result
  ${If} $Result != "0"
    SetErrorLevel 1
    Abort "Chrome setup instructions could not be prepared."
  ${EndIf}
  ${If} $Isolated == "1"
    StrCpy $RegistryKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift.PackagingTest"
    StrCpy $StartupKey "${TEST_STARTUP_RUN_KEY}"
    StrCpy $Shortcut "$INSTDIR\Downshift-test.lnk"
    CreateDirectory "$INSTDIR\Desktop"
    StrCpy $DesktopShortcut "$INSTDIR\Desktop\Downshift.lnk"
    StrCpy $Arguments "-IsolatedTest -MachineInstall"
  ${Else}
    StrCpy $RegistryKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift"
    StrCpy $StartupKey "${STARTUP_RUN_KEY}"
    CreateDirectory "$SMPROGRAMS\Downshift"
    StrCpy $Shortcut "$SMPROGRAMS\Downshift\Downshift.lnk"
    StrCpy $DesktopShortcut "$DESKTOP\Downshift.lnk"
    CreateShortCut "$SMPROGRAMS\Downshift\Chrome setup.lnk" "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" '-NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\scripts\Open-ChromeSetup.ps1"' "$INSTDIR\Downshift.exe"
    StrCpy $Arguments "-MachineInstall"
  ${EndIf}
  nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\Install-Portable.ps1" -ShortcutPath "$Shortcut" -AdditionalShortcutPath "$DesktopShortcut" $Arguments'
  Pop $Result
  Pop $Output
  DetailPrint $Output
  ${If} $Result != "0"
    SetErrorLevel 1
    Abort "Downshift integration could not be installed."
  ${EndIf}
  WriteINIStr "$INSTDIR\downshift-setup.ini" "Setup" "IsolatedTest" "$Isolated"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr SHCTX "$RegistryKey" "DisplayName" "Downshift"
  WriteRegStr SHCTX "$RegistryKey" "DisplayVersion" "${VERSION}"
  WriteRegStr SHCTX "$RegistryKey" "Publisher" "UshaLabs"
  WriteRegStr SHCTX "$RegistryKey" "InstallLocation" "$INSTDIR"
  WriteRegStr SHCTX "$RegistryKey" "DisplayIcon" "$INSTDIR\Downshift.exe"
  WriteRegStr SHCTX "$RegistryKey" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr SHCTX "$RegistryKey" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegDWORD SHCTX "$RegistryKey" "NoModify" 1
  WriteRegDWORD SHCTX "$RegistryKey" "NoRepair" 1
  ReadRegStr $Result SHCTX "$StartupKey" "${STARTUP_VALUE}"
  ${If} $Result == ""
    ClearErrors
    WriteRegStr SHCTX "$StartupKey" "${STARTUP_VALUE}" '$\"$INSTDIR\Downshift.exe$\"'
    ${If} ${Errors}
      SetErrorLevel 1
      Abort "Downshift is installed, but Windows sign-in launch could not be registered. Run setup again to retry."
    ${EndIf}
  ${EndIf}
SectionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext current
  ReadINIStr $Isolated "$INSTDIR\downshift-setup.ini" "Setup" "IsolatedTest"
  ${If} $Isolated == "1"
    SetShellVarContext current
    StrCpy $RegistryKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift.PackagingTest"
    StrCpy $Arguments "-IsolatedTest"
    StrCpy $StartupKey "${TEST_STARTUP_RUN_KEY}"
  ${ElseIf} $Isolated == "0"
    SetShellVarContext all
    StrCpy $RegistryKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift"
    StrCpy $Arguments "-MachineInstall"
    StrCpy $StartupKey "${STARTUP_RUN_KEY}"
  ${Else}
    Abort "Downshift setup ownership is missing. Application data was left untouched."
  ${EndIf}
FunctionEnd

Section "Uninstall"
  nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\Uninstall-MediaTools.ps1"'
  Pop $Result
  Pop $Output
  ${If} $Result != "0"
    SetErrorLevel 1
    Abort "Video tool ownership could not be verified. Application files were preserved."
  ${EndIf}
  nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\Uninstall-Portable.ps1" $Arguments'
  Pop $Result
  Pop $Output
  DetailPrint $Output
  ${If} $Result != "0"
    SetErrorLevel 1
    Abort "Downshift integration could not be removed."
  ${EndIf}
  ReadRegStr $Result SHCTX "$RegistryKey" "InstallLocation"
  ${If} $Result == "$INSTDIR"
    ${If} $Isolated == "0"
      Delete "$SMPROGRAMS\Downshift\Chrome setup.lnk"
    ${EndIf}
    DeleteRegKey SHCTX "$RegistryKey"
  ${EndIf}
  ReadRegStr $Result SHCTX "$StartupKey" "${STARTUP_VALUE}"
  ${If} $Result == '$\"$INSTDIR\Downshift.exe$\"'
    DeleteRegValue SHCTX "$StartupKey" "${STARTUP_VALUE}"
  ${EndIf}
  !include "${UNINSTALL_FILES}"
  Delete "$INSTDIR\downshift-setup.ini"
  Delete "$INSTDIR\Chrome-setup.html"
  Delete "$INSTDIR\Chrome-setup.txt"
  Delete "$INSTDIR\setup-video.log"
  Delete "$INSTDIR\Uninstall.exe"
  ${If} $Isolated == "0"
    RMDir "$SMPROGRAMS\Downshift"
  ${Else}
    RMDir "$INSTDIR\Desktop"
  ${EndIf}
  ; Per-user video support and downloads are outside this machine-wide installation.
  ; Only empty directories are removed. History, downloads and unknown files stay.
  RMDir "$INSTDIR"
SectionEnd
