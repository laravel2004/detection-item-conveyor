# Third-Party Dependencies

Folder ini digunakan untuk meletakkan library native C++ yang dibutuhkan saat kompilasi. Karena ukurannya besar (> 850 MB) dan file debug OpenCV melebihi batas 100 MB di GitHub, file binary tidak disimpan di repositori Git.

## Struktur Direktori yang Dibutuhkan

```text
third_party/
├── directml/
│   ├── bin/x64-win/
│   │   ├── DirectML.dll
│   │   └── DirectML.lib
│   └── include/
│       └── DirectML.h
│
├── onnxruntime-directml/
│   ├── build/native/include/
│   │   └── onnxruntime_cxx_api.h
│   └── runtimes/win-x64/native/
│       ├── onnxruntime.dll
│       ├── onnxruntime.lib
│       └── onnxruntime_providers_shared.dll
│
└── opencv/
    └── build/
        ├── include/
        │   └── opencv2/
        └── x64/vc16/ (atau vc17)
            ├── bin/
            │   ├── opencv_videoio_ffmpeg*.dll
            │   └── opencv_world*.dll
            └── lib/
                ├── OpenCVConfig.cmake
                └── opencv_world*.lib
```

## Sumber Download Library

1. **DirectML**:
   - Download NuGet package `Microsoft.AI.DirectML` (ekstrak isi `.nupkg` seperti file `.zip`).
2. **ONNX Runtime DirectML**:
   - Download NuGet package `Microsoft.ML.OnnxRuntime.DirectML` (ekstrak isi `.nupkg`).
3. **OpenCV Windows**:
   - Download installer Windows OpenCV 4.x dari [opencv.org/releases](https://opencv.org/releases/) lalu ekstrak folder `build` ke dalam `third_party/opencv/build`.
