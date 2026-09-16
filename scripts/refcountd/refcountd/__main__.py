"""Entry point for the refcountd module."""

from __future__ import annotations
import os
import re
import sys
from typing import NamedTuple
import typer

app = typer.Typer()

# The only two log records that carry a reference count. A message that does not begin with one of
# these is not a refcount event, whatever else it may mention.
EVENT_PREFIXES = ("BaseNode::retain ", "BaseNode::release ")

class LogEntry(NamedTuple):
    timestamp: str
    component: str
    level: str
    message: str

class RefCountEvent(NamedTuple):
    timestamp: str
    action: str  # "retain" or "release"
    pointer: str
    location: str  # file[line]
    ref_count: int

def parse_spdlog_line(line: str) -> LogEntry | None:
    """Parse a spdlog line and extract timestamp, component, level, and message.
    
    Example line: "[2025-08-13 16:44:55.311] [jucyaudio_logger] [debug] addParam int64_t 285510 at 1"
    """
    # Pattern to match spdlog format: [timestamp] [component] [level] message
    pattern = r'^\[([^\]]+)\]\s*\[([^\]]+)\]\s*\[([^\]]+)\]\s*(.*)$'
    match = re.match(pattern, line.strip())
    
    if match:
        timestamp, component, level, message = match.groups()
        return LogEntry(
            timestamp=timestamp.strip(),
            component=component.strip(),
            level=level.strip(),
            message=message.strip()
        )
    return None

def parse_refcount_message(entry: LogEntry) -> RefCountEvent | None:
    """Parse a BaseNode retain/release message.
    
    Examples:
    - "BaseNode::retain 0x1234 at file.cpp[123]: is now 2"
    - "BaseNode::release 0x1234 at file.cpp[123]: is now 1"
    - "BaseNode::release 0x1234 at file.cpp[123]: is now 0 <- delete this"
    """
    message = entry.message
    
    # Anchored at the start of the message, not searched for anywhere in it. The app logs plenty
    # of text it was handed by the user - a mix name, a folder, a track title - and an unanchored
    # search would find an event inside one of those. A name is not a log record: at best that
    # refuses a good log, at worst it parses as a retain that never happened and invents a leak.
    retain_pattern = r'^BaseNode::(retain|release)\s+(0x[0-9a-fA-F]+)\s+at\s+(.+?):\s+is\s+now\s+(\d+)(?:\s+<-\s+delete\s+this)?'
    match = re.match(retain_pattern, message)
    
    if match:
        action, pointer, location, ref_count = match.groups()
        return RefCountEvent(
            timestamp=entry.timestamp,
            action=action,
            pointer=pointer,
            location=location,
            ref_count=int(ref_count)
        )
    return None

def refuse(what: str, *, not_built: bool, log_advice: bool = True) -> None:
    """Say why this log cannot answer the question, and how to get one that can.

    Both remediations are printed rather than the one for the current platform, because the log being
    analysed is not necessarily from the machine analysing it, and a Windows-only instruction is what
    the first version of this message gave a macOS reader.

    log_advice is off for the one refusal the build and the log level cannot explain: a log this tool
    cannot read. Telling someone to rebuild when the problem is that the format moved sends them the
    wrong way.
    """
    print(f"\nNO DATA: {what}")
    if not_built:
        print("\nThe usual reason is that the app was not built with the instrumentation. On Windows:")
        print("    cmake --preset x64-release -DJUCYAUDIO_REFCOUNT_DEBUGGING=ON")
        print("    cmake --build build-x64-release --config Release --parallel")
        print("\nand on macOS, which does not use the presets:")
        print("    cmake -B build-arm64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \\")
        print("          -DJUCYAUDIO_REFCOUNT_DEBUGGING=ON")
        print("    cmake --build build-arm64 -j8")
    if log_advice:
        print("\nThe log level has to be debug, in jucyaudio.toml under the config root:")
        print("    [Logging]")
        print("    log_level = 'debug'")
        print("\nThe log is truncated at every start, so the run you care about has to be the last one.")
    print("See scripts/refcountd/README.md.")
    raise typer.Exit(code=2)


def default_log_path() -> str:
    """Where the app writes its log, following the same rules the app does.

    JUCYAUDIO_CONFIG wins if it is set - Main.cpp exports it at startup and honours it for everything
    else - otherwise the platform default config root, with Logs/jucyaudio.log underneath.

    This used to be hardcoded to %APPDATA%/jucyaudioApp_Dev/Logs, a path the app stopped writing to
    long enough ago that nobody noticed this tool asserting on it.
    """
    configured = os.environ.get("JUCYAUDIO_CONFIG")
    if configured:
        root = configured
    elif os.name == "nt":
        root = os.path.join(os.environ["LOCALAPPDATA"], "jucyaudio")
    elif sys.platform == "darwin":
        root = os.path.expanduser("~/Library/Application Support/jucyaudio")
    else:
        root = os.path.expanduser("~/.config/jucyaudio")
    return os.path.join(root, "Logs", "jucyaudio.log")


