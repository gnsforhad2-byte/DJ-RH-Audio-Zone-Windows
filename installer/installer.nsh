!macro customInstall
  SetOutPath "$COMMONFILES64\VST3\DJ RH Audio Zone.vst3"
  File /r "${PROJECT_DIR}\build\vst3-cmake\VST3\Release\DJ RH Audio Zone.vst3\*"
!macroend

!macro customUnInstall
  RMDir /r "$COMMONFILES64\VST3\DJ RH Audio Zone.vst3"
!macroend
