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
  esp32irpk::IRDecodedBits helper_bits = esp32irpk::bits::nec(0x00ff, 0x34);
  EXPECT_EQ("nec/protocol", esp32irpk::IRProtocolID::NEC, bits.protocol_id);
  EXPECT_EQ("nec/length", 32, bits.bit_length);
  EXPECT_EQ("nec/bits", 0xcb3400ffULL, bits.bits);
  EXPECT_EQ("nec/helper-bits", bits.bits, helper_bits.bits);
  EXPECT_EQ("nec/helper-length", bits.bit_length, helper_bits.bit_length);

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

void testEncodeRejectsInvalidInputs()
{
  uint16_t ticks[160]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = ticks;
  raw.capacity = sizeof(ticks) / sizeof(ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC, esp32irpk::specs::AEHA};

  esp32irpk::IRDecodedBits unknown{};
  unknown.protocol_id = esp32irpk::IRProtocolID::USER1;
  unknown.bit_length = 32;
  unknown.bits = 0x1234;
  EXPECT_TRUE("encode-invalid/unknown-protocol", !esp32irpk::codec::encodeBitsToRaw(unknown, specs, 2, raw));
  EXPECT_EQ("encode-invalid/unknown-len", 0, raw.len);

  esp32irpk::IRDecodedBits nec_short = esp32irpk::bits::nec(0x00ff, 0x34);
  nec_short.bit_length = 31;
  EXPECT_TRUE("encode-invalid/fixed-short", !esp32irpk::codec::encodeBitsToRaw(nec_short, specs, 2, raw));
  EXPECT_EQ("encode-invalid/fixed-short-len", 0, raw.len);

  esp32irpk::IRDecodedBits aeha_short{};
  aeha_short.protocol_id = esp32irpk::IRProtocolID::AEHA;
  aeha_short.bit_length = 47;
  aeha_short.bits = 0x123456789abcULL;
  EXPECT_TRUE("encode-invalid/variable-short", !esp32irpk::codec::encodeBitsToRaw(aeha_short, specs, 2, raw));
  EXPECT_EQ("encode-invalid/variable-short-len", 0, raw.len);

  esp32irpk::IRDecodedBits aeha_long = aeha_short;
  aeha_long.bit_length = 65;
  EXPECT_TRUE("encode-invalid/variable-long", !esp32irpk::codec::encodeBitsToRaw(aeha_long, specs, 2, raw));
  EXPECT_EQ("encode-invalid/variable-long-len", 0, raw.len);

  esp32irpk::frames::Sony12Frame repeat_sony{};
  repeat_sony.is_repeat = true;
  const esp32irpk::IRProtocolSpec sony_specs[] = {esp32irpk::specs::SONY12};
  EXPECT_TRUE("encode-invalid/repeat-unsupported",
              !esp32irpk::codec::encodeBitsToRaw(repeat_sony.toBits(), sony_specs, 1, raw));
  EXPECT_EQ("encode-invalid/repeat-unsupported-len", 0, raw.len);
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
  esp32irpk::IRDecodedBits repeat_bits = esp32irpk::bits::necRepeat();
  EXPECT_TRUE("nec-repeat/helper-frame-type", repeat_bits.isRepeat());
  EXPECT_EQ("nec-repeat/helper-length", 0, repeat_bits.bit_length);

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

void testNecRepeatEncode()
{
  esp32irpk::IRDecodedBits repeat_bits = esp32irpk::bits::necRepeat();
  uint16_t ticks[8]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = ticks;
  raw.capacity = sizeof(ticks) / sizeof(ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
  EXPECT_TRUE("nec-repeat/encode", esp32irpk::codec::encodeBitsToRaw(repeat_bits, specs, 1, raw));
  EXPECT_EQ("nec-repeat/raw-len", test_fixtures::nec_repeat_raw_len, raw.len);
  for (size_t i = 0; i < test_fixtures::nec_repeat_raw_len; ++i)
  {
    EXPECT_EQ("nec-repeat/raw-tick", test_fixtures::nec_repeat_raw_ticks[i], raw.ticks[i]);
  }
}

void testSenderEncodeLifecycle()
{
  esp32irpk::IRSender tx(4);
  esp32irpk::IRDecodedBits bits = esp32irpk::bits::nec(0x00ff, 0x34);
  uint16_t ticks[96]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = ticks;
  raw.capacity = sizeof(ticks) / sizeof(ticks[0]);

  EXPECT_TRUE("sender-encode/before-begin", !tx.encode(bits, raw));
  EXPECT_TRUE("sender-encode/null-decoded", !tx.send(static_cast<const esp32irpk::IRDecodedBits *>(nullptr)));
  EXPECT_TRUE("sender-encode/null-raw", !tx.send(static_cast<const esp32irpk::IRRawTickView *>(nullptr)));
  EXPECT_TRUE("sender-encode/begin", tx.begin());
  EXPECT_TRUE("sender-encode/after-begin", tx.encode(bits, raw));
  EXPECT_EQ("sender-encode/raw-len", 67, raw.len);
  EXPECT_TRUE("sender-encode/set-pin-after-begin", !tx.setPin(5));
  EXPECT_TRUE("sender-encode/second-begin", !tx.begin());
  tx.end();
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

void testSony12FixtureDecode()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::sony12_0a90_raw_ticks;
  view.len = test_fixtures::sony12_0a90_raw_len;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::SONY12};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("sony12-fixture/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("sony12-fixture/candidates", 1, result.count);
  EXPECT_EQ("sony12-fixture/protocol", esp32irpk::IRProtocolID::SONY12, result.candidates[0].protocol_id);
  EXPECT_EQ("sony12-fixture/bits", test_fixtures::sony12_0a90_bits, result.candidates[0].decoded.bits);
  EXPECT_EQ("sony12-fixture/length", test_fixtures::sony12_0a90_bit_length, result.candidates[0].decoded.bit_length);

  esp32irpk::frames::Sony12Frame frame =
      esp32irpk::frames::Sony12Frame::fromBits(result.candidates[0].decoded);
  EXPECT_EQ("sony12-fixture/data", 0x0a90u, frame.data);
}

void testSamsung32FixtureDecode()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::samsung32_e0e0_40bf_raw_ticks;
  view.len = test_fixtures::samsung32_e0e0_40bf_raw_len;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::SAMSUNG32};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("samsung32-fixture/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("samsung32-fixture/candidates", 1, result.count);
  EXPECT_EQ("samsung32-fixture/protocol", esp32irpk::IRProtocolID::SAMSUNG32, result.candidates[0].protocol_id);
  EXPECT_EQ("samsung32-fixture/bits", test_fixtures::samsung32_e0e0_40bf_bits, result.candidates[0].decoded.bits);
  EXPECT_EQ("samsung32-fixture/length", test_fixtures::samsung32_e0e0_40bf_bit_length, result.candidates[0].decoded.bit_length);

  esp32irpk::frames::Samsung32Frame frame =
      esp32irpk::frames::Samsung32Frame::fromBits(result.candidates[0].decoded);
  EXPECT_EQ("samsung32-fixture/address", 0xe0e0u, frame.address);
  EXPECT_EQ("samsung32-fixture/command", 0x40bfu, frame.command);
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

void testReceiverDecodeLifecycle()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  view.len = test_fixtures::nec_normal_00ff_34_raw_len;

  esp32irpk::IRReceiver<2> rx(4);
  esp32irpk::IRReceiveResult<2> result{};
  EXPECT_TRUE("receiver-decode/no-protocol", !rx.decode(view, result));

  EXPECT_TRUE("receiver-decode/add-nec", rx.addProtocol(esp32irpk::specs::NEC));
  EXPECT_TRUE("receiver-decode/works-before-begin", rx.decode(view, result));
  EXPECT_EQ("receiver-decode/count", 1, result.count);
  EXPECT_EQ("receiver-decode/name-0", 'N', result.candidates[0].name[0]);
  EXPECT_EQ("receiver-decode/name-1", 'E', result.candidates[0].name[1]);
  EXPECT_EQ("receiver-decode/name-2", 'C', result.candidates[0].name[2]);
  EXPECT_EQ("receiver-decode/name-nul", '\0', result.candidates[0].name[3]);

  EXPECT_TRUE("receiver-decode/clear", rx.clearProtocols());
  EXPECT_TRUE("receiver-decode/after-clear", !rx.decode(view, result));
}

void testReceiverConfigurationLifecycle()
{
  esp32irpk::IRReceiver<1> rx(4);
  EXPECT_TRUE("receiver-config/set-pin", rx.setPin(5));
  EXPECT_TRUE("receiver-config/set-inverted", rx.setInverted(true));
  EXPECT_TRUE("receiver-config/candidate-too-large", !rx.setDecodeCandidates(2));
  EXPECT_TRUE("receiver-config/raw-only", rx.setDecodeCandidates(0));
  EXPECT_TRUE("receiver-config/idle-threshold", rx.setIdleThresholdUs(42000));
  EXPECT_TRUE("receiver-config/score-threshold", rx.setScoreThreshold(10));
  EXPECT_TRUE("receiver-config/begin", rx.begin());
  EXPECT_TRUE("receiver-config/set-pin-after-begin", !rx.setPin(6));
  EXPECT_TRUE("receiver-config/set-inverted-after-begin", !rx.setInverted(false));
  EXPECT_TRUE("receiver-config/candidates-after-begin", !rx.setDecodeCandidates(1));
  EXPECT_TRUE("receiver-config/idle-after-begin", !rx.setIdleThresholdUs(30000));
  EXPECT_TRUE("receiver-config/score-after-begin", !rx.setScoreThreshold(0));
  EXPECT_TRUE("receiver-config/add-protocol-after-begin", !rx.addProtocol(esp32irpk::specs::NEC));
  EXPECT_TRUE("receiver-config/clear-after-begin", !rx.clearProtocols());
  EXPECT_TRUE("receiver-config/second-begin", !rx.begin());
  rx.end();
  EXPECT_TRUE("receiver-config/set-after-end", rx.setDecodeCandidates(1));
}

void testDecodeCandidateLimitZero()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  view.len = test_fixtures::nec_normal_00ff_34_raw_len;

  esp32irpk::IRReceiveResult<4> result{};
  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
  EXPECT_TRUE("candidate-limit-zero/reject",
              !esp32irpk::codec::decodeRawToBits(view, specs, 1, 0, 0, result));
  EXPECT_EQ("candidate-limit-zero/count", 0, result.count);
}

