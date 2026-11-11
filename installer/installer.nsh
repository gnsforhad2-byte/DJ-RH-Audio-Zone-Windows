!macro customInstall
  SetOutPath "$COMMONFILES64\VST3\DJ RH Audio Zone.vst3"
  File "${PROJECT_DIR}\build\vst3-cmake\VST3\Release\DJ RH Audio Zone.vst3\desktop.ini"
  File "${PROJECT_DIR}\build\vst3-cmake\VST3\Release\DJ RH Audio Zone.vst3\PlugIn.ico"

  SetOutPath "$COMMONFILES64\VST3\DJ RH Audio Zone.vst3\Contents\Resources"
  File "${PROJECT_DIR}\build\vst3-cmake\VST3\Release\DJ RH Audio Zone.vst3\Contents\Resources\moduleinfo.json"

  SetOutPath "$COMMONFILES64\VST3\DJ RH Audio Zone.vst3\Contents\x86_64-win"
  File "${PROJECT_DIR}\build\vst3-cmake\VST3\Release\DJ RH Audio Zone.vst3\Contents\x86_64-win\DJ RH Audio Zone.vst3"
!macroend

!macro customUnInstall
  RMDir /r "$COMMONFILES64\VST3\DJ RH Audio Zone.vst3"
!macroend
