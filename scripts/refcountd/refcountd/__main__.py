"""Entry point for the refcountd module."""

from __future__ import annotations
import os
import re
from typing import NamedTuple
import typer
from loguru import logger

app = typer.Typer()

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
    
    # More flexible pattern to handle Windows paths and hex addresses
    # Fixed to properly match 0x hex addresses
    retain_pattern = r'BaseNode::(retain|release)\s+(0x[0-9a-fA-F]+)\s+at\s+(.+?):\s+is\s+now\s+(\d+)(?:\s+<-\s+delete\s+this)?'
    match = re.search(retain_pattern, message)
    
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

@app.command()
def main() -> None:
    """Main entry point for refcountd memory leak detector."""
    logfile_name: str = os.path.join(os.environ["APPDATA"], "jucyaudioApp_Dev", "Logs", "jucyaudio.log")
    assert os.path.exists(logfile_name), f"Logfile {logfile_name} does not exist."

    # Track the final reference count for each pointer
    final_ref_counts = {}
    # Track creation location for each pointer (first seen)
    pointer_locations = {}
    # Track all events for debugging
    events = []
    
    total_lines = 0
    parsed_events = 0
    
    with open(logfile_name, "r", encoding="utf-8") as f:
        for line_num, line in enumerate(f, 1):
            total_lines += 1
            if "BaseNode::" in line:
                entry = parse_spdlog_line(line)
                if entry:
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
                        logger.warning(f"Failed to parse BaseNode message on line {line_num}: {entry.message}")
                else:
                    logger.warning(f"Failed to parse spdlog line {line_num}: {line.strip()}")
    
    # Report summary
    print(f"\n{'='*60}")
    print("MEMORY LEAK ANALYSIS SUMMARY")
    print(f"{'='*60}")
    print(f"Total lines processed: {total_lines}")
    print(f"BaseNode events parsed: {parsed_events}")
    print(f"Unique pointers tracked: {len(final_ref_counts)}")
    
    # Find memory leaks (pointers with non-zero final reference count)
    leaks = {ptr: count for ptr, count in final_ref_counts.items() if count > 0}
    
    if leaks:
        print(f"\n⚠️  MEMORY LEAKS DETECTED: {len(leaks)} pointers")
        print(f"{'Pointer':<16} {'Final Count':<12} {'First Seen At'}")
        print("-" * 70)
        for ptr, count in sorted(leaks.items(), key=lambda x: x[1], reverse=True):
            location = pointer_locations.get(ptr, "unknown")
            print(f"{ptr:<16} {count:>8}       {location}")
    else:
        print("\n✅ NO MEMORY LEAKS DETECTED - All tracked objects properly released!")
    
    # Show objects that were properly cleaned up (final count = 0)
    cleaned_up = {ptr: count for ptr, count in final_ref_counts.items() if count == 0}
    if cleaned_up:
        print(f"\n✅ PROPERLY CLEANED UP: {len(cleaned_up)} objects reached ref count 0")
    
    # Show statistics
    total_retains = sum(1 for event in events if event.action == "retain")
    total_releases = sum(1 for event in events if event.action == "release")
    print("\nStatistics:")
    print(f"  Total retains observed: {total_retains}")
    print(f"  Total releases observed: {total_releases}")
    print(f"  Objects properly deleted: {len(cleaned_up)}")
    print(f"  Objects with leaks: {len(leaks)}")

if __name__ == "__main__":
    app()