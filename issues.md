# JucyAudio - Issues & Code Review Findings

---

## Existing Feature Requests / Ideas

- If not all mixes can be shown because a folder is unmounted, show them hidden - if selected, instead of showing data, show a special screen "not available because XYZ is not mounted"
- Support being able to right-click on a file and play it in jucyaudio. Might have to add this file then to the database in case it does not exist yet?
- migrate from spdlog to quill - more logging, less performance impact.
- long-term task: support for folder reorganization needs to be improved

## New issues found while getting ready for the 1.0 release

- **Refactor: Extract crossfade calculation helper** - The crossfade/envelope logic in `UI/CreateMixDialogComponent.cpp` (append to mix) and `Database/Sqlite/SqliteMixManager.cpp` (createAndSaveAutoMix) are near-duplicates. Extract to a shared helper like `Database/Includes/MixTrackUtils.h` with a `calculateCrossfadeForTrack()` function to avoid drift if thresholds or envelope curves change.

