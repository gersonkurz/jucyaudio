# Resolving missing tracks in a mix

Design proposal. Status: **proposed, not built**. Written 2026-09-19.

## 1. The problem

A mix holds track ids. If the file behind one of them is gone, the mix still loads, the track still
holds its place in the timeline, and the user is told - but there is no way to say "use this other
recording instead". The mix is stuck until the original file comes back.

Worked example, from the report that prompted this:

```
[kept]    God - Hate Meditation
    D:\MP3\Unsorted\SHOEGAZE323\G\God\Possession\05. Hate Meditation.mp3
    (file not found)
```

## 2. What already exists

Most of the proposed UI is built. This is the main finding of the investigation and it changes the
size of the job.

| Piece | State | Where |
|---|---|---|
| A track-list view for a mix | **Exists**, `Cmd/Ctrl+2` | `DataAction::ShowTrackEditor`, `MainComponent::onShowTrackEditor` (`UI/MainComponent.cpp:4480`) |
| `MixNode` renders the mix's tracks as rows | **Exists** | `MixNode : LibraryNode` (`Database/Nodes/MixNode.h:14`) |
| Missing tracks shown in accent colour | **Exists** | `LibraryNode.cpp:320` - `is_missing` returns `RenderState::Accent`, explicitly chosen over `Inactive` so it does not read as grey in a dark theme |
| A per-track missing flag | **Exists** | `TrackInfo::is_missing` (`TrackInfo.h:153`), maintained by `MissingFileCheckTask` |
| A reusable searchable track list | **Exists** | `DataViewComponent` + `INavigationNode::setSearchTerms` |
| Whole-mix write path | **Exists** | `MixProjectLoader::getMixTracks()` (mutable) + `saveMix()` |
| Reporting failures on load | **Exists** | `MixEditorComponent.cpp:30-84`, `SkippedTracksDialog` |

So the proposal's "Mixes to support Track editor view. Identify missing tracks in accent color" is
already shipped. What is missing is the verb.

## 3. What is actually missing

1. A **"Replace with…"** action on a mix track row.
2. A **picker dialog**: the library list plus a search box, returning one `TrackId`.
3. The **substitution** itself - swapping the id in the mix and saving.
4. Deciding what happens to the replaced track's **cue and attach values**.

Items 1-3 are mechanical. Item 4 is the only real design question, and it is smaller than expected.

## 4. The timing question

The proposal assumed: *"we WILL have to adjust the mix positions for the rest of the mix, agreed"*.

That is not the case, and the reason is worth stating because it removes most of the risk.

Positions are **relative and chained**, not absolute. From `calculateMixTrackStarts`
(`Database/Includes/MixInfo.h:311`):

```cpp
const auto trackStart{i == 0 ? Duration_t{0} : previousTrackStart + tracks[i - 1].attachTo - tracks[i].attachFrom};
```

Durations do not appear. A track's start depends only on the previous track's `attachTo` and its own
`attachFrom`. The header says so directly, about this exact case:

> An unresolvable track still holds its place. It positions what follows exactly as it would if its
> audio were there, because `calculateMixTrackStarts` never asks - it just adds nothing to the end.

**Consequence:** if the replacement keeps the same `attachFrom` and `attachTo`, *nothing downstream
moves at all*. No other row is touched, in memory or in the database.

The mix's total length may change, but not for the reason it first appears. A missing file does not
mean a missing duration: the metadata row survives, and the stored summary reads `Tracks.duration`
without asking whether the file is there (`Database/Sqlite/SqliteMixSummary.cpp:52`), so a missing
track already contributes an end. What changes is only that the *new* track's duration is a different
number. The in-memory path can differ - `MixProjectLoader::calculateMixDuration` returns `nullopt` for
a track absent from its cache - which is part of why section 5.3 insists on a reload.

What can go wrong is narrower: `attachFrom` and `attachTo` are offsets from the track's own audio
start, and `MixInfo.h:135` requires them to be *"within the track's effective duration"*. A shorter
replacement can leave `attachTo` past the end of the new audio.

### Proposed rule

Old and new durations are both known - a missing track keeps its metadata row, including `duration`,
which is why the library listing in the report still shows `00:04:51`.

The constraint attach points must satisfy is stated at `MixInfo.h:26`, and it is not simply "inside
the track":

```
cueStart <= attach_point_time <= getCueEndActual(trackDuration)
```

with `getCueEndActual(d) = d + cueEnd`. So the legal window is the **retained segment**
`[cueStart, newDuration + cueEnd]`, not `[0, newDuration]`. Every rule below is expressed against that
window, because a rule that ignores the cue trims can put an attach point outside the audible part of
the track while looking perfectly valid.

