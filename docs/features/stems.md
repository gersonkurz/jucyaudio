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

# Codex Comments
- Demucs models are CC-BY-NC; note the non-commercial constraint and how it affects distribution.
- Define a cleanup/management story for stem files (storage location, deletion, and re-generation).
- Consider a per-track cache key (model version + params) to avoid recomputing stems.

# Claude Comments
- **CC-BY-NC licensing concern**: The non-commercial clause on MusDB-trained models is a real issue if JucyAudio is ever commercialized. Consider also supporting Open-Unmix (UMX) models which are MIT-licensed, though slightly lower quality. Let users choose.
- **ONNX model conversion**: Pre-converted ONNX models for htdemucs are not officially provided by Facebook. You'll need to convert them yourself using `torch.onnx.export()` and host them. Document this process and version the models.
- **Chunked processing**: The 10-second chunk recommendation is conservative. htdemucs works well with 30-second chunks with 5-second overlap, reducing boundary artifacts. Use overlap-add with a Hann window for smooth transitions.
- **Stem file management**: Store stems in `{DatabaseFolder}/Stems/{TrackId}/` rather than by artist name - this avoids filesystem issues with special characters and makes cleanup trivial (delete folder when track is removed).
- **Database schema**: Add `stem_generation_status` enum (none/pending/complete/failed) and `stem_model_version` to Tracks table. The "child track" approach adds complexity; consider a separate `Stems` table with foreign key to parent track.
- **Memory management**: Demucs needs ~4GB RAM for a 3-minute track. Check available memory before starting and queue stems for processing when resources are available. Show estimated wait time in UI.
- **GPU acceleration**: DirectML (Windows) and CoreML (macOS) providers in ONNX Runtime can provide 5-10x speedup. Make this the default with CPU fallback.
