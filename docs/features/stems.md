# AI Stem Separation Plan

## Goal
Enable users to separate any audio track into 4 distinct stems (**Drums, Bass, Vocals, Other**) to allow for creative, mashup-style mixing (e.g., mixing the drums of Track A with the vocals of Track B).

## 1. Legal & Licensing
*   **Inference Engine**: **ONNX Runtime** (MIT License). Fully GPL-compatible.
*   **Algorithm**: **Demucs** (MIT License).
*   **Models**: Standard Demucs models are trained on MusDB (CC-BY-NC).
    *   *Strategy*: We do not bundle the models. The application will download them on demand (like a "DLC"). The user uses them locally.

## 2. Technical Architecture

### 2.1 Inference Engine
We will use **ONNX Runtime (ORT)** in C++.
*   **Why**: Lighter than LibTorch, highly optimized for CPU (AVX2/AVX512), and has no complex Python dependencies.
*   **Execution**:
    1.  Convert input audio to Tensor (Float32, Stereo, 44.1kHz).
    2.  Run Inference (Split into chunks to avoid OOM).
    3.  Overlap-Add output chunks to reconstruct waveforms.

### 2.2 Integration Workflow
This is an **Offline Process** (not real-time). Real-time separation is too CPU intensive for a generic DJ app on average hardware.

**User Flow**:
1.  Right-click a track in Library -> "Generate Stems".
2.  **Background Task**:
    *   Show progress bar (e.g., "Separating... 45%").
    *   Engine runs inference.
    *   Output: 4 new WAV files stored in `UserMusic/Stems/Artist - Title/`.
3.  **Database Update**:
    *   The stems are imported as new "Child Tracks" linked to the original.
4.  **Mix Editor**:
    *   When the user drags the original track to the timeline, they can choose "Load Stems".
    *   This creates a **Group Track** containing the 4 stem tracks, routed to a bus.

## 3. Implementation Steps

### Phase 1: The AI Engine (`Audio/AI`)
1.  [ ] **Dependency**: Add `onnxruntime` via CMake (FetchContent or NuGet/system lib).
2.  [ ] **Class `StemSeparator`**:
    *   `loadModel(path)`: Loads `.onnx` model.
    *   `process(inputFile, outputFolder)`: Handles the audio IO and tensor conversion.

### Phase 2: Model Management
1.  [ ] **HuggingFace Integration**: Script/Code to download `htdemucs_ft.onnx` (quantized version for speed).
2.  [ ] **UI**: "AI Models" tab in Settings to manage downloads.

### Phase 3: Database & UI
1.  [ ] **Schema**: Add `parent_track_id` and `stem_type` ('drums', 'bass', etc.) to `Tracks` table.
2.  [ ] **Context Menu**: Add "Separate Stems" action.
3.  [ ] **Track Editor**: Visualize stems if available.

## 4. Hardware Acceleration
*   ONNX Runtime supports **DirectML** (Windows) and **CoreML** (macOS) automatically.
*   We should enable these providers in CMake to get GPU acceleration (10x speedup).

## 5. Risks
*   **Model Size**: Models are ~100MB+.
    *   *Mitigation*: Download on demand.
*   **Memory Usage**: Demucs needs RAM.
    *   *Mitigation*: Process audio in small 10s chunks.
*   **Audio Quality**: Artifacts are inevitable.
    *   *Mitigation*: Use the latest Hybrid Transformer models (htdemucs) which are state-of-the-art.

## 6. Conclusion
This brings "Serato Stems" capability to JucyAudio using purely open-source, GPL-compatible tech.
