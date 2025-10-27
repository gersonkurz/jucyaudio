cmake_minimum_required(VERSION 3.15)
project(jucyaudio VERSION 0.9.0)

# Enable export of compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build Configuration
# ============================================================================

# Enable debug info in Release builds for better debugging while maintaining performance
if(MSVC)
    # Visual Studio: Add debug info to Release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Zi /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG")
else()
    # macOS/Linux: Enable optimized builds with debug info
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -flto")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
endif()

# Set default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build.")
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# C++20 requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Architecture Detection and Output Directories
# ============================================================================

# Architecture detection for output directories
if(APPLE)
    if(CMAKE_OSX_ARCHITECTURES)
        string(REPLACE ";" "_" ARCH_DIR "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(ARCH_DIR "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_DIR "x64")
else()
    set(ARCH_DIR "x86")
endif()

# Set output directories based on architecture and configuration
if(CMAKE_CONFIGURATION_TYPES)
    # Multi-config generator (Xcode, Visual Studio)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${ARCH_DIR}-$<CONFIG>")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${ARCH_DIR}-$<CONFIG>")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${ARCH_DIR}-$<CONFIG>")
else()
    # Single-config generator (Unix Makefiles, Ninja)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${ARCH_DIR}-${CMAKE_BUILD_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${ARCH_DIR}-${CMAKE_BUILD_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${ARCH_DIR}-${CMAKE_BUILD_TYPE}")
endif()

# Find or fetch JUCE
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE
)

# Add nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)

# Add tabulate for pretty table printing
FetchContent_Declare(
    tabulate
    GIT_REPOSITORY https://github.com/p-ranav/tabulate.git
    GIT_TAG v1.5
    GIT_SHALLOW TRUE
)

# Add spdlog with std::format support
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.16.0
    GIT_SHALLOW TRUE
)

# Configure spdlog to use std::format and build as compiled static library
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_ENABLE_PCH ON CACHE BOOL "" FORCE)
set(SPDLOG_WCHAR_SUPPORT OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

# Add tomlplusplus for configuration file parsing
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)

# Add taglib for ID3 tag reading
FetchContent_Declare(
    taglib
    GIT_REPOSITORY https://github.com/taglib/taglib.git
    GIT_TAG v2.1.1
    GIT_SHALLOW TRUE
)

# Add libsndfile for audio file I/O
FetchContent_Declare(
    libsndfile
    GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
    GIT_TAG 1.2.2
    GIT_SHALLOW TRUE
)

