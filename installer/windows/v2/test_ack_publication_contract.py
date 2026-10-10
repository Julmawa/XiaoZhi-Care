"""Structural regressions for the physical ACK publication contract.

These tests validate the real application source ordering; they do NOT simulate
RTOS scheduling or replace runtime/physical hardware verification.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
SOURCE = (ROOT / 'main' / 'application.cc').read_text(encoding='utf-8-sig')


def method(name, next_marker):
    start = SOURCE.index(name)
    end = SOURCE.index(next_marker, start + len(name))
    return SOURCE[start:end]

class PublicationContractTests(unittest.TestCase):
    def test_ticket_invalidated_before_target_changes_and_published_last(self):
        src = method('bool Application::RequestCareAcknowledgementListening(', 'return true;')
        invalid = src.index('care_ack_physical_ticket_.store(0)')
        get = src.index('xiaozhi_care_ack_physical_ticket(target_id.c_str())')
        set_target = src.index('care_ack_physical_target_ = new_ticket ? target_id : std::string()')
        publish = src.index('care_ack_physical_ticket_.store(new_ticket)')
        self.assertLess(invalid, get)
        self.assertLess(get, set_target)
        self.assertLess(set_target, publish)

    def test_button_schedule_double_checks_ticket_before_reading_target(self):
        src = method('void Application::ConfirmCareAlertFromButton(', 'void Application::ConfirmPendingCareAlertFromTouch(')
        self.assertGreaterEqual(src.count('care_ack_physical_ticket_.load() != pressed_ticket'), 2)
        self.assertIn('xiaozhi_care_ack_physical_confirm(target.c_str(), pressed_ticket)', src)
        self.assertIn('care_ack_physical_ticket_.store(0)', src)

    def test_runtime_validation_under_one_mutex(self):
        runtime = (ROOT / 'components' / 'care-voice' / 'src' / 'reminder_voice_runtime.cc').read_text(encoding='utf-8-sig')
        src = runtime.split('bool ReminderVoiceRuntime::AcknowledgePhysicalTicket(')[1].split('ReminderAckResult ReminderVoiceRuntime::AcknowledgeTarget(')[0]
        self.assertLess(src.index('std::lock_guard<std::mutex> lock(ack_mutex_)'), src.index('newest->acknowledged = true'))
        self.assertIn('newest->sequence != ticket', src)
        self.assertIn('newest->acknowledged', src)

    def test_physical_button_not_using_target_only_ack(self):
        board = (ROOT / 'main' / 'boards' / 'bread-compact-wifi-lcd' / 'compact_wifi_board_lcd.cc').read_text(encoding='utf-8-sig')
        self.assertIn('ConfirmCareAlertFromButton(touch_ticket_)', board)
        self.assertNotIn('ConfirmPendingCareAlertFromTouch(', board)

if __name__ == '__main__':
    unittest.main()
