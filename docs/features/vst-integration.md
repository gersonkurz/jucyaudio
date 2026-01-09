# VST3 Integration Plan for JucyAudio

## Status: Ready for Implementation
**Scope:** Master bus effects only (v1). Per-track effects deferred to v2.

## Goal

Enable JucyAudio to host **VST3 audio plugins** on the master bus, allowing users to apply professional-quality effects (EQ, Compression, Reverb, Limiting) to the final mix output.

**Motivation:** JucyAudio's built-in EQ and Reverb are basic implementations. VST3 support allows users to use professional-grade free plugins (TDR Nova, Dragonfly Reverb, etc.) without requiring us to become DSP experts.

## 1. Legal & Licensing

- **SDK**: Steinberg VST3 SDK (bundled with JUCE)
- **License**: Dual GPLv3/Proprietary (Compatible with JucyAudio's GPLv3)
- **Constraint**: **VST3 only**. VST2 is legally deprecated.

## 2. Scope: Master Bus Only (v1)

### What's In Scope
- VST3 plugin chain on master output (after EQ/Reverb, before visualizer)
- Plugin scanning (out-of-process for stability) and persistence
- Plugin UI windows (both generic and custom editors)
- Plugin state save/load (application settings)
- **Offline Export Support**: Effects must be applied to exported WAV/MP3 files

### What's Deferred (v2)
- Per-track effects
- Plugin Delay Compensation (PDC) — not needed for master-only
- Mix project plugin state storage (v1 uses global app settings for master chain)

### Why Master Bus Only?
1. **Simpler implementation** — No PDC complexity
2. **Immediate value** — Users get better EQ/Reverb/Limiting on final output
3. **Lower risk** — Easier to test and stabilize
4. **Extensible** — Architecture supports adding per-track later

## 3. Recommended Test Plugins (Free VST3)

These plugins are recommended for testing and can be suggested to users:

### Primary Test Set

| Plugin | Type | License | Download |
|--------|------|---------|----------|
| **TDR Nova** | Dynamic EQ | Freeware | https://www.tokyodawn.net/tdr-nova/ |
| **Dragonfly Reverb** | Reverb (4 types) | GPL-3.0 | https://github.com/michaelwillis/dragonfly-reverb/releases |
| **Airwindows Consolidated** | ~400 effects | MIT | https://www.airwindows.com/consolidated/ |

### Additional Test Plugins

| Plugin | Type | License | Download |
|--------|------|---------|----------|
| **Valhalla Supermassive** | Reverb/Delay | Freeware | https://valhalladsp.com/shop/reverb/valhalla-supermassive/ |
| **Kilohearts Essentials** | 32 effects bundle | Freeware | https://kilohearts.com/products/kilohearts_essentials |

### Test Coverage Matrix

| Scenario | Plugin to Use |
|----------|---------------|
| Custom GUI editor | TDR Nova, Dragonfly |
| Generic/minimal GUI | Airwindows Consolidated |
| Open source (debuggable) | Dragonfly Reverb |
| Many plugins in one | Airwindows Consolidated, Kilohearts |
| Latency-reporting plugin | Most compressors/limiters |

## 4. Architecture

### 4.1 Components

```
┌─────────────────────────────────────────────────────────────────┐
│                     Audio Signal Flow                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  PlaybackController::getNextAudioBlock()                        │
│       │                                                          │
│       ▼                                                          │
│  ┌─────────────┐                                                │
│  │ Built-in EQ │  (existing)                                    │
│  └──────┬──────┘                                                │
│         ▼                                                        │
│  ┌──────────────┐                                               │
│  │ Built-in     │  (existing)                                   │
│  │ Reverb       │                                               │
│  └──────┬───────┘                                               │
│         ▼                                                        │
│  ┌──────────────────────────────────────┐                       │
│  │ VST3 Plugin Chain (NEW)              │                       │
│  │                                      │                       │
│  │  Plugin 1 → Plugin 2 → ... → Plugin N│                       │
│  └──────┬───────────────────────────────┘                       │
│         ▼                                                        │
│  ┌─────────────────┐                                            │
│  │ Visualizer FIFO │  (existing)                                │
│  └──────┬──────────┘                                            │
│         ▼                                                        │
│     Audio Output                                                 │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 New Classes

**`Audio/Plugins/PluginManagerService.h`**
- Singleton service for plugin management
- Wraps `juce::AudioPluginFormatManager` and `juce::KnownPluginList`
- Handles scanning (out-of-process), blocklisting, persistence

**`Audio/Plugins/PluginChain.h`**
- Ordered list of `juce::AudioPluginInstance` pointers
- `processBlock(juce::AudioBuffer&)` iterates through plugins
- **Thread Safety**: Uses lock-free pointer swapping or ReferenceCountedObjects for safe graph updates during playback
- Bypass support per-plugin and global

**`UI/Plugins/PluginScanDialog.h`**
- Modal dialog for scanning plugin directories
- Progress bar, found/failed counts
- Blocklist management

**`UI/Plugins/PluginChainEditor.h`**
- UI component showing the master plugin chain
- Add/remove/reorder plugins
- Open plugin editor windows

**`UI/Plugins/PluginWindow.h`**
- Desktop window wrapping plugin's `AudioProcessorEditor`
- Falls back to generic editor if plugin has no custom UI

### 4.3 Storage

**Plugin List:** `~/.config/jucyaudio/plugins.xml` (or platform equivalent)
- Uses JUCE's `KnownPluginList::createXml()` format
- Includes blocklisted plugins with failure count

**Master Chain State:** Stored in **SQLite** (same DB as tracks)
- **Table:** `ApplicationSettings` or a new `MasterChainPlugins` table
- **Data:** `juce::MemoryBlock` (plugin state blob) + sort order + enabled state
- **Rationale:** TOML is unsuitable for large binary blobs; SQLite handles this natively.

**Stable Plugin UID Proposal**
- **Primary key:** `(format, id)` where:
  - `format` = `PluginDescription::pluginFormatName` (expect `VST3`)
  - `id` = `PluginDescription::fileOrIdentifier` (VST3 module + class)
- **Aux fields (diagnostics/migration hints):** `name`, `manufacturerName`, `version`
- **Restore matching:** first by `(format, id)`; if missing, optionally try `(name, manufacturerName)` with a warning and mark as unverified.

## 5. Implementation Plan

### Phase 1: Infrastructure & Scanning
1. [ ] Enable VST3 hosting in CMake (`JUCE_PLUGINHOST_VST3=1`)
2. [ ] Implement `PluginManagerService` singleton
3. [ ] Implement `PluginScanDialog` using **out-of-process scanning** (via `juce::AudioPluginFormatManager::scanPlugins` with `AudioPluginFormatManager::Scanner`) to prevent crashes.
4. [ ] Persist plugin list between sessions
5. [ ] Test with TDR Nova, Dragonfly, Airwindows

### Phase 2: Plugin Chain & Audio Engine
1. [ ] Implement `PluginChain` wrapper class (thread-safe)
2. [ ] Add `PluginChain` to `PlaybackController` (master bus)
3. [ ] Wire into `getNextAudioBlock()` after EQ/Reverb
4. [ ] **Offline Export:** Integrate `PluginChain` into `MixExporter` / `ExportMixImplementation` to ensure master effects are applied during offline render.
5. [ ] Test audio processing with multiple plugins

### Channel Layout Policy (Master Bus)
- Fix master chain to stereo in v1 (JUCE `AudioChannelSet::stereo()`).
- Validate each plugin with `isBusesLayoutSupported()` or equivalent; if unsupported, skip/disable and warn the user.
- Ensure the chain fails safe: no silent output if a plugin rejects the layout.

### Phase 3: UI Integration
1. [ ] Implement `PluginWindow` for hosting plugin editors
2. [ ] Implement `PluginChainEditor` component
3. [ ] Add "Master Effects" button/panel to UI (e.g., near Master Volume or EQ/Reverb controls)
4. [ ] Handle plugin editor open/close lifecycle

### Phase 4: State Persistence
1. [ ] Implement SQLite schema for `MasterChainPlugins`
2. [ ] Save master chain configuration (order + state blobs) to SQLite
3. [ ] Load and restore plugin states on startup
4. [ ] Handle missing plugins gracefully (warning, not crash)
5. [ ] Test save/load cycle with real plugins

### Phase 5: Polish
1. [ ] Add plugin bypass toggle (per-plugin and global)
2. [ ] Add CPU usage indicator
3. [ ] Settings UI for plugin scan paths
4. [ ] Documentation for users

## 6. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Bad plugin crashes app | High | **Out-of-process scanning**; blocklist after crash; recommend stable plugins |
| Plugin uses too much CPU | Medium | Add CPU meter; document performance expectations |
| Plugin not found on reload | Medium | Warning dialog; graceful degradation |
| Plugin has no UI | Low | JUCE provides generic parameter editor |

## 7. Future Expansion (v2)

When adding per-track effects:

1. **PDC Required**: Per-track plugins introduce latency misalignment
   - Calculate max latency across all tracks
   - Add delay lines to "fast" tracks to match "slow" tracks
   - Cap max latency at 8192 samples (~185ms)

2. **Mix Project Storage**: Plugin states need to be stored per-mix
   - New `MixPluginInstances` table in SQLite
   - Store plugin UID + version + state blob
   - Handle missing plugins on mix load

3. **UI Changes**:
   - "Add Effect" button per track in Mix Editor
   - Per-track bypass toggles

---

## Reviewer Comments

### Codex Comments
- **Scanning:** Out-of-process scanning is critical. Use `juce::KnownPluginList::scanPlugins` with a `FileSearchPath` and `AudioPluginFormatManager`.
- **Storage:** SQLite is definitely the right choice for state blobs.
- **PDC:** For v2, PDC needs a clear policy for max latency and what happens if plugins report extreme values.

### Claude Comments
- **Real-time safety:** `AudioPluginInstance::processBlock()` should be RT-safe for well-behaved plugins, but allocating/deallocating plugins must happen on the UI thread. Use garbage collection pattern (or `juce::ReferenceCountedObject`).
- **Blocklist persistence:** Store crashed plugins with timestamp and count, allowing users to retry after plugin updates.
- **Generic editor:** JUCE's `GenericAudioProcessorEditor` works well for Airwindows-style plugins with no custom UI.
- **Thread safety:** Plugin chain modifications must not happen during `getNextAudioBlock()`. Use atomic pointer swap or message queue pattern.