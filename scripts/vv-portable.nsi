; vv-portable.nsi — wrap the staged Windows bundle into one portable .exe.
;
; Not an installer: no admin, no Start-menu/registry entries, no uninstaller.
; Running the .exe extracts the bundle once to a per-build folder under
; %LOCALAPPDATA%\vv\ and launches vv.exe from there; later runs detect the
; existing extraction and just relaunch, so startup is instant instead of
; re-unzipping every time. Delete that folder to reclaim the space.
;
; Driven entirely from the command line by scripts/package-windows.sh:
;   makensis -DVERSION=.. -DCACHEKEY=.. -DSTAGE_DIR=<win> -DOUTFILE=<win> vv-portable.nsi

Unicode true
ManifestDPIAware true
RequestExecutionLevel user      ; per-user, never prompts for elevation
SilentInstall silent            ; no wizard UI — extract + launch, then exit
SetCompressor /SOLID lzma       ; smallest single-file output

Name "vv"
Icon "${ICON}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\vv\${CACHEKEY}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "vv"
VIAddVersionKey "FileDescription" "vv — Mesh Viewer (portable)"
VIAddVersionKey "FileVersion" "${VERSION}.0"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "vv"

Section
  ; Fast path: this build is already unpacked — skip extraction, just relaunch.
  IfFileExists "$INSTDIR\ok.marker" launch

  RMDir /r "$INSTDIR"
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*.*"
  FileOpen $0 "$INSTDIR\ok.marker" w
  FileWrite $0 "${VERSION}"
  FileClose $0

launch:
  ; Forward argv (e.g. a mesh file dropped on / double-clicked into the exe)
  ; to the real binary — vv, unlike a plain GUI app, is driven by file args.
  SetOutPath "$INSTDIR"
  Exec '"$INSTDIR\vv.exe" $CMDLINE'
SectionEnd