# Allow libsndfile's older CMake version requirement
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# Configure taglib options (must be before MakeAvailable)
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(TAGLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(TAGLIB_BUILD_BINDINGS OFF CACHE BOOL "" FORCE)

# Configure libsndfile options
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_MPEG OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(JUCE nlohmann_json tabulate spdlog tomlplusplus taglib libsndfile)

# Restore BUILD_SHARED_LIBS
if(DEFINED BUILD_SHARED_LIBS_SAVED)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
else()
    unset(BUILD_SHARED_LIBS)
endif()

# Get taglib source and binary directories for include paths
FetchContent_GetProperties(taglib)
if(taglib_POPULATED)
    # Add taglib include directories
    # Include the parent directory so that #include <taglib/fileref.h> works
    # Also include taglib/ subdirectory for internal taglib headers that use relative includes
    set(TAGLIB_INCLUDE_DIRS
        ${taglib_SOURCE_DIR}
        ${taglib_SOURCE_DIR}/taglib
        ${taglib_SOURCE_DIR}/taglib/toolkit
        ${taglib_SOURCE_DIR}/taglib/mpeg
        ${taglib_SOURCE_DIR}/taglib/mpeg/id3v2
        ${taglib_BINARY_DIR}
    )
endif()

# Global compile definitions
add_compile_definitions(
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_ENABLE_STAT4
    SQLITE_DQS=0
    SPDLOG_USE_STD_FORMAT
)

if(WIN32)
    add_compile_definitions(
        SPDLOG_COMPILED_LIB
        SPDLOG_MSVC_UTF8
        TAGLIB_STATIC
    )
endif()

# Platform-specific library paths and includes
if(WIN32)
     set(WIN32_DEPS_ROOT "C:/Projects/jucyaudio_deps_win32_x64")

    # Windows paths from jucer file
    set(AUBIO_LIB_PATH "${WIN32_DEPS_ROOT}/aubio/lib")
    # spdlog is now handled by FetchContent
    # taglib is now handled by FetchContent
    set(LAME_LIB_PATH "${WIN32_DEPS_ROOT}/lame")
    set(PTHREADS_LIB_PATH "${WIN32_DEPS_ROOT}/pthreads-w32/lib")
    set(FFTW_LIB_PATH "${WIN32_DEPS_ROOT}/fftw")
    # libsndfile is now handled by FetchContent
    set(FFMPEG_LIB_PATH "${WIN32_DEPS_ROOT}/ffmpeg/lib")


    set(AUBIO_INCLUDE_PATH "${WIN32_DEPS_ROOT}/aubio/include")
    # spdlog is now handled by FetchContent
    # taglib is now handled by FetchContent
    set(LAME_INCLUDE_PATH "${WIN32_DEPS_ROOT}/lame/include")
    # tomlplusplus is now handled by FetchContent
    set(PTHREADS_INCLUDE_PATH "${WIN32_DEPS_ROOT}/pthreads-w32/include")
    set(FFTW_INCLUDE_PATH "${WIN32_DEPS_ROOT}/fftw")
    # libsndfile is now handled by FetchContent
    set(FFMPEG_INCLUDE_PATH "${WIN32_DEPS_ROOT}/ffmpeg/include")   
    set(ICU_LIB_PATH_DEBUG "${WIN32_DEPS_ROOT}/icu/lib")
    set(ICU_LIB_PATH_RELEASE "${WIN32_DEPS_ROOT}/icu/lib")
    set(ICU_INCLUDE_PATH "${WIN32_DEPS_ROOT}/icu/include")
    
    # Use debug or release paths based on build type
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(ICU_LIB_PATH ${ICU_LIB_PATH_DEBUG})
    else()
        set(ICU_LIB_PATH ${ICU_LIB_PATH_RELEASE})
    endif()
    
    # Find libraries on Windows
    find_library(AUBIO_LIBRARY aubio PATHS ${AUBIO_LIB_PATH} REQUIRED)
    # spdlog is now handled by FetchContent
    # taglib is now handled by FetchContent
    find_library(PTHREADS_LIBRARY pthreadVC2 PATHS ${PTHREADS_LIB_PATH} REQUIRED)
    find_library(LAME_LIBRARY mp3lame PATHS ${LAME_LIB_PATH} REQUIRED)
    find_library(FFTW3F_LIBRARY libfftw3f-3 PATHS ${FFTW_LIB_PATH} REQUIRED)
    find_library(FFTW3_LIBRARY libfftw3-3 PATHS ${FFTW_LIB_PATH})
    # libsndfile is now handled by FetchContent
    
    # Find FFmpeg libraries
    find_library(AVCODEC_LIBRARY avcodec PATHS ${FFMPEG_LIB_PATH} REQUIRED)
    find_library(AVFORMAT_LIBRARY avformat PATHS ${FFMPEG_LIB_PATH} REQUIRED)
    find_library(AVUTIL_LIBRARY avutil PATHS ${FFMPEG_LIB_PATH} REQUIRED)
    find_library(SWRESAMPLE_LIBRARY swresample PATHS ${FFMPEG_LIB_PATH} REQUIRED)
    find_library(LAME_LIBRARY libmp3lame PATHS ${LAME_LIB_PATH} REQUIRED)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(ICU_UC_LIBRARY "${ICU_LIB_PATH_DEBUG}/icuucd.lib")
        set(ICU_I18N_LIBRARY "${ICU_LIB_PATH_DEBUG}/icuind.lib")
    else() # Release or other build types
        set(ICU_UC_LIBRARY "${ICU_LIB_PATH_RELEASE}/icuuc.lib")
        set(ICU_I18N_LIBRARY "${ICU_LIB_PATH_RELEASE}/icuin.lib")
    endif()
    message(STATUS "JucyAudio ICU Check: Found UC='${ICU_UC_LIBRARY}' and I18N='${ICU_I18N_LIBRARY}'")
    # Windows external includes
    set(EXTERNAL_INCLUDES
        ${AUBIO_INCLUDE_PATH}
        # spdlog is now handled by FetchContent
        ${TAGLIB_INCLUDE_DIRS}
        ${LAME_INCLUDE_PATH}
        # tomlplusplus is now handled by FetchContent
        ${PTHREADS_INCLUDE_PATH}
        ${FFTW_INCLUDE_PATH}
        # libsndfile is now handled by FetchContent
        ${ICU_INCLUDE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    # Find pkg-config only on non-Windows platforms
else()
    find_package(PkgConfig REQUIRED)
    
    # Set ICU root path for Homebrew installation
    set(ICU_ROOT "/opt/homebrew/opt/icu4c@77")
    
    # Find ICU (International Components for Unicode)
    find_package(ICU REQUIRED COMPONENTS uc i18n)
    
    # macOS/Linux paths (original)
    find_library(AUBIO_LIBRARY aubio PATHS /Users/gersonkurz/development/aubio_install_macos/lib)
    # spdlog is now handled by FetchContent
    # taglib is now handled by FetchContent
    find_library(MP3LAME_LIBRARY mp3lame PATHS /opt/homebrew/lib)
    
    # macOS/Linux external includes
    set(EXTERNAL_INCLUDES
        /Users/gersonkurz/development/aubio_install_macos/include
        # spdlog is now handled by FetchContent
        ${TAGLIB_INCLUDE_DIRS}
        # tomlplusplus is now handled by FetchContent
        /opt/homebrew/include
    )
endif()

# Create the main application
juce_add_gui_app(jucyaudio
    PRODUCT_NAME "JucyAudio"
    BUNDLE_ID "com.p-nand-q.jucyaudio"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "P-nand-Q"
    COMPANY_COPYRIGHT "Copyright (c) 2025 P-nand-Q"
)

configure_file(
    "${CMAKE_SOURCE_DIR}/setup/version.nsi.in"
    "${CMAKE_SOURCE_DIR}/setup/version.nsi"
    @ONLY
)

# Add Windows resource file for icon
if(WIN32)
    target_sources(jucyaudio PRIVATE windows_icon.rc )
endif()

# Add all source files
target_sources(jucyaudio PRIVATE
    # Config files
    Config/section.cpp
    Config/config_backend.h
    Config/toml_backend.h
    Config/section.h
    Config/typed_value.h
    Config/typed_vector_value.h
    Config/value_interface.h
    
    # Audio files
    Audio/ExportMixToWav.cpp
    Audio/AudioLibrary.h
    Audio/ExportMixImplementation.cpp
    Audio/ExportMixImplementation.h
    Audio/ExportMixToMp3.cpp
    Audio/ExportMixToMp3.h
    Audio/ExportMixToM3U.cpp
    Audio/ExportMixToM3U.h
    Audio/ExportMixToWav.cpp
    Audio/ExportMixToWav.h
    Audio/MixExporter.cpp
    Audio/MixExporter.h
    Audio/Includes/IMixExporter.h
    Audio/MixProjectLoader.cpp
    Audio/MixProjectLoader.h
    Audio/MixPlaybackEngine.cpp
    Audio/MixPlaybackEngine.h
    Audio/Equalizer.cpp
    Audio/Equalizer.h
    Audio/Reverb.cpp
    Audio/Reverb.h
    Audio/Model/EQSettings.cpp
    Audio/Model/EQSettings.h
    Audio/Model/ReverbSettings.h
    
    # Utils files
    Utils/AssortedUtils.cpp
    Utils/StringWriter.cpp
    Utils/UiUtils.cpp
    Utils/LoggingUtils.cpp
    Utils/AssortedUtils.h
    Utils/StringWriter.h
    Utils/UiUtils.h
    
    # Database files
    Database/TrackLibrary.cpp
    Database/TrackScanner.cpp
    Database/TrackLibrary.h
    Database/TrackScanner.h
    Database/BackgroundService.cpp
    Database/BackgroundService.h
    Database/DatabaseBackupManager.cpp
    Database/DatabaseBackupManager.h
    Database/UndoManager.cpp
    Database/UndoManager.h
    
    # Background Tasks
    Database/BackgroundTasks/BpmAnalysis.cpp
    Database/BackgroundTasks/BpmAnalysis.h
    Database/BackgroundTasks/BpmAnalysisTask.cpp
    Database/BackgroundTasks/BpmAnalysisTask.h
    Database/BackgroundTasks/Mp3QuickCheck.cpp
    Database/BackgroundTasks/Mp3QuickCheck.h
    Database/BackgroundTasks/AudioAnalysis.cpp
    Database/BackgroundTasks/AudioAnalysis.h
    Database/BackgroundTasks/WaveformLoadingTask.cpp
    Database/BackgroundTasks/WaveformLoadingTask.h
    
    # Database includes
    UI/ILongRunningTask.h
    Database/Includes/Constants.h
    Database/Includes/AlbumInfo.h
    Database/Includes/DataColumn.h
    Database/Includes/FolderInfo.h
    Database/Includes/IAlbumManager.h
    Database/Includes/IBackgroundTask.h
    Database/Includes/IEQPresetManager.h
    Database/Includes/IReverbPresetManager.h
    Database/Includes/IFolderDatabase.h
    Database/Includes/IMixManager.h
    Database/Includes/IMarkerManager.h
    Database/Includes/IMixMarkerManager.h
    Database/Includes/INavigationNode.h
    
    Database/Includes/IRefCounted.h
    Database/Includes/ITagManager.h
    Database/Includes/ITrackDatabase.h
    Database/Includes/ITrackInfoScanner.h
    Database/Includes/IWorkingSetManager.h
    Database/Includes/MixInfo.h
    Database/Includes/ExportFolderInfo.h
    Database/Includes/TrackInfo.h
    Database/Includes/TrackMarker.h
    Database/Includes/MixMarker.h
    Database/Includes/TrackQueryArgs.h
    Database/Includes/EQPreset.h
    Database/Includes/ReverbPreset.h
    
    # SQLite files
    Database/Sqlite/sqlite3.c
    Database/Sqlite/sqlite3.h
    Database/Sqlite/sqlite3ext.h
    Database/Sqlite/SqliteDatabase.cpp
    Database/Sqlite/SqliteDatabase.h
    Database/Sqlite/SqliteFolderDatabase.cpp
    Database/Sqlite/SqliteFolderDatabase.h
    Database/Sqlite/SqliteMixManager.cpp
    Database/Sqlite/SqliteMixManager.h
    
    
    Database/Sqlite/SqliteMarkerManager.cpp
    Database/Sqlite/SqliteMarkerManager.h
    Database/Sqlite/SqliteMixMarkerManager.cpp
    Database/Sqlite/SqliteMixMarkerManager.h
    
    
    Database/Sqlite/SqliteStatement.cpp
    Database/Sqlite/SqliteStatement.h
    Database/Sqlite/SqliteStatementConstruction.cpp
    Database/Sqlite/SqliteStatementConstruction.h
    Database/Sqlite/SqliteTagManager.cpp
    Database/Sqlite/SqliteTagManager.h
    Database/Sqlite/SqliteTrackDatabase.cpp
    Database/Sqlite/SqliteTrackDatabase.h
    Database/Sqlite/SqliteTransaction.cpp
    Database/Sqlite/SqliteTransaction.h
    Database/Sqlite/SqliteWorkingSetManager.cpp
    Database/Sqlite/SqliteWorkingSetManager.h
    Database/Sqlite/SqliteAlbumManager.cpp
    Database/Sqlite/SqliteAlbumManager.h
    Database/Sqlite/SqliteEQPresetManager.cpp
    Database/Sqlite/SqliteEQPresetManager.h
    Database/Sqlite/SqliteReverbPresetManager.cpp
    Database/Sqlite/SqliteReverbPresetManager.h
    
    # Database Nodes
    Database/Nodes/WorkingSetsOverview.cpp
    Database/Nodes/BaseNode.cpp
    Database/Nodes/LibraryNode.cpp
    Database/Nodes/AlbumsNode.cpp
    Database/Nodes/MixesOverview.cpp
    Database/Nodes/VirtualFolderNode.cpp
    Database/Nodes/VirtualFoldersOverview.cpp
    Database/Nodes/MixNode.cpp
    Database/Nodes/RootNode.cpp
    Database/Nodes/WorkingSetNode.cpp
    Database/Nodes/ExportedRootNode.cpp
    Database/Nodes/ExportFolderNode.cpp
    Database/Nodes/ExportYearNode.cpp
    Database/Nodes/ExportMonthNode.cpp
    Database/Nodes/TypedItemsOverview.h
    Database/Nodes/TypedOverviewNode.h
    Database/Nodes/WorkingSetsOverview.h
    Database/Nodes/BaseNode.h
    Database/Nodes/LibraryNode.h
    Database/Nodes/AlbumsNode.h
    Database/Nodes/VirtualFolderNode.h
    Database/Nodes/VirtualFoldersOverview.h
    Database/Nodes/MixesOverview.h
    Database/Nodes/MixNode.h
    Database/Nodes/RootNode.h
    Database/Nodes/TypedContainerNode.h
    Database/Nodes/WorkingSetNode.h
    Database/Nodes/ExportedRootNode.h
    Database/Nodes/ExportFolderNode.h
    Database/Nodes/ExportYearNode.h
    Database/Nodes/ExportMonthNode.h
    
    # Database Scanners
    Database/Scanners/AubioScanner.cpp
    Database/Scanners/AubioScanner.h
    Database/Scanners/Id3TagScanner.cpp
    Database/Scanners/Id3TagScanner.h
    
    # UI files
    UI/ColumnConfiguratorDialog.cpp
    UI/DynamicColumnManager.cpp
    UI/Settings.cpp
    UI/CreateMixDialogComponent.cpp
    UI/CreateWorkingSetDialogComponent.cpp
    UI/DataViewComponent.cpp
    UI/DividerComponent.cpp
    UI/DynamicToolbarComponent.cpp
    UI/SettingsDialog.cpp
    UI/JucyLookAndFeel.cpp
    UI/JucyLookAndFeel.h
    UI/TaskDialog.cpp
    UI/Main.cpp
    UI/MainComponent.cpp
    UI/MainPlaybackAndStatusComponent.cpp
    UI/TimerMultiplexer.cpp
    UI/NavigationPanelComponent.cpp
    UI/PlaybackController.cpp
    UI/EnhancedPlayerComponent.cpp
    UI/EqualizerComponent.cpp
    UI/EqualizerDialog.cpp
    UI/ReverbComponent.cpp
    UI/ReverbDialog.cpp
    UI/LibraryRootsComponent.cpp
    UI/ColumnConfiguratorDialog.h
    UI/DynamicColumnManager.h
    UI/Settings.h
    UI/CreateMixDialogComponent.h
    UI/CreateWorkingSetDialogComponent.h
    UI/DataViewComponent.h
    UI/DividerComponent.h
    UI/DynamicToolbarComponent.h
    UI/SettingsDialog.h
    UI/SingletonDialog.h
    UI/TaskDialog.h
    UI/MainComponent.h
    UI/MainPlaybackAndStatusComponent.h
    UI/NavigationPanelComponent.h
    UI/PlaybackController.h
    UI/EnhancedPlayerComponent.h
    UI/EqualizerComponent.h
    UI/ReverbComponent.h
    UI/LibraryRootsComponent.h
    UI/CheckboxLookAndFeel.h
    UI/CheckboxLookAndFeel.cpp
    UI/ThemeManager.cpp
    UI/ThemeManager.h
    UI/MenuManager.cpp
    UI/MenuManager.h
    UI/AboutDialog.cpp
    UI/AboutDialog.h
    UI/MenuPresenter.cpp
    UI/MenuPresenter.h
    UI/MixEditorComponent.cpp
    UI/MixEditorComponent.h
    UI/MarkerRulerComponent.cpp
    UI/MarkerRulerComponent.h
    UI/MixTrackComponent.cpp
    UI/MixTrackComponent.h
    UI/TimelineComponent.cpp
    UI/TimelineComponent.h
    UI/VirtualTimelineComponent.cpp
    UI/VirtualTimelineComponent.h
    UI/OverlayComponent.cpp
    UI/OverlayComponent.h
    UI/TrackRenderer.cpp
    UI/TrackRenderer.h
    UI/EditWorkingSetMetaDataDialog.cpp
    UI/EditWorkingSetMetaDataDialog.h
    UI/MarkerEditDialog.cpp
    UI/MarkerEditDialog.h
    UI/NavigationTree.cpp
    UI/NavigationTree.h
    UI/EditMixMetaDataDialog.cpp
    UI/EditMixMetaDataDialog.h
    Database/Includes/WorkingSetInfo.h
    Database/Includes/LibraryRootInfo.h
    Database/Includes/ILibraryRootManager.h
    Database/Sqlite/SqliteLibraryRootManager.h
    Database/Sqlite/SqliteLibraryRootManager.cpp
    UI/SplashScreenComponent.h
    UI/SplashScreenComponent.cpp
    UI/ExportMixDialog.h
    UI/ExportMixDialog.cpp
    UI/VUMeterComponent.h
    UI/VUMeterComponent.cpp
    UI/StatusBarComponent.h
    UI/StatusBarComponent.cpp
    Audio/Includes/ActiveExportSettings.h)

# Add Windows-specific manifest
if(WIN32)
    target_sources(jucyaudio PRIVATE app.manifest)
endif()

# Add binary resources (SVG files and images)
juce_add_binary_data(jucyaudio_assets
    SOURCES
        Assets/pause.svg
        Assets/play_arrow.svg
        Assets/stop.svg
        Assets/prev.svg
        Assets/next.svg
        Assets/orange-jucyaudio-logo.png
        Assets/orange-jucyaudio-small.png
        Assets/mute.svg
        Assets/high-audio.svg
        Assets/low-audio.svg
        Assets/shuffle.svg
        Assets/repeat.svg
        Assets/create_working_set.svg
        Assets/create_mix.svg
        Assets/show_mix_editor.svg
        Assets/show_track_editor.svg
        Assets/export_mix.svg
        Assets/delete.svg
        Assets/remove_tracks.svg
        Assets/show_in_folder.svg
        Assets/remove_duplicates.svg
        Assets/run_bpm_analysis.svg
        Assets/show_details.svg
        Assets/edit_mix_metadata.svg
        Assets/edit_workingset_metadata.svg
        Assets/settings.svg
        Assets/scan_folders.svg
        Assets/equalizer.svg
        Assets/reverb.svg
)

# Platform-specific library linking
if(WIN32)
    target_link_libraries(jucyaudio PRIVATE
        # JUCE modules
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        
        # Binary resources
        jucyaudio_assets

        # nlohmann/json
        nlohmann_json::nlohmann_json
        
        # tabulate
        tabulate::tabulate

        # tomlplusplus
        tomlplusplus::tomlplusplus

        # External libraries (Windows)
        ${AUBIO_LIBRARY}
        spdlog::spdlog
        tag
        ${PTHREADS_LIBRARY}
        ${LAME_LIBRARY}
        ${FFTW3F_LIBRARY}
        SndFile::sndfile

        ${ICU_UC_LIBRARY}
        ${ICU_I18N_LIBRARY}
    )
else()
    target_link_libraries(jucyaudio PRIVATE
        # JUCE modules
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        
        # Binary resources
        jucyaudio_assets

        # nlohmann/json
        nlohmann_json::nlohmann_json
        
        # tabulate
        tabulate::tabulate

        # tomlplusplus
        tomlplusplus::tomlplusplus

        # External libraries (macOS/Linux)
        ${AUBIO_LIBRARY}
        spdlog::spdlog_header_only
        tag
        ${MP3LAME_LIBRARY}
        SndFile::sndfile

        z  # zlib

        ICU::uc
        ICU::i18n
    )
endif()

# --- Post-Build Step: Force-inject the macOS Icon (Manual Override) ---
if(APPLE)
    # Define paths using CMake generator expressions to get the final build locations
    set(APP_BUNDLE_PATH "$<TARGET_BUNDLE_DIR:jucyaudio>")
    set(PLIST_PATH "${APP_BUNDLE_PATH}/Contents/Info.plist")
    set(RESOURCES_PATH "${APP_BUNDLE_PATH}/Contents/Resources")
    set(SOURCE_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Assets/JucyAudio.icns")

    add_custom_command(
        TARGET jucyaudio POST_BUILD
        
        # COMMAND 1: Forcibly copy our correct icon into the app bundle's Resources folder.
        # This will overwrite any garbage file JUCE may have created.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SOURCE_ICON_PATH}"
                "${RESOURCES_PATH}/JucyAudio.icns"
        
        # COMMAND 2: Use Apple's 'plutil' tool to directly modify the Info.plist file.
        # This will set the icon file key to the correct name.
        COMMAND /usr/bin/plutil -replace CFBundleIconFile -string "JucyAudio.icns" "${PLIST_PATH}"
        
        COMMENT "Manually fixing macOS application icon..."
    )
endif()

# Include directories
target_include_directories(jucyaudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${EXTERNAL_INCLUDES}
)

# Platform-specific preprocessor definitions
if(WIN32)
    target_compile_definitions(jucyaudio PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:jucyaudio,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:jucyaudio,JUCE_VERSION>"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_USE_MP3AUDIOFORMAT=1
        PROJECT_NAME="${PROJECT_NAME}"
        PROJECT_VERSION="${PROJECT_VERSION}"
        # Disable JUCE assertions in Debug builds to avoid overly strict thread checking
        $<$<CONFIG:Debug>:JUCE_DISABLE_ASSERTIONS=1>
    )
    
    # Windows-specific compiler flags
    target_compile_options(jucyaudio PRIVATE
        /utf-8
    )
else()
    target_compile_definitions(jucyaudio PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:jucyaudio,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:jucyaudio,JUCE_VERSION>"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_USE_MP3AUDIOFORMAT=1
        PROJECT_NAME="${PROJECT_NAME}"
        PROJECT_VERSION="${PROJECT_VERSION}"
        # Disable JUCE assertions in Debug builds to avoid overly strict thread checking
        $<$<CONFIG:Debug>:JUCE_DISABLE_ASSERTIONS=1>
    )
endif()

# Platform-specific settings
if(APPLE)
    target_compile_definitions(jucyaudio PRIVATE
        JUCE_USE_OGGVORBIS=1
    )
endif()

# Compiler flags for non-Windows platforms
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(jucyaudio PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Special handling for sqlite3.c to avoid warnings
set_source_files_properties(Database/Sqlite/sqlite3.c PROPERTIES
    COMPILE_FLAGS "$<IF:$<PLATFORM_ID:Windows>,-w,-w>"
)

# --- Post-Build Step: Copy Resources (Modern Approach) ---

# Define a variable for the source directory for clarity
set(THEMES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/themes")

if(APPLE)
    # On macOS, the generator expression for the Resources directory is clean.
    set(THEMES_DEST_DIR "$<TARGET_FILE_DIR:jucyaudio>/../Resources/themes")
elseif(WIN32)
    # On Windows, we want it next to the .exe file.
    set(THEMES_DEST_DIR "$<TARGET_FILE_DIR:jucyaudio>/themes")
else()
    # Sensible Linux fallback
    set(THEMES_DEST_DIR "$<TARGET_FILE_DIR:jucyaudio>/themes")
endif()

# Add a custom command that runs after the build.
# It uses generator expressions to get the final paths at build time.
add_custom_command(
    TARGET jucyaudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${THEMES_DEST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${THEMES_SOURCE_DIR}" # Source
            "${THEMES_DEST_DIR}"   # Destination
    COMMENT "Copying themes to application bundle..."
)

# ============================================================================
# macOS Bundle Dependencies - Bundle all dylibs into the .app
# ============================================================================
if(APPLE)
    # Find dylibbundler
    find_program(DYLIBBUNDLER dylibbundler)

    if(DYLIBBUNDLER)
        message(STATUS "Found dylibbundler: ${DYLIBBUNDLER}")

        # Add custom command to bundle dependencies after build
        add_custom_command(TARGET jucyaudio POST_BUILD
            # Clean extended attributes that interfere with codesigning
            COMMAND xattr -cr "$<TARGET_BUNDLE_DIR:jucyaudio>"
            # Bundle dependencies
            COMMAND ${DYLIBBUNDLER}
                -od -b
                -x "$<TARGET_BUNDLE_DIR:jucyaudio>/Contents/MacOS/jucyaudio"
                -d "$<TARGET_BUNDLE_DIR:jucyaudio>/Contents/libs/"
                -p "@executable_path/../libs/"
            COMMENT "Bundling dependencies into .app..."
            VERBATIM
        )
    else()
        message(WARNING "dylibbundler not found. Install with: brew install dylibbundler")
        message(WARNING "The .app will require Homebrew libraries to be installed.")
    endif()
endif()

# ============================================================================
# Installation Rules
# ============================================================================
include(GNUInstallDirs)

if(APPLE)
    # macOS Bundle Installation
    install(TARGETS jucyaudio BUNDLE DESTINATION .)

    # Install themes to the bundle
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/themes"
            DESTINATION "jucyaudio.app/Contents/Resources")

    # Code signing support (optional)
    if(DEFINED ENV{MACOS_CODESIGN_IDENTITY})
        set(CODESIGN_IDENTITY "$ENV{MACOS_CODESIGN_IDENTITY}")
        message(STATUS "Will sign with identity: ${CODESIGN_IDENTITY}")

        install(CODE "
            execute_process(
                COMMAND codesign --deep --force --verify --verbose
                        --sign \"${CODESIGN_IDENTITY}\"
                        --options runtime
                        \"\${CMAKE_INSTALL_PREFIX}/jucyaudio.app\"
                RESULT_VARIABLE result
                OUTPUT_VARIABLE output
                ERROR_VARIABLE error
            )
            if(NOT result EQUAL 0)
                message(WARNING \"Code signing failed: \${error}\")
            else()
                message(STATUS \"Code signing successful\")
            endif()
        ")
    endif()

elseif(WIN32)
    install(TARGETS jucyaudio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/themes" DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
    install(TARGETS jucyaudio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/themes" DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ============================================================================
# CPack Configuration - Creates DMG on macOS, ZIP on Windows
# ============================================================================
# Skip InstallRequiredSystemLibraries on macOS - we bundle everything in the .app
if(NOT APPLE)
    include(InstallRequiredSystemLibraries)
endif()

set(CPACK_PACKAGE_NAME "JucyAudio")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "P-nand-Q")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "JucyAudio - Professional Audio Curation & Mixing")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "JucyAudio")

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "JucyAudio ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO")  # Compressed

    # Only include the .app bundle, not bin/lib/include directories
    set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};ALL;/")

    # Package filename includes architecture
    if(CMAKE_OSX_ARCHITECTURES)
        string(REPLACE ";" "_" ARCH_STRING "${CMAKE_OSX_ARCHITECTURES}")
        set(CPACK_PACKAGE_FILE_NAME "JucyAudio-${PROJECT_VERSION}-macOS-${ARCH_STRING}")
    else()
        set(CPACK_PACKAGE_FILE_NAME "JucyAudio-${PROJECT_VERSION}-macOS-${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    # Optional: Custom DMG appearance
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/dmg-background.png")
        set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/Assets/dmg-background.png")
    endif()
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME "JucyAudio-${PROJECT_VERSION}-Windows-${ARCH_DIR}")
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_FILE_NAME "JucyAudio-${PROJECT_VERSION}-Linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Include CPack AFTER setting all variables
include(CPack)

# ============================================================================
# Status Messages
# ============================================================================
message(STATUS "=====================================")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    message(STATUS "Target Architecture: ${CMAKE_OSX_ARCHITECTURES}")
endif()
message(STATUS "=====================================")