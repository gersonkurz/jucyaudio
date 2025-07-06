cmake_minimum_required(VERSION 3.22)
project(jucyaudio VERSION 0.2.0)

# C++20 requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find or fetch JUCE
find_package(PkgConfig REQUIRED)
include(FetchContent)

# Option 1: Use local JUCE installation
# set(JUCE_ROOT "/Users/gersonkurz/development/JUCE")
# find_package(JUCE CONFIG REQUIRED)

# Option 2: Fetch JUCE from GitHub (recommended)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.7
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Find external libraries
find_library(AUBIO_LIBRARY aubio PATHS /Users/gersonkurz/development/aubio_install_macos/lib)
find_library(SPDLOG_LIBRARY spdlog PATHS /Users/gersonkurz/development/spdlog/build)
find_library(TAGLIB_LIBRARY tag PATHS /Users/gersonkurz/development/taglib_install/lib)
find_library(MP3LAME_LIBRARY mp3lame PATHS /opt/homebrew/lib)

# Include directories
set(EXTERNAL_INCLUDES
    /Users/gersonkurz/development/aubio_install_macos/include
    /Users/gersonkurz/development/spdlog/include
    /Users/gersonkurz/development/taglib_install/include
    /Users/gersonkurz/development/tomlplusplus-master/include
    /opt/homebrew/include
)

# Create the main application
juce_add_gui_app(jucyaudio
    PRODUCT_NAME "jucyaudio"
    BUNDLE_ID "com.yourcompany.jucyaudio"
    VERSION 1.0.0
    MACOS_DEPLOYMENT_TARGET 15.0
)

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
    Audio/MixExporter.cpp
    Audio/AudioLibrary.h
    Audio/ExportMixToWav.h
    Audio/MixExporter.h
    Audio/Includes/IMixExporter.h
    
    # Utils files
    Utils/AssortedUtils.cpp
    Utils/StringWriter.cpp
    Utils/UiUtils.cpp
    Utils/AssortedUtils.h
    Utils/StringWriter.h
    Utils/UiUtils.h
    
    # Database files
    Database/TrackLibrary.cpp
    Database/TrackScanner.cpp
    Database/TrackLibrary.h
    Database/TrackScanner.h
    
    # Database includes
    UI/ILongRunningTask.h
    Database/Includes/Constants.h
    Database/Includes/FolderInfo.h
    Database/Includes/IFolderDatabase.h
    Database/Includes/IMixManager.h
    Database/Includes/INavigationNode.h
    Database/Includes/ITagManager.h
    Database/Includes/ITrackDatabase.h
    Database/Includes/ITrackInfoScanner.h
    Database/Includes/IWorkingSetManager.h
    Database/Includes/MixInfo.h
    Database/Includes/TrackInfo.h
    Database/Includes/TrackQueryArgs.h
    
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
    
    # Database Nodes
    Database/Nodes/WorkingSetsOverview.cpp
    Database/Nodes/BaseNode.cpp
    Database/Nodes/LibraryNode.cpp
    Database/Nodes/MixesOverview.cpp
    Database/Nodes/LogicalFolderNode.cpp
    Database/Nodes/MixNode.cpp
    Database/Nodes/RootNode.cpp
    Database/Nodes/WorkingSetNode.cpp
    Database/Nodes/TypedItemsOverview.h
    Database/Nodes/TypedOverviewNode.h
    Database/Nodes/WorkingSetsOverview.h
    Database/Nodes/BaseNode.h
    Database/Nodes/LibraryNode.h
    Database/Nodes/MixesOverview.h
    Database/Nodes/LogicalFolderNode.h
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
    UI/TaskDialog.cpp
    UI/Main.cpp
    UI/MainComponent.cpp
    UI/MainPlaybackAndStatusComponent.cpp
    UI/NavigationPanelComponent.cpp
    UI/PlaybackController.cpp
    UI/PlaybackToolbarComponent.cpp
    UI/ScanDialogComponent.cpp
    UI/ColumnConfiguratorDialog.h
    UI/DynamicColumnManager.h
    UI/Settings.h
    UI/CreateMixDialogComponent.h
    UI/CreateWorkingSetDialogComponent.h
    UI/DataViewComponent.h
    UI/DividerComponent.h
    UI/DynamicToolbarComponent.h
    UI/TaskDialog.h
    UI/MainComponent.h
    UI/MainPlaybackAndStatusComponent.h
    UI/NavigationPanelComponent.h
    UI/PlaybackController.h
    UI/PlaybackToolbarComponent.h
    UI/ScanDialogComponent.h
    UI/ThemeManager.cpp
    UI/ThemeManager.h
)

# Add binary resources (SVG files)
juce_add_binary_data(jucyaudio_assets
    SOURCES
        Assets/pause.svg
        Assets/play_arrow.svg
        Assets/repeat.svg
        Assets/repeat_all.svg
        Assets/repeat_one.svg
        Assets/shuffle.svg
        Assets/shuffle_on.svg
        Assets/stop.svg
        Assets/volume_down.svg
        Assets/volume_off.svg
        Assets/volume_up.svg
)

# Link everything together
target_link_libraries(jucyaudio PRIVATE
    # JUCE modules
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    
    # Binary resources
    jucyaudio_assets
    
    # External libraries
    ${AUBIO_LIBRARY}
    ${SPDLOG_LIBRARY}
    ${TAGLIB_LIBRARY}
    ${MP3LAME_LIBRARY}
    z  # zlib
)

# Include directories
target_include_directories(jucyaudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${EXTERNAL_INCLUDES}
)

# Preprocessor definitions
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

# Platform-specific settings
if(APPLE)
    target_compile_definitions(jucyaudio PRIVATE
        JUCE_USE_OGGVORBIS=1
    )
endif()

# Compiler flags
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
    COMPILE_FLAGS "-w"  # Disable warnings for sqlite3.c
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