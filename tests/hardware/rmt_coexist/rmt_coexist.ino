// Single-board RMT coexistence test.
//
// Creates the library's RX and TX channels (and a WS2812-style RMT channel from
// another user) in every order on one board and reports whether each begin()
// succeeds and a frame can be sent. SoCs other than ESP32 / ESP32-S2 share one
// RMT group clock, which the first channel created picks; a later channel with
// a too different resolution used to fail ("channel prescale out of the range").
// Needs no IR wiring: the TX pins only need to be free.
#include <ESP32IRPulseKit.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

// Free output pins for the TX channels (nothing needs to be attached).
#if defined(CONFIG_IDF_TARGET_ESP32)
const int kTxGpio = 25;
const int kOtherGpio = 26;
#else
const int kTxGpio = 5;
const int kOtherGpio = 6;
#endif

namespace
{
enum class Step
{
  Rx,
  TxPhaseAligned,
  TxModulated,
  Other10MHz, // another RMT user, e.g. a WS2812 driver at 10MHz
};

const char *stepName(Step s)
{
  switch (s)
  {
  case Step::Rx:
    return "rx";
  case Step::TxPhaseAligned:
    return "tx_phase";
  case Step::TxModulated:
    return "tx_mod";
  case Step::Other10MHz:
    return "other10mhz";
  }
  return "?";
}

// Runs the steps in order, sends a short frame on every TX, then releases all.
void runCase(const char *name, std::initializer_list<Step> steps)
{
  esp32irpk::IRReceiver<> rx(kIrRxGpio, kIrRxInverted);
  esp32irpk::IRSender tx_phase(kTxGpio, false);
  esp32irpk::IRSender tx_mod(kTxGpio, false);
  tx_mod.setPhaseAlignedCarrier(false);
  bool other_begun = false;

  bool ok = true;
  String detail;
  for (Step s : steps)
  {
    bool r = false;
    switch (s)
    {
    case Step::Rx:
      r = rx.begin();
      break;
    case Step::TxPhaseAligned:
      r = tx_phase.begin();
      break;
    case Step::TxModulated:
      r = tx_mod.begin();
      break;
    case Step::Other10MHz:
      r = other_begun = rmtInit(kOtherGpio, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
      break;
    }
    detail += String(" ") + stepName(s) + "=" + (r ? "1" : "0");
    ok = ok && r;
  }

  const uint16_t ticks[] = {900, 450, 56, 56, 56, 169, 56};
  esp32irpk::IRRawTickView raw{};
  raw.ticks = ticks;
  raw.len = sizeof(ticks) / sizeof(ticks[0]);
  for (Step s : steps)
  {
    if (s == Step::TxPhaseAligned || s == Step::TxModulated)
    {
      bool r = (s == Step::TxPhaseAligned ? tx_phase : tx_mod).send(raw, 0);
      detail += String(" send_") + stepName(s) + "=" + (r ? "1" : "0");
      ok = ok && r;
    }
  }

  rx.end();
  tx_phase.end();
  tx_mod.end();
  if (other_begun)
    rmtDeinit(kOtherGpio);

  Serial.print("CASE ");
  Serial.print(name);
  Serial.print(ok ? " OK" : " FAIL");
  Serial.println(detail);
}

void runAll()
{
  runCase("rx_then_tx_phase", {Step::Rx, Step::TxPhaseAligned});
  runCase("tx_phase_then_rx", {Step::TxPhaseAligned, Step::Rx});
  runCase("rx_then_tx_mod", {Step::Rx, Step::TxModulated});
  runCase("tx_mod_then_rx", {Step::TxModulated, Step::Rx});
  runCase("other_then_rx_then_tx_phase", {Step::Other10MHz, Step::Rx, Step::TxPhaseAligned});
  runCase("rx_then_other", {Step::Rx, Step::Other10MHz});
  Serial.println("DONE");
}

bool readLine(String &line)
{
  if (!Serial.available())
  {
    return false;
  }
  line = Serial.readStringUntil('\n');
  line.trim();
  return line.length() > 0;
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("COEXIST_READY");
}

void loop()
{
  String line;
  if (readLine(line))
  {
    if (line == "READY")
    {
      Serial.println("COEXIST_READY");
    }
    else if (line == "RUN")
    {
      runAll();
    }
  }
  delay(1);
}
