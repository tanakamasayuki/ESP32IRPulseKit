import re

import pytest
from pexpect import EOF, TIMEOUT


RX_ACTIVITY = re.compile(rb"RX_(RAW len=\d+|DECODE .*)")
NEC_DECODE = re.compile(rb"RX_DECODE protocol=NEC score=(-?\d+) len=32 bits=0x0*CB3400FF type=NORMAL")


def wait_boards_ready(dut, peers):
    tx = peers["tx"]
    rx = dut

    rx.expect(re.compile(rb"RX_READY gpio=\d+ inverted=[01]"), timeout=20)
    tx.expect(re.compile(rb"TX_READY gpio=\d+ inverted=[01]"), timeout=20)
    return tx, rx


def assert_serial_control(tx, rx):
    tx.write("PING\n")
    tx.expect_exact("PONG", timeout=5)

    rx.write("PING\n")
    rx.expect_exact("PONG", timeout=5)


def expect_rx_activity(rx):
    try:
        rx.expect(RX_ACTIVITY, timeout=20)
    except (EOF, TIMEOUT) as exc:
        pytest.fail(
            "RX did not report any IR activity while TX was continuously sending. "
            "Check IR LED drive, receiver module power, board direction/distance, "
            "TEST_IR_TX_GPIO, TEST_IR_RX_GPIO, and inversion settings.",
            pytrace=False,
        )


def expect_nec_decode(rx):
    try:
        rx.expect(NEC_DECODE, timeout=20)
    except (EOF, TIMEOUT):
        pytest.fail(
            "RX saw IR activity but did not decode the expected NEC frame "
            "00ff/34. Check receiver inversion, timing, distance, and protocol "
            "decode behavior.",
            pytrace=False,
        )


def test_link_smoke_nec_two_board(dut, peers):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    try:
        tx.write("LOOP NEC 00ff 34 250\n")
        tx.expect_exact("TX_LOOP NEC 00ff 34", timeout=5)
        tx.expect_exact("TX_LOOP_SENT", timeout=5)

        # First prove that the receiver sees any IR activity at all. If this
        # times out, check wiring, GPIOs, module power, direction, and distance
        # before debugging protocol decode.
        expect_rx_activity(rx)

        # Then require the expected protocol decode while TX continues sending.
        expect_nec_decode(rx)
    finally:
        tx.write("STOP\n")
        tx.expect_exact("TX_STOPPED", timeout=5)
