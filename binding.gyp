{
  "targets": [
    {
      "target_name": "wasapi",
      "sources": [
        "native/wasapi/addon.cpp",
        "native/vendor/rtaudio/RtAudio.cpp"
      ],
      "include_dirs": [
        "native/vendor/rtaudio",
        "native/vendor/rtaudio/include"
      ],
      "defines": [
        "__WINDOWS_WASAPI__",
        "NOMINMAX",
        "WIN32_LEAN_AND_MEAN"
      ],
      "libraries": [
        "ksuser.lib",
        "mfplat.lib",
        "mfuuid.lib",
        "wmcodecdspuuid.lib",
        "winmm.lib",
        "ole32.lib"
      ],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "AdditionalOptions": [
            "/std:c++17",
            "/EHsc"
          ]
        }
      }
    }
  ]
}
