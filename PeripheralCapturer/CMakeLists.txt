# CMakeList.txt: PeripheralCapturer 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#

# 将源代码添加到此项目的可执行文件。
add_executable (PeripheralCapturer
    main.cpp
    Layout/MainWindow.h
    Layout/MainWindow.cpp
    Layout/MainWindow.ui
    overlay/PovWindow.h
    overlay/PovWindow.cpp
    overlay/OverlayHub.h
    overlay/OverlayHub.cpp
    overlay/ViteDevServer.h
    overlay/ViteDevServer.cpp
    capture/HiddenCaptureWindow.h
    capture/HiddenCaptureWindow.cpp
    web/pov.qrc
    utils/Logger.h
    utils/Logger.cpp
    Input/InputEvent.h
    Input/Timer.h
    Input/Timer.cpp
    Input/DeviceRegistry.h
    Input/DeviceRegistry.cpp
    Input/GamepadProbe.h
    Input/GamepadProbe.cpp
    Input/RawInputPacket.h
    Input/BoundedQueue.h
    Input/InputEventBus.h
    Input/InputEventBus.cpp
    Input/QueuePresets.h
    Input/InputPipeline.h
    Input/InputPipeline.cpp
    storage/AppConfig.h
    storage/AppConfig.cpp
    storage/Database.h
    storage/Database.cpp
    storage/RecordingLayout.h
    storage/FrameBatchWriter.h
    storage/FrameBatchWriter.cpp
    storage/Recorder.h
    storage/Recorder.cpp
)

target_link_libraries(PeripheralCapturer PRIVATE ${QT_LIBRARIES} spdlog::spdlog Xinput dwmapi ws2_32)
target_include_directories(PeripheralCapturer PRIVATE "${SPDLOG_INCLUDE_DIR}")
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/web" PC_WEB_DIR)
target_compile_definitions(PeripheralCapturer PRIVATE "PC_WEB_SOURCE_DIR=\"${PC_WEB_DIR}\"")

if (WIN32)
  find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt windeployqt.exe
    HINTS "${CMAKE_PREFIX_PATH}/bin"
  )
  if (WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET PeripheralCapturer POST_BUILD
      COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        --no-translations
        --no-compiler-runtime
        "$<TARGET_FILE:PeripheralCapturer>"
      COMMENT "Deploying Qt runtime next to the executable"
    )
  endif()
endif()

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET PeripheralCapturer PROPERTY CXX_STANDARD 20)
endif()

# TODO: 如有需要，请添加测试并安装目标。
