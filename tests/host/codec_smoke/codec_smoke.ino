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

void testProtocolCarrierPreferences()
{
  EXPECT_EQ("carrier/nec", esp32irpk::kDefaultCarrierHz, esp32irpk::specs::NEC.carrier_hz);
  EXPECT_EQ("carrier/aeha", esp32irpk::kDefaultCarrierHz, esp32irpk::specs::AEHA.carrier_hz);
  EXPECT_EQ("carrier/samsung32", esp32irpk::kDefaultCarrierHz, esp32irpk::specs::SAMSUNG32.carrier_hz);
  EXPECT_EQ("carrier/samsung36", esp32irpk::kDefaultCarrierHz, esp32irpk::specs::SAMSUNG36.carrier_hz);
  EXPECT_EQ("carrier/jvc", 37900, esp32irpk::specs::JVC.carrier_hz);
  EXPECT_EQ("carrier/sony12", 40000, esp32irpk::specs::SONY12.carrier_hz);
  EXPECT_EQ("carrier/sony15", 40000, esp32irpk::specs::SONY15.carrier_hz);
  EXPECT_EQ("carrier/sony20", 40000, esp32irpk::specs::SONY20.carrier_hz);
  EXPECT_EQ("carrier/rc5", 36000, esp32irpk::specs::RC5.carrier_hz);
  EXPECT_EQ("carrier/rc6-m0", 36000, esp32irpk::specs::RC6_M0_16.carrier_hz);
  EXPECT_EQ("carrier/rc6-m6", 36000, esp32irpk::specs::RC6_M6_32.carrier_hz);
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

void testToleranceBoundaries()
{
  esp32irpk::IRProtocolSpec spec{};
  spec.protocol_id = esp32irpk::IRProtocolID::USER2;
  spec.name[0] = 'T';
  spec.scheme = esp32irpk::IRProtocolScheme::SPACE_ENC;
  spec.family = esp32irpk::IRProtocolFamily::UNKNOWN;
  spec.header = {.mark_us = 1000, .space_us = 1000};
  spec.one = {.mark_us = 1000, .space_us = 2000};
  spec.zero = {.mark_us = 1000, .space_us = 1000};
  spec.trailer = {.mark_us = 1000, .space_us = 0};
  spec.lsb_first = true;
  spec.bit_length = 1;
  spec.bit_tol_pct = 25;

  const esp32irpk::IRProtocolSpec specs[] = {spec};

  const uint16_t upper_boundary_ticks[] = {
      125, 125,
      125, 250,
      125};
  esp32irpk::IRRawTickView upper_view{};
  upper_view.ticks = upper_boundary_ticks;
  upper_view.len = sizeof(upper_boundary_ticks) / sizeof(upper_boundary_ticks[0]);

  esp32irpk::IRReceiveResult<4> upper_result{};
  EXPECT_TRUE("tolerance/upper-boundary-decode",
              esp32irpk::codec::decodeRawToBits(upper_view, specs, 1, 4, 0, upper_result));
  EXPECT_EQ("tolerance/upper-boundary-bits", 1ULL, upper_result.candidates[0].decoded.bits);

  const uint16_t lower_boundary_ticks[] = {
      75, 75,
      75, 150,
      75};
  esp32irpk::IRRawTickView lower_view{};
  lower_view.ticks = lower_boundary_ticks;
  lower_view.len = sizeof(lower_boundary_ticks) / sizeof(lower_boundary_ticks[0]);

  esp32irpk::IRReceiveResult<4> lower_result{};
  EXPECT_TRUE("tolerance/lower-boundary-decode",
              esp32irpk::codec::decodeRawToBits(lower_view, specs, 1, 4, 0, lower_result));
  EXPECT_EQ("tolerance/lower-boundary-bits", 1ULL, lower_result.candidates[0].decoded.bits);

  const uint16_t outside_ticks[] = {
      126, 125,
      125, 250,
      125};
  esp32irpk::IRRawTickView outside_view{};
  outside_view.ticks = outside_ticks;
  outside_view.len = sizeof(outside_ticks) / sizeof(outside_ticks[0]);

  esp32irpk::IRReceiveResult<4> outside_result{};
  EXPECT_TRUE("tolerance/outside-reject",
              !esp32irpk::codec::decodeRawToBits(outside_view, specs, 1, 4, 0, outside_result));
  EXPECT_EQ("tolerance/outside-count", 0, outside_result.count);
}

void testSpaceEncodedRelaxedCandidateScoring()
{
  esp32irpk::IRProtocolSpec spec{};
  spec.protocol_id = esp32irpk::IRProtocolID::USER3;
  spec.name[0] = 'R';
  spec.scheme = esp32irpk::IRProtocolScheme::SPACE_ENC;
  spec.family = esp32irpk::IRProtocolFamily::UNKNOWN;
  spec.header = {.mark_us = 1000, .space_us = 1000};
  spec.one = {.mark_us = 500, .space_us = 1500};
  spec.zero = {.mark_us = 500, .space_us = 500};
  spec.trailer = {.mark_us = 500, .space_us = 0};
  spec.lsb_first = true;
  spec.bit_length = 4;
  spec.bit_tol_pct = 25;

  const esp32irpk::IRProtocolSpec specs[] = {spec};

  const uint16_t ideal_ticks[] = {
      100, 100,
      50, 50,
      50, 150,
      50, 50,
      50, 150,
      50};
  esp32irpk::IRRawTickView ideal_view{};
  ideal_view.ticks = ideal_ticks;
  ideal_view.len = sizeof(ideal_ticks) / sizeof(ideal_ticks[0]);

  esp32irpk::IRReceiveResult<4> ideal_result{};
  EXPECT_TRUE("relaxed-space/ideal-decode",
              esp32irpk::codec::decodeRawToBits(ideal_view, specs, 1, 4, 0, ideal_result));
  EXPECT_EQ("relaxed-space/ideal-bits", 0x0aULL, ideal_result.candidates[0].decoded.bits);

  const uint16_t noisy_ticks[] = {
      100, 100,
      70, 68,
      50, 150,
      50, 68,
      50, 150,
      50};
  esp32irpk::IRRawTickView noisy_view{};
  noisy_view.ticks = noisy_ticks;
  noisy_view.len = sizeof(noisy_ticks) / sizeof(noisy_ticks[0]);

  esp32irpk::IRReceiveResult<4> noisy_result{};
  EXPECT_TRUE("relaxed-space/noisy-decode",
              esp32irpk::codec::decodeRawToBits(noisy_view, specs, 1, 4, 0, noisy_result));
  EXPECT_EQ("relaxed-space/noisy-bits", 0x0aULL, noisy_result.candidates[0].decoded.bits);
  EXPECT_TRUE("relaxed-space/noisy-score-lower",
              noisy_result.candidates[0].score < ideal_result.candidates[0].score);
  EXPECT_TRUE("relaxed-space/noisy-score-accepted", noisy_result.candidates[0].score >= 0);
}

void testSpaceEncodedAmbiguousSpaceRejects()
{
  esp32irpk::IRProtocolSpec spec{};
  spec.protocol_id = esp32irpk::IRProtocolID::USER4;
  spec.name[0] = 'A';
  spec.scheme = esp32irpk::IRProtocolScheme::SPACE_ENC;
  spec.family = esp32irpk::IRProtocolFamily::UNKNOWN;
  spec.header = {.mark_us = 1000, .space_us = 1000};
  spec.one = {.mark_us = 500, .space_us = 1500};
  spec.zero = {.mark_us = 500, .space_us = 500};
  spec.trailer = {.mark_us = 500, .space_us = 0};
  spec.lsb_first = true;
  spec.bit_length = 1;
  spec.bit_tol_pct = 25;

  const uint16_t ambiguous_ticks[] = {
      100, 100,
      50, 100,
      50};
  esp32irpk::IRRawTickView view{};
  view.ticks = ambiguous_ticks;
  view.len = sizeof(ambiguous_ticks) / sizeof(ambiguous_ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {spec};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("relaxed-space/ambiguous-reject",
              !esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("relaxed-space/ambiguous-count", 0, result.count);
}

void expectEncodeDecodeRoundtrip(const char *name,
                                 const esp32irpk::IRProtocolSpec &spec,
                                 const esp32irpk::IRDecodedBits &bits)
{
  uint16_t ticks[160]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = ticks;
  raw.capacity = sizeof(ticks) / sizeof(ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {spec};
  EXPECT_TRUE(name, esp32irpk::codec::encodeBitsToRaw(bits, specs, 1, raw));

  esp32irpk::IRRawTickView view{};
  view.ticks = raw.ticks;
  view.len = raw.len;

  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE(name, esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ(name, 1, result.count);
  EXPECT_EQ(name, bits.protocol_id, result.candidates[0].decoded.protocol_id);
  EXPECT_EQ(name, bits.bit_length, result.candidates[0].decoded.bit_length);
  EXPECT_EQ(name, bits.bits, result.candidates[0].decoded.bits);
}

void testGeneratedProtocolRoundtrips()
{
  esp32irpk::frames::Sony15Frame sony15{};
  sony15.data = 0x3456;
  expectEncodeDecodeRoundtrip("generated/sony15",
                              esp32irpk::specs::SONY15,
                              sony15.toBits());

  esp32irpk::frames::Sony20Frame sony20{};
  sony20.data = 0xabcde;
  expectEncodeDecodeRoundtrip("generated/sony20",
                              esp32irpk::specs::SONY20,
                              sony20.toBits());

  esp32irpk::frames::Samsung36Frame samsung36{};
  samsung36.address = 0x1234;
  samsung36.command = 0xabcde;
  expectEncodeDecodeRoundtrip("generated/samsung36",
                              esp32irpk::specs::SAMSUNG36,
                              samsung36.toBits());

  esp32irpk::frames::JVCFrame jvc{};
  jvc.address = 0xde;
  jvc.command = 0xc0;
  expectEncodeDecodeRoundtrip("generated/jvc",
                              esp32irpk::specs::JVC,
                              jvc.toBits());
}

void testBiphaseEncodeDecodeRoundtrips()
{
  esp32irpk::IRDecodedBits rc5{};
  rc5.protocol_id = esp32irpk::IRProtocolID::RC5;
  rc5.frame_type = esp32irpk::IRFrameType::NORMAL;
  rc5.bit_length = 14;
  rc5.bits = 0x300f;
  expectEncodeDecodeRoundtrip("generated/rc5",
                              esp32irpk::specs::RC5,
                              rc5);

  esp32irpk::IRDecodedBits rc6_m0{};
  rc6_m0.protocol_id = esp32irpk::IRProtocolID::RC6_M0_16;
  rc6_m0.frame_type = esp32irpk::IRFrameType::NORMAL;
  rc6_m0.bit_length = 21;
  rc6_m0.bits = 0x111234;
  expectEncodeDecodeRoundtrip("generated/rc6-m0",
                              esp32irpk::specs::RC6_M0_16,
                              rc6_m0);

  esp32irpk::IRDecodedBits rc6_m6{};
  rc6_m6.protocol_id = esp32irpk::IRProtocolID::RC6_M6_32;
  rc6_m6.frame_type = esp32irpk::IRFrameType::NORMAL;
  rc6_m6.bit_length = 36;
  rc6_m6.bits = 0xe89abcdefULL;
  expectEncodeDecodeRoundtrip("generated/rc6-m6",
                              esp32irpk::specs::RC6_M6_32,
                              rc6_m6);
}

void perturbRawTicks(const esp32irpk::IRRawTickBuffer &src,
                     uint16_t *dst,
                     size_t capacity,
                     esp32irpk::IRRawTickView &out)
{
  size_t n = src.len < capacity ? src.len : capacity;
  for (size_t i = 0; i < n; ++i)
  {
    uint16_t tick = src.ticks[i];
    if (i >= 2)
    {
      if ((i % 4) == 0 && tick > 2)
        tick = static_cast<uint16_t>(tick - 2);
      else if ((i % 4) == 1)
        tick = static_cast<uint16_t>(tick + 3);
      else if ((i % 4) == 2 && tick > 1)
        tick = static_cast<uint16_t>(tick - 1);
      else if ((i % 4) == 3)
        tick = static_cast<uint16_t>(tick + 2);
    }
    dst[i] = tick;
  }
  out.ticks = dst;
  out.len = n;
}

void testSimilarSpaceEncodedRankingWithNoisyTiming()
{
  esp32irpk::IRDecodedBits aeha{};
  aeha.protocol_id = esp32irpk::IRProtocolID::AEHA;
  aeha.frame_type = esp32irpk::IRFrameType::NORMAL;
  aeha.bit_length = 48;
  aeha.bits = 0x123456749abcULL; // AEHA parity nibble matches low16 customer code.

  const esp32irpk::IRProtocolSpec tx_aeha_specs[] = {esp32irpk::specs::AEHA};
  const esp32irpk::IRProtocolSpec rank_specs[] = {
      esp32irpk::specs::AEHA,
      esp32irpk::specs::NEC,
  };

  uint16_t raw_ticks[128]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = raw_ticks;
  raw.capacity = sizeof(raw_ticks) / sizeof(raw_ticks[0]);

  EXPECT_TRUE("ranking/aeha-encode", esp32irpk::codec::encodeBitsToRaw(aeha, tx_aeha_specs, 1, raw));
  uint16_t noisy_aeha_ticks[128]{};
  esp32irpk::IRRawTickView noisy_aeha{};
  perturbRawTicks(raw, noisy_aeha_ticks, sizeof(noisy_aeha_ticks) / sizeof(noisy_aeha_ticks[0]), noisy_aeha);

  esp32irpk::IRReceiveResult<4> aeha_result{};
  EXPECT_TRUE("ranking/aeha-noisy-decode",
              esp32irpk::codec::decodeRawToBits(noisy_aeha, rank_specs, 2, 4, 0, aeha_result));
  EXPECT_EQ("ranking/aeha-noisy-first", esp32irpk::IRProtocolID::AEHA, aeha_result.candidates[0].protocol_id);
  EXPECT_EQ("ranking/aeha-noisy-bits", aeha.bits, aeha_result.candidates[0].decoded.bits);
}

void testNecLikeRankingWithNoisyTiming()
{
  esp32irpk::IRDecodedBits nec{};
  nec.protocol_id = esp32irpk::IRProtocolID::NEC;
  nec.frame_type = esp32irpk::IRFrameType::NORMAL;
  nec.bit_length = 32;
  nec.bits = 0xcb3400ffULL;

  esp32irpk::IRDecodedBits samsung{};
  samsung.protocol_id = esp32irpk::IRProtocolID::SAMSUNG32;
  samsung.frame_type = esp32irpk::IRFrameType::NORMAL;
  samsung.bit_length = 32;
  samsung.bits = 0x40bfe0e0ULL;

  esp32irpk::IRDecodedBits jvc{};
  jvc.protocol_id = esp32irpk::IRProtocolID::JVC;
  jvc.frame_type = esp32irpk::IRFrameType::NORMAL;
  jvc.bit_length = 16;
  jvc.bits = 0xc0deULL;

  const esp32irpk::IRProtocolSpec tx_nec_specs[] = {esp32irpk::specs::NEC};
  const esp32irpk::IRProtocolSpec tx_samsung_specs[] = {esp32irpk::specs::SAMSUNG32};
  const esp32irpk::IRProtocolSpec tx_jvc_specs[] = {esp32irpk::specs::JVC};
  const esp32irpk::IRProtocolSpec rank_specs[] = {
      esp32irpk::specs::SAMSUNG32,
      esp32irpk::specs::NEC,
      esp32irpk::specs::JVC,
  };

  uint16_t raw_ticks[128]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = raw_ticks;
  raw.capacity = sizeof(raw_ticks) / sizeof(raw_ticks[0]);

  EXPECT_TRUE("ranking/nec-like-nec-encode", esp32irpk::codec::encodeBitsToRaw(nec, tx_nec_specs, 1, raw));
  uint16_t noisy_nec_ticks[128]{};
  esp32irpk::IRRawTickView noisy_nec{};
  perturbRawTicks(raw, noisy_nec_ticks, sizeof(noisy_nec_ticks) / sizeof(noisy_nec_ticks[0]), noisy_nec);

  esp32irpk::IRReceiveResult<4> nec_result{};
  EXPECT_TRUE("ranking/nec-like-nec-decode",
              esp32irpk::codec::decodeRawToBits(noisy_nec, rank_specs, 3, 4, 0, nec_result));
  EXPECT_EQ("ranking/nec-like-nec-first", esp32irpk::IRProtocolID::NEC, nec_result.candidates[0].protocol_id);
  EXPECT_EQ("ranking/nec-like-nec-bits", nec.bits, nec_result.candidates[0].decoded.bits);
  EXPECT_TRUE("ranking/nec-like-nec-score-gap",
              nec_result.count == 1 || nec_result.candidates[0].score > nec_result.candidates[1].score);

  EXPECT_TRUE("ranking/nec-like-samsung-encode", esp32irpk::codec::encodeBitsToRaw(samsung, tx_samsung_specs, 1, raw));
  uint16_t noisy_samsung_ticks[128]{};
  esp32irpk::IRRawTickView noisy_samsung{};
  perturbRawTicks(raw, noisy_samsung_ticks, sizeof(noisy_samsung_ticks) / sizeof(noisy_samsung_ticks[0]), noisy_samsung);

  esp32irpk::IRReceiveResult<4> samsung_result{};
  EXPECT_TRUE("ranking/nec-like-samsung-decode",
              esp32irpk::codec::decodeRawToBits(noisy_samsung, rank_specs, 3, 4, 0, samsung_result));
  EXPECT_EQ("ranking/nec-like-samsung-first", esp32irpk::IRProtocolID::SAMSUNG32, samsung_result.candidates[0].protocol_id);
  EXPECT_EQ("ranking/nec-like-samsung-bits", samsung.bits, samsung_result.candidates[0].decoded.bits);
  EXPECT_TRUE("ranking/nec-like-samsung-score-gap",
              samsung_result.count == 1 || samsung_result.candidates[0].score > samsung_result.candidates[1].score);

  EXPECT_TRUE("ranking/nec-like-jvc-encode", esp32irpk::codec::encodeBitsToRaw(jvc, tx_jvc_specs, 1, raw));
  uint16_t noisy_jvc_ticks[128]{};
  esp32irpk::IRRawTickView noisy_jvc{};
  perturbRawTicks(raw, noisy_jvc_ticks, sizeof(noisy_jvc_ticks) / sizeof(noisy_jvc_ticks[0]), noisy_jvc);

  esp32irpk::IRReceiveResult<4> jvc_result{};
  EXPECT_TRUE("ranking/nec-like-jvc-decode",
              esp32irpk::codec::decodeRawToBits(noisy_jvc, rank_specs, 3, 4, 0, jvc_result));
  EXPECT_EQ("ranking/nec-like-jvc-first", esp32irpk::IRProtocolID::JVC, jvc_result.candidates[0].protocol_id);
  EXPECT_EQ("ranking/nec-like-jvc-bits", jvc.bits, jvc_result.candidates[0].decoded.bits);
  EXPECT_TRUE("ranking/nec-like-jvc-score-gap",
              jvc_result.count == 1 || jvc_result.candidates[0].score > jvc_result.candidates[1].score);
}

void testSonyFamilyRankingWithNoisyTiming()
{
  esp32irpk::IRDecodedBits sony12{};
  sony12.protocol_id = esp32irpk::IRProtocolID::SONY12;
  sony12.frame_type = esp32irpk::IRFrameType::NORMAL;
  sony12.bit_length = 12;
  sony12.bits = 0x0a90ULL;

  esp32irpk::IRDecodedBits sony20{};
  sony20.protocol_id = esp32irpk::IRProtocolID::SONY20;
  sony20.frame_type = esp32irpk::IRFrameType::NORMAL;
  sony20.bit_length = 20;
  sony20.bits = 0x0abcdeULL;

  const esp32irpk::IRProtocolSpec tx_sony12_specs[] = {esp32irpk::specs::SONY12};
  const esp32irpk::IRProtocolSpec tx_sony20_specs[] = {esp32irpk::specs::SONY20};
  const esp32irpk::IRProtocolSpec rank_specs[] = {
      esp32irpk::specs::SONY20,
      esp32irpk::specs::SONY15,
      esp32irpk::specs::SONY12,
  };

  uint16_t raw_ticks[96]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = raw_ticks;
  raw.capacity = sizeof(raw_ticks) / sizeof(raw_ticks[0]);

  EXPECT_TRUE("ranking/sony12-encode", esp32irpk::codec::encodeBitsToRaw(sony12, tx_sony12_specs, 1, raw));
  uint16_t noisy_sony12_ticks[96]{};
  esp32irpk::IRRawTickView noisy_sony12{};
  perturbRawTicks(raw, noisy_sony12_ticks, sizeof(noisy_sony12_ticks) / sizeof(noisy_sony12_ticks[0]), noisy_sony12);

  esp32irpk::IRReceiveResult<4> sony12_result{};
  EXPECT_TRUE("ranking/sony12-noisy-decode",
              esp32irpk::codec::decodeRawToBits(noisy_sony12, rank_specs, 3, 4, 0, sony12_result));
  EXPECT_EQ("ranking/sony12-noisy-first", esp32irpk::IRProtocolID::SONY12, sony12_result.candidates[0].protocol_id);
  EXPECT_EQ("ranking/sony12-noisy-bits", sony12.bits, sony12_result.candidates[0].decoded.bits);

  EXPECT_TRUE("ranking/sony20-encode", esp32irpk::codec::encodeBitsToRaw(sony20, tx_sony20_specs, 1, raw));
  uint16_t noisy_sony20_ticks[96]{};
  esp32irpk::IRRawTickView noisy_sony20{};
  perturbRawTicks(raw, noisy_sony20_ticks, sizeof(noisy_sony20_ticks) / sizeof(noisy_sony20_ticks[0]), noisy_sony20);

  esp32irpk::IRReceiveResult<4> sony20_result{};
  EXPECT_TRUE("ranking/sony20-noisy-decode",
              esp32irpk::codec::decodeRawToBits(noisy_sony20, rank_specs, 3, 4, 0, sony20_result));
  EXPECT_EQ("ranking/sony20-noisy-first", esp32irpk::IRProtocolID::SONY20, sony20_result.candidates[0].protocol_id);
  EXPECT_EQ("ranking/sony20-noisy-bits", sony20.bits, sony20_result.candidates[0].decoded.bits);
  EXPECT_TRUE("ranking/sony20-noisy-score-gap",
              sony20_result.count == 1 || sony20_result.candidates[0].score > sony20_result.candidates[1].score);
}

void testLengthVariantRankingWithNoisyTiming()
{
  esp32irpk::IRDecodedBits samsung36{};
  samsung36.protocol_id = esp32irpk::IRProtocolID::SAMSUNG36;
  samsung36.frame_type = esp32irpk::IRFrameType::NORMAL;
  samsung36.bit_length = 36;
  samsung36.bits = 0xabcdeabcdULL;

  const esp32irpk::IRProtocolSpec tx_samsung36_specs[] = {esp32irpk::specs::SAMSUNG36};
  const esp32irpk::IRProtocolSpec samsung_rank_specs[] = {
      esp32irpk::specs::SAMSUNG32,
      esp32irpk::specs::SAMSUNG36,
  };

  uint16_t raw_ticks[128]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = raw_ticks;
  raw.capacity = sizeof(raw_ticks) / sizeof(raw_ticks[0]);

  EXPECT_TRUE("ranking/samsung36-encode", esp32irpk::codec::encodeBitsToRaw(samsung36, tx_samsung36_specs, 1, raw));
  uint16_t noisy_samsung36_ticks[128]{};
  esp32irpk::IRRawTickView noisy_samsung36{};
  perturbRawTicks(raw, noisy_samsung36_ticks, sizeof(noisy_samsung36_ticks) / sizeof(noisy_samsung36_ticks[0]), noisy_samsung36);

  esp32irpk::IRReceiveResult<4> samsung_result{};
  EXPECT_TRUE("ranking/samsung36-noisy-decode",
              esp32irpk::codec::decodeRawToBits(noisy_samsung36, samsung_rank_specs, 2, 4, 0, samsung_result));
  EXPECT_EQ("ranking/samsung36-noisy-first", esp32irpk::IRProtocolID::SAMSUNG36, samsung_result.candidates[0].protocol_id);
  EXPECT_EQ("ranking/samsung36-noisy-bits", samsung36.bits, samsung_result.candidates[0].decoded.bits);
}

void testSpaceEncodedDecodeAllowsClippedFinalSpace()
{
  esp32irpk::frames::Sony12Frame sony12{};
  sony12.data = 0x0a90;
  esp32irpk::IRDecodedBits bits = sony12.toBits();

  uint16_t ticks[32]{};
  esp32irpk::IRRawTickBuffer raw{};
  raw.ticks = ticks;
  raw.capacity = sizeof(ticks) / sizeof(ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::SONY12};
  EXPECT_TRUE("clipped-final-space/encode", esp32irpk::codec::encodeBitsToRaw(bits, specs, 1, raw));
  EXPECT_EQ("clipped-final-space/raw-len", 26, raw.len);

  esp32irpk::IRRawTickView view{};
  view.ticks = raw.ticks;
  view.len = raw.len - 1;

  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("clipped-final-space/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("clipped-final-space/count", 1, result.count);
  EXPECT_EQ("clipped-final-space/bits", bits.bits, result.candidates[0].decoded.bits);
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
  EXPECT_TRUE("sender-carrier/reject-low", !tx.setCarrierHz(19999));
  EXPECT_TRUE("sender-carrier/reject-high", !tx.setCarrierHz(60001));
  EXPECT_TRUE("sender-carrier/set-before-begin-min", tx.setCarrierHz(20000));
  EXPECT_TRUE("sender-carrier/set-before-begin-max", tx.setCarrierHz(60000));
  EXPECT_TRUE("sender-carrier/clear-before-begin", tx.clearCarrierHz());
  EXPECT_TRUE("sender-carrier/set-zero-before-begin", tx.setCarrierHz(0));
  EXPECT_TRUE("sender-encode/begin", tx.begin());
  EXPECT_TRUE("sender-encode/after-begin", tx.encode(bits, raw));
  EXPECT_EQ("sender-encode/raw-len", 67, raw.len);
  EXPECT_TRUE("sender-carrier/set-after-begin", tx.setCarrierHz(40000));
  EXPECT_TRUE("sender-carrier/clear-after-begin", tx.clearCarrierHz());
  EXPECT_TRUE("sender-carrier/set-zero-after-begin", tx.setCarrierHz(0));
  EXPECT_TRUE("sender-carrier/reject-low-after-begin", !tx.setCarrierHz(19999));
  EXPECT_TRUE("sender-carrier/reject-high-after-begin", !tx.setCarrierHz(60001));
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

void testNecLikeScoresOutOfToleranceZeroSpaces()
{
  const uint16_t arduino_irremote_nec_ticks[] = {
      892, 471, 51, 189, 53, 189, 51, 78, 51, 78, 53, 189, 51, 78,
      51, 189, 53, 189, 53, 76, 53, 76, 53, 189, 51, 191, 51, 78,
      51, 189, 53, 76, 53, 78, 51, 78, 51, 78, 51, 78, 51, 78,
      51, 78, 54, 75, 53, 76, 53, 76, 53, 190, 53, 189, 50, 189,
      53, 189, 54, 189, 50, 189, 53, 189, 54, 189, 50};

  esp32irpk::IRRawTickView view{};
  view.ticks = arduino_irremote_nec_ticks;
  view.len = sizeof(arduino_irremote_nec_ticks) / sizeof(arduino_irremote_nec_ticks[0]);

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("nec-relaxed-zero/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("nec-relaxed-zero/candidates", 1, result.count);
  EXPECT_EQ("nec-relaxed-zero/protocol", esp32irpk::IRProtocolID::NEC, result.candidates[0].protocol_id);
  EXPECT_EQ("nec-relaxed-zero/bits", 0xff002cd3ULL, result.candidates[0].decoded.bits);
  EXPECT_EQ("nec-relaxed-zero/length", 32, result.candidates[0].decoded.bit_length);
  EXPECT_TRUE("nec-relaxed-zero/score-penalized", result.candidates[0].score < 900);
  EXPECT_TRUE("nec-relaxed-zero/score-accepted", result.candidates[0].score >= 0);
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

void testAeha48FixtureDecode()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::aeha48_123456789abc_raw_ticks;
  view.len = test_fixtures::aeha48_123456789abc_raw_len;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::AEHA};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("aeha48-fixture/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("aeha48-fixture/candidates", 1, result.count);
  EXPECT_EQ("aeha48-fixture/protocol", esp32irpk::IRProtocolID::AEHA, result.candidates[0].protocol_id);
  EXPECT_EQ("aeha48-fixture/bits", test_fixtures::aeha48_123456789abc_bits, result.candidates[0].decoded.bits);
  EXPECT_EQ("aeha48-fixture/length", test_fixtures::aeha48_123456789abc_bit_length, result.candidates[0].decoded.bit_length);

  esp32irpk::frames::AEHAFrame frame =
      esp32irpk::frames::AEHAFrame::fromBits(result.candidates[0].decoded);
  EXPECT_EQ("aeha48-fixture/data", 0x123456789abcULL, frame.data);
  EXPECT_EQ("aeha48-fixture/frame-length", 48, frame.bit_length);
}

void testJvcFixtureDecode()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::jvc_c0de_raw_ticks;
  view.len = test_fixtures::jvc_c0de_raw_len;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::JVC};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("jvc-fixture/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("jvc-fixture/candidates", 1, result.count);
  EXPECT_EQ("jvc-fixture/protocol", esp32irpk::IRProtocolID::JVC, result.candidates[0].protocol_id);
  EXPECT_EQ("jvc-fixture/bits", test_fixtures::jvc_c0de_bits, result.candidates[0].decoded.bits);
  EXPECT_EQ("jvc-fixture/length", test_fixtures::jvc_c0de_bit_length, result.candidates[0].decoded.bit_length);

  esp32irpk::frames::JVCFrame frame =
      esp32irpk::frames::JVCFrame::fromBits(result.candidates[0].decoded);
  EXPECT_EQ("jvc-fixture/address", 0xdeu, frame.address);
  EXPECT_EQ("jvc-fixture/command", 0xc0u, frame.command);
}

void testRc5FixtureDecode()
{
  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::RC5};
  {
    esp32irpk::IRRawTickView view{};
    view.ticks = test_fixtures::rc5_3fff_raw_ticks;
    view.len = test_fixtures::rc5_3fff_raw_len;

    esp32irpk::IRReceiveResult<4> result{};
    EXPECT_TRUE("rc5-fixture/decode-3fff", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
    EXPECT_EQ("rc5-fixture/candidates-3fff", 1, result.count);
    EXPECT_EQ("rc5-fixture/protocol-3fff", esp32irpk::IRProtocolID::RC5, result.candidates[0].protocol_id);
    EXPECT_EQ("rc5-fixture/bits-3fff", test_fixtures::rc5_3fff_bits, result.candidates[0].decoded.bits);
    EXPECT_EQ("rc5-fixture/length-3fff", test_fixtures::rc5_3fff_bit_length, result.candidates[0].decoded.bit_length);

    esp32irpk::frames::RC5Frame frame =
        esp32irpk::frames::RC5Frame::fromBits(result.candidates[0].decoded);
    EXPECT_EQ("rc5-fixture/data-3fff", 0x3fffu, frame.data);
  }
  {
    esp32irpk::IRRawTickView view{};
    view.ticks = test_fixtures::rc5_300f_raw_ticks;
    view.len = test_fixtures::rc5_300f_raw_len;

    esp32irpk::IRReceiveResult<4> result{};
    EXPECT_TRUE("rc5-fixture/decode-300f", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
    EXPECT_EQ("rc5-fixture/candidates-300f", 1, result.count);
    EXPECT_EQ("rc5-fixture/protocol-300f", esp32irpk::IRProtocolID::RC5, result.candidates[0].protocol_id);
    EXPECT_EQ("rc5-fixture/bits-300f", test_fixtures::rc5_300f_bits, result.candidates[0].decoded.bits);
    EXPECT_EQ("rc5-fixture/length-300f", test_fixtures::rc5_300f_bit_length, result.candidates[0].decoded.bit_length);

    esp32irpk::frames::RC5Frame frame =
        esp32irpk::frames::RC5Frame::fromBits(result.candidates[0].decoded);
    EXPECT_EQ("rc5-fixture/data-300f", 0x300fu, frame.data);
  }
}

void testRc6M0FixtureDecode()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::rc6_m0_11234_raw_ticks;
  view.len = test_fixtures::rc6_m0_11234_raw_len;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::RC6_M0_16};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("rc6-m0-fixture/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("rc6-m0-fixture/candidates", 1, result.count);
  EXPECT_EQ("rc6-m0-fixture/protocol", esp32irpk::IRProtocolID::RC6_M0_16, result.candidates[0].protocol_id);
  EXPECT_EQ("rc6-m0-fixture/bits", test_fixtures::rc6_m0_11234_bits, result.candidates[0].decoded.bits);
  EXPECT_EQ("rc6-m0-fixture/length", test_fixtures::rc6_m0_11234_bit_length, result.candidates[0].decoded.bit_length);

  esp32irpk::frames::RC6M0Frame frame =
      esp32irpk::frames::RC6M0Frame::fromBits(result.candidates[0].decoded);
  EXPECT_EQ("rc6-m0-fixture/data", 0x111234u, frame.data);
}

void testRc6M6FixtureDecode()
{
  esp32irpk::IRRawTickView view{};
  view.ticks = test_fixtures::rc6_m6_689abcdef_raw_ticks;
  view.len = test_fixtures::rc6_m6_689abcdef_raw_len;

  const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::RC6_M6_32};
  esp32irpk::IRReceiveResult<4> result{};
  EXPECT_TRUE("rc6-m6-fixture/decode", esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result));
  EXPECT_EQ("rc6-m6-fixture/candidates", 1, result.count);
  EXPECT_EQ("rc6-m6-fixture/protocol", esp32irpk::IRProtocolID::RC6_M6_32, result.candidates[0].protocol_id);
  EXPECT_EQ("rc6-m6-fixture/bits", test_fixtures::rc6_m6_689abcdef_bits, result.candidates[0].decoded.bits);
  EXPECT_EQ("rc6-m6-fixture/length", test_fixtures::rc6_m6_689abcdef_bit_length, result.candidates[0].decoded.bit_length);

  esp32irpk::frames::RC6M6Frame frame =
      esp32irpk::frames::RC6M6Frame::fromBits(result.candidates[0].decoded);
  EXPECT_EQ("rc6-m6-fixture/data", 0xe89abcdefULL, frame.data);
}

void testBiphaseDecodeAllowsClippedFinalHalf()
{
  // Encode a frame, then drop the final tick to simulate the last half-bit being
  // clipped/absorbed into idle. The decoder's trailing-half padding should still
  // recover the value.
  uint16_t rc5_buf[64]{};
  esp32irpk::IRRawTickBuffer rc5_raw{};
  rc5_raw.ticks = rc5_buf;
  rc5_raw.capacity = 64;
  esp32irpk::IRDecodedBits rc5_bits{};
  rc5_bits.protocol_id = esp32irpk::IRProtocolID::RC5;
  rc5_bits.frame_type = esp32irpk::IRFrameType::NORMAL;
  rc5_bits.bit_length = 14;
  rc5_bits.bits = 0x300fULL;
  const esp32irpk::IRProtocolSpec rc5_specs[] = {esp32irpk::specs::RC5};
  EXPECT_TRUE("rc5-clipped-final-half/encode",
              esp32irpk::codec::encodeBitsToRaw(rc5_bits, rc5_specs, 1, rc5_raw));
  esp32irpk::IRRawTickView rc5_view{};
  rc5_view.ticks = rc5_raw.ticks;
  rc5_view.len = rc5_raw.len - 1; // clip the final half

  esp32irpk::IRReceiveResult<4> rc5_result{};
  EXPECT_TRUE("rc5-clipped-final-half/decode",
              esp32irpk::codec::decodeRawToBits(rc5_view, rc5_specs, 1, 4, 0, rc5_result));
  EXPECT_EQ("rc5-clipped-final-half/protocol", esp32irpk::IRProtocolID::RC5, rc5_result.candidates[0].protocol_id);
  EXPECT_EQ("rc5-clipped-final-half/bits", 0x300fULL, rc5_result.candidates[0].decoded.bits);

  uint16_t rc6_buf[128]{};
  esp32irpk::IRRawTickBuffer rc6_raw{};
  rc6_raw.ticks = rc6_buf;
  rc6_raw.capacity = 128;
  esp32irpk::IRDecodedBits rc6_bits{};
  rc6_bits.protocol_id = esp32irpk::IRProtocolID::RC6_M6_32;
  rc6_bits.frame_type = esp32irpk::IRFrameType::NORMAL;
  rc6_bits.bit_length = 36;
  rc6_bits.bits = 0xe89abcdefULL;
  const esp32irpk::IRProtocolSpec rc6_specs[] = {esp32irpk::specs::RC6_M0_16, esp32irpk::specs::RC6_M6_32};
  EXPECT_TRUE("rc6-m6-clipped-final-half/encode",
              esp32irpk::codec::encodeBitsToRaw(rc6_bits, rc6_specs, 2, rc6_raw));
  esp32irpk::IRRawTickView rc6_view{};
  rc6_view.ticks = rc6_raw.ticks;
  rc6_view.len = rc6_raw.len - 1; // clip the final half

  esp32irpk::IRReceiveResult<4> rc6_result{};
  EXPECT_TRUE("rc6-m6-clipped-final-half/decode",
              esp32irpk::codec::decodeRawToBits(rc6_view, rc6_specs, 2, 4, 0, rc6_result));
  EXPECT_EQ("rc6-m6-clipped-final-half/protocol", esp32irpk::IRProtocolID::RC6_M6_32, rc6_result.candidates[0].protocol_id);
  EXPECT_EQ("rc6-m6-clipped-final-half/bits", 0xe89abcdefULL, rc6_result.candidates[0].decoded.bits);
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
  // MSB-first layout: address in the top 16 bits, command in the low 20 bits.
  EXPECT_EQ("frame/samsung36-bits", 0xabcdabcdeULL, samsung36_bits.bits);
  esp32irpk::frames::Samsung36Frame samsung36_roundtrip =
      esp32irpk::frames::Samsung36Frame::fromBits(samsung36_bits);
  EXPECT_EQ("frame/samsung36-address", 0xabcdu, samsung36_roundtrip.address);
  EXPECT_EQ("frame/samsung36-command", 0xabcdeu, samsung36_roundtrip.command);

  esp32irpk::frames::JVCFrame jvc{};
  jvc.address = 0xde;
  jvc.command = 0xc0;
  esp32irpk::IRDecodedBits jvc_bits = jvc.toBits();
  EXPECT_EQ("frame/jvc-length", 16, jvc_bits.bit_length);
  EXPECT_EQ("frame/jvc-mask", 0xc0deULL, jvc_bits.bits);
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);

  testNecEncodeDecodeRoundtrip();
  testProtocolCarrierPreferences();
  testNecRejectsUndersizedBuffer();
  testVariableLengthEncodeDecode();
  testEncodeRejectsInvalidInputs();
  testMsbFirstVariableLengthDecode();
  testToleranceBoundaries();
  testSpaceEncodedRelaxedCandidateScoring();
  testSpaceEncodedAmbiguousSpaceRejects();
  testGeneratedProtocolRoundtrips();
  testBiphaseEncodeDecodeRoundtrips();
  testSimilarSpaceEncodedRankingWithNoisyTiming();
  testNecLikeRankingWithNoisyTiming();
  testSonyFamilyRankingWithNoisyTiming();
  testLengthVariantRankingWithNoisyTiming();
  testSpaceEncodedDecodeAllowsClippedFinalSpace();
  testNecRepeatDecode();
  testNecRepeatEncode();
  testSenderEncodeLifecycle();
  testNecFixtureDecode();
  testNecLikeScoresOutOfToleranceZeroSpaces();
  testSony12FixtureDecode();
  testSamsung32FixtureDecode();
  testAeha48FixtureDecode();
  testJvcFixtureDecode();
  testRc5FixtureDecode();
  testRc6M0FixtureDecode();
  testRc6M6FixtureDecode();
  testBiphaseDecodeAllowsClippedFinalHalf();
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
