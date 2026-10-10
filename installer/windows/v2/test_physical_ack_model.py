import unittest


class PhysicalAckModel:
    HOLD_US = 3_000_000

    def __init__(self):
        self.pressed = False
        self.started_us = 0

    def down(self, now_us):
        self.pressed = True
        self.started_us = now_us

    def up(self, now_us, alert_pending):
        if not self.pressed:
            return "none"

        self.pressed = False

        if now_us < self.started_us:
            return "none"

        elapsed = now_us - self.started_us

        if elapsed >= self.HOLD_US:
            return "confirm" if alert_pending else "none"

        return "click"


class TestPhysicalAckModel(unittest.TestCase):

    def test_short_click(self):
        b = PhysicalAckModel()
        b.down(1_000_000)
        self.assertEqual(b.up(1_500_000, False), "click")

    def test_three_seconds(self):
        b = PhysicalAckModel()
        b.down(2_000_000)
        self.assertEqual(b.up(5_000_000, True), "confirm")

    def test_long_without_alert(self):
        b = PhysicalAckModel()
        b.down(6_000_000)
        self.assertEqual(b.up(10_000_000, False), "none")

    def test_release_without_press(self):
        b = PhysicalAckModel()
        self.assertEqual(b.up(11_000_000, True), "none")

    def test_invalid_time(self):
        b = PhysicalAckModel()
        b.down(15_000_000)
        self.assertEqual(b.up(14_000_000, True), "none")


if __name__ == "__main__":
    unittest.main()
