# JucyAudio 2.2.0

An audio player, mix editor and library manager for people with large music collections and a
preference for building their own sets.

This is the first release since **1.0.0** (December 2025). 1.1.0 was tagged but never published, so
everything in it is here too — over 230 commits across roughly nine months. Two things happened in that
time: the feature set grew a lot, and the parts that touch your files and your database were taken
apart and made much harder to break.

---

## Highlights

**VST3 plugin hosting.** A master-bus plugin chain with real-time processing, per-plugin and global
bypass, plugin editor windows (the plugin's own UI where it has one, a generic fallback where it does
not), and live CPU-load monitoring. The chain is saved to the database and comes back on restart.
Scanning uses a dead-man's pedal, so a plugin that brings the scan down is recorded and skipped the
next time you scan — the scan that hits it can still take JucyAudio with it, because scanning runs
in-process. VST processing is applied to WAV and MP3 exports, not just to playback.

**projectM visualizer.** Hardware-accelerated visuals via projectM v4, with the ~9,800-preset "Cream
of the Crop" collection bundled. Placeable at the bottom, left or right, with automatic preset
switching on track change and a configurable rotation interval.

**Smart Automix.** Transition points are chosen from the audio rather than from a fixed crossfade
length: a full-track energy contour and phrase-boundary detection pick attach points that match energy
and land on phrase boundaries. Analysis is done once at mix creation and cached in the database, with
a deterministic fallback when it cannot be computed. It can be turned off.

