include(CMakeDependentOption)

include(FixInterfaceIncludes)

set(SOURCES_QT_SDL
    main.cpp
    main_shaders.h
    Screen.cpp
    ScreenCapture.cpp
    Window.cpp
    EmuInstance.cpp
    EmuInstanceAudio.cpp
    EmuInstanceInput.cpp
    EmuThread.cpp
    CheatImportDialog.cpp
    CheatsDialog.cpp
    Config.cpp
    DateTimeDialog.cpp
    EmuSettingsDialog.cpp
    PowerManagement/PowerManagementDialog.cpp
    PowerManagement/resources/battery.qrc
    InputConfig/InputConfigDialog.cpp
    InputConfig/MapButton.h
    InputConfig/resources/ds.qrc
    VideoSettingsDialog.cpp
    VideoSettingsLayout.cpp
    WholeScene2DDebugExport.cpp
    WholeScene2DDebugDialog.cpp
    TextureScalingDebugDialog.cpp
    CameraSettingsDialog.cpp
    AudioSettingsDialog.cpp
    FirmwareSettingsDialog.cpp
    PathSettingsDialog.cpp
    MPSettingsDialog.cpp
    WifiSettingsDialog.cpp
    InterfaceSettingsDialog.cpp
    ROMInfoDialog.cpp
    RAMInfoDialog.cpp
    TitleManagerDialog.cpp
    OSD_shaders.h
    font.h
    Platform.cpp
    Platform_AAC.cpp
    QPathInput.h
    SaveManager.cpp
    CameraManager.cpp
    AboutDialog.cpp
    AboutDialog.h
    AboutDialog.ui
    ARCodeQDecl.h

    ArchiveUtil.h
    ArchiveUtil.cpp

    ../ScreenLayout.cpp
    ../mic_blow.h

    ../glad/glad.c
    ../duckstation/gl/context.cpp

    ${CMAKE_SOURCE_DIR}/res/melon.qrc

    CLI.h
    CLI.cpp

    RendererTest.h
    RendererTest.cpp
    RendererTestAssertions.h
    RendererTestAssertions.cpp

    LANDialog.cpp
    NetplayDialog.cpp
)

option(USE_QT6 "Use Qt 6 instead of Qt 5" ON)

if (USE_QT6)
    find_package(Qt6 COMPONENTS Core Gui Widgets Network Multimedia OpenGL OpenGLWidgets Svg REQUIRED)
    set(QT_LINK_LIBS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::OpenGL Qt6::OpenGLWidgets)
else()
    find_package(Qt5 COMPONENTS Core Gui Widgets Network Multimedia Svg REQUIRED)
    set(QT_LINK_LIBS Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Multimedia)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

if (BUILD_STATIC)
    list(APPEND PKG_CONFIG_EXECUTABLE "--static")
