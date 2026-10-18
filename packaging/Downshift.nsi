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
!define MUI_WELCOMEPAGE_TEXT "Developed by UshaLabs.$\r$\n$\r$\nThis setup installs Downshift and automatically installs its verified video tools. Keep your internet connection on during installation.$\r$\n$\r$\nThe Chrome extension files are included. Chrome requires a one-time Load unpacked step for this unpublished preview; the finish page opens the instructions.$\r$\n$\r$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PROJECT_ROOT}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!define MUI_PAGE_HEADER_SUBTEXT "Video support will download about 136 MiB. Keep your internet connection on."
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT "Downshift and its video tools are installed. Open Downshift from the Desktop shortcut or Start menu.$\r$\n$\r$\nConnect Chrome using the setup guide. This unpublished preview requires one-time Load unpacked in Chrome.$\r$\n$\r$\nThis local preview is unsigned. Downloads and history are kept when you uninstall."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open Chrome setup instructions"
!define MUI_FINISHPAGE_RUN_FUNCTION OpenChromeSetup
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var Isolated
Var RegistryKey
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

Function OpenChromeSetup
  ${If} $Isolated != "1"
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\Open-ChromeSetup.ps1"'
    Pop $Result
    Pop $Output
  ${EndIf}
FunctionEnd

Section "Downshift" Main
  SetOutPath "$INSTDIR"
  SetOverwrite on
  !include "${INSTALL_FILES}"
  ; Bootstrap the unmodified pinned upstream tools on the recipient's PC.
  ; They are not redistributed inside this preview's installer.
  DetailPrint "Installing and verifying video support. This may take several minutes..."
  nsExec::ExecToStack /TIMEOUT=1800000 '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\Install-MediaTools.ps1" -CacheDirectory "$INSTDIR\.setup-media-cache" -RemoveDownloadCache'
  Pop $Result
  Pop $Output
  DetailPrint $Output
  FileOpen $1 "$INSTDIR\setup-video.log" w
  FileWrite $1 "Video setup exit: $Result$\r$\n$Output$\r$\n"
  FileClose $1
  ${If} $Result != "0"
    SetErrorLevel 1
    IfSilent +2
    MessageBox MB_ICONSTOP "Video support could not be installed. Check your internet connection and run setup again. Existing downloads and history are preserved."
    Abort "Video support installation failed. Run setup again to retry."
  ${EndIf}
  nsExec::ExecToLog '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\Open-ChromeSetup.ps1" -PrepareOnly'
  Pop $Result
  ${If} $Result != "0"
    SetErrorLevel 1
    Abort "Chrome setup instructions could not be prepared."
  ${EndIf}
  ${If} $Isolated == "1"
    StrCpy $RegistryKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift.PackagingTest"
    StrCpy $Shortcut "$INSTDIR\Downshift-test.lnk"
    CreateDirectory "$INSTDIR\Desktop"
    StrCpy $DesktopShortcut "$INSTDIR\Desktop\Downshift.lnk"
    StrCpy $Arguments "-IsolatedTest -MachineInstall"
  ${Else}
    StrCpy $RegistryKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift"
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
SectionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext current
  ReadINIStr $Isolated "$INSTDIR\downshift-setup.ini" "Setup" "IsolatedTest"
  ${If} $Isolated == "1"
    SetShellVarContext current
    StrCpy $RegistryKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift.PackagingTest"
    StrCpy $Arguments "-IsolatedTest"
  ${ElseIf} $Isolated == "0"
    SetShellVarContext all
    StrCpy $RegistryKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\Downshift"
    StrCpy $Arguments "-MachineInstall"
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
  ; Only empty directories are removed. History, downloads and unknown files stay.
  RMDir "$INSTDIR"
SectionEnd