**A real theme system.** Over twenty [base16](https://github.com/chriskempson/base16) schemes plus a
brand default, loaded from upstream `.yaml` rather than hand-converted copies, with a theme picker.

**Scheduled and batch export.** Queue mixes for export and run them together later, with per-track and
overall progress, a cancel button that actually stops the render, and exports that leave the previous
file intact when they fail.

**A Windows installer that works.** The old NSIS script predated the visualizer and shipped neither
`projectM-4.dll` nor the presets. It is gone, replaced by a self-contained MSI that ships the MSVC
runtime app-local — no VC++ redistributable to chase.

---

## Downloads

| Platform | File |
|---|---|
| Windows x64 | `jucyaudio-2.2.0-x64.msi` |
| Windows arm64 | `jucyaudio-2.2.0-arm64.msi` |
| macOS Apple Silicon | `JucyAudio-2.2.0-macOS-arm64.dmg` |
| macOS Intel | `JucyAudio-2.2.0-macOS-x86_64.dmg` |

The Windows installers are self-contained: desktop and Start Menu shortcuts, an "Open with jucyaudio"
shell entry, and an Add/Remove Programs entry. No redistributable is required.

**On Windows on ARM, choose deliberately.** The arm64 build runs natively, and its VST3 host can only
load plugins built for ARM64 — an ARM64X hybrid counts, but x64 and ARM64EC-only plugins will not load
at all. The x64 build runs on the same machine under emulation and keeps your existing x64 plugins
working. So: **arm64 if you use no plugins or have ARM64 ones, x64 if your plugin collection is x64.**
Both are the same product to Windows Installer — they share an upgrade code — so install one or the
other, not both.

---

## Upgrading from 1.0.0

**Your library upgrades itself.** The database schema has moved a long way — the current version is
**32** — and the migration ladder runs automatically the first time 2.2.0 opens your library. There is
nothing to run by hand.

**A backup *check* runs before the database is opened**, not after. It makes a backup if the newest
one is old enough, and skips if a recent one already exists — so it is a safety net, not a guarantee
that the state immediately before this particular migration is on disk. **Take your own copy before
upgrading**, and keep it until you are satisfied.

Quit every JucyAudio instance first, then copy `jucyaudio.db`. The database runs in WAL mode and more
than one instance is allowed, so copying the file while anything still has it open can miss committed
pages that are still in `jucyaudio.db-wal` - which produces a file that looks like a backup and
restores torn. A clean quit settles that. (The application's own backups use SQLite's backup API, which
reads a consistent snapshot of a live database, so they do not have this problem.)

How many backups are kept is `NumberOfBackups` (default 5) under `[Backup]` in the config, and pruning
can be switched off entirely with `EnablePruning`.

**If your library was created by a 2.0-era development build, v32 repairs it.** Six tables, two
indexes and eight triggers used to be created only by the migration ladder and not by the code that
built a brand-new database, so libraries created from scratch during 2.0 development were quietly
missing full-text search, both marker tables, and the EQ and reverb presets. Separately, a `PRIMARY
KEY(mix_id, track_id)` that only migrated libraries ever had prevented a mix from holding the same
track twice. v32 fixes both, and a self test now runs a frozen version-12 database up every rung and
compares it structurally against a freshly created one, so the two cannot drift apart again.

**Where things live.** `%LOCALAPPDATA%\jucyaudio` on Windows, `~/Library/Application Support/jucyaudio`
on macOS. Both can be overridden with the `JUCYAUDIO_CONFIG` environment variable, which is also the
safe way to try this release against a scratch copy of your library first.

---

## What's new

### Mixing

- Mix recovery: when a mix is exported to WAV or MP3, what it contained is recorded separately, so the
  record survives the loss of the tracks it describes. A damaged mix is then described rather than
  refused. Records are written at export time only — exporting a `.m3u` does not write one, and mixes
  that already existed are not backfilled, so a mix has a recovery record only once you have exported
  it under this version.
- Companion `.m3u` playlist written alongside an exported mix, which outlives the database.
- Set an album's genre from a tag cloud in the mix editor; renaming a genre updates every album that
  used it.
- Fast MP3 seeking via warmed, reusable readers, with bounded warm-up.
- Missing tracks are shown as missing in track lists, can be re-checked against disk without a full
  rescan, and stop being missing when the file comes back.
- Mix creation checks for missing files before writing anything, and a cancelled check returns you to
  the dialog rather than dropping the mix.
- The mix editor refuses to edit a mix it could not read, rather than editing an empty one.
- Stored mix length is derived from the tracks rather than carried along, so editing a mix now leaves
  a correct length behind it. Eight of the paths that wrote a mix used to pass along whatever total
  they were holding, and appending to a mix added to the previous total instead of replacing it —
  which is how a five-hour mix came to be stored as sixty-six hours. Values that are *already* wrong
  are not fixed on startup: run `JucyAudio --repair-mix-durations` once to sweep the library. It
  forces a backup first and refuses to continue without one.

### Library

- Track Details dialog: selectable and paged.
- Copy the current selection to the clipboard.
- Full-text search index kept in sync with the library.
- Filter-syntax help popup, so the query language is discoverable.
- Non-Latin names render correctly in the navigation tree instead of as `????`.
- Play count and last-played timestamp are updated on playback.
- Window position is restored where you left it.
- Database backups, with a definite answer about whether each one worked.

### Exporting

- The export dialog can rename the mix, so renaming and exporting no longer need two dialogs. The
  track title, track number and output filename follow the new name unless you have edited them
  yourself.
- Scheduled exports: defer mixes and batch-run them, with dual progress and correct weighting by track
  count.
- A render that fails keeps the file it was replacing, proven by a test that injects the failure.
- Cancel stops the render.

### Audio

- Playlist queue with next/previous navigation, now-playing highlight, and persisted shuffle and
  repeat modes.
- Negative `cueStart` (pre-silence) handled in both playback and export.
- Mix duration uses effective duration rather than full file duration.
- Export no longer clamps gain differently from real-time playback.
- Equalizer parameters update through a lock-free atomic shared pointer.

### Under the hood

- **JUCE 9.0.2** (from 8.0.x), **SQLite 3.53.3**, and a self test that demonstrates what the JUCE
  upgrade fixes: MP3 VBR files with padding after the ID3v2 header now read correctly, and a truncated
  WAV whose data chunk claims more bytes than the file holds no longer reads as if the missing samples
  existed.
- The VST3 chain no longer allocates, locks or logs on the audio callback. A plugin that throws used
  to be logged from the callback — a string allocation, formatting, a sink mutex and a flush, at the
  moment you are listening. It is now recorded into a fixed buffer and reported from the UI timer, and
  it is stopped by the host's own flag rather than by a call that takes a lock the message thread also
  holds. This covers the plugin chain; it is not a claim about every path in the audio engine.
- A scan no longer deletes or flags tracks under a library root it could not reach — a drive that is
  not plugged in is not evidence that its files are gone — and refuses outright when the folder cache
  it reads its scope from could not be built.
- "Last Scanned" now means a scan that finished.
- Folder track counts are maintained by the scanner itself, so a cancelled or failed scan no longer
  leaves them stale.
- One row per folder path, and a merge that makes it true.
- SQL injection in filter criteria fixed; parameterised queries throughout.
- Several memory-safety fixes: a double-buffer use-after-free, a dangling pointer in SQLite blob
  binding, a TOCTOU race in the undo manager, and a buffer-overflow risk in the playback engine.
- A headless self test — 849 checks across eight suites — runs the scanner, the migration ladder, the
  exporters, the folder cache and the transaction layer without a GUI.

### Fixed

Among many: a mix editor playhead that jumped to the start after an attach-point edit; drag-and-drop
reorder collisions and drop targeting; a double undo required after deleting a mix track; accidental
track reorder from a plain click; navigation tree rebuilds after deleting a node; MP3 exports that
wrote stack memory into a large ID3v2 tag and truncated it; exported MP3s carrying an unfinished LAME
Info frame at the head and a duplicate at the end, which made duration and seeking unreliable in other
players; Windows failing to read zlib-compressed ID3v2 metadata; and macOS linking two copies of zlib.

---

## Known limitations

- **Reorganising your files can break the mixes that used them.** A *moved* file can keep its
  identity, and every mix that references it with it — but only when the move is unambiguous: exactly
  one library row matching by filename and size, exactly one new file matching, and the old location
  confirmed absent. That last condition is why a drive that is not plugged in is never mistaken for a
  deletion, and it is also why a move can go unrecognised. Anything ambiguous is recorded as a new
  track, and the mixes that used the old one are left pointing at a track that is now missing.
  Renaming never matches, because the match is on the filename.

  So moving folders is usually safe and renaming files is not, but neither is a guarantee. Rescan
  afterwards and check your mixes. There is currently no in-app repair for a mix that ends up pointing
  at a missing track; the design for one is
  [#65](https://github.com/gersonkurz/jucyaudio/issues/65).
- A mix's timeline is corrected on the next edit rather than immediately in one stale-state case
  ([#37](https://github.com/gersonkurz/jucyaudio/issues/37)).
- A folder read can miss while the cache is being rebuilt
  ([#36](https://github.com/gersonkurz/jucyaudio/issues/36)).
- The full duplicate-detection system (SHA-256, Chromaprint, library-wide review) is deferred. What
  ships is the in-working-set metadata dedup.
- No user manual yet.

---

## Requirements

- **Windows:** x64, or arm64 for Windows on ARM. See the note under Downloads about VST3 plugin
  architecture before picking.
- **macOS:** Apple Silicon or Intel.

## Building

`just build` and `just run`; see
[`docs/build.md`](https://github.com/gersonkurz/jucyaudio/blob/main/docs/build.md). Dependencies are
fetched automatically by CMake on first build, and `just build-offline` reuses the cache afterwards.

## License

GPL-3.0. Third-party notices ship with the application and are listed in
`THIRD_PARTY_NOTICES.txt`.
