"""Evidence-gate fixtures; these tests do not execute a GPU or prove device loss."""

import unittest

from gpu_timer_test import verify_evidence


class TimerEvidenceTests(unittest.TestCase):
    def loss(self):
        return {
            "realDawnDevice": True,
            "uncapturedErrors": 0,
            "pendingCallbacksBeforeAction": 3,
            "gpuSamples": 0,
            "forceLossInjected": True,
            "deviceLostCallback": True,
            "deviceLostMessage": "GFX-045 isolated device loss injection",
        }

    def test_native_loss_requires_device_and_observed_callback(self):
        verify_evidence("timer-device-loss", self.loss())
        for field, value in (("realDawnDevice", False), ("deviceLostCallback", False),
                             ("forceLossInjected", False), ("deviceLostMessage", ""),
                             ("pendingCallbacksBeforeAction", 0), ("gpuSamples", 1),
                             ("uncapturedErrors", 1)):
            with self.subTest(field=field):
                with self.assertRaises(ValueError):
                    verify_evidence("timer-device-loss", {**self.loss(), field: value})

    def test_recovery_requires_fresh_samples_without_device_loss(self):
        recovery = {**self.loss(), "gpuSamples": 3, "forceLossInjected": False, "deviceLostCallback": False}
        verify_evidence("timer-map-error", recovery)
        verify_evidence("timer-shutdown", recovery)
        with self.assertRaises(ValueError):
            verify_evidence("timer-map-error", {**recovery, "gpuSamples": 0})
        with self.assertRaises(ValueError):
            verify_evidence("timer-shutdown", {**recovery, "deviceLostCallback": True})

    def test_unsupported_device_must_not_publish_samples(self):
        unsupported = {**self.loss(), "pendingCallbacksBeforeAction": 0,
                       "forceLossInjected": False, "deviceLostCallback": False}
        verify_evidence("timer-unsupported", unsupported)
        with self.assertRaises(ValueError):
            verify_evidence("timer-unsupported", {**unsupported, "gpuSamples": 1})


if __name__ == "__main__":
    unittest.main()