@app.command()
def main(
    logfile: str = typer.Argument(None, help="Log to read. Defaults to the app's own log."),
) -> None:
    """Main entry point for refcountd memory leak detector."""
    logfile_name: str = logfile or default_log_path()
    # isfile, not exists: a directory passes an existence check and then raises on open, which used
    # to leave the process exiting 1 - the status that means leaks were found.
    if not os.path.isfile(logfile_name):
        raise typer.BadParameter(f"No log file at {logfile_name}. Pass one as an argument, or set JUCYAUDIO_CONFIG.")

    # Track the final reference count for each pointer
    final_ref_counts = {}
    # Track creation location for each pointer (first seen)
    pointer_locations = {}
    # Track all events for debugging
    events = []
    
    total_lines = 0
    parsed_events = 0
    # Lines that look like refcount events and could not be read as one. Collected rather than
    # warned about, because the summary below is only meaningful over a stream that was understood
    # in full - see the guard.
    unreadable: list[str] = []
    
    # Anything that stops this log being read is inconclusive, never a verdict. Both of these
    # escaped as unhandled exceptions before, which exits 1 - and 1 now means leaks were found,
    # so a log that could not be opened would have been reported as a leaking one. A directory
    # passed as the argument did exactly that.
    try:
        with open(logfile_name, "r", encoding="utf-8") as f:
            for line_num, line in enumerate(f, 1):
                total_lines += 1
                # Cheap pre-filter only. What decides whether this is an event is the parsed message
                # below, because the marker can appear inside a line without the line being one.
                if "BaseNode::" not in line:
                    continue

                entry = parse_spdlog_line(line)
                if entry is not None:
                    # The message has to *begin* with one of the two records that carry a count.
                    #
                    # Not "contains", for two different reasons. Three other messages begin BaseNode::
                    # and are not events - ILongRunningTask logs BaseNode::initialize per task
                    # (ILongRunningTask.h:82), BaseNode logs "Not implemented: BaseNode::removeObjectAtRow"
                    # (BaseNode.cpp:198) - and handing those to the parser would fail, count as unreadable
                    # events, and refuse every real instrumented log. And the app logs text the user chose:
                    # a mix named "BaseNode::retain ..." appears inside
                    # "Attempting to create auto-mix with name: '...'" (CreateMixDialogComponent.cpp:398),
                    # which a "contains" test would pick up as an event nobody logged.
                    if not entry.message.startswith(EVENT_PREFIXES):
                        continue

                    event = parse_refcount_message(entry)
                    if event:
                        parsed_events += 1
                        events.append(event)

                        # Track the actual reference count reported in the log
                        final_ref_counts[event.pointer] = event.ref_count

                        # Track first seen location
                        if event.pointer not in pointer_locations:
                            pointer_locations[event.pointer] = event.location

                        # Print each event as it's processed
                        print(f"{event.timestamp}: {event.action.upper()} {event.pointer} at {event.location} -> count: {event.ref_count}")
                    else:
                        unreadable.append(f"  line {line_num}: {entry.message}")

                elif any(prefix in line for prefix in EVENT_PREFIXES):
                    # The spdlog prefix did not parse, and the line mentions an event. Conservative on
                    # purpose, and "contains" here is right where it is wrong above.
                    #
                    # Above, the line parsed, so the message is known exactly and the marker inside it is
                    # user text. Here nothing is known: the prefix is damaged, so where the message
                    # starts cannot be established. "BROKEN [jucyaudio_logger] [debug] BaseNode::retain
                    # 0x1 at f.cpp[9]: is now 2" is a real event with a mangled prefix, and requiring the
                    # *line* to start with the marker dropped it - which is this tool's original defect
                    # in its purest form: one side of the stream silently omitted, the rest still
                    # producing a verdict.
                    #
                    # Nothing well-formed reaches here, whatever the user typed: parse_spdlog_line takes
                    # any line with three bracketed fields and treats the rest as the message, so a name
                    # containing brackets, quotes or the marker itself still parses and is handled above.
                    unreadable.append(f"  line {line_num}: {line.strip()}")
    except OSError as exc:
        refuse(
            f"the log at {logfile_name} could not be read: {exc}",
            not_built=False,
            log_advice=False,
        )
    except UnicodeDecodeError as exc:
        refuse(
            f"the log at {logfile_name} stopped decoding as UTF-8 at byte {exc.start}. Nothing past"
            " that point was read, so a balance over what came before it means nothing.",
            not_built=False,
            log_advice=False,
        )

    # Report summary
    print(f"\n{'='*60}")
    print("MEMORY LEAK ANALYSIS SUMMARY")
    print(f"{'='*60}")
    print(f"Total lines processed: {total_lines}")
    print(f"BaseNode events parsed: {parsed_events}")
    print(f"Unique pointers tracked: {len(final_ref_counts)}")
    
    # Two ways this log can be useless, and both of them used to read as good news.
    #
    # This is the failure the tool was itself guilty of, and it has three variants. Reporting "no
    # leaks" over a log that proves nothing tells you your code is clean when what it means is that
    # you were not looking - which is how issue #48 stayed invisible: a build that logged nothing, a
    # define that would not compile, a tool that asserted on the wrong path, each one silent.
    total_retains = sum(1 for event in events if event.action == "retain")

    if unreadable:
        # First, because it is the only one of the three that can be true while the log is otherwise
        # full of events - and because the other two messages would send the reader to rebuild when
        # the real problem is that this tool no longer understands what the app writes.
        #
        # Warning about these and carrying on is what this used to do. The balance is computed by
        # matching retains against releases, so dropping some of one side is exactly the input that
        # produces a wrong answer rather than a smaller one - and it produced it silently, under a
        # loguru warning nobody reads when the last line says no leaks.
        shown = "\n".join(unreadable[:5])
        more = f"\n  ... and {len(unreadable) - 5} more" if len(unreadable) > 5 else ""
        refuse(
            f"{len(unreadable)} line(s) look like refcount events and could not be read, so the\n"
            f"{parsed_events} that were read are not the whole stream and no balance over them means\n"
            f"anything. The format this tool expects has probably moved:\n{shown}{more}",
            not_built=False,
            log_advice=False,
        )

    if parsed_events == 0:
        refuse("this log contains no retain/release events, so nothing was checked.", not_built=True)

    if total_retains == 0:
        # The subtle one, and the reason this check is not just parsed_events == 0.
        #
        # A retain logs at debug. So does a release that leaves the count above zero. The *final*
        # release, the one that deletes the node, logs at warning. So a run left at the default info
        # level records only the nodes that were successfully freed, and nothing else - every leak is
        # a node that never reached that line and therefore never appears at all.
        #
        # Such a log parses, passes the check above, contains only zero counts, and reports a clean
        # bill of health over a set of objects selected for having no problem.
        refuse(
            f"this log has {parsed_events} event(s) but not one retain, so it holds no debug-level"
            " refcount evidence.\n"
            "Retains are logged at debug; only the final release is logged at warning, and a warning\n"
            "survives any level. So this lists the nodes that were freed and cannot show you one that\n"
            "was not - a leak is invisible in it by construction.",
            not_built=False,
        )

    # Find memory leaks (pointers with non-zero final reference count)
    leaks = {ptr: count for ptr, count in final_ref_counts.items() if count > 0}
    
    if leaks:
        print(f"\n!!  MEMORY LEAKS DETECTED: {len(leaks)} pointers")
        print(f"{'Pointer':<16} {'Final Count':<12} {'First Seen At'}")
        print("-" * 70)
        for ptr, count in sorted(leaks.items(), key=lambda x: x[1], reverse=True):
            location = pointer_locations.get(ptr, "unknown")
            print(f"{ptr:<16} {count:>8}       {location}")
    else:
        print("\nOK: NO MEMORY LEAKS DETECTED - All tracked objects properly released!")
    
    # Show objects that were properly cleaned up (final count = 0)
    cleaned_up = {ptr: count for ptr, count in final_ref_counts.items() if count == 0}
    if cleaned_up:
        print(f"\nOK: PROPERLY CLEANED UP: {len(cleaned_up)} objects reached ref count 0")
    
    # Show statistics
    total_releases = sum(1 for event in events if event.action == "release")
    print("\nStatistics:")
    print(f"  Total retains observed: {total_retains}")
    print(f"  Total releases observed: {total_releases}")
    print(f"  Objects properly deleted: {len(cleaned_up)}")
    print(f"  Objects with leaks: {len(leaks)}")

    # The answer, as an exit code, because something other than a person reads this too: 0 clean,
    # 1 leaks found, 2 the log could not answer. It used to exit 0 whatever it found, so a script
    # that ran this and checked the status was told everything was fine while the leaks were printed
    # above it.
    if leaks:
        raise typer.Exit(code=1)

if __name__ == "__main__":
    app()