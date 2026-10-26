# Copyright 2026 ZironZ
# SPDX-License-Identifier: GPL-3.0-or-later

import copy
import unittest
from validate_replay_comparison import UnsafeComparison, validate_pair


class ReplayComparisonTests(unittest.TestCase):
    def setUp(self):
        self.run = {"completed": True, "framesRun": 150, "warmupFrames": 0, "environment": {
            "schemaVersion": 1, "purpose": "comparison", "safeguardsPassed": True,
            "imageComparisonEligible": True, "performanceComparisonEligible": True,
            "captureLumaProbeRequested": False, "captureLumaProbeEnabled": False,
            "captureLumaProbeReadbacks": 0, "hostRtcWrites": 0, "framesChecked": 150,
            "rtcMode": "savestate", "hostRtcSyncSuppressed": True, "imageReadbacksRequested": False,
            "initialRtcDateTime": "2024-01-01T12:00:00", "initialRtcRegisters": "24010101120000",
            "initialRtcStatus1": 2, "initialRtcStatus2": 0, "savestateSha256": "same-state"}}

    def test_matching_environments(self):
        for kind in ("images", "performance"):
            validate_pair(self.run, copy.deepcopy(self.run), kind)

    def test_old_verdict_rejected(self):
        with self.assertRaises(UnsafeComparison):
            validate_pair({"completed": True}, self.run, "images")

    def test_unsafe_effective_environment_rejected(self):
        for field, value in (("captureLumaProbeEnabled", True), ("captureLumaProbeReadbacks", 1),
                             ("hostRtcWrites", 1), ("rtcMode", "host"), ("framesChecked", 0),
                             ("safeguardsPassed", False), ("purpose", "diagnostic"),
                             ("hostRtcSyncSuppressed", False)):
            with self.subTest(field=field):
                after = copy.deepcopy(self.run)
                after["environment"][field] = value
                with self.assertRaises(UnsafeComparison):
                    validate_pair(self.run, after, "images")

    def test_different_rtc_or_state_rejected(self):
        for field in ("initialRtcDateTime", "initialRtcRegisters", "savestateSha256"):
            after = copy.deepcopy(self.run)
            after["environment"][field] += "-different"
            with self.assertRaises(UnsafeComparison):
                validate_pair(self.run, after, "images")

    def test_image_runs_not_usable_as_performance_runs(self):
        run = copy.deepcopy(self.run)
        run["environment"]["imageReadbacksRequested"] = True
        run["environment"]["performanceComparisonEligible"] = False
        validate_pair(run, run, "images")
        with self.assertRaises(UnsafeComparison):
            validate_pair(run, run, "performance")

    def test_fixed_clock_must_match_seed(self):
        run = copy.deepcopy(self.run)
        run["environment"]["rtcMode"] = "fixed"
        run["environment"]["bootRtcSeed"] = run["environment"]["initialRtcDateTime"]
        validate_pair(run, run, "images")
        run["environment"]["bootRtcSeed"] = "2000-01-01T12:00:00"
        with self.assertRaises(UnsafeComparison):
            validate_pair(run, run, "images")


if __name__ == "__main__":
    unittest.main()
