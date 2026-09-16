"""Exit-status checks for refcountd.

Five logs, one per way the tool can end, because the exit status is the half of the answer that a
script reads and a person does not - and it was wrong. Before issue #50 a detected leak exited 0, so
anything checking the status was told the run was clean while the leaks were printed above it.

No framework: plain asserts and typer's CliRunner, which is already a dependency. Run it with

    uv run python test_refcountd.py

from this directory, or `just test-refcountd` from the repo root. Exit 0 means all checks passed.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

from typer.testing import CliRunner

from refcountd.__main__ import app

runner = CliRunner()

# Real lines, copied from a session log, with the paths shortened. The shapes matter: a retain and a
# non-final release at debug, and the final release at warning with its "<- delete this" suffix.
RETAIN = "[2026-09-16 17:31:46.385] [jucyaudio_logger] [debug] BaseNode::retain {ptr} at C:\\p\\Node.cpp[10]: is now {n}"
RELEASE = "[2026-09-16 17:31:47.835] [jucyaudio_logger] [debug] BaseNode::release {ptr} at C:\\p\\Node.cpp[20]: is now {n}"
FINAL = "[2026-09-16 17:31:48.000] [jucyaudio_logger] [warning] BaseNode::release {ptr} at C:\\p\\Node.cpp[30]: is now 0 <- delete this"

# The other reference-counted thing. It used to log under BaseNode:: too, which made a task
# indistinguishable from a node in the output and a count over both callable only "objects".
TASK_RETAIN = "[2026-09-16 17:31:46.000] [jucyaudio_logger] [debug] LongRunningTask::retain {ptr} at C:\\p\\Task.h[43]: is now {n}"
TASK_FINAL = "[2026-09-16 17:31:49.000] [jucyaudio_logger] [warning] LongRunningTask::release {ptr} at C:\\p\\Task.h[55]: is now 0 <- delete this"

failures: list[str] = []


def check(condition: bool, description: str) -> None:
    print(f"{'PASS' if condition else 'FAIL'}  {description}")
    if not condition:
        failures.append(description)


def leak_rows(out: str) -> list[list[str]]:
    """The rows of the leak table, as fields.

    Not "the first line mentioning the address": every event is printed above the table, so that
    finds a RETAIN line and reports on the event stream while claiming to report on the table.
    """
    lines = out.splitlines()
    try:
        start = next(i for i, ln in enumerate(lines) if ln.startswith("Kind") and "Address" in ln)
    except StopIteration:
        return []
    rows = []
    for line in lines[start + 2:]:
        if not line.strip():
            break
        rows.append(line.split())
    return rows


def run(lines: list[str], tmp: Path, name: str) -> tuple[int, str]:
    path = tmp / name
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    result = runner.invoke(app, [str(path)])
    return result.exit_code, result.output


def main() -> int:
    with tempfile.TemporaryDirectory() as raw:
        tmp = Path(raw)

        # 1. A log with no refcount events at all: an uninstrumented build.
        code, out = run(["[2026-09-16 17:00:00.000] [jucyaudio_logger] [info] nothing to see"], tmp, "empty.log")
        check(code == 2, f"a log with no refcount events is refused, not reported clean (exit {code})")
        check("NO DATA" in out, "and says so")
        check("JUCYAUDIO_REFCOUNT_DEBUGGING" in out, "and says how to get a log that has them")

        # 2. Events but no retain: an instrumented build left at the default info level, where only
        #    the warning-level final releases survive. Every node in such a log is one that was
        #    freed, so a leak cannot appear in it.
        code, out = run(
            [FINAL.format(ptr="0x1111"), FINAL.format(ptr="0x2222")],
            tmp,
            "infoonly.log",
        )
        check(code == 2, f"a log with releases but no retains is refused (exit {code})")
        check("not one retain" in out, "and says which half is missing")

        # 3. A clean session: every node retained and released down to zero.
        code, out = run(
            [
                RETAIN.format(ptr="0x1111", n=2),
                RELEASE.format(ptr="0x1111", n=1),
                FINAL.format(ptr="0x1111"),
                RETAIN.format(ptr="0x2222", n=2),
                FINAL.format(ptr="0x2222"),
            ],
            tmp,
            "clean.log",
        )
        check(code == 0, f"a clean log exits 0 (exit {code})")
        check("NO MEMORY LEAKS DETECTED" in out, "and says so")

        # 4. A leak: one node retained and never released to zero. This is the case that used to
        #    print the leaks and then exit 0.
        code, out = run(
            [
                RETAIN.format(ptr="0x1111", n=2),
                FINAL.format(ptr="0x1111"),
                RETAIN.format(ptr="0x3333", n=2),
                RELEASE.format(ptr="0x3333", n=1),
            ],
            tmp,
            "leak.log",
        )
        check(code == 1, f"a log with a leak exits 1, not 0 (exit {code})")
        check("MEMORY LEAKS DETECTED: 1 pointers" in out, "and names how many leaked")
        check("0x3333" in out, "and which")

        # 5. A line that looks like an event and cannot be read: the format moved. Carrying on here
        #    means computing a balance over one side of a stream, which is how a wrong answer gets
        #    produced rather than a smaller one.
        code, out = run(
            [
                RETAIN.format(ptr="0x1111", n=2),
                FINAL.format(ptr="0x1111"),
                "[2026-09-16 17:31:49.000] [jucyaudio_logger] [debug] BaseNode::retain but not as we know it",
            ],
            tmp,
            "drift.log",
        )
        check(code == 2, f"a log with an unreadable event is refused rather than partly analysed (exit {code})")
        check("could not be read" in out, "and says that is why")
        check("not as we know it" in out, "and shows the line, so the format can be compared")
        check("NO MEMORY LEAKS" not in out, "and does not also report a verdict it cannot support")

        # 6. Other BaseNode:: messages, which are not refcount events and must not be mistaken for
        #    ones the tool failed to read. ILongRunningTask logs BaseNode::initialize for every task
        #    it constructs, and BaseNode logs "Not implemented: BaseNode::removeObjectAtRow". With
        #    the guard above matching on "BaseNode::" alone, either would have refused the whole log
        #    - so every real instrumented debug-level run would have been refused.
        code, out = run(
            [
                "[2026-09-16 17:31:44.000] [jucyaudio_logger] [debug] BaseNode::initialize 0x9999 at C:\\p\\Task.h[82]: is now 1",
                RETAIN.format(ptr="0x1111", n=2),
                FINAL.format(ptr="0x1111"),
                "[2026-09-16 17:31:50.000] [jucyaudio_logger] [info] Not implemented: BaseNode::removeObjectAtRow(3)",
            ],
            tmp,
            "othermessages.log",
        )
        check(code == 0, f"BaseNode:: messages that are not retain/release do not refuse the log (exit {code})")
        check("NO MEMORY LEAKS DETECTED" in out, "and the events around them are still analysed")
        check("could not be read" not in out, "and they are not counted as events that failed to parse")

        # 7. The marker inside a message the app logged on the user's behalf. A mix name goes
        #    straight into the log (CreateMixDialogComponent.cpp:398), so the user chooses part of
        #    the text this tool reads. Matching anywhere in the line would refuse a good log over a
        #    mix name; an unanchored parse would go further and read one as an event that never
        #    happened, inventing a leak out of a string somebody typed.
        code, out = run(
            [
                RETAIN.format(ptr="0x1111", n=2),
                "[2026-09-16 17:31:50.000] [jucyaudio_logger] [info] Attempting to create auto-mix with name: "
                "'BaseNode::retain 0xdeadbeef at Evil.cpp[1]: is now 9' from 3 tracks.",
                FINAL.format(ptr="0x1111"),
            ],
            tmp,
            "mixname.log",
        )
        check(code == 0, f"a mix name that looks like an event does not refuse the log (exit {code})")
        check("0xdeadbeef" not in out, "and is not parsed as an event that never happened")
        check("NO MEMORY LEAKS DETECTED" in out, "and the real events around it are analysed")

        # 8. An event whose spdlog prefix is damaged. It is still an event, and dropping it omits one
        #    side of the stream while the rest goes on to produce a verdict - this tool's original
        #    defect. Requiring the *line* to start with the marker was not enough: the marker is in
        #    the middle, behind the wreckage of the prefix.
        code, out = run(
            [
                RETAIN.format(ptr="0x1111", n=2),
                "BROKEN [jucyaudio_logger] [debug] BaseNode::retain 0x2222 at C:\\p\\Node.cpp[10]: is now 2",
                FINAL.format(ptr="0x1111"),
            ],
            tmp,
            "brokenprefix.log",
        )
        check(code == 2, f"an event behind a damaged spdlog prefix is not silently dropped (exit {code})")
        check("BROKEN" in out, "and the line is shown")
        check("NO MEMORY LEAKS" not in out, "and no verdict is given over the events that did parse")

        # 9. A directory. It passes an existence check and then raises on open - which used to
        #    escape as an unhandled exception and exit 1, the status that means leaks were found.
        result = runner.invoke(app, [str(tmp)])
        check(result.exit_code == 2, f"a directory instead of a log exits 2, not 1 (exit {result.exit_code})")

        # 9. Bytes that are not UTF-8. Raises during iteration, so it happens after some of the log
        #    has been read - the worst shape, because a balance over the part that decoded looks
        #    like an answer.
        bad = tmp / "notutf8.log"
        bad.write_bytes(
            RETAIN.format(ptr="0x1111", n=2).encode("utf-8") + b"\n" + b"\xff\xfe invalid \x80\x81\n"
        )
        result = runner.invoke(app, [str(bad)])
        check(result.exit_code == 2, f"a log that is not valid UTF-8 exits 2, not 1 (exit {result.exit_code})")

        # 10. Both kinds in one log, counted apart. A task is reference counted the same way a
        #     navigation node is, and an unbalanced one is just as much a leak - but they have
        #     different lifetimes and different reasons to leak, and a report that calls the sum
        #     "nodes" is saying something it cannot know. It used to have no choice: both logged
        #     under BaseNode::.
        code, out = run(
            [
                RETAIN.format(ptr="0x1111", n=2),
                TASK_RETAIN.format(ptr="0x2222", n=2),
                FINAL.format(ptr="0x1111"),
                TASK_FINAL.format(ptr="0x2222"),
            ],
            tmp,
            "bothkinds.log",
        )
        check(code == 0, f"a log holding both nodes and tasks is analysed (exit {code})")
        check("2 objects reached ref count 0" in out, "and both are tracked")
        check("1 node, 1 task" in out, "and reported apart rather than as one number")

        # 11. A leaking task says it is a task, rather than reading as a node with an odd location.
        code, out = run(
            [
                RETAIN.format(ptr="0x1111", n=2),
                FINAL.format(ptr="0x1111"),
                TASK_RETAIN.format(ptr="0x3333", n=2),
            ],
            tmp,
            "taskleak.log",
        )
        check(code == 1, f"a leaked task is a leak (exit {code})")
        rows = [r for r in leak_rows(out) if "0x3333" in r]
        check(len(rows) == 1, f"and the leak table has a row for it ({len(rows)} row(s))")
        check(bool(rows) and rows[0][0] == "task", f"whose kind column says task ({rows[0][0] if rows else 'no row'})")

        # 12. Task events only. The summary label used to say "BaseNode events parsed" whatever was
        #     in the log, which is the same misclassification in the one line a reader looks at first.
        code, out = run(
            [TASK_RETAIN.format(ptr="0x4444", n=2), TASK_FINAL.format(ptr="0x4444")],
            tmp,
            "tasksonly.log",
        )
        check(code == 0, f"a log of nothing but task events is analysed (exit {code})")
        check("Refcount events parsed: 2" in out, "and the count does not call them BaseNode events")
        check("BaseNode" not in out, "nor mention that name anywhere")

        # 13. The allocator reusing an address across kinds. An address is not an object: a node
        #     freed at 0x1111 and a task later allocated there are two objects in one place, and this
        #     tool keys everything by address. Recording only the last kind made the first vanish.
        code, out = run(
            [
                RETAIN.format(ptr="0x1111", n=2),
                FINAL.format(ptr="0x1111"),
                RETAIN.format(ptr="0x2222", n=2),
                FINAL.format(ptr="0x2222"),
                TASK_RETAIN.format(ptr="0x1111", n=2),
            ],
            tmp,
            "reuse.log",
        )
        check(code == 1, f"a log where an address is reused across kinds, and the task leaks (exit {code})")
        check("2 nodes, 1 task" in out, "counts both node addresses as nodes")
        check("used by more than one kind" in out, "and states the overlap rather than letting it look like an error")

        # The leak is the task. The node at that address reached zero and was freed, and the history
        # of an address is not a list of things leaking at it.
        check("Objects with leaks: 1 (" not in out, "the leak count does not break down a single leak into two kinds")
        reuse_rows = [r for r in leak_rows(out) if "0x1111" in r]
        check(len(reuse_rows) == 1, f"the leak table has one row for the reused address ({len(reuse_rows)} row(s))")
        check(bool(reuse_rows) and reuse_rows[0][0] == "task",
              f"and blames the task that is still holding a count, not the node that was freed ({reuse_rows[0][0] if reuse_rows else 'no row'})")

        # 14. A log that is not there at all.
        result = runner.invoke(app, [str(tmp / "nosuchfile.log")])
        check(result.exit_code == 2, f"a missing log is a usage error, exit 2 (exit {result.exit_code})")

    print()
    if failures:
        print(f"{len(failures)} check(s) failed:")
        for description in failures:
            print(f"  {description}")
        return 1
    print("refcountd checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