void testDecodeConsumesConcatenatedFramePrefix()
{
  uint16_t ticks[96]{};
  size_t pos = 0;
  for (size_t i = 0; i < test_fixtures::nec_normal_00ff_34_raw_len; ++i)
  {
    ticks[pos++] = test_fixtures::nec_normal_00ff_34_raw_ticks[i];
  }
  ticks[pos++] = 3000; // 30ms gap in ticks
  ticks[pos++] = 900;  // next NEC header mark fragment
  ticks[pos++] = 450;  // next NEC header space fragment

  esp32irpk::IRRawTickView view{};
  view.ticks = ticks;
  view.len = pos;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("concat/decode-first", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("concat/consumed-len", test_fixtures::nec_normal_00ff_34_raw_len + 1, result.candidates[0].consumed_len);
  EXPECT_EQ("concat/raw-len", result.candidates[0].consumed_len, result.raw.len);
  EXPECT_EQ("concat/bits", test_fixtures::nec_normal_00ff_34_bits, result.candidates[0].decoded.bits);
}

void testFrameConversions()
{
  esp32irpk::frames::Sony12Frame sony12{};
  sony12.data = 0x1abc;
  esp32irpk::IRDecodedBits sony12_bits = sony12.toBits();
  EXPECT_EQ("frame/sony12-protocol", esp32irpk::IRProtocolID::SONY12, sony12_bits.protocol_id);
  EXPECT_EQ("frame/sony12-length", 12, sony12_bits.bit_length);
  EXPECT_EQ("frame/sony12-mask", 0x0abcULL, sony12_bits.bits);
  EXPECT_EQ("frame/sony12-from", 0x0abcu, esp32irpk::frames::Sony12Frame::fromBits(sony12_bits).data);

  esp32irpk::frames::Sony15Frame sony15{};
  sony15.data = 0xffff;
  esp32irpk::IRDecodedBits sony15_bits = sony15.toBits();
  EXPECT_EQ("frame/sony15-length", 15, sony15_bits.bit_length);
  EXPECT_EQ("frame/sony15-mask", 0x7fffULL, sony15_bits.bits);

  esp32irpk::frames::Sony20Frame sony20{};
  sony20.data = 0x1fffff;
  esp32irpk::IRDecodedBits sony20_bits = sony20.toBits();
  EXPECT_EQ("frame/sony20-length", 20, sony20_bits.bit_length);
  EXPECT_EQ("frame/sony20-mask", 0x0fffffULL, sony20_bits.bits);

  esp32irpk::frames::Samsung36Frame samsung36{};
  samsung36.address = 0xabcd;
  samsung36.command = 0x1abcde;
  esp32irpk::IRDecodedBits samsung36_bits = samsung36.toBits();
  EXPECT_EQ("frame/samsung36-length", 36, samsung36_bits.bit_length);
  EXPECT_EQ("frame/samsung36-bits", 0xabcdeabcdULL, samsung36_bits.bits);
  esp32irpk::frames::Samsung36Frame samsung36_roundtrip =
      esp32irpk::frames::Samsung36Frame::fromBits(samsung36_bits);
  EXPECT_EQ("frame/samsung36-address", 0xabcdu, samsung36_roundtrip.address);
  EXPECT_EQ("frame/samsung36-command", 0xabcdeu, samsung36_roundtrip.command);

  esp32irpk::frames::JVC24Frame jvc24{};
  jvc24.data = 0x1abcdef;
  esp32irpk::IRDecodedBits jvc24_bits = jvc24.toBits();
  EXPECT_EQ("frame/jvc24-length", 24, jvc24_bits.bit_length);
  EXPECT_EQ("frame/jvc24-mask", 0xabcdefULL, jvc24_bits.bits);

  esp32irpk::frames::Panasonic48Frame panasonic48{};
  panasonic48.data = 0x123456789abcULL;
  esp32irpk::IRDecodedBits panasonic48_bits = panasonic48.toBits();
  EXPECT_EQ("frame/panasonic48-protocol", esp32irpk::IRProtocolID::PANASONIC48, panasonic48_bits.protocol_id);
  EXPECT_EQ("frame/panasonic48-length", 48, panasonic48_bits.bit_length);
  EXPECT_EQ("frame/panasonic48-bits", 0x123456789abcULL, panasonic48_bits.bits);
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);

  testNecEncodeDecodeRoundtrip();
  testNecRejectsUndersizedBuffer();
  testVariableLengthEncodeDecode();
  testEncodeRejectsInvalidInputs();
  testMsbFirstVariableLengthDecode();
  testNecRepeatDecode();
  testNecRepeatEncode();
  testSenderEncodeLifecycle();
  testNecFixtureDecode();
  testSony12FixtureDecode();
  testSamsung32FixtureDecode();
  testScoreThresholdFiltersCandidate();
  testCandidateOrderBreaksScoreTies();
  testReceiverDecodeLifecycle();
  testReceiverConfigurationLifecycle();
  testDecodeCandidateLimitZero();
  testDecodeConsumesConcatenatedFramePrefix();
  testFrameConversions();

  Serial.print("TEST done ");
  Serial.print(g_passed);
  Serial.print("/");
  Serial.println(g_total);
}

void loop()
{
  delay(1);
}