endif()

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(LibArchive REQUIRED IMPORTED_TARGET libarchive)
pkg_check_modules(Zstd REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(Faad REQUIRED IMPORTED_TARGET faad2)

fix_interface_includes(PkgConfig::SDL2 PkgConfig::LibArchive)

add_compile_definitions(ARCHIVE_SUPPORT_ENABLED)

add_executable(melonDS ${SOURCES_QT_SDL})

add_subdirectory("../../net"
    ${CMAKE_BINARY_DIR}/net)

target_link_libraries(melonDS PRIVATE net-utils)

if (WIN32)
    target_link_libraries(melonDS PUBLIC opengl32)

    target_sources(melonDS PRIVATE
        ../duckstation/gl/context_wgl.cpp

        ../glad/glad_wgl.c
    )

    if (MINGW AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET melonDS PROPERTY AUTORCC_OPTIONS "--no-zstd")
    endif()

    if (BUILD_STATIC)
        find_package(SDL2 CONFIG COMPONENTS SDL2main)
        if (TARGET SDL2::SDL2main)
            target_link_libraries(melonDS PRIVATE SDL2::SDL2main)
        endif()
    endif()
elseif (APPLE)
    if (NOT USE_QT6)
        find_library(COCOA_LIB Cocoa)
        target_link_libraries(melonDS PRIVATE ${COCOA_LIB})
    endif()
    find_library(OPENGL_LIB OpenGL)
    target_link_libraries(melonDS PRIVATE ${OPENGL_LIB})
    target_sources(melonDS PRIVATE
        ../duckstation/gl/context_agl.mm
    )
    set_source_files_properties(
        ../duckstation/gl/context_agl.mm
        PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations"
    )
else()
    find_package(X11 REQUIRED)
    find_package(EGL REQUIRED)

    option(ENABLE_WAYLAND "Enable Wayland support" ON)

    if (ENABLE_WAYLAND)
        # we only need ECM for Wayland
        # so we only require it from here
        find_package(ECM REQUIRED NO_MODULE)
        list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")

        find_package(Wayland REQUIRED Client)

        target_compile_definitions(melonDS PRIVATE WAYLAND_ENABLED)

        target_sources(melonDS PRIVATE
            ../duckstation/gl/context_egl_wayland.cpp
        )
    endif()

    target_sources(melonDS PRIVATE
        ../duckstation/gl/context_egl.cpp
        ../duckstation/gl/context_egl_x11.cpp
        ../duckstation/gl/context_glx.cpp
        ../duckstation/gl/x11_window.cpp

        ../glad/glad_egl.c
        ../glad/glad_glx.c
    )
    target_link_libraries(melonDS PRIVATE "${X11_LIBRARIES}" "${EGL_LIBRARIES}")
    target_include_directories(melonDS PRIVATE "${X11_INCLUDE_DIR}")
    add_compile_definitions(QAPPLICATION_CLASS=QApplication)
endif()


if (BUILD_STATIC)
    qt_import_plugins(melonDS INCLUDE Qt::QSvgPlugin)
    if (WIN32 AND USE_QT6)
        qt_import_plugins(melonDS INCLUDE Qt::QModernWindowsStylePlugin)
    endif()
    if (USE_VCPKG AND UNIX AND NOT APPLE)
        pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
        target_link_libraries(melonDS PRIVATE PkgConfig::ALSA)
    elseif (NOT APPLE)
        target_link_options(melonDS PRIVATE -static)
    endif()
endif()

target_include_directories(melonDS PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/..")

if (USE_QT6)
    target_include_directories(melonDS PUBLIC ${Qt6Gui_PRIVATE_INCLUDE_DIRS})
else()
    target_include_directories(melonDS PUBLIC ${Qt5Gui_PRIVATE_INCLUDE_DIRS})
endif()
target_link_libraries(melonDS PRIVATE core)
target_link_libraries(melonDS PRIVATE PkgConfig::SDL2 PkgConfig::LibArchive PkgConfig::Zstd PkgConfig::Faad)
target_link_libraries(melonDS PRIVATE ${QT_LINK_LIBS} ${CMAKE_DL_LIBS})

if (WIN32)
    option(PORTABLE "Make a portable build that looks for its configuration in the current directory" ON)

    if (PORTABLE)
        target_compile_definitions(melonDS PRIVATE WIN32_PORTABLE)
    endif()

    string(REPLACE . , MELON_RC_VERSION "${MELONDS_FORK_VERSION}.0")
    configure_file("${CMAKE_SOURCE_DIR}/res/melon.rc.in" "${CMAKE_BINARY_DIR}/res/melon.rc")
    target_sources(melonDS PUBLIC "${CMAKE_BINARY_DIR}/res/melon.rc")
    target_include_directories(melonDS PRIVATE "${CMAKE_BINARY_DIR}/res")

    if (${ARCHITECTURE} STREQUAL x86_64)
        set(WIN32_ARCHITECTURE amd64)
    elseif (${ARCHITECTURE} STREQUAL x86)
        set(WIN32_ARCHITECTURE x86)
    elseif (${ARCHITECTURE} MATCHES "ARM(64)?")
        string(TOLOWER "${ARCHITECTURE}" WIN32_ARCHITECTURE)
    endif()


    target_link_libraries(melonDS PRIVATE ws2_32 iphlpapi)
    set_target_properties(melonDS PROPERTIES WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,OFF,ON>)

    if (MINGW)
        configure_file("${CMAKE_SOURCE_DIR}/res/xp.manifest.in" "${CMAKE_BINARY_DIR}/res/xp.manifest")
    else()
        # Work around faad2 unconditionally linking libm even on Windows
        if (USE_VCPKG)
            add_library(m STATIC)
            target_link_libraries(melonDS PUBLIC m)
        endif()
    endif()
endif()

if (APPLE)
    # Copy icon into the bundle
    set(RESOURCE_FILES "${CMAKE_SOURCE_DIR}/res/melon.icns")
    target_sources(melonDS PUBLIC "${RESOURCE_FILES}")

    set_target_properties(melonDS PROPERTIES
        MACOSX_BUNDLE true
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/res/melon.plist.in
        OUTPUT_NAME melonDS
        RESOURCE "${RESOURCE_FILES}")


    option(MACOS_BUNDLE_LIBS "Bundle libraries with the app on macOS" OFF)
    option(MACOS_BUILD_DMG "Build DMG image of the macOS application bundle" OFF)

    if (MACOS_BUNDLE_LIBS)
        if (MACOS_BUILD_DMG)
            set(DMGARG "--dmg")
        endif()
        add_custom_command(TARGET melonDS POST_BUILD
            COMMAND ${CMAKE_SOURCE_DIR}/tools/mac-libs.rb ${DMGARG} ${CMAKE_BINARY_DIR}
            COMMENT "Bundling macOS libraries...")
    endif()
endif()

if (UNIX AND NOT APPLE)
    foreach(SIZE 16 32 48 64 128 256)
        install(FILES ${CMAKE_SOURCE_DIR}/res/icon/melon_${SIZE}x${SIZE}.png
            DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/${SIZE}x${SIZE}/apps
            RENAME net.kuribo64.melonDS.png)
    endforeach()

    install(FILES ${CMAKE_SOURCE_DIR}/res/net.kuribo64.melonDS.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)
    install(TARGETS melonDS BUNDLE DESTINATION ${CMAKE_BINARY_DIR} RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

    if (NOT USE_QT6)
        set_target_properties(melonDS PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION OFF
            INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
    endif()
elseif(APPLE)
    install(TARGETS melonDS BUNDLE DESTINATION "${CMAKE_INSTALL_PREFIX}/Applications")
endif()

if (ENABLE_OGLRENDERER)
    set(MELONDS_GL_HEADER \"frontend/glad/glad.h\" CACHE STRING "Path to a header that contains OpenGL function and type declarations.")

    target_compile_definitions(melonDS PUBLIC OGLRENDERER_ENABLED)
    target_compile_definitions(melonDS PUBLIC MELONDS_GL_HEADER=${MELONDS_GL_HEADER})
    target_compile_definitions(core PUBLIC MELONDS_GL_HEADER=${MELONDS_GL_HEADER})
endif()

option(BUILD_DEBUG_EXPORT_TESTS "Build debug export writer tests" OFF)
option(BUILD_RENDERER_SHADER_TESTS "Build OpenGL 4.3 coverage shader tests (requires GPU context)" OFF)
if (BUILD_RENDERER_SHADER_TESTS)
    if (ENABLE_OGLRENDERER)
        target_sources(melonDS PRIVATE "${CMAKE_SOURCE_DIR}/test/renderer/GLRendererLifetimeTests.cpp")
        target_compile_definitions(melonDS PRIVATE BUILD_GL_LIFETIME_TESTS)
        add_test(NAME gl-renderer-lifetime-tests COMMAND melonDS --test-gl-lifetime)
    endif()
    add_executable(widescreen-capture-tests
        "${CMAKE_SOURCE_DIR}/test/renderer/WidescreenCaptureTests.cpp" ../glad/glad.c)
    target_include_directories(widescreen-capture-tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/frontend")
    target_link_libraries(widescreen-capture-tests PRIVATE ${QT_LINK_LIBS})
    add_test(NAME widescreen-capture-tests COMMAND widescreen-capture-tests)
    add_executable(screen-capture-tests
        "${CMAKE_SOURCE_DIR}/test/renderer/ScreenCaptureTests.cpp"
        ScreenCapture.cpp ../glad/glad.c)
    target_include_directories(screen-capture-tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/frontend" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(screen-capture-tests PRIVATE ${QT_LINK_LIBS})
    add_test(NAME screen-capture-tests COMMAND screen-capture-tests)
    add_executable(compute-dispatch-tests "${CMAKE_SOURCE_DIR}/test/renderer/ComputeDispatchTests.cpp")
    target_link_libraries(compute-dispatch-tests PRIVATE ${QT_LINK_LIBS})
    add_test(NAME compute-dispatch-tests COMMAND compute-dispatch-tests)
    add_test(NAME compute-dispatch-negative-control COMMAND compute-dispatch-tests --broken-index)
    set_tests_properties(compute-dispatch-negative-control PROPERTIES WILL_FAIL TRUE)
    add_executable(compute-widescreen-tests "${CMAKE_SOURCE_DIR}/test/renderer/ComputeWidescreenTests.cpp")
    target_link_libraries(compute-widescreen-tests PRIVATE ${QT_LINK_LIBS})
    add_test(NAME compute-widescreen-tests COMMAND compute-widescreen-tests)
    add_test(NAME compute-wide-center-negative-control COMMAND compute-widescreen-tests --broken-center)
    add_test(NAME compute-wide-padding-negative-control COMMAND compute-widescreen-tests --broken-padding)
    set_tests_properties(compute-wide-center-negative-control compute-wide-padding-negative-control PROPERTIES WILL_FAIL TRUE)
    add_executable(nnedi3-coverage-tests
        "${CMAKE_SOURCE_DIR}/test/renderer/NNEDI3CoverageTests.cpp"
        "${CMAKE_SOURCE_DIR}/test/renderer/NNEDI3CompositionDiagnostic.cpp"
        "${CMAKE_SOURCE_DIR}/test/renderer/TextureScaleRegionTests.cpp"
        "${CMAKE_SOURCE_DIR}/test/renderer/ScalerAlphaFusionTests.cpp")
    target_link_libraries(nnedi3-coverage-tests PRIVATE ${QT_LINK_LIBS})
    target_compile_definitions(nnedi3-coverage-tests PRIVATE SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/OpenGL_shaders")
    enable_testing()
    add_test(NAME nnedi3-coverage-tests COMMAND nnedi3-coverage-tests)
    add_test(NAME texture-scale-region-tests COMMAND nnedi3-coverage-tests --test-edge-regions)
    add_test(NAME scaler-alpha-fusion-tests COMMAND nnedi3-coverage-tests --test-scaler-alpha-fusion)
    add_test(NAME texture-scale-region-negative-control COMMAND nnedi3-coverage-tests --test-edge-regions --corrupt-edge-origin)
    set_tests_properties(texture-scale-region-negative-control PROPERTIES WILL_FAIL TRUE)
    add_test(NAME nnedi3-coverage-negative-control COMMAND nnedi3-coverage-tests --unbounded)
    set_tests_properties(nnedi3-coverage-negative-control PROPERTIES WILL_FAIL TRUE)
    add_executable(opaque-obj-assembly-tests
        "${CMAKE_SOURCE_DIR}/test/renderer/OpaqueOBJAssemblyTests.cpp"
        "${CMAKE_SOURCE_DIR}/test/renderer/ResolvedOBJMergeTests.cpp"
        "${CMAKE_SOURCE_DIR}/test/renderer/EnhancedBGSamplingTests.cpp")
    target_link_libraries(opaque-obj-assembly-tests PRIVATE ${QT_LINK_LIBS})
    target_compile_definitions(opaque-obj-assembly-tests PRIVATE SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/OpenGL_shaders")
    add_test(NAME opaque-obj-assembly-tests COMMAND opaque-obj-assembly-tests)
    add_test(NAME enhanced-bg-domain-negative-control COMMAND opaque-obj-assembly-tests --broken-bg-domain)
    add_test(NAME enhanced-bg-wrap-negative-control COMMAND opaque-obj-assembly-tests --broken-bg-wrap)
    set_tests_properties(enhanced-bg-domain-negative-control enhanced-bg-wrap-negative-control PROPERTIES WILL_FAIL TRUE)
    add_test(NAME resolved-obj-boundary-negative-control COMMAND opaque-obj-assembly-tests --legacy-obj-boundary)
    set_tests_properties(resolved-obj-boundary-negative-control PROPERTIES WILL_FAIL TRUE)
    add_test(NAME resolved-obj-special-boundary-negative-control COMMAND opaque-obj-assembly-tests --opaque-only-obj-boundary)
    set_tests_properties(resolved-obj-special-boundary-negative-control PROPERTIES WILL_FAIL TRUE)
    add_test(NAME opaque-obj-assembly-spline-rgb-negative-control COMMAND opaque-obj-assembly-tests --clipped-spline-rgb)
    set_tests_properties(opaque-obj-assembly-spline-rgb-negative-control PROPERTIES WILL_FAIL TRUE)
endif()

if (BUILD_DEBUG_EXPORT_TESTS)
    add_executable(debug-export-tests
        "${CMAKE_SOURCE_DIR}/test/debug-export/DebugExportTests.cpp"
        WholeScene2DDebugExport.cpp)
    target_include_directories(debug-export-tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(debug-export-tests PRIVATE ${QT_LINK_LIBS})
    enable_testing()
    add_test(NAME debug-export-tests COMMAND debug-export-tests)
    add_test(NAME debug-export-shutdown COMMAND debug-export-tests --shutdown-test)
endif()
