SetCompressor /SOLID LZMA 

; Include the generated version file from the build directory
; Note: This assumes the script is run from the build directory context
!include "version.nsi"

!include "MUI2.nsh"

XPStyle on 


Name "jucyaudio ${CURRENT_VERSION}" 
OutFile "jucyaudio-${CURRENT_VERSION}-setup-x64.exe"
InstallDir "$PROGRAMFILES64\jucyaudio"
InstallDirRegKey HKLM SOFTWARE\p-nand-q.com\jucyaudio "Install_Dir"

!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!define MUI_WELCOMEPAGE_TITLE "jucyaudio ${CURRENT_VERSION}"
!define MUI_ABORTWARNING

!define MUI_FINISHPAGE_RUN "$INSTDIR\jucyaudio.exe"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH  

!insertmacro MUI_LANGUAGE "English"

BrandingText "p-nand-q.com"

ShowInstDetails show

RequestExecutionLevel admin

  VIProductVersion "${CURRENT_VERSION}.0"
  VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "jucyaudio"
  VIAddVersionKey /LANG=${LANG_ENGLISH} "Comments" "GPL Licensed"
  VIAddVersionKey /LANG=${LANG_ENGLISH} "CompanyName" "p-nand-q.com"
  VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalTrademarks" ""
  VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "(C) p-nand-q.com"
  VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "jucyaudio"
  VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" ${CURRENT_VERSION}

Section  "-Jucyaudio (required)"
    SetRegView 64
    SetOutPath $INSTDIR
    File /R ..\out\install\X64-Release\bin\*
    
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\jucyaudio" "DisplayName" "jucyaudio (Remove only)"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\jucyaudio" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteUninstaller "$INSTDIR\uninstall.exe"
    DetailPrint "Done."
SectionEnd

Section "Create Desktop Shortcut"
    CreateShortCut "$DESKTOP\jucyaudio.lnk" "$INSTDIR\jucyaudio.exe" ""
SectionEnd

Section "Create Start Menu Shortcuts"
    CreateDirectory "$SMPROGRAMS\jucyaudio"
    CreateShortCut "$SMPROGRAMS\jucyaudio\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
    CreateShortCut "$SMPROGRAMS\jucyaudio\jucyaudio.lnk" "$INSTDIR\jucyaudio.exe" "" "$INSTDIR\jucyaudio.exe" 0
SectionEnd

Section "Register Shell Extension"
    WriteRegStr HKCR "*\shell\jucyaudio" "Icon" "$INSTDIR\jucyaudio.exe"
    WriteRegStr HKCR "*\shell\jucyaudio" "Position" "Middle"
    WriteRegStr HKCR "*\shell\jucyaudio\Command" "" '$INSTDIR\jucyaudio.exe %0'
SectionEnd

Section "Uninstall"
    SetRegView 64
	Delete $INSTDIR\uninstall.exe
    Delete "$DESKTOP\jucyaudio.lnk"
    RMDir /r "$SMPROGRAMS\jucyaudio"
	ReadRegStr $INSTDIR HKLM SOFTWARE\p-nand-q.com\jucyaudio "Install_Dir"
    RMDir /r "$INSTDIR\"
SectionEnd
