#include <ESP32IRPulseKit.h>
#include <codec/Encoder.h>
#include "verified_fixtures.h"

namespace
{
int g_total = 0;
int g_passed = 0;

#define EXPECT_TRUE(name, cond)                                                     \
  do                                                                                \
  {                                                                                 \
    ++g_total;                                                                      \
    if (cond)                                                                       \
    {                                                                               \
      ++g_passed;                                                                   \
    }                                                                               \
    else                                                                            \
    {                                                                               \
      Serial.print("FAIL ");                                                       \
      Serial.println(name);                                                         \
    }                                                                               \
  } while (0)

#define EXPECT_EQ(name, expected, actual) EXPECT_TRUE(name, (expected) == (actual))

void testNecEncodeDecodeRoundtrip()
{
  esp32irpk::frames::NECFrame frame{};
  frame.address = 0x00ff;
  frame.command = 0x34;

  esp32irpk::IRDecodedBits bits = frame.toBits();
  EXPECT_EQ("nec/protocol", esp32irpk::IRProtocolID::NEC, bits.protocol_id);
  EXPECT_EQ("nec/length", 32, bits.bit_length);
  EXPECT_EQ("nec/bits", 0xcb3400ffULL, bits.bits);

  uint16_t ticks[96]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = ticks;
  raw.capacity = sizeof(ticks) / sizeof(ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
  EXPECT_TRUE("nec/encode", esp32irpk::codec::encodeBitsToRaw(bits, specs, 1, raw));
  EXPECT_EQ("nec/raw-len", 67, raw.len);

  esp32irpk::IRRawTickView view{};
  view.ticks = raw.ticks;
  view.len = raw.len;

  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("nec/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("nec/candidates", 1, result.count);
  EXPECT_EQ("nec/decoded-protocol", esp32irpk::IRProtocolID::NEC, result.candidates[0].protocol_id);
  EXPECT_EQ("nec/decoded-length", bits.bit_length, result.candidates[0].decoded.bit_length);
  EXPECT_EQ("nec/decoded-bits", bits.bits, result.candidates[0].decoded.bits);
  EXPECT_EQ("nec/score", 1000, result.candidates[0].score);

  esp32irpk::frames::NECFrame decoded_frame =
      esp32irpk::frames::NECFrame::fromBits(result.candidates[0].decoded);
  EXPECT_EQ("nec/frame-address", frame.address, decoded_frame.address);
  EXPECT_EQ("nec/frame-command", frame.command, decoded_frame.command);
}

void testNecRejectsUndersizedBuffer()
{
  esp32irpk::frames::NECFrame frame{};
  frame.address = 0x1234;
  frame.command = 0x56;

  esp32irpk::IRDecodedBits bits = frame.toBits();
  uint16_t ticks[8]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = ticks;
  raw.capacity = sizeof(ticks) / sizeof(ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
  EXPECT_TRUE("nec/small-buffer-reject",
              !esp32irpk::codec::encodeBitsToRaw(bits, specs, 1, raw));
  EXPECT_EQ("nec/small-buffer-len", 0, raw.len);
}

void testVariableLengthEncodeDecode()
{
  esp32irpk::IRDecodedBits bits{};
  bits.protocol_id = esp32irpk::IRProtocolID::AEHA;
  bits.bit_length = 48;
  bits.bits = 0x123456789abcULL;

  uint16_t ticks[120]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = ticks;
  raw.capacity = sizeof(ticks) / sizeof(ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::AEHA};
  EXPECT_TRUE("variable/encode-48", esp32irpk::codec::encodeBitsToRaw(bits, specs, 1, raw));
  EXPECT_EQ("variable/raw-len", 99, raw.len);

  esp32irpk::IRRawTickView view{};
  view.ticks = raw.ticks;
  view.len = raw.len;

  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("variable/decode-48", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("variable/candidates", 1, result.count);
  EXPECT_EQ("variable/length", bits.bit_length, result.candidates[0].decoded.bit_length);
  EXPECT_EQ("variable/bits", bits.bits, result.candidates[0].decoded.bits);
}

void testMsbFirstVariableLengthDecode()
{
  esp32irpk::IRProtocolSpec spec{};
  spec.protocol_id = esp32irpk::IRProtocolID::USER1;
  spec.name[0] = 'M';
  spec.scheme = esp32irpk::IRProtocolScheme::SPACE_ENC;
  spec.family = esp32irpk::IRProtocolFamily::UNKNOWN;
  spec.header = {.mark_us = 1000, .space_us = 500};
  spec.one = {.mark_us = 100, .space_us = 300};
  spec.zero = {.mark_us = 100, .space_us = 100};
  spec.trailer = {.mark_us = 100, .space_us = 0};
  spec.lsb_first = false;
  spec.bit_length = 8;
  spec.min_bit_length = 4;
  spec.max_bit_length = 8;
  spec.bit_tol_pct = 25;

  const uint16_t raw_ticks[] = {
      100, 50,
      10, 30,
      10, 10,
      10, 30,
      10, 10,
      10};

  esp32irpk::IRRawTickView view{};
  view.ticks = raw_ticks;
  view.len = sizeof(raw_ticks) / sizeof(raw_ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {spec};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("msb-variable/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("msb-variable/length", 4, result.candidates[0].decoded.bit_length);
  EXPECT_EQ("msb-variable/bits", 0x0aULL, result.candidates[0].decoded.bits);
}

void testNecRepeatDecode()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::nec_repeat_raw_ticks;
  view.len = test_fixtures::nec_repeat_raw_len;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("nec-repeat/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("nec-repeat/candidates", 1, result.count);
  EXPECT_EQ("nec-repeat/protocol", esp32irpk::IRProtocolID::NEC, result.candidates[0].protocol_id);
  EXPECT_TRUE("nec-repeat/frame-type", result.candidates[0].decoded.isRepeat());
  EXPECT_EQ("nec-repeat/length", test_fixtures::nec_repeat_bit_length, result.candidates[0].decoded.bit_length);
}

void testNecFixtureDecode()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  view.len = test_fixtures::nec_normal_00ff_34_raw_len;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("nec-fixture/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("nec-fixture/candidates", 1, result.count);
  EXPECT_EQ("nec-fixture/bits", test_fixtures::nec_normal_00ff_34_bits, result.candidates[0].decoded.bits);
  EXPECT_EQ("nec-fixture/length", test_fixtures::nec_normal_00ff_34_bit_length, result.candidates[0].decoded.bit_length);

  esp32irpk::frames::NECFrame frame =
      esp32irpk::frames::NECFrame::fromBits(result.candidates[0].decoded);
  EXPECT_EQ("nec-fixture/address", 0x00ff, frame.address);
  EXPECT_EQ("nec-fixture/command", 0x34, frame.command);
}

void testScoreThresholdFiltersCandidate()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  view.len = test_fixtures::nec_normal_00ff_34_raw_len;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("threshold/rejects-perfect-score",
              !esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 1001, result));
  EXPECT_EQ("threshold/no-candidates", 0, result.count);
}

void testCandidateOrderBreaksScoreTies()
{
  esp32irpk::IRProtocolSpec first = esp32irpk::specs::NEC;
  esp32irpk::IRProtocolSpec second = esp32irpk::specs::NEC;
  first.protocol_id = esp32irpk::IRProtocolID::USER1;
  second.protocol_id = esp32irpk::IRProtocolID::USER2;
  first.order = 1;
  second.order = 0;
  const esp32irpk::IRProtocolSpec specs[] = {first, second};

  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  view.len = test_fixtures::nec_normal_00ff_34_raw_len;

  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("candidate-order/decode", esp32irpk::codec::decodeRawToBits(view, specs, 2, 4, 0, result));
  EXPECT_EQ("candidate-order/count", 2, result.count);
  EXPECT_EQ("candidate-order/first", esp32irpk::IRProtocolID::USER2, result.candidates[0].protocol_id);
  EXPECT_EQ("candidate-order/second", esp32irpk::IRProtocolID::USER1, result.candidates[1].protocol_id);
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);

  testNecEncodeDecodeRoundtrip();
  testNecRejectsUndersizedBuffer();
  testVariableLengthEncodeDecode();
  testMsbFirstVariableLengthDecode();
  testNecRepeatDecode();
  testNecFixtureDecode();
  testScoreThresholdFiltersCandidate();
  testCandidateOrderBreaksScoreTies();

  Serial.print("TEST done ");
  Serial.print(g_passed);
  Serial.print("/");
  Serial.println(g_total);
}

void loop()
{
  delay(1);
}
