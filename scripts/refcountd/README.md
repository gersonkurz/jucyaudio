# refcountd

Finds navigation nodes that were retained and never released, by reading the app's own log.

`INavigationNode` extends `IRefCounted` and is reference counted by hand: every `retain()` has to be
matched by a `release()`, and nothing in the compiler or the self test checks that for you. This is
the thing that does.

## Why it needs a special build

The retain and release calls only log when the app is compiled with `USE_REFCOUNT_DEBUGGING`. That is
off by default, because it writes a line per call and an ordinary session produces a great many of
them. Turn it on at configure time:

On Windows, which uses the CMake presets:

```
cmake --preset x64-release -DJUCYAUDIO_REFCOUNT_DEBUGGING=ON
cmake --build build-x64-release --config Release --parallel
```

On macOS, which does not:

```
cmake -B build-arm64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DJUCYAUDIO_REFCOUNT_DEBUGGING=ON
cmake --build build-arm64 -j8
```

Turn it back off the same way with `-DJUCYAUDIO_REFCOUNT_DEBUGGING=OFF` when you are done, or the
logs stay enormous.

You also need the log level at `debug`, since that is what the events are written at. In
`jucyaudio.toml` under the config root:

```toml
[Logging]
log_level = 'debug'
```

## Running it

```
cd scripts/refcountd
uv run -m refcountd                     # reads the app's own log
uv run -m refcountd path/to/other.log   # or one you point it at
```

The `-m` matters. This project declares no build backend, so it is never installed into the
environment and there is no `refcountd` executable to run. Plain `uv run refcountd` happens to work
anyway on some versions, because uv falls back to the module, but it is not something to rely on -
the reviewer of issue #48 found it failing in a freshly synchronised environment while it worked
here.

The log is truncated every time the app starts, so whatever you want to analyse has to happen in one
session. Do the thing you are suspicious of, close the app so the last of it is flushed, then run
this.

### Exit status

The half of the answer a script reads, and for a while it was always 0:

| Code | Meaning |
|---|---|
| 0 | No leaks. Every tracked object reached a reference count of zero. |
| 1 | Leaks found. They are listed above the summary. |
| 2 | The log could not answer the question, or there was no log. Nothing was concluded. |

Exit 1 is the one that used to be 0: the leaks were printed and the status said everything was fine,
so anything driving this from a script - a `just` recipe, a CI step, a `&&` - got the opposite of the
result.

### When it refuses

It will not give a verdict over a log that cannot support one. Five cases, all exit 2 - two about
the log's content, two about the file itself, and one about the path:

- **no events at all** - the app was not built with the instrumentation;
- **events, but not one retain** - no debug-level evidence, which is what an instrumented build run
  below `debug` produces. This one is worth understanding, because it looks like data. A retain logs
  at debug, and so does a release that leaves the count above zero, but the *final* release - the one
  that deletes the node - logs at warning, and a warning survives any level. So a log written at
  `info` lists exactly the nodes that were freed and nothing else. A leak is a node that never
  reached that line, so it cannot appear. Analysing such a log would report a clean result over a set
  of objects selected for having no problem;
- **a retain or release line it cannot read** - the format the app writes has moved away from what
  this parses. The balance is retains matched against releases, so dropping some of one side is the
  input that produces a *wrong* answer rather than a smaller one. It prints the first few lines it
  choked on, so you can see what changed;
- **a file it cannot open**, including a directory passed by mistake;
- **bytes that are not UTF-8**, which stop the read part way through. A balance over the part that
  decoded would look like an answer and would not be one.

A clean bill of health over a log that proves nothing is the most misleading thing this tool could
print, and for a while it was what it printed.

### What counts as an event

Only a message that **begins** with `BaseNode::retain ` or `BaseNode::release `. Two different things
make that stricter than it looks:

- Other messages begin `BaseNode::` and carry no count - `BaseNode::initialize`, which
  `ILongRunningTask` logs for every task it constructs, and
  `Not implemented: BaseNode::removeObjectAtRow`. They are ignored, not treated as lines that failed
  to parse.
- **The app logs text the user chose.** A mix name goes into the log verbatim
  (`CreateMixDialogComponent.cpp:398`), so part of what this reads is a string somebody typed. A mix
  named `BaseNode::retain 0xdeadbeef at Evil.cpp[1]: is now 9` is a perfectly legal name, and with a
  match that scanned the whole line it became a retain that never happened - a leak invented out of
  a mix name. The message-start test and the anchored parse are both there for that.

The one place a *contained* marker does count is a line whose spdlog prefix did not parse at all -
`BROKEN [jucyaudio_logger] [debug] BaseNode::retain ...`. That is a real event behind a damaged
prefix, and where its message begins cannot be established, so the line is refused rather than
dropped. Nothing well-formed reaches that branch, whatever the name contains.

### Its own checks

```
just test-refcountd          # from the repo root
```

Ten synthetic inputs, one per way this can end: no events, events without a retain, a clean session,
a leak, an unreadable line, other `BaseNode::` messages, a mix name shaped like an event, an event behind a damaged prefix, a
directory,
and bytes that are not UTF-8. They assert the exit status, which is the part that was wrong.
`just selftest` does not run them - that is the C++ binary.

By default it looks where the app writes: `$JUCYAUDIO_CONFIG/Logs/jucyaudio.log` if that variable is
set, otherwise the platform config root - `%LOCALAPPDATA%\jucyaudio` on Windows,
`~/Library/Application Support/jucyaudio` on macOS.

## Reading the output

Every retain and release is printed as it is parsed, then a summary. A pointer whose final count is
above zero was never fully released. The location shown is where it was *first* seen, which is
usually where it was created rather than where the missing release should have been - the event list
above it is what tells you that.

A count that never reaches zero is not always a leak: a node the tree still holds is supposed to have
a live reference when the log ends. What you are looking for is a node that should have been finished
with - one whose dialog was closed, or whose view was navigated away from - and still has a count.

## A pattern worth knowing

The leaks found so far were all the same shape: a node retained across a dialog and released in the
dialog's callback. JUCE's `DialogWindow` close button and its window-level Escape both dismiss the
window without invoking that callback, so the release never runs. Tying the release to the lifetime
of the callback rather than to it being called fixes it:

```cpp
node->retain(REFCOUNT_DEBUG_ARGS);
const std::shared_ptr<INavigationNode> nodeRef{node,
    [](INavigationNode *n) { n->release(REFCOUNT_DEBUG_ARGS); }};
// capture nodeRef in the dialog's callback and use nodeRef.get()
```

`EnsureNodeIsReleased` cannot be used for that - it is deliberately non-copyable and non-movable, so
it will not go into a `std::function`.