Let `delta = newDuration - oldDuration`, `segStart = cueStart`, `segEnd = newDuration + cueEnd`.

1. **First, is the segment still viable?** If `segEnd <= segStart`, the retained cue trims describe
   nothing - a 60s replacement for a 300s track trimmed `cueStart=30s, cueEnd=-20s` leaves
   `[30s, 40s]`, and a 20s replacement leaves an empty segment. The cues **cannot** be carried over.
   Reset them to `0` and say so in the confirmation, or refuse the substitution; do not silently keep
   a segment that does not exist.
2. `gainAdjustment`: carry over unchanged. It is a scalar and has no relationship to duration.
3. `cueStart`, `cueEnd`: carry over unchanged **when step 1 passes**. `cueStart` is an in-point and
   `cueEnd` is an offset from the end, so both stay meaningful against different audio.
4. `attachFrom`: carry over unchanged. It sits near `segStart`, where a duration change does not
   reach - then clamp into `[segStart, segEnd]` for safety.
5. `attachTo`: shift by `delta`, so it stays the same distance from the end, which is what a crossfade
   out is. Then clamp into `[segStart, segEnd]`.
6. **If the result is degenerate** - `attachFrom >= attachTo` after clamping - recompute *within the
   retained segment*, not against the raw duration:

   ```
   segLength = segEnd - segStart
   cf        = calculateCrossfadeForTrack(segLength, defaultCrossfade)
   attachFrom = segStart + cf.attachFrom
   attachTo   = segStart + cf.attachTo
   ```

   `calculateCrossfadeForTrack` reasons about a *length*, so feeding it the segment length and
   offsetting the results by `segStart` keeps its short-track handling (reduced, then eliminated
   crossfade) while staying inside the window. Calling it with `newDuration` directly is wrong: with
   the `[30s, 40s]` segment above and a 10s crossfade it returns `[10s, 50s]`, both outside.

### Envelope points

**`scaleEnvelopePointsForAttachChange` cannot do this job**, and an earlier draft of this document
claimed it could. Its signature takes a single `trackDuration` (`MixInfo.h:195`), so it cannot express
"the source duration changed from X to Y": passing the new duration leaves points positioned against
the old end untouched, and passing the old one puts the endpoint in the wrong place. It is for
retuning attach points on a track whose audio is fixed.

`interpolateVolumeFromEnvelope` documents its input as *"The vector of envelope points sorted by
time"* (`Audio/AudioUtils.h:16`), so whatever the mapping does it must come out ordered. A rule that
moves some points and not others does not give that for free: translating only the tail by a negative
`delta` can move a tail point in front of a retained middle point, silently, with every point still
inside the segment so no clamp or drop catches it. An earlier draft of this document did exactly that.

The mapping is therefore **piecewise and monotonic by construction**, over the three regions the
attach points already define:

| Region | Maps to | Why |
|---|---|---|
| `[segStart, attachFrom]` - the fade-in | unchanged | anchored to `segStart`, which does not move, and a crossfade in keeps its length |
| `(attachFrom, attachTo_old)` - the middle | scaled into `(attachFrom, attachTo_new)` | the body absorbs the length change; this is the least damaging place to put it |
| `[attachTo_old, segEnd_old]` - the fade-out | translated by `delta` | the tail keeps its shape and moves with the end of the audio |

```
newMid = attachTo_new - attachFrom
oldMid = attachTo_old - attachFrom
t' = t                                                  for t <= attachFrom
t' = attachFrom + (t - attachFrom) * newMid / oldMid    for attachFrom < t < attachTo_old
t' = t + delta                                          for t >= attachTo_old
```

The three pieces are contiguous, each non-decreasing, and they meet at `attachFrom` and
`attachTo_new`, so the whole map is non-decreasing and **sortedness is preserved rather than restored
by a later sort**. Sorting afterwards would be worse than useless here: it would slide a retained
middle point into the middle of the fade-out it was never part of.

The image is exactly `[segStart, segEnd_new]`, because `segEnd_new = segEnd_old + delta` and the
fade-out piece maps `segEnd_old` onto it. So nothing needs clamping and nothing is dropped.

**The map is only monotonic while `newMid > 0`.** Once the new middle collapses the scale factor goes
negative and the middle points reverse. That is exactly the condition under which step 6 fires, so the
order is: decide the attach points first, and apply this map only on the carry-over path.

**On the step-6 fallback path**, discard the envelope and take `cf.envelopePoints` **offset by
`segStart`**, the same offset as the attach points. `calculateCrossfadeForTrack` was handed a segment
*length*, so everything it returns is relative to the segment, not to the source audio: for the
`[30s, 40s]` segment it returns points at `0s` and `10s`, both outside the window. Carrying the old
points instead would be wrong for a different reason - they were shaped for attach points that no
longer exist.

