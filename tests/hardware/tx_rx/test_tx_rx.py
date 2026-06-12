import re


def wait_ready(dut, peers):
    tx = peers["tx"]
    rx = dut

    rx.expect(re.compile(rb"RX_READY gpio=\d+ inverted=[01]"), timeout=20)
    tx.expect(re.compile(rb"TX_READY gpio=\d+ inverted=[01]"), timeout=20)
    return tx, rx


def test_tx_rx_nec_roundtrip(dut, peers):
    tx, rx = wait_ready(dut, peers)

    tx.write("PING\n")
    tx.expect_exact("PONG", timeout=5)

    rx.write("PING\n")
    rx.expect_exact("PONG", timeout=5)

    tx.write("SEND NEC 00ff 34\n")
    tx.expect_exact("TX_OK NEC 00ff 34", timeout=5)
    rx.expect(
        re.compile(rb"RX_DECODE protocol=NEC score=(-?\d+) len=32 bits=0x0*CB3400FF type=NORMAL"),
        timeout=10,
    )
