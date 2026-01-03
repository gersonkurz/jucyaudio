# ProjectM Integration Plan for JucyAudio (Reviewed & Extended)

## Status: Approved for Implementation
**Reviewers:** Codex (Architect), Claude (Systems Integration)

## 1. Executive Summary
This document outlines the architectural plan to integrate **projectM v4** into JucyAudio. The goal is to provide a high-performance, hardware-accelerated music visualization system that runs natively on Windows and macOS. The integration handles the complexities of bridging JUCE's audio/rendering threads with projectM's OpenGL requirements.

## 2. Architecture & Components

### 2.1. Build System (`CMakeLists.txt`)
We leverage `FetchContent` for a hermetic build.
*   **Dependency**: `projectM` (Tag: `v4.1.1` - Stable).
*   **Options**:
    *   `PROJECTM_BUILD_TESTING=OFF`
    *   `PROJECTM_BUILD_EXAMPLES=OFF`
    *   `PROJECTM_BUILD_LLVM_JIT=OFF` (Avoid heavy LLVM dependency unless strictly necessary; default parser is sufficient).
*   **Linking**: Link `projectM::projectM` privately to `jucyaudio`.
*   **Post-Build**: 
    *   Copy `projectM` default assets (textures, shaders) to `Runtime/Assets/projectm`.
    *   Copy a curated set of `.milk` presets to `Runtime/Assets/presets`.

### 2.2. Audio Pipeline (Thread-Safe Bridge)
**Challenge**: Tapping the audio stream without blocking the sensitive audio mixing thread.
**Solution**: A lock-free Single Producer, Single Consumer (SPSC) FIFO.

*   **Component**: `AudioVisualizerFIFO` (New Class)
    *   **Mechanism**: `juce::AbstractFifo` with a `std::vector<float>` circular buffer.
    *   **Format**: Mono downmix (L+R)/2, float, 48kHz (or host rate).
    *   **Capacity**: ~2048 samples (approx 40ms buffer is sufficient for visualization latency).
    *   **Writer (Audio Thread)**: `MixPlaybackEngine` pushes mixed blocks *after* processing.
    *   **Reader (Render Thread)**: `ProjectMVisualizerComponent` polls this buffer every frame.

### 2.3. Rendering Pipeline (OpenGL Integration)
**Challenge**: Managing OpenGL contexts between JUCE and projectM.
**Solution**: JUCE owns the context; projectM renders into it.

*   **Component**: `ProjectMVisualizerComponent`
    *   **Inherits**: `juce::Component`, `juce::OpenGLRenderer`.
    *   **Lifecycle**:
        1.  **`newOpenGLContextCreated`**:
            *   Initialize `projectm_handle`.
            *   **CRITICAL**: Pass a custom OpenGL loader function if projectM v4 requires it, or rely on JUCE having established the context. *Note: projectM v4 typically uses an internal loader (glad) or system headers. We must ensure the context is active.*
            *   Load textures from `File::getSpecialLocation(currentExecutableLocation)`.
        2.  **`renderOpenGL`**:
            *   Check `m_visualizerFifo` for new audio data.
            *   `projectm_pcm_add_float()`: Feed pending audio.
            *   `projectm_opengl_render_frame()`: Draw to the current framebuffer.
        3.  **`openGLContextClosing`**:
            *   `projectm_destroy()`: Cleanup to prevent leaks.
    *   **Optimization**:
        *   Only render when the component is visible (`isVisible()`).
        *   Limit frame rate to 30 or 60 FPS via `setContinuousRepainting(false)` and using a `Timer` or `VBlankAttachment` if needed to save battery.

## 3. Detailed Implementation Steps

### Phase 1: Build & Dependencies
1.  **Update `CMakeLists.txt`**:
    *   Add `FetchContent` block for projectM.
    *   Define asset copy commands.
2.  **Sanity Check**: Verify projectM compiles and links on Windows (MSVC) and macOS (Clang).

### Phase 2: Core Infrastructure (The Bridge)
1.  **`Utils/AudioVisualizerFIFO.h`**:
    *   Implement the lock-free buffer.
2.  **`Audio/MixPlaybackEngine.h/cpp`**:
    *   Add `AudioVisualizerFIFO` member.
    *   Inject/Setter method to attach the FIFO.
    *   Modify `getNextAudioBlock` to perform the write.

### Phase 3: Visualizer Engine
1.  **`UI/Visualizer/ProjectMComponent.h`**:
    *   Define the class structure.
2.  **`UI/Visualizer/ProjectMComponent.cpp`**:
    *   **Initialization**: Locate assets dynamically.
    *   **Rendering**: Implement the bridge to `libprojectM` C API.
    *   **Input**: Handle resize events (`projectm_set_window_size`).

### Phase 4: Integration & UX
1.  **UI Placement**:
    *   Add `ProjectMComponent` to `MainComponent`.
    *   Create a toggle/tab to show/hide it.
2.  **Preset Management**:
    *   Scan the `presets` directory.
    *   Implement `Next`/`Prev` preset logic (mapped to UI buttons or keys).
3.  **Settings**:
    *   Add "Visualizer FPS" or "High Quality" toggle in SettingsDialog.

## 4. Risk Mitigation
*   **OpenGL Version**: ProjectM v4 requires modern GL (3.3+). We must ensure `juce::OpenGLContext` requests a compatible version (`setOpenGLVersionRequired`).
*   **Context Loss**: If the window moves between monitors or minimizes, context might be lost. `newOpenGLContextCreated` handles recreation, but we must ensure projectM is fully re-initialized.
*   **Asset Paths**: Mac bundles place assets in `Resources`, Windows next to `.exe`. Use a unified `AssetLocator` helper.

# Codex Comments
- The FIFO size (2048 samples) may underflow if the render thread stalls; consider a larger buffer and drop strategy.
- projectM expects a sample rate; document how to pass the actual host rate (or resample) so visuals stay stable at 44.1k vs 48k.
- Add an explicit fallback/log when asset paths are missing to avoid silent black frames.

# Claude Comments
- The SPSC FIFO design is sound, but consider using `juce::AbstractFifo` with a power-of-2 buffer size (2048 is good) for optimal performance. The mono downmix (L+R)/2 is appropriate for visualization.
- For OpenGL context management: JUCE's `OpenGLContext::setOpenGLVersionRequired()` should request GL 3.3 Core Profile explicitly. ProjectM v4 uses modern GL and will fail silently on legacy contexts.
- Thread priority: The render thread should not compete with audio. Consider setting `setContinuousRepainting(false)` and using a 30fps Timer rather than VBlank sync to reduce CPU load when visuals aren't the focus.
- Memory consideration: ProjectM presets can allocate significant GPU memory. Implement a "low memory" mode that disables the visualizer when system resources are constrained.
- The asset copy strategy should use CMake's `install(DIRECTORY...)` rather than post-build commands for better cross-platform compatibility and incremental builds.