The confirmation should say which of the two happened - envelope carried over, or envelope
regenerated - because they are different amounts of lost work.

This is the part to build first and test on its own. It is a pure function from
`(MixTrack, oldDuration, newDuration, defaultCrossfade)` to `MixTrack`, with no UI and no database, and
it is the only place where being wrong corrupts a saved mix.

The rules above were checked by an arithmetic probe before being written down, in integer
milliseconds, covering shortening, lengthening, a non-zero `segStart`, and the collapsed middle. Two
drafts of the envelope rule failed it. When this is built, that probe becomes the self test, and the
first case in it should be the 300s-to-200s shortening that the translate-only rule got wrong:

```
points   [0s, 10s, 195s, 290s, 300s]   attachFrom=10s  attachTo=290s
translate-only -> [0, 10, 195, 190, 200]   unsorted, and every point still inside the segment
piecewise      -> [0, 10, 128.93, 190, 200]  sorted; fade-in fixed, tail moved by delta
```

## 5. Proposed design

### 5.1 Entry point

Add `DataAction::ReplaceTrack`, offered in `MixTrackRowActions` (`MixNode.cpp:11`).

**Not on double-click.** Double-click is already taken: `BaseNode::onRowActivated` returns
`RowActivationResultType::CheckTrackFile` for a missing track, with the comment that double-clicking a
missing track *"is much more likely to mean 'I put this back, look again'"*. That is the right default
and the cheaper fix - it costs nothing and often works. Replacement belongs in the context menu, one
deliberate step further on.

Offer the action on any row, not only missing ones. Substituting a track that is present is the same
operation and is a reasonable thing to want.

### 5.2 The picker

A modal dialog holding a `DataViewComponent` bound to a fresh `LibraryNode`, plus a search box wired
to `setSearchTerms` + `refreshView(true)` - the same pair the main window already uses, so the filter
syntax in `Utils/FilterParser` comes along unchanged.

Returns one `TrackId` or nothing. Seed the search box with the missing track's artist and title, since
that is nearly always what is wanted.

Show the chosen track's duration next to the old one, and warn when the difference is large enough to
trigger the step-4 fallback. The user is entitled to know the crossfade is about to be recomputed
before they commit, not after.

### 5.3 The write

Through `MixProjectLoader`, not `IMixManager::updateMixTrack`:

```cpp
auto &tracks = loader.getMixTracks();
tracks[row].trackId = newTrackId;   // plus the cue/attach rules above
if (!loader.saveMix(mixManager)) { /* see below */ }
if (!loader.reloadFromDatabase()) { /* see below */ }
```

`updateMixTrack` cannot do this job. It is `UPDATE MixTracks SET mix_data = ? WHERE mix_id = ? AND
track_id = ?` (`SqliteMixManager.cpp:1558`), so it cannot change the id it keys on - and since v32
allowed a mix to hold the same track twice, that `WHERE` can match two rows. `saveMix` rewrites the
whole track list, which is what a substitution needs.

**The reload is not optional.** `MixProjectLoader` keeps `m_trackInfosMap`, a map from `TrackId` to a
pointer into `m_trackInfos`, rebuilt only on load and reload (`MixProjectLoader.cpp:19`, called at
lines 100 and 160). Writing a new id into `m_mixTracks` does not put that track's metadata anywhere.
Until a reload:

- `getTrackInfoForRow()` returns `nullptr` for the substituted row, so the track-list view has no
  title, artist or duration to draw;
- `MixProjectLoader::calculateMixDuration` sees `nullopt` for it (`MixProjectLoader.cpp:247`) and
  leaves it out of the in-memory total, which then disagrees with the stored summary the save just
  computed correctly.

So the sequence is mutate, save, reload, and only then report success. Three failure points, and each
needs an answer rather than a shrug:

| Failure | Response |
|---|---|
| Save fails | The database is unchanged but the in-memory mix is not. Reload to discard the edit, and report the substitution as failed. |
| Save succeeds, reload fails | The database is correct and the in-memory state is stale. This is the existing `RemovalOutcome::ReloadFailed` case, which already tells the user to close and reopen the mix; reuse that wording rather than inventing a second one. |
| Save succeeds, reload succeeds | Report success, refresh the view. |

### 5.4 Guards

- **Exported mixes are read-only.** `RemovalOutcome::SkippedReadOnly` already exists for this; the
  action should be hidden or refused for a mix with an export folder.
