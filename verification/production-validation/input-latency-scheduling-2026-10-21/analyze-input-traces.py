"""Read diagnostic COM1 exports; no product code or guest actions are executed."""
import argparse
import json
import math
from pathlib import Path


def stats(values):
    ordered = sorted(values)
    if not ordered:
        return {"count": 0}
    return {
        "count": len(ordered),
        "p50Ms": ordered[math.ceil(len(ordered) * .50) - 1],
        "p95Ms": ordered[math.ceil(len(ordered) * .95) - 1],
        "maxMs": ordered[-1],
        "over50Ms": sum(value > 50 for value in ordered),
    }


def analyze(text):
    sessions, active = [], None
    for line in text.splitlines():
        if line.startswith("JZ_INPUT_TRACE_BEGIN,"):
            if active is not None:
                raise ValueError("Unclosed trace")
            _, pid, frequency, total, capacity = line.split(",")
            active = {"pid": int(pid), "frequency": int(frequency),
                      "total": int(total), "capacity": int(capacity), "events": []}
            if active["frequency"] <= 0:
                raise ValueError("Invalid QPC frequency")
        elif line == "JZ_INPUT_TRACE_END":
            if active is None:
                raise ValueError("End without begin")
            sessions.append(summarize(active))
            active = None
        elif line.startswith("JZ_INPUT_TRACE,"):
            if active is None:
                raise ValueError("Event outside trace")
            _, tick, seq, stage, thread, kind, x, y, width, height = line.split(",")
            active["events"].append({"tick": int(tick), "seq": int(seq),
                "stage": stage, "thread": int(thread), "kind": int(kind),
                "x": int(x), "y": int(y), "width": int(width), "height": int(height)})
    if active is not None or not sessions:
        raise ValueError("No complete trace")
    return sessions


def summarize(session):
    events = sorted(session["events"], key=lambda event: event["tick"])
    if session["total"] > session["capacity"] or len(events) != session["total"]:
        raise ValueError("Overflow or missing records: repeat a shorter observation")
    scale = 1000 / session["frequency"]
    packets, native_stacks, native = {}, {}, []
    for event in events:
        seq, stage = event["seq"], event["stage"]
        if seq:
            phases = packets.setdefault(seq, {})
            if stage in phases:
                raise ValueError("Duplicate packet stage")
            phases[stage] = event
        elif stage == "native_begin":
            native_stacks.setdefault(event["thread"], []).append(event)
        elif stage == "native_end":
            stack = native_stacks.setdefault(event["thread"], [])
            if not stack or stack[-1]["kind"] != event["kind"]:
                raise ValueError("Unpaired native callback")
            begin = stack.pop()
            native.append({"message": event["kind"], "startQpc": begin["tick"],
                           "endQpc": event["tick"],
                           "ms": (event["tick"] - begin["tick"]) * scale})
    if any(native_stacks.values()):
        raise ValueError("Unclosed native callback")
    groups, details = {}, []
    for seq, phases in packets.items():
        if "queued" not in phases:
            raise ValueError("Packet has no queue record")
        queued = phases["queued"]
        group = groups.setdefault(str(queued["kind"]), {"queue": [], "dispatch": [], "mouse": []})
        row = {"sequence": seq, "kind": queued["kind"], "queuedQpc": queued["tick"]}
        for start, end, metric in (("queued", "dispatch", "queue"),
                                   ("dispatch", "dispatched", "dispatch"),
                                   ("fire", "fired", "mouse")):
            if start in phases and end in phases:
                duration = (phases[end]["tick"] - phases[start]["tick"]) * scale
                if duration < 0:
                    raise ValueError("Reversed packet time")
                group[metric].append(duration)
                row[metric + "Ms"] = duration
        row["mouseApiReached"] = "fire" in phases
        if row.get("queueMs", 0) > 10:
            row["overlappingNativeCallbacks"] = [value for value in native
                if value["endQpc"] > queued["tick"]
                and value["startQpc"] < phases["dispatch"]["tick"]]
        details.append(row)
    return {
        "pid": session["pid"], "events": len(events), "packets": len(packets),
        "groupedBySdkInputKind": {kind: {name: stats(values) for name, values in group.items()}
                                   for kind, group in groups.items()},
        "nativeCallback": stats([value["ms"] for value in native]),
        "longestNativeCallbacks": sorted(native, key=lambda value: value["ms"], reverse=True)[:20],
        "slowestPackets": sorted(details, key=lambda value: value.get("queueMs", 0)
                                  + value.get("dispatchMs", 0), reverse=True)[:30],
        "paintCallbacks": sum(event["stage"] == "paint" for event in events),
        "physicalPresentationMeasured": False,
        "note": "Paint callbacks are not attributed to inputs; mouseApiReached does not prove an action succeeded.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    arguments = parser.parse_args()
    print(json.dumps(analyze(arguments.input.read_text(encoding="utf-8-sig", errors="replace")),
                     ensure_ascii=False, indent=2))
