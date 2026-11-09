cmake_minimum_required(VERSION 3.20)
project(sack_rtsp_cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(LOCAL_OPENCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/build")
set(LOCAL_OPENCV_RUNTIME_DIR "")
foreach(OPENCV_TOOLSET vc17 vc16)
    if(EXISTS "${LOCAL_OPENCV_DIR}/x64/${OPENCV_TOOLSET}/lib/OpenCVConfig.cmake")
        if(NOT OpenCV_DIR)
            set(OpenCV_DIR "${LOCAL_OPENCV_DIR}/x64/${OPENCV_TOOLSET}/lib")
        endif()
        set(LOCAL_OPENCV_RUNTIME_DIR "${LOCAL_OPENCV_DIR}/x64/${OPENCV_TOOLSET}/bin")
        break()
    endif()
endforeach()
if(NOT LOCAL_OPENCV_RUNTIME_DIR)
    message(FATAL_ERROR "OpenCV lokal tidak ditemukan di cpp_rtsp/third_party/opencv")
endif()

find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc highgui videoio dnn)

set(ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime-directml")
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_DIR}/build/native/include")
set(ONNXRUNTIME_NATIVE_DIR "${ONNXRUNTIME_DIR}/runtimes/win-x64/native")
set(DIRECTML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/directml")
set(DIRECTML_INCLUDE_DIR "${DIRECTML_DIR}/include")
set(DIRECTML_NATIVE_DIR "${DIRECTML_DIR}/bin/x64-win")
if(NOT EXISTS "${ONNXRUNTIME_NATIVE_DIR}/onnxruntime.lib" OR
   NOT EXISTS "${DIRECTML_NATIVE_DIR}/DirectML.dll")
    message(FATAL_ERROR "ONNX Runtime DirectML lokal belum lengkap")
endif()

add_library(sack_core
    src/async_capture.cpp
    src/report_paths.cpp
    src/stable_tracker.cpp
    src/yolo_detector.cpp
)
target_include_directories(sack_core PUBLIC
    src
    "${ONNXRUNTIME_INCLUDE_DIR}"
    "${DIRECTML_INCLUDE_DIR}"
)
target_link_libraries(sack_core PUBLIC
    ${OpenCV_LIBS}
    "${ONNXRUNTIME_NATIVE_DIR}/onnxruntime.lib"
    "${DIRECTML_NATIVE_DIR}/DirectML.lib"
)
target_compile_definitions(sack_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(sack_rtsp src/main.cpp)
target_link_libraries(sack_rtsp PRIVATE sack_core)

enable_testing()
add_executable(stable_tracker_test tests/stable_tracker_test.cpp)
target_link_libraries(stable_tracker_test PRIVATE sack_core)
add_test(NAME stable_tracker_test COMMAND stable_tracker_test)

if(WIN32)
    file(GLOB OPENCV_RUNTIME_DLLS
        "${LOCAL_OPENCV_RUNTIME_DIR}/opencv_world*.dll"
        "${LOCAL_OPENCV_RUNTIME_DIR}/opencv_videoio_ffmpeg*.dll"
    )
    list(FILTER OPENCV_RUNTIME_DLLS EXCLUDE REGEX "opencv_world[0-9]+d\\.dll$")
    foreach(OPENCV_DLL IN LISTS OPENCV_RUNTIME_DLLS)
        add_custom_command(TARGET sack_rtsp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENCV_DLL}" "$<TARGET_FILE_DIR:sack_rtsp>"
        )
    endforeach()
    foreach(GPU_RUNTIME_DLL
            "${ONNXRUNTIME_NATIVE_DIR}/onnxruntime.dll"
            "${ONNXRUNTIME_NATIVE_DIR}/onnxruntime_providers_shared.dll"
            "${DIRECTML_NATIVE_DIR}/DirectML.dll")
        add_custom_command(TARGET sack_rtsp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GPU_RUNTIME_DLL}" "$<TARGET_FILE_DIR:sack_rtsp>"
        )
    endforeach()
endif()
