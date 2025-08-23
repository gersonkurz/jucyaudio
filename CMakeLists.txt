cmake_minimum_required(VERSION 3.22)
project(jucyaudio VERSION 0.7.0)

# Enable debug info in Release builds for better debugging while maintaining performance
if(MSVC)
  # Visual Studio: Add debug info to Release builds
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
else()
  # macOS/Linux: Enable RelWithDebInfo-like flags for Release builds
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -g1 -DNDEBUG")
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

# Find or fetch JUCE
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE
)

# Add nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

# Add tabulate for pretty table printing
FetchContent_Declare(
    tabulate
    GIT_REPOSITORY https://github.com/p-ranav/tabulate.git
    GIT_TAG v1.5
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE nlohmann_json tabulate)

# Platform-specific library paths and includes
if(WIN32)
     set(WIN32_DEPS_ROOT "C:/Projects/jucyaudio_deps_win32_x64")

    # Windows paths from jucer file
    set(AUBIO_LIB_PATH "${WIN32_DEPS_ROOT}/aubio/lib")
    set(SPDLOG_LIB_PATH_DEBUG "${WIN32_DEPS_ROOT}/spdlog/out/install/x64-Debug/lib")
    set(SPDLOG_LIB_PATH_RELEASE "${WIN32_DEPS_ROOT}/spdlog/out/install/x64-Release/lib")
    set(TAGLIB_LIB_PATH_DEBUG "${WIN32_DEPS_ROOT}/taglib/out/install/x64-Debug/lib")
    set(TAGLIB_LIB_PATH_RELEASE "${WIN32_DEPS_ROOT}/taglib/out/install/x64-Release/lib")
    set(LAME_LIB_PATH "${WIN32_DEPS_ROOT}/lame")
    set(PTHREADS_LIB_PATH "${WIN32_DEPS_ROOT}/pthreads-w32/lib")
    set(FFTW_LIB_PATH "${WIN32_DEPS_ROOT}/fftw")
    set(LIBSNDFILE_LIB_PATH "${WIN32_DEPS_ROOT}/libsndfile/lib")
    set(FFMPEG_LIB_PATH "${WIN32_DEPS_ROOT}/ffmpeg/lib")

    
    set(AUBIO_INCLUDE_PATH "${WIN32_DEPS_ROOT}/aubio/include")
    set(SPDLOG_INCLUDE_PATH_DEBUG "${WIN32_DEPS_ROOT}/spdlog/out/install/x64-Debug/include")
    set(SPDLOG_INCLUDE_PATH_RELEASE "${WIN32_DEPS_ROOT}/spdlog/out/install/x64-Release/include")
    set(TAGLIB_INCLUDE_PATH_DEBUG "${WIN32_DEPS_ROOT}/taglib/out/install/x64-Debug/include")
    set(TAGLIB_INCLUDE_PATH_RELEASE "${WIN32_DEPS_ROOT}/taglib/out/install/x64-Release/include")
    set(LAME_INCLUDE_PATH "${WIN32_DEPS_ROOT}/lame/include")
    set(TOMLPLUSPLUS_INCLUDE_PATH "${WIN32_DEPS_ROOT}/tomlplusplus/include")
    set(PTHREADS_INCLUDE_PATH "${WIN32_DEPS_ROOT}/pthreads-w32/include")
    set(FFTW_INCLUDE_PATH "${WIN32_DEPS_ROOT}/fftw")
    set(LIBSNDFILE_INCLUDE_PATH "C:/Projects/libsndfile/include")
    set(FFMPEG_INCLUDE_PATH "${WIN32_DEPS_ROOT}/ffmpeg/include")   
    set(ICU_LIB_PATH_DEBUG "${WIN32_DEPS_ROOT}/icu/lib")
    set(ICU_LIB_PATH_RELEASE "${WIN32_DEPS_ROOT}/icu/lib")
    set(ICU_INCLUDE_PATH "${WIN32_DEPS_ROOT}/icu/include")
    
    # Use debug or release paths based on build type
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(SPDLOG_LIB_PATH ${SPDLOG_LIB_PATH_DEBUG})
        set(TAGLIB_LIB_PATH ${TAGLIB_LIB_PATH_DEBUG})
        set(ICU_LIB_PATH ${ICU_LIB_PATH_DEBUG}) 
        set(SPDLOG_INCLUDE_PATH ${SPDLOG_INCLUDE_PATH_DEBUG})
        set(TAGLIB_INCLUDE_PATH ${TAGLIB_INCLUDE_PATH_DEBUG})
    else()
        set(SPDLOG_LIB_PATH ${SPDLOG_LIB_PATH_RELEASE})
        set(TAGLIB_LIB_PATH ${TAGLIB_LIB_PATH_RELEASE})
        set(ICU_LIB_PATH ${ICU_LIB_PATH_RELEASE})
        set(SPDLOG_INCLUDE_PATH ${SPDLOG_INCLUDE_PATH_RELEASE})
        set(TAGLIB_INCLUDE_PATH ${TAGLIB_INCLUDE_PATH_RELEASE})
    endif()
    
    # Find libraries on Windows
    find_library(AUBIO_LIBRARY aubio PATHS ${AUBIO_LIB_PATH} REQUIRED)
    find_library(SPDLOG_LIBRARY spdlog PATHS ${SPDLOG_LIB_PATH} REQUIRED)
    find_library(TAGLIB_LIBRARY tag PATHS ${TAGLIB_LIB_PATH} REQUIRED)
    find_library(PTHREADS_LIBRARY pthreadVC2 PATHS ${PTHREADS_LIB_PATH} REQUIRED)
    find_library(LAME_LIBRARY mp3lame PATHS ${LAME_LIB_PATH} REQUIRED)
    find_library(FFTW3F_LIBRARY libfftw3f-3 PATHS ${FFTW_LIB_PATH} REQUIRED)
    find_library(FFTW3_LIBRARY libfftw3-3 PATHS ${FFTW_LIB_PATH})
    find_library(LIBSNDFILE_LIBRARY libsndfile-1 PATHS ${LIBSNDFILE_LIB_PATH} REQUIRED)
    
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
        ${SPDLOG_INCLUDE_PATH}
        ${TAGLIB_INCLUDE_PATH}
        ${LAME_INCLUDE_PATH}
        ${TOMLPLUSPLUS_INCLUDE_PATH}
        ${PTHREADS_INCLUDE_PATH}
        ${FFTW_INCLUDE_PATH}
        ${LIBSNDFILE_INCLUDE_PATH}
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
    find_library(SPDLOG_LIBRARY spdlog PATHS /Users/gersonkurz/development/spdlog/build)
    find_library(TAGLIB_LIBRARY tag PATHS /Users/gersonkurz/development/taglib_install/lib)
    find_library(MP3LAME_LIBRARY mp3lame PATHS /opt/homebrew/lib)
    
    # macOS/Linux external includes
    set(EXTERNAL_INCLUDES
        /Users/gersonkurz/development/aubio_install_macos/include
        /Users/gersonkurz/development/spdlog/include
        /Users/gersonkurz/development/taglib_install/include
        /Users/gersonkurz/development/tomlplusplus-master/include
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

# Add Windows resource file for icon
if(WIN32)
    target_sources(jucyaudio PRIVATE windows_icon.rc)
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
    
    # Background Tasks
    Database/BackgroundTasks/BpmAnalysis.cpp
    Database/BackgroundTasks/BpmAnalysis.h
    Database/BackgroundTasks/BpmAnalysisTask.cpp
    Database/BackgroundTasks/BpmAnalysisTask.h
    Database/BackgroundTasks/Mp3QuickCheck.cpp
    Database/BackgroundTasks/Mp3QuickCheck.h
    Database/BackgroundTasks/AudioAnalysis.cpp
    Database/BackgroundTasks/AudioAnalysis.h
    
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
    Database/Includes/IUndoManager.h
    Database/Includes/IRefCounted.h
    Database/Includes/ITagManager.h
    Database/Includes/ITrackDatabase.h
    Database/Includes/ITrackInfoScanner.h
    Database/Includes/IWorkingSetManager.h
    Database/Includes/MixInfo.h
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
    Database/Sqlite/SqliteMixManagerWithUndo.cpp
    Database/Sqlite/SqliteMixManagerWithUndo.h
    Database/Sqlite/SqliteMarkerManager.cpp
    Database/Sqlite/SqliteMarkerManager.h
    Database/Sqlite/SqliteMixMarkerManager.cpp
    Database/Sqlite/SqliteMixMarkerManager.h
    Database/Sqlite/SqliteUndoManager.cpp
    Database/Sqlite/SqliteUndoManager.h
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
        
        # External libraries (Windows)
        ${AUBIO_LIBRARY}
        ${SPDLOG_LIBRARY}
        ${TAGLIB_LIBRARY}
        ${PTHREADS_LIBRARY}
        ${LAME_LIBRARY}
        ${FFTW3F_LIBRARY}
        ${LIBSNDFILE_LIBRARY}

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
        
        # External libraries (macOS/Linux)
        ${AUBIO_LIBRARY}
        ${SPDLOG_LIBRARY}
        ${TAGLIB_LIBRARY}
        ${MP3LAME_LIBRARY}
        
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
        SQLITE_ENABLE_FTS5
        SQLITE_ENABLE_JSON1
        SQLITE_ENABLE_STAT4
        SQLITE_DQS=0
        SPDLOG_COMPILED_LIB
        SPDLOG_USE_STD_FORMAT
        SPDLOG_MSVC_UTF8
        TAGLIB_STATIC
        PROJECT_NAME="${PROJECT_NAME}"
        PROJECT_VERSION="${PROJECT_VERSION}"
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
        SPDLOG_COMPILED_LIB
        TAGLIB_STATIC
        PROJECT_NAME="${PROJECT_NAME}"
        PROJECT_VERSION="${PROJECT_VERSION}"
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