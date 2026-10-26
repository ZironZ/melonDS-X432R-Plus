#!/usr/bin/env python3
# Copyright 2026 ZironZ
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail closed before comparing replay timings or images (standard library only)."""

import argparse
import json
from pathlib import Path


class UnsafeComparison(ValueError):
    pass


def validate_run(verdict, kind):
    if kind not in ("performance", "images"):
        raise UnsafeComparison("Unknown comparison kind.")
    if verdict.get("completed") is not True:
        raise UnsafeComparison("Replay did not complete successfully.")
    if type(verdict.get("framesRun")) is not int or verdict["framesRun"] <= 0:
        raise UnsafeComparison("Missing or empty replay frame schedule.")
    env = verdict.get("environment", {})
    eligibility = "performanceComparisonEligible" if kind == "performance" else "imageComparisonEligible"
    if env.get("schemaVersion") != 1 or env.get("safeguardsPassed") is not True:
        raise UnsafeComparison("Missing or failed replay safeguards; re-record with the safeguarded harness.")
    if env.get(eligibility) is not True or env.get("purpose") not in ("comparison", "performance"):
        raise UnsafeComparison(f"Replay is not eligible for {kind} comparison.")
    for field in ("captureLumaProbeReadbacks", "hostRtcWrites"):
        if type(env.get(field)) is not int or env[field] != 0:
            raise UnsafeComparison(f"Contaminated or missing {field}.")
    if env.get("captureLumaProbeEnabled") is not False or env.get("captureLumaProbeRequested") is not False:
        raise UnsafeComparison("Capture-luma probe must be explicitly disabled.")
    if type(env.get("framesChecked")) is not int or env["framesChecked"] <= 0:
        raise UnsafeComparison("No effective replay frames were checked.")
    if env.get("rtcMode") not in ("fixed", "savestate") or env.get("hostRtcSyncSuppressed") is not True:
        raise UnsafeComparison("Replay RTC is not deterministic.")
    for field in ("initialRtcDateTime", "initialRtcRegisters"):
        if not isinstance(env.get(field), str) or not env[field]:
            raise UnsafeComparison(f"Missing {field}.")
    for field in ("initialRtcStatus1", "initialRtcStatus2"):
        if type(env.get(field)) is not int:
            raise UnsafeComparison(f"Missing {field}.")
    if env["rtcMode"] == "fixed" and env.get("bootRtcSeed") != env["initialRtcDateTime"]:
        raise UnsafeComparison("Initial RTC does not match the requested fixed seed.")
    if env["rtcMode"] == "savestate" and not env.get("savestateSha256"):
        raise UnsafeComparison("Missing savestate fingerprint.")
    if kind == "performance" and env.get("imageReadbacksRequested") is not False:
        raise UnsafeComparison("Image readbacks invalidate a performance comparison.")
    return env


def validate_pair(before, after, kind):
    a, b = validate_run(before, kind), validate_run(after, kind)
    identity = ("rtcMode", "initialRtcDateTime", "initialRtcRegisters", "initialRtcStatus1",
                "initialRtcStatus2", "savestateSha256")
    if a["rtcMode"] == "fixed":
        identity += ("bootRtcSeed",)
    for field in identity:
        if a.get(field) != b.get(field):
            raise UnsafeComparison(f"Replay inputs differ: {field}.")
    if before.get("framesRun") != after.get("framesRun") or before.get("warmupFrames") != after.get("warmupFrames"):
        raise UnsafeComparison("Replay frame schedules differ.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path, help="Baseline verdict.json or its containing export directory")
    parser.add_argument("after", type=Path, help="Candidate verdict.json or its containing export directory")
    parser.add_argument("--kind", choices=("performance", "images"), required=True)
    args = parser.parse_args()
    try:
        def read(path):
            return json.loads((path / "verdict.json" if path.is_dir() else path).read_text(encoding="utf-8"))
        validate_pair(read(args.before), read(args.after), args.kind)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"REJECTED: {error}\n")
    print(f"PASS: replay environments match for {args.kind} comparison.")


if __name__ == "__main__":
    main()