- **Never save a mix whose load failed.** `MixNode::isCacheLoaded()` exists for exactly this and
  documents the hazard: *"saving what the loader holds after a failed read replaces the mix with
  nothing."* The action must check it.
- **Refuse a replacement whose own file is missing.** Swapping one missing track for another is never
  what the user meant.

## 6. When this is actually needed

The reporter's own follow-up - "it might be that I reordered that file and I first need to refresh the
library" - turns out to be the important question, because **a moved file is already handled and does
not need this feature at all.**

`TrackScanner` detects a move and keeps the track id, which keeps every mix that uses it
(`TrackScanner.cpp:543`):

```
Track {} ({}) moved; kept its id, and every mix that uses it.
```

It is deliberately conservative. A move is recognised only when **all three** hold
(`TrackScanner.cpp:507`): exactly one library row matches by normalized filename and size, exactly one
new file matches, and the old file is *confirmed* absent by going and looking - so a disconnected
drive is never mistaken for a deletion. Anything else is inserted as a new track, with the reasoning
stated in the source: *"That loses the history, which can be put back by hand; the alternative attaches
a mix to the wrong file and never says so."*

That is the right trade, and it draws the line around this feature precisely:

| Case | Fixed by a rescan? |
|---|---|
| File moved, unambiguously | **Yes** - id kept, mix intact. Nothing to build. |
| File moved, but two rows or two files share a name and size | No - inserted as new; old row missing |
| File moved while its old location was on an unplugged drive | No - not confirmed gone; inserted as new |
| File **renamed** | No - the key is (filename, size), so a rename never matches |
| File deleted for good, or a different recording is wanted | No - nothing to detect |

So the feature is for the tail, not the common case. The common case already works, and the first
thing the UI should do for a missing track is still "look again" - which double-click already does.

That tail is not small, though. Renaming is the one to note: reorganizing a library by renaming files
breaks every mix that used them, silently, with no way back. That connects this directly to issue #41
("Improve support for folder reorganization"), which is open, labelled `enhancement`, and explicitly
un-scoped: *"What 'improved' means is not defined in the original note, so this needs scoping before it
is actionable."* This proposal is one concrete answer to that scoping question.

## 7. Open questions for the human

1. **Where did the track actually end up?** Pending the reporter's library refresh. If the rescan
   restores it, this instance was the common case and proves the existing path works; the feature is
   still wanted for the tail above.
2. **Should this also offer "relink"** - repoint the *library* row at a new path, fixing every mix that
   uses it at once - rather than only substituting within one mix? Relink is the better fix whenever
   the file still exists somewhere, and after a bulk rename it is the only tolerable one: substituting
   track by track through fifty mixes is not a workflow. Relink has a wider blast radius and probably
   wants its own confirmation, but it may be the more valuable half.
3. **Multi-select.** Replace one row at a time, or "replace all missing in this mix"? One at a time is
   the honest first version. But see the rename case - if that is the real driver, per-mix repair is
   the wrong altitude and the answer is a library-level tool.

## 8. Effort and recommendation

Smaller than it first looked, because the view, the colouring, the search machinery and the write path
all exist.

| Piece | Size |
|---|---|
| `DataAction::ReplaceTrack` + menu wiring | small |
| Picker dialog | medium - mostly assembly, but not purely. `DataViewComponent`'s double-click handler calls `MainComponent::playDataRow(rowNumber)` directly (`UI/DataViewComponent.cpp:660`), which would play into the main view from inside the picker. Selection and activation need picker-specific behaviour before it can be reused as a chooser. |
| Cue/attach rules + guards | small |
| Self-test | small-to-medium; the rules are pure functions over `MixTrack` and testable without UI |

**Recommendation: do it, but not in one sitting, and not today if today also has to end with a
release.** The tree is at 2.2.0 with zero P1 and zero P2, and the outstanding release gates are a
macOS build, a package run and manual QA. A new dialog and a new mix-mutating write is the wrong thing
to land immediately before a cut - the write touches the one path that can replace a mix with nothing
if it is wrong.

Suggested order: settle the questions in section 7, cut 2.2.0, then build this against the whole
`docs/features` treatment it deserves.

One caveat on priority. Section 6 shows the common case already works, so the value here is narrower
than it first appeared - but question 2 may point somewhere more valuable than what was asked for. If
the real problem is a renamed or reorganized library rather than one lost file, a per-mix substitution
dialog repairs the symptom one row at a time while a library-level relink repairs the cause. Worth
settling before building either.

The cue/attach arithmetic is worth building first and on its own, whenever it starts. It is pure,
testable, and it is the only part where being wrong corrupts a mix rather than merely annoying
someone.
