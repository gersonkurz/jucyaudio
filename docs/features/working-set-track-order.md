# Working-set track order and "Shuffle Selection"

Status: design, not implemented. Requested 2026-10-12.

## Problem

A working set has no track order of its own. `WorkingSetTracks` is `(ws_id, track_id)` and nothing
else (`SqliteTrackDatabase.cpp:579`). What the user sees is the `ORDER BY` built from the working
set's saved column sort (`WorkingSets.sort_order`, applied by `SqliteStatementConstruction::addOrderByClause`).

For a set where the sort key carries little information, the order is effectively accidental. The
working set "DARK AMBIENT" (ws_id 7) holds 31,205 tracks from 4,170 folders, sorted by `bpm`
ascending. 25,397 have `bpm IS NULL` and 1,659 have `bpm = 0`; only 4,149 have a measured BPM. The
first ~27,000 rows are therefore ordered by whatever SQLite returns for equal keys, which in practice
is `track_id` order, i.e. import order, i.e. one folder/album after another.

The user wants to select a block of rows, right-click, and have exactly those rows put in random
order. The rest of the set must not move.

## Decisions taken with the user

- **U1** - Shuffle applies to the selected rows only, not to the whole set.
- **U2** - The order is stored per working set. Default for a new set: BPM ascending.
- **U3** - Column sorting stays as it is from the user's point of view: clicking a header sorts the
  set by that column.
- **U4** - Tracks added to an existing set are appended at the end; the set is not re-sorted.
- **U5** - After a shuffle the header keeps showing the last column sort applied (e.g. "BPM ▲").
  It is a record of the last sort, not a claim that the current order matches it.

## Design

### D1 - Schema: `WorkingSetTracks.position`

```sql
CREATE TABLE IF NOT EXISTS WorkingSetTracks(
    ws_id INTEGER NOT NULL, track_id INTEGER NOT NULL, position INTEGER, PRIMARY KEY(ws_id, track_id),
    FOREIGN KEY(ws_id) REFERENCES WorkingSets(ws_id) ON DELETE CASCADE,
    FOREIGN KEY(track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_workingsettracks_position ON WorkingSetTracks(ws_id, position);
```

`position` is nullable on purpose: `ALTER TABLE ADD COLUMN` cannot add a `NOT NULL` column without
a default, and the migration self-test compares nullability and defaults between a fresh and a
migrated database, so both must declare it the same way. A `NULL` position is handled by D3.

Positions within a set are unique after every write this design makes, but there is no `UNIQUE`
constraint: the shuffle (D5) and renumber (D4) rewrite many rows in one statement or transaction,
and a unique index would need a two-phase update to avoid transient collisions. Not worth it for
an ordering column.

Positions are not required to be dense. Removing tracks leaves gaps; nothing depends on density.

### D2 - Migration: schema v33

