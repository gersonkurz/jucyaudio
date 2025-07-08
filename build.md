# Building Instructions for JucyAudio

## Building on the Mac
... TBD ...

## Building on Windows

### Pre-Requisites

- Get jucyaudio from GIT: `git clone git@github.com:gersonkurz/jucyaudio.git`
- Download `jucyaudio_deps_win32_x64.7z` and extract it to `C:\Projects`, so that you end up with a folder `C:\Projects\jucyaudio_deps_win32_x64` that includes all the Windows x64 dependencies. (Go ahead, try to compile everything on your own, I dare you. I did it, so you don't have to. And I am not going to document it, because I don't want to do it again. If you want to do it, go ahead and PR me the instructions.)

### Building with Visual Studio 2022

- Open Visual Studio 2022 
- Select "Open folder" (not: "Open project or solution")
- Navigate to the `jucyaudio` folder you just cloned
- Visual Studio will automatically detect the CMakeLists.txt file and configure the project
- You may need to install the C++ CMake tools for Windows if prompted
- Once the project is configured, you can build it by selecting "Build" from the menu and then "Build Solution" (or pressing `Ctrl+Shift+B`)
- Before you attempt to start it for the first time, you *must* copy the files from `C:\Projects\jucyaudio_deps_win32_x64\dlls`to the `jucyaudio\build\Debug` or `jucyaudio\build\Release` folder, depending on your build configuration. This is necessary because the application depends on these DLLs to run properly.
That's it! You should now have a working build of JucyAudio on Windows.

### Building with Visual Studio Code

- Open Visual Studio Code
- Install the CMake Tools extension if you haven't already
- Open the `jucyaudio` folder you cloned
- CMake Tools should automatically detect the CMakeLists.txt file
- You may need to configure the CMake kit if prompted (select the appropriate Visual Studio 2022 kit)
- Once configured, you can build the project by running the "CMake: Build" command from the command palette (`Ctrl+Shift+P`)
- You can also run the project by selecting "CMake: Run" from the command palette
- Before you attempt to start it for the first time, you *must* copy the files from `C:\Projects\jucyaudio_deps_win32_x64\dlls`to the `jucyaudio\build\Debug` or `jucyaudio\build\Release` folder, depending on your build configuration. This is necessary because the application depends on these DLLs to run properly.