- `initialSqlStatements`: the new column in `CREATE TABLE WorkingSetTracks` and the new index.
- `latestSchemaVersion`: 32 → 33.
- New rung `if (currentVersion < 33)`, in one transaction:
  1. `ALTER TABLE WorkingSetTracks ADD COLUMN position INTEGER;`, **only if `PRAGMA
     table_info(WorkingSetTracks)` does not already list `position`** (see "Why the rung is
     idempotent" below).
  2. `CREATE INDEX IF NOT EXISTS idx_workingsettracks_position ...;`
  3. Sets with no saved sort get `sort_order = '[{"column":"bpm","descending":false}]'`
     (`WHERE sort_order IS NULL OR sort_order = ''`), so the header arrow matches what the set now
     is (U2).
  4. For every working set: `numberUnpositionedTracks(db, wsId, sortOrder)` (D8) with the set's
     saved `sort_order`. It only touches rows whose `position IS NULL`, which after step 1 on a real
     v32 library is every row, and on an already-complete database is none.

**Why the rung is idempotent.** Several migration self-tests build a database from today's schema
and stamp `SchemaInfo` back to an older version to exercise one rung (`SelfTests.cpp` around lines
4891, 5227, 5427, 5524, 5653, stamping 29-31). After this change those databases already hold
`position`, so an unconditional `ADD COLUMN` fails with "duplicate column". Two alternatives were
rejected:

- Reverting `WorkingSetTracks` to its v32 shape in each fixture (`DROP COLUMN position`). The
  convergence check at `SelfTests.cpp:5427` compares `sqlite_master` **text** between a fresh
  database and one sent back up the ladder, and requires equality. SQLite's `ADD COLUMN` appends the
  column text at the end of the stored `CREATE` statement, after the table constraints, so the
  re-added column would never match the fresh declaration and that check would fail by construction.
- Declaring the fresh column with the exact text `ADD COLUMN` produces. Fragile, unreadable.

The project already does this: the v10 rung checks `PRAGMA table_info(LibraryRoots)` before adding
columns, and the v32 rung is all `IF NOT EXISTS` so that "a database that already has all of it" is
a no-op (its own comment). For a real library below v33 the column cannot exist, so the check does
not change production behaviour; it only makes the rung safe to re-run, which the convergence test
requires of every rung above its stamp. The v12 fixtures, which have the genuine old shape, cover
the `ADD COLUMN` path.

The stamped fixtures need no edit: their `WorkingSetTracks` tables are empty, so step 4 has nothing
to number, and step 3 only touches working sets without a saved sort, of which none of them
create one. The convergence check therefore still sees zero changes. (Verified when implemented;
if any fixture does create a working set, step 3 changes data, not schema text, and the check
compares schema text.)

After the rung, every existing row has a non-`NULL` position, and every existing working set
displays exactly as it did before: the saved sort it was displayed by is the order that got stored.
DARK AMBIENT keeps its current chunked order until the user shuffles.

`convergenceSqlStatements` is not touched: this is a new object, so per CLAUDE.md it goes into
`initialSqlStatements` plus its own rung. The v12 fixtures in the migration self-test climb to 33
without edits, and the structural comparison covers the new column and index.

`trackDedupeSteps` (`SqliteTrackDatabase.cpp:180`) inserts into `WorkingSetTracks` without a
position. It only runs inside the v24 and v31 rungs, before this column exists, so it stays as it is.

Mixes are not affected. A mix's order is `MixTracks.order_in_mix`, which this design does not touch.

### D3 - Query: a working set is always displayed in `position` order

In `SqliteStatementConstruction::addOrderByClause`, when `args.workingSetId > 0`:

```sql
ORDER BY (SELECT position FROM WorkingSetTracks WHERE ws_id = ?N AND track_id = Tracks.track_id) NULLS LAST, track_id
```

`?N` reuses the parameter index already bound for the `WHERE ... ws_id = ?N` condition (numbered
SQLite parameters may appear more than once), so nothing extra is bound and the parameter-binding
code in `finalizeStatement` and the FTS branch is unchanged. That means both the plain and the
FTS-search branch need to remember which index they used for `ws_id`.

`args.sortBy` is ignored for working-set queries. The correlated subquery is used instead of a join
because a join would make `track_id` ambiguous in `trackColumnsForDecoding` and in the filter
criteria, which name columns unqualified.

`NULLS LAST, track_id` is the fallback for a row without a position, e.g. one written by an older
build that opened the database after the upgrade. Such a row is shown, at the end, in a stable place.

Cost: SQLite resolves the subquery through the `(ws_id, track_id)` primary key and sorts the set
for each page, the same work the current `ORDER BY bpm` already does. For 31k rows this is in the
tens of milliseconds; will be measured on DARK AMBIENT and reported.

Every consumer of a working-set `SELECT` gets position order, not only the view: `createMix`
(selected rows, in row order) and Remove Duplicates (`MainComponent.cpp:2014`, which keeps the
first row it sees). Both now follow what the user sees, which is the intended behaviour.

Creating a working set from another working set is different: the `INSERT ... SELECT` builder
(`createInsertIntoSelectTrackIdsStatement`) has no `ORDER BY`, so it copies membership only. The
new set is then numbered by D8 like any new set: BPM ascending (U2).

### D4 - Column sort renumbers the set

`IWorkingSetManager::updateSortOrder` is replaced by
`applySortOrder(WorkingSetId, const std::vector<SortOrderInfo> &)`, which in one transaction:

1. `UPDATE WorkingSetTracks SET position = NULL WHERE ws_id = ?;`
2. `numberUnpositionedTracks(m_db, wsId, sortOrders)` (D8), which numbers every row from 1;
3. saves `sort_order` as `updateSortOrder` does today.

`WorkingSetNode::setSortOrder` calls it **only when the requested sort differs from the saved
one**. This matters: `DataViewComponent` re-applies the saved sort through `sortOrderChanged` →
`setSortOrder` every time a node is shown (`DataViewComponent.cpp:314-338`, `reSortTable()`).
Without the comparison, opening a working set would renumber it and throw away every shuffle.
A header click always changes the sort (another column, or the same column with the direction
toggled), so a click always renumbers.

Consequence for the user: to restore "BPM ascending" after shuffling while the header already
shows "BPM ▲", click BPM twice (▼, then ▲).

`WorkingSetNode::setSortOrder` no longer forwards to `LibraryNode::setSortOrder` for the query
(D3 ignores `sortBy` for working sets); it flushes the row cache and count as that does.

### D5 - Shuffle Selection

`IWorkingSetManager::shuffleTracks(WorkingSetId, const std::vector<TrackId> &)`, in one transaction:

1. `SELECT track_id, position FROM WorkingSetTracks WHERE ws_id = ? AND track_id IN (...)`.
2. Collect the positions, `std::shuffle` them with a `std::mt19937` seeded from
   `std::random_device`, and assign them back to the same track ids.
3. `UPDATE WorkingSetTracks SET position = ? WHERE ws_id = ? AND track_id = ?` per row, one
   prepared statement reused.

Because only the positions the selection already held are permuted:

- no row outside the selection moves;
- the selected rows occupy the same row indices after the shuffle as before, so the table's
  selection still covers exactly the shuffled tracks and the user can shuffle again;
- this holds under an active search/filter too, because the visible rows are a subset ordered by
  position, and the selected ones still hold the same set of positions.

Rows with a `NULL` position (D3's fallback case) get one first: before step 1, the same transaction
calls `numberUnpositionedTracks` (D8) with the sort `track_id ASC`. That is exactly the order D3
displays them in (after every positioned row, by `track_id`), so the repair moves no row on screen
and the selected row indices still cover the selected tracks. Repairing with the saved sort
instead could move an unselected `NULL` row into the selected range.

Selections of one row are a no-op. Duplicate track ids cannot occur: the primary key is
`(ws_id, track_id)`.

### D6 - Insert paths assign positions

All seven places that insert into `WorkingSetTracks` are in `SqliteWorkingSetManager.cpp` or are
called from it (`createWorkingSetFromQuery` → `SqliteStatementConstruction::createInsertIntoSelectTrackIdsStatement`,
`createWorkingSetFromVirtualFolder` ×2, `createWorkingSetFromTrackInfos`, `addToWorkingSet`,
`addVirtualFolderToWorkingSet` ×2). The insert statements stay as they are. Each of these functions,
inside its transaction:

1. **before** the insert, calls `numberUnpositionedTracks` (D8) with `track_id ASC`, which fixes any
   pre-existing `NULL` rows where D3 already displays them (same reasoning as D5);
2. inserts;
3. calls `numberUnpositionedTracks` with the set's saved sort, which now only sees the new rows.

- New set: the create functions first save `sort_order = bpm ascending`, so every (new) row is
  numbered BPM ascending (U2) and the header shows it.
- Existing set: new rows go after the current maximum, ordered among themselves by the set's saved
  sort (U4). `INSERT OR IGNORE` leaves tracks that were already in the set where they were.

### D8 - One numbering helper

D2, D4 and D6 all do the same thing: give rows with `position IS NULL` positions after the set's
current maximum, in a given column order. One function does it:

```cpp
// Database/Sqlite/SqliteWorkingSetManager.h, namespace jucyaudio::database
bool numberUnpositionedTracks(SqliteDatabase &db, WorkingSetId workingSetId, const std::vector<SortOrderInfo> &sortOrder);
std::vector<SortOrderInfo> parseSortOrderJson(std::string_view json); // moved out of workingSetInfoFromStatement
```

```sql
UPDATE WorkingSetTracks SET position = base.m + r.rn
FROM (SELECT COALESCE(MAX(position), 0) AS m FROM WorkingSetTracks WHERE ws_id = ?1) base,
     (SELECT Tracks.track_id, row_number() OVER (ORDER BY <cols> COLLATE NOCASE ASC|DESC, ..., Tracks.track_id) AS rn
      FROM WorkingSetTracks w JOIN Tracks ON Tracks.track_id = w.track_id
      WHERE w.ws_id = ?1 AND w.position IS NULL) r
WHERE WorkingSetTracks.ws_id = ?1 AND WorkingSetTracks.track_id = r.track_id;
```

- It takes the connection and does **not** open a transaction; every caller already holds one
  (`SqliteTransaction` issues `BEGIN IMMEDIATE`, so nesting would fail).
- `<cols>` are built the way `addOrderByClause` builds them (same `COLLATE NOCASE`, same direction
  keywords), qualified with `Tracks.`. Column names come from `DataColumn::sqlId` or from a saved
  `sort_order` that was written from one; a name that is not a column of `Tracks` (per
  `PRAGMA table_info(Tracks)`) is skipped with a warning, so a hand-edited `sort_order` cannot inject
  SQL. Checked against the table rather than `LibraryColumns`, which is local to `LibraryNode.cpp`
  and would make the SQLite layer depend on the nodes. An empty sort falls back to `bpm ASC`.
- `parseSortOrderJson` is the existing parsing from `workingSetInfoFromStatement`, moved so the
  migration rung (in `SqliteTrackDatabase.cpp`) can read `sort_order` the same way.

A trigger was considered instead (below) and rejected.

### D7 - UI

- `DataAction::ShuffleSelection`, label "Shuffle Selection" in `dataActionToString`.
- Added to `WorkingSetRowActions` (`WorkingSetNode.cpp:18`), so it appears in the right-click menu of
  working-set rows and nowhere else.
- `MainComponent::handleRowActionFromDataView`: collects `getSelectedRowIndices()` (falling back to
  the clicked row, as `ShowDetails` does), maps them to track ids with
  `getTrackInfosForOperation`, calls `shuffleTracks`, then `m_dataViewComponent.refreshView(true)`.
  The `true` matters: `refreshView()` without it only calls `updateContent()`, and `LibraryNode`
  keeps serving the page of rows it fetched before the shuffle (`DataViewComponent.cpp:282`).
- No toolbar button, no keyboard shortcut, no menu-bar entry. Can be added later if wanted.
- No icon is needed: `dataActionToIcon` (`Utils/UiUtils.cpp:123`) is only called from
  `DynamicToolbarComponent`, and the action is not added to the toolbar.

## Tests (new self-test suite, `runWorkingSetOrderSelfTest`)

Runs on a scratch database under `JUCYAUDIO_SELFTEST_ROOT`, wired into `runSelfTestAndQuit` and
both `selftest` recipes like the existing suites. Each check is mutation-tested: the handover reports
which mutation of the code under test makes which check fail.

- **T1** - Shuffle permutes only the selection: a set of N tracks with known positions; shuffle a
  subset S of 20 tracks. Positions outside S are unchanged; the set of positions held by S is
  unchanged; the order within S changed (the chance that a uniform shuffle of 20 returns the
  identity is 1/20!, so no seed injection is needed). (Mutations: shuffle all rows; assign fresh
  positions instead of permuting; skip the write-back.)
- **T2** - Append: add tracks to an existing set; they get positions greater than every existing
  one; existing positions are unchanged; re-adding an existing track does not move it.
- **T3** - New set is BPM ascending: create from track infos in a scrambled order; reading the set
  back via `getTracks` returns BPM-ascending order with `NULL` BPM first, matching `ORDER BY bpm`.
- **T4** - Column sort renumbers: `applySortOrder` by title descending, read back, order matches.
- **T5** - Opening does not renumber: shuffle, then call `WorkingSetNode::setSortOrder` with the
  saved sort; positions are unchanged. (Mutation: drop the comparison in D4.)
- **T6** - Query order: positions set directly in SQL; `getTracks` with paging over two pages
  returns them in position order, and a row with `NULL` position comes last. Repeated with an FTS
  search term, with a structured filter (`bpm:>100` style criteria), and with both, because D3
  changes two separately built `SELECT` branches with different parameter offsets.
- **T5a** - `NULL` repair keeps the displayed order: a set with positioned rows plus `NULL` rows
  whose `track_id` order differs from the saved sort (e.g. titles C, B, A on ids 1, 2, 3, saved sort
  title ascending). (a) Shuffle a selection of displayed rows: every unselected row keeps its row
  index, and the selected row indices hold exactly the selected tracks. (b) Append tracks: every
  pre-existing row keeps its row index, and all new tracks come after them. (Mutation: repair with
  the saved sort instead of `track_id`.)
- **T6a** - Shuffle under a filter: with a filter active, the rows visible before and after a
  shuffle of the visible selection are the same set, and hidden rows keep their positions.
- **T7** - Migration: covered by the existing migration self-test (fixtures climb to 33 and are
  compared structurally; the convergence check at `SelfTests.cpp:5427` must still see no change).
  The v12 fixture already seeds a working set with two tracks; it gains checks that after the climb
  both rows have a position and the set has `sort_order = bpm ascending`. Additionally, a database
  built at v32 shape with two working sets, one with a saved `title` sort and one without, is
  migrated; their position order equals their old display order.

What the tests do not cover: the context-menu wiring, the selection staying on the shuffled rows in
the table, and the view showing the new order after a shuffle (the cache flush in D7). Those need a
manual check in the UI: shuffle twice, scroll past the first page (`QUERY_PAGE_SIZE`) and back.

## Not changed

- `MixTracks` and mix ordering.
- The working-sets overview list (`SqliteWorkingSetManager::getWorkingSets`) and its own sorting.
- Library, folder and album views: they keep column sorting as a pure `ORDER BY`.
- `createWorkingSetFromQuery` commits even when its insert fails (`SqliteWorkingSetManager.cpp:196`).
  That is an existing defect, unrelated to this change; filed as #74. Two more found in review:
  #75 (structured filters ignored when creating a set from a view without search text) and #76
  (non-recursive folder creation queries a nonexistent column).

## Alternatives considered

- **A1 - Shuffle the in-memory rows only.** Rejected: `LibraryNode` loads rows in pages of
  `QUERY_PAGE_SIZE` via `LIMIT/OFFSET`, so any page fetch or refresh restores the SQL order.
- **A2 - Seeded hash as a tie-breaker, no schema change.** Orders the whole set by
  `(track_id * seed) % prime` after the column sort. Rejected by the user: it shuffles everything
  with equal keys, not the selection (U1).
- **A3 - Column sort stays a pure `ORDER BY`, `position` used only when no column sort is active.**
  Two sources of order, and a shuffle would be invisible until the user cleared the sort, which the
  JUCE header cannot do directly. Rejected for D4's single source of truth.
- **A4 - `AFTER INSERT` trigger to assign positions.** Would cover future insert sites
  automatically, but runs once per row (31k times for a large create), orders a bulk insert by
  insertion order rather than BPM, and hides behaviour in the schema. Rejected for D6's explicit
  helper; all insert sites are in one file.
- **A5 - Store a separate `shuffle_key` column and order by `(sort column, shuffle_key)`.**
  Only reorders ties, and a shuffled selection that spans different BPM values would not move.
  Rejected.

## Effort

Roughly 10 files: `SqliteTrackDatabase.cpp` (schema, rung), `SqliteStatementConstruction.{h,cpp}`,
`IWorkingSetManager.h`, `SqliteWorkingSetManager.{h,cpp}`, `WorkingSetNode.cpp`, `Constants.h`,
`UiUtils.cpp`, `MainComponent.cpp`, `Tests/SelfTests.{h,cpp}`, `UI/Main.cpp`, `justfile`,
`CHANGELOG.md`.
