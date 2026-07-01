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
  EXPECT_TRUE("receiver-config/max-rx-symbols", rx.setMaxRxSymbols(1024));
  EXPECT_TRUE("receiver-config/max-rx-symbols-zero", !rx.setMaxRxSymbols(0));
  EXPECT_TRUE("receiver-config/begin", rx.begin());
  EXPECT_TRUE("receiver-config/set-pin-after-begin", !rx.setPin(6));
  EXPECT_TRUE("receiver-config/set-inverted-after-begin", !rx.setInverted(false));
  EXPECT_TRUE("receiver-config/candidates-after-begin", !rx.setDecodeCandidates(1));
  EXPECT_TRUE("receiver-config/idle-after-begin", !rx.setIdleThresholdUs(30000));
  EXPECT_TRUE("receiver-config/score-after-begin", !rx.setScoreThreshold(0));
  EXPECT_TRUE("receiver-config/max-rx-symbols-after-begin", !rx.setMaxRxSymbols(2048));
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

// Generic AC pulse-distance codec (esp32irpk::ac::AcCodec): vendor-independent
// tick<->byte roundtrip, including multi-frame concatenation via `pos`.
void testAcCodecRoundtrip()
{
  esp32irpk::ac::AcTiming t{};
  t.header_mark_us = 3500;
  t.header_space_us = 1750;
  t.bit_mark_us = 435;
  t.zero_space_us = 435;
  t.one_space_us = 1300;
  t.trailer_mark_us = 435;
  t.frame_gap_us = 10000;
  t.tol_pct = 30;
  t.lsb_first = true;

  const uint8_t f1[] = {0x02, 0x20, 0xE0};        // 24 bits
  const uint8_t f2[] = {0xA5, 0x3C, 0xFF, 0x00, 0x12}; // 40 bits

  uint16_t ticks[256];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("ac-codec/encode-f1", esp32irpk::ac::bytesFrameToRaw(f1, 24, t, buf));
  EXPECT_TRUE("ac-codec/encode-f2", esp32irpk::ac::bytesFrameToRaw(f2, 40, t, buf));

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  size_t pos = 0;
  uint8_t d1[8] = {};
  size_t bits1 = esp32irpk::ac::rawFrameToBytes(view, pos, t, d1, sizeof(d1));
  EXPECT_EQ("ac-codec/decode-f1-bits", 24, bits1);
  EXPECT_EQ("ac-codec/decode-f1-b0", 0x02, d1[0]);
  EXPECT_EQ("ac-codec/decode-f1-b1", 0x20, d1[1]);
  EXPECT_EQ("ac-codec/decode-f1-b2", 0xE0, d1[2]);

  uint8_t d2[8] = {};
  size_t bits2 = esp32irpk::ac::rawFrameToBytes(view, pos, t, d2, sizeof(d2));
  EXPECT_EQ("ac-codec/decode-f2-bits", 40, bits2);
  EXPECT_EQ("ac-codec/decode-f2-b0", 0xA5, d2[0]);
  EXPECT_EQ("ac-codec/decode-f2-b1", 0x3C, d2[1]);
  EXPECT_EQ("ac-codec/decode-f2-b2", 0xFF, d2[2]);
  EXPECT_EQ("ac-codec/decode-f2-b3", 0x00, d2[3]);
  EXPECT_EQ("ac-codec/decode-f2-b4", 0x12, d2[4]);
  EXPECT_EQ("ac-codec/decode-consumed-all", view.len, pos);

  // A corrupted header must be rejected.
  uint16_t bad_ticks[64];
  esp32irpk::IRRawTickBuffer bad{bad_ticks, sizeof(bad_ticks) / sizeof(bad_ticks[0]), 0};
  esp32irpk::ac::bytesFrameToRaw(f1, 24, t, bad);
  bad.ticks[0] = 10; // wipe the header mark
  esp32irpk::IRRawTickView bad_view{bad.ticks, bad.len};
  size_t bad_pos = 0;
  uint8_t bd[8] = {};
  EXPECT_EQ("ac-codec/reject-bad-header", 0,
            esp32irpk::ac::rawFrameToBytes(bad_view, bad_pos, t, bd, sizeof(bd)));
}

// Panasonic AC: build a frame, render it, decode it back, and confirm the
// logical fields and checksum survive the roundtrip (provisional field map; the
// self-roundtrip is internally consistent regardless of the real-device map).
void testPanasonicAcRoundtrip()
{
  esp32irpk::ac::Panasonic::Frame f{};
  f.setPower(true);
  f.setMode(esp32irpk::ac::Panasonic::Mode::COOL);
  f.setTemperatureC(26);
  f.setFan(esp32irpk::ac::Panasonic::Fan::AUTO);

  // In-memory accessor roundtrip.
  EXPECT_TRUE("panasonic/power-get", f.power());
  EXPECT_TRUE("panasonic/mode-get", f.mode() == esp32irpk::ac::Panasonic::Mode::COOL);
  EXPECT_EQ("panasonic/temp-get", 26, f.temperatureC());
  EXPECT_TRUE("panasonic/fan-get", f.fan() == esp32irpk::ac::Panasonic::Fan::AUTO);

  uint16_t ticks[esp32irpk::ac::Panasonic::Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("panasonic/encode", f.toRaw(buf));
  EXPECT_TRUE("panasonic/encode-nonempty", buf.len > 0);

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  esp32irpk::ac::Panasonic::Frame g{};
  EXPECT_TRUE("panasonic/decode", esp32irpk::ac::Panasonic::Frame::fromRaw(view, g));
  EXPECT_TRUE("panasonic/decode-checksum", g.checksum_ok);
  EXPECT_EQ("panasonic/decode-bytelen", esp32irpk::ac::Panasonic::Frame::kBytes, g.byte_length);
  EXPECT_TRUE("panasonic/decode-power", g.power());
  EXPECT_TRUE("panasonic/decode-mode", g.mode() == esp32irpk::ac::Panasonic::Mode::COOL);
  EXPECT_EQ("panasonic/decode-temp", 26, g.temperatureC());
  EXPECT_TRUE("panasonic/decode-fan", g.fan() == esp32irpk::ac::Panasonic::Fan::AUTO);

  // The encoded frame must be the full canonical Panasonic state a real remote
  // sends, including the fixed feature bytes ([15]=0x80, [19]=0x0E, [20]=0xE0,
  // [23]=0x81). Verified byte-for-byte against IRremoteESP8266's IRPanasonicAc
  // for power=on / cool / 26C / fan=auto (studies/compat_matrix_ac).
  static const uint8_t kCanonicalCoolAuto26[esp32irpk::ac::Panasonic::Frame::kBytes] = {
      0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06,
      0x02, 0x20, 0xE0, 0x04, 0x00, 0x31, 0x34, 0x80, 0xA0, 0x00,
      0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0xFA};
  bool canonical_match = true;
  for (size_t i = 0; i < esp32irpk::ac::Panasonic::Frame::kBytes; ++i)
    if (g.bytes[i] != kCanonicalCoolAuto26[i])
      canonical_match = false;
  EXPECT_TRUE("panasonic/canonical-bytes", canonical_match);

  // CKP is not implemented; it must refuse to encode rather than silently emit a
  // frame with the wrong semantics. The supported models (JKE/DKE/NKE/LKE/RKR)
  // are covered in testPanasonicAcModels().
  uint16_t m_ticks[esp32irpk::ac::Panasonic::Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer m_buf{m_ticks, sizeof(m_ticks) / sizeof(m_ticks[0]), 0};
  esp32irpk::ac::Panasonic::Frame ckp{};
  ckp.model = esp32irpk::ac::Panasonic::Model::CKP;
  EXPECT_TRUE("panasonic/encode-rejects-ckp", !ckp.toRaw(m_buf));

  // A non-Panasonic waveform (NEC) must be rejected.
  esp32irpk::IRRawTickView nec{};
  nec.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  nec.len = test_fixtures::nec_normal_00ff_34_raw_len;
  esp32irpk::ac::Panasonic::Frame nf{};
  EXPECT_TRUE("panasonic/reject-nec", !esp32irpk::ac::Panasonic::Frame::fromRaw(nec, nf));
}

// 0.5C half-degree (byte22 bit7) and louver (low nibble of the fan byte) — the
// two newly mapped Panasonic fields. Offsets are validated against a real remote
// capture (part ACXA75C15870, cool / 22.5C / louver P3 / powerful) so they are
// device-confirmed, not just self-consistent.
void testPanasonicAcHalfDegreeAndLouver()
{
  using esp32irpk::ac::Panasonic::Fan;
  using esp32irpk::ac::Panasonic::Frame;
  using esp32irpk::ac::Panasonic::Louver;
  using esp32irpk::ac::Panasonic::Mode;

  // Accessor + encode/decode roundtrip. setTemperatureC takes 0.5C steps in one
  // call (no separate half-degree setter).
  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setTemperatureC(22.5f);
  f.setFan(Fan::AUTO);
  f.setLouver(Louver::P3);
  EXPECT_EQ("panasonic/half-temp-get", 22.5f, f.temperatureC()); // get is float, symmetric with set
  EXPECT_TRUE("panasonic/half-get", f.halfDegree());
  EXPECT_TRUE("panasonic/louver-get", f.louver() == Louver::P3);
  EXPECT_EQ("panasonic/half-bit", 0x80, f.bytes[22] & 0x80);
  EXPECT_EQ("panasonic/half-temp-byte", 0x2C, f.bytes[14]); // 22<<1, the 0.5 is NOT in this byte
  EXPECT_EQ("panasonic/louver-nibble", 0x03, f.bytes[16] & 0x0F);
  EXPECT_TRUE("panasonic/louver-keeps-fan", f.fan() == Fan::AUTO); // low nibble didn't disturb fan

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("panasonic/half-encode", f.toRaw(buf));
  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("panasonic/half-decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("panasonic/half-decode-checksum", g.checksum_ok);
  EXPECT_EQ("panasonic/half-decode-temp", 22.5f, g.temperatureC());
  EXPECT_TRUE("panasonic/half-decode-half", g.halfDegree());
  EXPECT_TRUE("panasonic/half-decode-louver", g.louver() == Louver::P3);

  // A whole-degree setpoint clears the half bit; louver AUTO is the 0xF nibble.
  f.setTemperatureC(22.0f);
  EXPECT_TRUE("panasonic/half-clear", !f.halfDegree());
  EXPECT_EQ("panasonic/whole-temp-get", 22.0f, f.temperatureC());
  f.setLouver(Louver::AUTO);
  EXPECT_EQ("panasonic/louver-auto-nibble", 0x0F, f.bytes[16] & 0x0F);

  // Device ground truth: a real ACXA75C15870 capture of cool / 22.5C / louver P3
  // / powerful. Validate the field offsets directly on the bytes (independent of
  // our encoder).
  Frame d{};
  static const uint8_t kCool225Louver3[Frame::kBytes] = {
      0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06,
      0x02, 0x20, 0xE0, 0x04, 0x00, 0x31, 0x2C, 0x80, 0xA3, 0x00,
      0x00, 0x06, 0x60, 0x41, 0x80, 0x80, 0x00, 0x06, 0x33};
  for (size_t i = 0; i < Frame::kBytes; ++i)
    d.bytes[i] = kCool225Louver3[i];
  EXPECT_TRUE("panasonic/real-power", d.power());
  EXPECT_TRUE("panasonic/real-mode", d.mode() == Mode::COOL);
  EXPECT_EQ("panasonic/real-temp", 22.5f, d.temperatureC()); // device capture is 22.5C
  EXPECT_TRUE("panasonic/real-half", d.halfDegree());
  EXPECT_TRUE("panasonic/real-louver", d.louver() == Louver::P3);
  EXPECT_TRUE("panasonic/real-fan", d.fan() == Fan::POWERFUL); // capture has the powerful flag
}

// Fan selector: speeds (MIN..MAX, fan-byte high nibble) and the quiet/powerful
// comfort modes (byte21 flags, fan nibble stays auto). They are one selector —
// setting any clears the others. Offsets validated against real captures (cool +
// powerful in the test above; heat 23.5 + louver2 + quiet here).
void testPanasonicAcFanModes()
{
  using esp32irpk::ac::Panasonic::Fan;
  using esp32irpk::ac::Panasonic::Frame;
  using esp32irpk::ac::Panasonic::Mode;

  Frame f{};
  f.setMode(Mode::COOL);

  // Quiet: fan nibble auto + byte21 bit5, powerful clear.
  f.setFan(Fan::QUIET);
  EXPECT_TRUE("panasonic/quiet-get", f.fan() == Fan::QUIET);
  EXPECT_EQ("panasonic/quiet-nibble", 0xA, (f.bytes[16] >> 4) & 0x0F);
  EXPECT_EQ("panasonic/quiet-bit", 0x20, f.bytes[21] & 0x20);
  EXPECT_EQ("panasonic/quiet-no-powerful", 0x00, f.bytes[21] & 0x01);

  // Powerful is mutually exclusive with quiet.
  f.setFan(Fan::POWERFUL);
  EXPECT_TRUE("panasonic/powerful-get", f.fan() == Fan::POWERFUL);
  EXPECT_EQ("panasonic/powerful-bit", 0x01, f.bytes[21] & 0x01);
  EXPECT_EQ("panasonic/powerful-no-quiet", 0x00, f.bytes[21] & 0x20);

  // Selecting a speed clears the comfort flags and sets the nibble.
  f.setFan(Fan::MAX_SPEED);
  EXPECT_TRUE("panasonic/maxspeed-get", f.fan() == Fan::MAX_SPEED);
  EXPECT_EQ("panasonic/maxspeed-nibble", 0x7, (f.bytes[16] >> 4) & 0x0F);
  EXPECT_EQ("panasonic/maxspeed-no-flags", 0x00, f.bytes[21] & 0x21);

  // Encode/decode roundtrip of a comfort mode.
  f.setPower(true);
  f.setTemperatureC(24.0f);
  f.setFan(Fan::QUIET);
  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("panasonic/quiet-encode", f.toRaw(buf));
  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("panasonic/quiet-decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("panasonic/quiet-decode-checksum", g.checksum_ok);
  EXPECT_TRUE("panasonic/quiet-decode-fan", g.fan() == Fan::QUIET);

  // Device ground truth: heat / 23.5C / louver P2 / quiet (byte21=0x20).
  Frame q{};
  static const uint8_t kHeat235Quiet[Frame::kBytes] = {
      0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06,
      0x02, 0x20, 0xE0, 0x04, 0x00, 0x41, 0x2E, 0x80, 0xA2, 0x00,
      0x00, 0x06, 0x60, 0x20, 0x80, 0x80, 0x00, 0x06, 0x23};
  for (size_t i = 0; i < Frame::kBytes; ++i)
    q.bytes[i] = kHeat235Quiet[i];
  EXPECT_TRUE("panasonic/real-quiet", q.fan() == Fan::QUIET);
  EXPECT_TRUE("panasonic/real-quiet-mode", q.mode() == Mode::HEAT);
}

// Model coverage: JKE/DKE/NKE/LKE/RKR share the power/mode/temperature/fan field
// map and differ only in fixed marker bytes (13/17/23/25). Each model must
// encode, decode back to the same logical fields, self-identify on decode, and
// carry the documented marker bytes. The marker values mirror the documented
// Panasonic-AC format; the independent byte-level ground truth is the
// compat_matrix_ac study against IRremoteESP8266 (per-model setModel/getModel).
void testPanasonicAcModels()
{
  using esp32irpk::ac::Panasonic::Fan;
  using esp32irpk::ac::Panasonic::Frame;
  using esp32irpk::ac::Panasonic::Mode;
  using esp32irpk::ac::Panasonic::Model;

  struct ModelCase
  {
    const char *name;
    Model model;
    uint8_t b13_marker; // expected byte13 marker bits (1..3)
    uint8_t b17;
    uint8_t b23;
    uint8_t b25;
  };
  static const ModelCase cases[] = {
      {"jke", Model::JKE, 0x00, 0x00, 0x81, 0x00},
      {"dke", Model::DKE, 0x00, 0x06, 0x01, 0x06},
      {"nke", Model::NKE, 0x00, 0x06, 0x81, 0x00},
      {"lke", Model::LKE, 0x02, 0x06, 0x81, 0x00},
      {"rkr", Model::RKR, 0x08, 0x00, 0x89, 0x00},
  };

  for (const ModelCase &mc : cases)
  {
    char lbl[64];

    Frame f{};
    f.setPower(true);
    f.setMode(Mode::COOL);
    f.setTemperatureC(26);
    f.setFan(Fan::AUTO);
    f.model = mc.model;

    uint16_t ticks[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-encode", mc.name);
    EXPECT_TRUE(lbl, f.toRaw(buf));

    esp32irpk::IRRawTickView view{buf.ticks, buf.len};
    Frame g{};
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-decode", mc.name);
    EXPECT_TRUE(lbl, Frame::fromRaw(view, g));
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-checksum", mc.name);
    EXPECT_TRUE(lbl, g.checksum_ok);
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-detect", mc.name);
    EXPECT_TRUE(lbl, g.model == mc.model);

    // Logical fields survive regardless of model.
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-power", mc.name);
    EXPECT_TRUE(lbl, g.power());
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-mode", mc.name);
    EXPECT_TRUE(lbl, g.mode() == Mode::COOL);
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-temp", mc.name);
    EXPECT_EQ(lbl, 26, g.temperatureC());
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-fan", mc.name);
    EXPECT_TRUE(lbl, g.fan() == Fan::AUTO);

    // Marker bytes match the documented per-model layout.
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-b13", mc.name);
    EXPECT_EQ(lbl, mc.b13_marker, static_cast<uint8_t>(g.bytes[13] & 0x0Eu));
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-b17", mc.name);
    EXPECT_EQ(lbl, mc.b17, g.bytes[17]);
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-b23", mc.name);
    EXPECT_EQ(lbl, mc.b23, g.bytes[23]);
    snprintf(lbl, sizeof(lbl), "panasonic/model-%s-b25", mc.name);
    EXPECT_EQ(lbl, mc.b25, g.bytes[25]);
  }
}

// Every state the compat_matrix_ac harness exercises must encode (setters ->
// toRaw -> fromRaw) to the exact canonical 27 bytes IRremoteESP8266 produces.
// Guards the full encoder field map + fixed feature bytes across modes/fans.
void testPanasonicAcCanonicalStates()
{
  using esp32irpk::ac::Panasonic::Fan;
  using esp32irpk::ac::Panasonic::Frame;
  using esp32irpk::ac::Panasonic::Mode;

  struct CanonCase
  {
    const char *name;
    bool power;
    Mode mode;
    uint8_t temp;
    Fan fan;
    uint8_t bytes[Frame::kBytes];
  };

  static const CanonCase cases[] = {
      {"cool/auto/26", true, Mode::COOL, 26, Fan::AUTO,
       {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02, 0x20, 0xE0, 0x04, 0x00, 0x31,
        0x34, 0x80, 0xA0, 0x00, 0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0xFA}},
      {"heat/high/22", true, Mode::HEAT, 22, Fan::HIGH_SPEED,
       {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02, 0x20, 0xE0, 0x04, 0x00, 0x41,
        0x2C, 0x80, 0x60, 0x00, 0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0xC2}},
      {"dry/low/24", true, Mode::DRY, 24, Fan::LOW_SPEED,
       {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02, 0x20, 0xE0, 0x04, 0x00, 0x21,
        0x30, 0x80, 0x40, 0x00, 0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0x86}},
      {"cool/med/18", true, Mode::COOL, 18, Fan::MED_SPEED,
       {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02, 0x20, 0xE0, 0x04, 0x00, 0x31,
        0x24, 0x80, 0x50, 0x00, 0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0x9A}},
      {"auto/auto/25/off", false, Mode::AUTO, 25, Fan::AUTO,
       {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02, 0x20, 0xE0, 0x04, 0x00, 0x00,
        0x32, 0x80, 0xA0, 0x00, 0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0xC7}},
  };

  for (const auto &c : cases)
  {
    Frame f{};
    f.setPower(c.power);
    f.setMode(c.mode);
    f.setTemperatureC(c.temp);
    f.setFan(c.fan);

    uint16_t ticks[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
    EXPECT_TRUE("panasonic/canon-encode", f.toRaw(buf));

    esp32irpk::IRRawTickView view{buf.ticks, buf.len};
    Frame g{};
    EXPECT_TRUE("panasonic/canon-decode", Frame::fromRaw(view, g));
    EXPECT_EQ("panasonic/canon-bytelen", Frame::kBytes, g.byte_length);

    bool match = g.checksum_ok && g.byte_length == Frame::kBytes;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (g.bytes[i] != c.bytes[i])
        match = false;
    EXPECT_TRUE(c.name, match);
  }
}

// A real capture of HeatpumpIR's PanasonicJKEHeatpumpIR over an LEDC carrier
// (cool / 26C / fan auto / power on), recorded by our RX at 10us/tick in
// studies/compat_matrix_ac/heatpumpir_tx. HeatpumpIR's ESP32 bit-banger skews
// every space ~150us long (zero ~620us vs the 432us nominal), which a
// narrow-window decoder rejects. Decoding it confirms the nearest-threshold
// classification tolerates real-world sender skew the way a physical A/C unit
// does, and that the field map reads an independent encoder's frame correctly.
void testPanasonicAcDecodesSkewedTiming()
{
  using esp32irpk::ac::Panasonic::Fan;
  using esp32irpk::ac::Panasonic::Frame;
  using esp32irpk::ac::Panasonic::Mode;

  static const uint16_t ticks[] = {
      346, 208, 43, 60, 41, 150, 40, 63, 40, 62, 41, 63, 40, 62, 41, 62, 40, 62, 41, 62,
      41, 62, 40, 62, 41, 62, 40, 62, 43, 148, 43, 60, 40, 62, 41, 62, 41, 62, 40, 62,
      41, 62, 40, 62, 41, 150, 41, 149, 41, 150, 43, 60, 41, 62, 40, 150, 41, 62, 43, 60,
      40, 62, 40, 62, 41, 62, 40, 63, 41, 62, 40, 62, 41, 62, 40, 62, 40, 63, 40, 62, 41,
      62, 40, 63, 40, 62, 41, 62, 41, 62, 41, 63, 40, 62, 41, 62, 40, 63, 40, 62, 41, 62,
      41, 62, 40, 62, 41, 62, 40, 62, 41, 62, 43, 60, 40, 63, 40, 150, 41, 150, 40, 63,
      40, 62, 40, 62, 41, 62, 40, 63, 40, 1015, 349, 195, 41, 62, 40, 150, 43, 60, 40, 62,
      41, 62, 40, 62, 41, 62, 40, 62, 41, 62, 40, 62, 43, 60, 40, 62, 41, 62, 40, 151, 40,
      62, 41, 62, 40, 62, 41, 62, 41, 62, 41, 63, 40, 62, 40, 151, 40, 151, 43, 148, 40,
      62, 40, 62, 41, 150, 41, 61, 43, 60, 40, 62, 40, 63, 40, 62, 41, 62, 40, 62, 41, 62,
      40, 62, 41, 63, 40, 62, 41, 62, 41, 62, 40, 150, 41, 62, 41, 61, 41, 150, 41, 150,
      40, 150, 41, 64, 38, 62, 41, 62, 40, 63, 40, 150, 41, 62, 40, 150, 41, 150, 40, 62,
      41, 62, 40, 62, 41, 62, 41, 61, 41, 62, 40, 65, 41, 59, 40, 63, 40, 150, 41, 150, 40,
      150, 41, 150, 40, 151, 40, 62, 41, 150, 41, 61, 41, 150, 40, 62, 41, 62, 41, 61, 41,
      62, 40, 62, 41, 62, 40, 62, 41, 62, 40, 62, 41, 65, 41, 59, 41, 62, 40, 62, 41, 62,
      41, 62, 40, 62, 41, 62, 40, 150, 41, 150, 41, 150, 40, 62, 41, 62, 40, 63, 41, 62,
      41, 62, 41, 62, 40, 62, 40, 62, 41, 62, 40, 151, 40, 150, 41, 150, 41, 62, 40, 62,
      41, 62, 43, 59, 41, 63, 40, 63, 40, 62, 40, 62, 41, 62, 41, 62, 40, 62, 41, 62, 41,
      61, 41, 62, 40, 62, 40, 63, 43, 147, 43, 59, 41, 62, 43, 60, 40, 62, 41, 62, 40, 62,
      41, 150, 40, 62, 41, 62, 43, 59, 41, 62, 40, 62, 41, 62, 40, 63, 40, 62, 43, 59, 41,
      63, 40, 63, 43, 60, 43, 59, 41, 62, 40, 65, 38, 62, 40, 150, 41, 62, 40, 62, 41, 62,
      43, 148, 40, 62, 43, 60, 42, 60, 40};

  esp32irpk::IRRawTickView view{ticks, sizeof(ticks) / sizeof(ticks[0])};
  Frame g{};
  EXPECT_TRUE("panasonic/skewed-decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("panasonic/skewed-checksum", g.checksum_ok);
  EXPECT_TRUE("panasonic/skewed-power", g.power());
  EXPECT_TRUE("panasonic/skewed-mode", g.mode() == Mode::COOL);
  EXPECT_EQ("panasonic/skewed-temp", 26, g.temperatureC());
  EXPECT_TRUE("panasonic/skewed-fan", g.fan() == Fan::AUTO);
}

// Gree AC: build a frame, render the two-block burst (the second block carries
// no header), decode it back, and confirm the logical fields and Kelvinator
// block checksum survive the roundtrip. The canonical bytes are derived from
// the documented field layout + checksum algorithm; the hardware study in
// studies/compat_matrix_ac verifies them against IRremoteESP8266's IRGreeAC.
void testGreeAcRoundtrip()
{
  using esp32irpk::ac::Gree::Fan;
  using esp32irpk::ac::Gree::Frame;
  using esp32irpk::ac::Gree::Mode;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setTemperatureC(25);
  f.setFan(Fan::AUTO);

  // In-memory accessor roundtrip.
  EXPECT_TRUE("gree/power-get", f.power());
  EXPECT_TRUE("gree/mode-get", f.mode() == Mode::COOL);
  EXPECT_EQ("gree/temp-get", 25, f.temperatureC());
  EXPECT_TRUE("gree/fan-get", f.fan() == Fan::AUTO);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("gree/encode", f.toRaw(buf));
  EXPECT_TRUE("gree/encode-nonempty", buf.len > 0);

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("gree/decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("gree/decode-checksum", g.checksum_ok);
  EXPECT_EQ("gree/decode-bytelen", Frame::kBytes, g.byte_length);
  EXPECT_TRUE("gree/decode-power", g.power());
  EXPECT_TRUE("gree/decode-mode", g.mode() == Mode::COOL);
  EXPECT_EQ("gree/decode-temp", 25, g.temperatureC());
  EXPECT_TRUE("gree/decode-fan", g.fan() == Fan::AUTO);

  // Power on / cool / 25C / fan auto, derived from the documented layout and the
  // Kelvinator block checksum (byte7 high nibble).
  static const uint8_t kCanonicalCool25[Frame::kBytes] = {
      0x09, 0x09, 0x20, 0x50, 0x00, 0x20, 0x00, 0xE0};
  bool canonical_match = true;
  for (size_t i = 0; i < Frame::kBytes; ++i)
    if (g.bytes[i] != kCanonicalCool25[i])
      canonical_match = false;
  EXPECT_TRUE("gree/canonical-bytes", canonical_match);

  // Temperature clamps to the supported range.
  Frame t{};
  t.setTemperatureC(40);
  EXPECT_EQ("gree/temp-clamp-high", 30, t.temperatureC());
  t.setTemperatureC(5);
  EXPECT_EQ("gree/temp-clamp-low", 16, t.temperatureC());

  // An unimplemented model must fail to encode, not silently emit a YBOFB frame.
  Frame um{};
  um.setMode(Mode::COOL);
  um.model = esp32irpk::ac::Gree::Model::YAW1F;
  uint16_t um_ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer um_buf{um_ticks, sizeof(um_ticks) / sizeof(um_ticks[0]), 0};
  EXPECT_TRUE("gree/encode-rejects-unimpl-model", !um.toRaw(um_buf));

  // A non-Gree waveform (NEC) must be rejected.
  esp32irpk::IRRawTickView nec{};
  nec.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  nec.len = test_fixtures::nec_normal_00ff_34_raw_len;
  Frame nf{};
  EXPECT_TRUE("gree/reject-nec", !Frame::fromRaw(nec, nf));
}

// Every mode/fan/temperature combination must encode (setters -> toRaw ->
// fromRaw) back to itself with a valid checksum, exercising the full field map
// and the two-block (no-header second block) codec path.
void testGreeAcStateMatrix()
{
  using esp32irpk::ac::Gree::Fan;
  using esp32irpk::ac::Gree::Frame;
  using esp32irpk::ac::Gree::Mode;

  static const Mode modes[] = {Mode::AUTO, Mode::COOL, Mode::HEAT, Mode::DRY, Mode::FAN};
  static const Fan fans[] = {Fan::AUTO, Fan::MIN_SPEED, Fan::MED_SPEED, Fan::MAX_SPEED};

  bool all_ok = true;
  for (Mode m : modes)
    for (Fan fan : fans)
      for (uint8_t temp = 16; temp <= 30; ++temp)
        for (uint8_t power = 0; power <= 1; ++power)
        {
          Frame f{};
          f.setPower(power != 0);
          f.setMode(m);
          f.setFan(fan);
          f.setTemperatureC(temp);

          uint16_t ticks[Frame::kMaxTicks];
          esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
          if (!f.toRaw(buf))
          {
            all_ok = false;
            continue;
          }
          esp32irpk::IRRawTickView view{buf.ticks, buf.len};
          Frame g{};
          if (!Frame::fromRaw(view, g) || !g.checksum_ok ||
              g.power() != (power != 0) || g.mode() != m || g.fan() != fan ||
              g.temperatureC() != temp)
            all_ok = false;
        }
  EXPECT_TRUE("gree/state-matrix", all_ok);
}

// Mitsubishi AC: build a frame, render the burst (an 18-byte frame sent twice),
// decode the first copy back, and confirm the logical fields and sum checksum
// survive the roundtrip. The canonical bytes are derived from the documented
// field layout; the hardware study verifies them against IRMitsubishiAC.
void testMitsubishiAcRoundtrip()
{
  using esp32irpk::ac::Mitsubishi::Fan;
  using esp32irpk::ac::Mitsubishi::Frame;
  using esp32irpk::ac::Mitsubishi::Mode;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setTemperatureC(22);
  f.setFan(Fan::AUTO);

  EXPECT_TRUE("mitsubishi/power-get", f.power());
  EXPECT_TRUE("mitsubishi/mode-get", f.mode() == Mode::COOL);
  EXPECT_EQ("mitsubishi/temp-get", 22, f.temperatureC());
  EXPECT_TRUE("mitsubishi/fan-get", f.fan() == Fan::AUTO);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("mitsubishi/encode", f.toRaw(buf));
  EXPECT_TRUE("mitsubishi/encode-nonempty", buf.len > 0);

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("mitsubishi/decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("mitsubishi/decode-checksum", g.checksum_ok);
  EXPECT_EQ("mitsubishi/decode-bytelen", Frame::kBytes, g.byte_length);
  EXPECT_TRUE("mitsubishi/decode-power", g.power());
  EXPECT_TRUE("mitsubishi/decode-mode", g.mode() == Mode::COOL);
  EXPECT_EQ("mitsubishi/decode-temp", 22, g.temperatureC());
  EXPECT_TRUE("mitsubishi/decode-fan", g.fan() == Fan::AUTO);

  // Power on / cool / 22C / fan auto, derived from the documented layout and the
  // sum checksum (byte 17).
  static const uint8_t kCanonicalCool22[Frame::kBytes] = {
      0x23, 0xCB, 0x26, 0x01, 0x00, 0x20, 0x18, 0x06, 0x36,
      0xC0, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB0};
  bool canonical_match = true;
  for (size_t i = 0; i < Frame::kBytes; ++i)
    if (g.bytes[i] != kCanonicalCool22[i])
      canonical_match = false;
  EXPECT_TRUE("mitsubishi/canonical-bytes", canonical_match);

  // Temperature clamps to the supported range.
  Frame t{};
  t.setTemperatureC(40);
  EXPECT_EQ("mitsubishi/temp-clamp-high", 31, t.temperatureC());
  t.setTemperatureC(5);
  EXPECT_EQ("mitsubishi/temp-clamp-low", 16, t.temperatureC());

  // A non-Mitsubishi waveform (NEC) must be rejected.
  esp32irpk::IRRawTickView nec{};
  nec.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  nec.len = test_fixtures::nec_normal_00ff_34_raw_len;
  Frame nf{};
  EXPECT_TRUE("mitsubishi/reject-nec", !Frame::fromRaw(nec, nf));
}

// Every mode/fan/temperature/power combination must encode (setters -> toRaw ->
// fromRaw) back to itself with a valid checksum, exercising the full field map.
void testMitsubishiAcStateMatrix()
{
  using esp32irpk::ac::Mitsubishi::Fan;
  using esp32irpk::ac::Mitsubishi::Frame;
  using esp32irpk::ac::Mitsubishi::Mode;

  static const Mode modes[] = {Mode::AUTO, Mode::COOL, Mode::HEAT, Mode::DRY, Mode::FAN};
  static const Fan fans[] = {Fan::AUTO, Fan::QUIET, Fan::LOW_SPEED,
                             Fan::MED_SPEED, Fan::HIGH_SPEED, Fan::MAX_SPEED};

  bool all_ok = true;
  for (Mode m : modes)
    for (Fan fan : fans)
      for (uint8_t temp = 16; temp <= 31; ++temp)
        for (uint8_t power = 0; power <= 1; ++power)
        {
          Frame f{};
          f.setPower(power != 0);
          f.setMode(m);
          f.setFan(fan);
          f.setTemperatureC(temp);

          uint16_t ticks[Frame::kMaxTicks];
          esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
          if (!f.toRaw(buf))
          {
            all_ok = false;
            continue;
          }
          esp32irpk::IRRawTickView view{buf.ticks, buf.len};
          Frame g{};
          if (!Frame::fromRaw(view, g) || !g.checksum_ok ||
              g.power() != (power != 0) || g.mode() != m || g.fan() != fan ||
              g.temperatureC() != temp)
            all_ok = false;
        }
  EXPECT_TRUE("mitsubishi/state-matrix", all_ok);
}

// Vane (vertical), wide vane (horizontal), and the 0.5C half-degree bit survive
// a setter -> toRaw -> fromRaw roundtrip and land in the documented bits.
void testMitsubishiAcVaneAndHalfDegree()
{
  using esp32irpk::ac::Mitsubishi::Frame;
  using esp32irpk::ac::Mitsubishi::Mode;
  using esp32irpk::ac::Mitsubishi::Vane;
  using esp32irpk::ac::Mitsubishi::WideVane;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::HEAT);
  f.setTemperatureC(22.5f);
  f.setVane(Vane::P3);
  f.setWideVane(WideVane::RIGHT); // after setMode, which resets the wide vane

  EXPECT_EQ("mitsubishi/half-temp-get", 22.5f, f.temperatureC());
  EXPECT_TRUE("mitsubishi/half-flag", f.halfDegree());
  EXPECT_TRUE("mitsubishi/vane-get", f.vane() == Vane::P3);
  EXPECT_TRUE("mitsubishi/widevane-get", f.wideVane() == WideVane::RIGHT);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("mitsubishi/vane-encode", f.toRaw(buf));
  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("mitsubishi/vane-decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("mitsubishi/vane-decode-checksum", g.checksum_ok);
  EXPECT_EQ("mitsubishi/vane-decode-temp", 22.5f, g.temperatureC());
  EXPECT_TRUE("mitsubishi/vane-decode-vane", g.vane() == Vane::P3);
  EXPECT_TRUE("mitsubishi/vane-decode-widevane", g.wideVane() == WideVane::RIGHT);

  // A whole-degree setpoint clears the half-degree bit.
  f.setTemperatureC(24.0f);
  EXPECT_EQ("mitsubishi/whole-temp-get", 24.0f, f.temperatureC());
  EXPECT_TRUE("mitsubishi/whole-flag", !f.halfDegree());

  // setMode rewrites byte 8 and resets the wide vane to MIDDLE (documented).
  Frame m{};
  m.setWideVane(WideVane::RIGHT);
  m.setMode(Mode::COOL);
  EXPECT_TRUE("mitsubishi/setmode-resets-widevane", m.wideVane() == WideVane::MIDDLE);
}

// Vertical/horizontal swing survive a setter -> toRaw -> fromRaw roundtrip, and
// setSwingV keeps the byte-0 SwingAuto bit consistent with the chosen value.
void testGreeAcSwing()
{
  using esp32irpk::ac::Gree::Frame;
  using esp32irpk::ac::Gree::Mode;
  using esp32irpk::ac::Gree::SwingH;
  using esp32irpk::ac::Gree::SwingV;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setSwingV(SwingV::MIDDLE);
  f.setSwingH(SwingH::LEFT);

  EXPECT_TRUE("gree/swingv-get", f.swingV() == SwingV::MIDDLE);
  EXPECT_TRUE("gree/swingh-get", f.swingH() == SwingH::LEFT);
  // A fixed position leaves the SwingAuto bit (byte 0 bit 6) clear.
  EXPECT_TRUE("gree/swingv-fixed-autobit", (f.bytes[0] & 0x40u) == 0);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("gree/swing-encode", f.toRaw(buf));
  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("gree/swing-decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("gree/swing-decode-checksum", g.checksum_ok);
  EXPECT_TRUE("gree/swing-decode-v", g.swingV() == SwingV::MIDDLE);
  EXPECT_TRUE("gree/swing-decode-h", g.swingH() == SwingH::LEFT);

  // An *_AUTO value asserts the SwingAuto bit; a fixed position clears it again.
  f.setSwingV(SwingV::UP_AUTO);
  EXPECT_TRUE("gree/swingv-auto-get", f.swingV() == SwingV::UP_AUTO);
  EXPECT_TRUE("gree/swingv-auto-autobit", (f.bytes[0] & 0x40u) != 0);
  f.setSwingV(SwingV::DOWN);
  EXPECT_TRUE("gree/swingv-clears-autobit", (f.bytes[0] & 0x40u) == 0);
}

// Fujitsu AC: build a long frame, render it, decode it back, and confirm the
// logical fields and complement checksum survive the roundtrip. The canonical
// bytes are derived from the documented ARRAH2E field layout; the hardware study
// verifies them against IRFujitsuAC.
void testFujitsuAcRoundtrip()
{
  using esp32irpk::ac::Fujitsu::Fan;
  using esp32irpk::ac::Fujitsu::Frame;
  using esp32irpk::ac::Fujitsu::Mode;
  using esp32irpk::ac::Fujitsu::Swing;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setTemperatureC(26);
  f.setFan(Fan::AUTO);
  f.setSwing(Swing::OFF);

  EXPECT_TRUE("fujitsu/power-get", f.power());
  EXPECT_TRUE("fujitsu/mode-get", f.mode() == Mode::COOL);
  EXPECT_EQ("fujitsu/temp-get", 26, f.temperatureC());
  EXPECT_TRUE("fujitsu/fan-get", f.fan() == Fan::AUTO);
  EXPECT_TRUE("fujitsu/swing-get", f.swing() == Swing::OFF);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("fujitsu/encode", f.toRaw(buf));
  EXPECT_TRUE("fujitsu/encode-nonempty", buf.len > 0);

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("fujitsu/decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("fujitsu/decode-checksum", g.checksum_ok);
  EXPECT_EQ("fujitsu/decode-bytelen", Frame::kBytes, g.byte_length);
  EXPECT_TRUE("fujitsu/decode-power", g.power());
  EXPECT_TRUE("fujitsu/decode-mode", g.mode() == Mode::COOL);
  EXPECT_EQ("fujitsu/decode-temp", 26, g.temperatureC());
  EXPECT_TRUE("fujitsu/decode-fan", g.fan() == Fan::AUTO);
  EXPECT_TRUE("fujitsu/decode-swing", g.swing() == Swing::OFF);

  // Power on / cool / 26C / fan auto / swing off (ARRAH2E long frame: byte 5 =
  // 0xFE marker, byte 6 = rest-length 0x09, byte 7 = protocol 0x30, byte 15 =
  // complement checksum over bytes 7..14).
  static const uint8_t kCanonicalCool26[Frame::kBytes] = {
      0x14, 0x63, 0x00, 0x10, 0x10, 0xFE, 0x09, 0x30,
      0xA0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2F};
  bool canonical_match = true;
  for (size_t i = 0; i < Frame::kBytes; ++i)
    if (g.bytes[i] != kCanonicalCool26[i])
      canonical_match = false;
  EXPECT_TRUE("fujitsu/canonical-bytes", canonical_match);

  // Temperature clamps to the supported range.
  Frame t{};
  t.setTemperatureC(40);
  EXPECT_EQ("fujitsu/temp-clamp-high", 30, t.temperatureC());
  t.setTemperatureC(5);
  EXPECT_EQ("fujitsu/temp-clamp-low", 16, t.temperatureC());

  // A non-Fujitsu waveform (NEC) must be rejected.
  esp32irpk::IRRawTickView nec{};
  nec.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  nec.len = test_fixtures::nec_normal_00ff_34_raw_len;
  Frame nf{};
  EXPECT_TRUE("fujitsu/reject-nec", !Frame::fromRaw(nec, nf));
}

// Power-off renders the 7-byte short frame {14 63 00 10 10 02 FD}; decoding it
// reports power=off with a valid (inverted-command) checksum, and a 7-byte
// fromBytes reconstructs it. Mode/temp/fan are vendor don't-cares on power-off.
void testFujitsuAcPowerOff()
{
  using esp32irpk::ac::Fujitsu::Frame;
  using esp32irpk::ac::Fujitsu::Mode;

  Frame f{};
  f.setMode(Mode::HEAT); // a no-setter field on the OFF path; must be ignored
  f.setPower(false);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("fujitsu/off-encode", f.toRaw(buf));

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("fujitsu/off-decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("fujitsu/off-decode-checksum", g.checksum_ok);
  EXPECT_EQ("fujitsu/off-decode-bytelen", 7, g.byte_length);
  EXPECT_TRUE("fujitsu/off-decode-power", !g.power());

  static const uint8_t kShortOff[7] = {0x14, 0x63, 0x00, 0x10, 0x10, 0x02, 0xFD};
  bool short_match = true;
  for (size_t i = 0; i < 7; ++i)
    if (g.bytes[i] != kShortOff[i])
      short_match = false;
  EXPECT_TRUE("fujitsu/off-short-bytes", short_match);

  // A 7-byte short state round-trips through fromBytes.
  Frame fb{};
  EXPECT_TRUE("fujitsu/off-frombytes", Frame::fromBytes(kShortOff, 7, fb));
  EXPECT_TRUE("fujitsu/off-frombytes-power", !fb.power());
  EXPECT_TRUE("fujitsu/off-frombytes-checksum", fb.checksum_ok);
}

// Every mode/fan/swing/temperature combination must encode (setters -> toRaw ->
// fromRaw) back to itself with a valid checksum, exercising the full field map.
void testFujitsuAcStateMatrix()
{
  using esp32irpk::ac::Fujitsu::Fan;
  using esp32irpk::ac::Fujitsu::Frame;
  using esp32irpk::ac::Fujitsu::Mode;
  using esp32irpk::ac::Fujitsu::Swing;

  static const Mode modes[] = {Mode::AUTO, Mode::COOL, Mode::DRY, Mode::FAN, Mode::HEAT};
  static const Fan fans[] = {Fan::AUTO, Fan::HIGH_SPEED, Fan::MED_SPEED,
                             Fan::LOW_SPEED, Fan::QUIET};
  static const Swing swings[] = {Swing::OFF, Swing::VERTICAL, Swing::HORIZONTAL, Swing::BOTH};

  bool all_ok = true;
  for (Mode m : modes)
    for (Fan fan : fans)
      for (Swing sw : swings)
        for (uint8_t temp = 16; temp <= 30; ++temp)
        {
          Frame f{};
          f.setPower(true);
          f.setMode(m);
          f.setFan(fan);
          f.setSwing(sw);
          f.setTemperatureC(temp);

          uint16_t ticks[Frame::kMaxTicks];
          esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
          if (!f.toRaw(buf))
          {
            all_ok = false;
            continue;
          }
          esp32irpk::IRRawTickView view{buf.ticks, buf.len};
          Frame g{};
          if (!Frame::fromRaw(view, g) || !g.checksum_ok || !g.power() ||
              g.mode() != m || g.fan() != fan || g.swing() != sw ||
              g.temperatureC() != temp)
            all_ok = false;
        }
  EXPECT_TRUE("fujitsu/state-matrix", all_ok);
}

// Daikin AC (classic / ARC433): build a frame, render the burst (a 5-bit preamble
// + three pulse-distance sections), decode it back, and confirm the logical fields
// and the three per-section checksums survive the roundtrip. The canonical bytes
// are derived from the documented field layout; the hardware study verifies them
// against IRDaikinESP.
void testDaikinAcRoundtrip()
{
  using esp32irpk::ac::Daikin::Fan;
  using esp32irpk::ac::Daikin::Frame;
  using esp32irpk::ac::Daikin::Mode;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setTemperatureC(25);
  f.setFan(Fan::AUTO);
  f.setSwingVertical(false);
  f.setSwingHorizontal(false);

  EXPECT_TRUE("daikin/power-get", f.power());
  EXPECT_TRUE("daikin/mode-get", f.mode() == Mode::COOL);
  EXPECT_EQ("daikin/temp-get", 25.0f, f.temperatureC());
  EXPECT_TRUE("daikin/fan-get", f.fan() == Fan::AUTO);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("daikin/encode", f.toRaw(buf));
  EXPECT_TRUE("daikin/encode-nonempty", buf.len > 0);

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("daikin/decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("daikin/decode-checksum", g.checksum_ok);
  EXPECT_EQ("daikin/decode-bytelen", Frame::kBytes, g.byte_length);
  EXPECT_TRUE("daikin/decode-power", g.power());
  EXPECT_TRUE("daikin/decode-mode", g.mode() == Mode::COOL);
  EXPECT_EQ("daikin/decode-temp", 25.0f, g.temperatureC());
  EXPECT_TRUE("daikin/decode-fan", g.fan() == Fan::AUTO);
  EXPECT_TRUE("daikin/decode-swingv", !g.swingVertical());
  EXPECT_TRUE("daikin/decode-swingh", !g.swingHorizontal());

  // Power on / cool / 25C / fan auto / swing off, derived from the documented
  // layout and the three per-section sum checksums (bytes 7 / 15 / 34).
  static const uint8_t kCanonicalCool25[Frame::kBytes] = {
      0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
      0x11, 0xDA, 0x27, 0x00, 0x42, 0x00, 0x00, 0x54,
      0x11, 0xDA, 0x27, 0x00, 0x00, 0x39, 0x32, 0x00, 0xA0, 0x00,
      0x00, 0x06, 0x60, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x43};
  bool canonical_match = true;
  for (size_t i = 0; i < Frame::kBytes; ++i)
    if (g.bytes[i] != kCanonicalCool25[i])
      canonical_match = false;
  EXPECT_TRUE("daikin/canonical-bytes", canonical_match);

  // Temperature clamps to the supported range.
  Frame t{};
  t.setTemperatureC(40);
  EXPECT_EQ("daikin/temp-clamp-high", 32.0f, t.temperatureC());
  t.setTemperatureC(5);
  EXPECT_EQ("daikin/temp-clamp-low", 10.0f, t.temperatureC());

  // A non-Daikin waveform (NEC) must be rejected.
  esp32irpk::IRRawTickView nec{};
  nec.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  nec.len = test_fixtures::nec_normal_00ff_34_raw_len;
  Frame nf{};
  EXPECT_TRUE("daikin/reject-nec", !Frame::fromRaw(nec, nf));
}

// Every mode/fan/temperature/power/swing combination must encode (setters ->
// toRaw -> fromRaw) back to itself with valid section checksums, exercising the
// full field map and the multi-section codec path.
void testDaikinAcStateMatrix()
{
  using esp32irpk::ac::Daikin::Fan;
  using esp32irpk::ac::Daikin::Frame;
  using esp32irpk::ac::Daikin::Mode;

  static const Mode modes[] = {Mode::AUTO, Mode::DRY, Mode::COOL, Mode::HEAT, Mode::FAN};
  static const Fan fans[] = {Fan::AUTO, Fan::QUIET, Fan::MIN_SPEED, Fan::LOW_SPEED,
                             Fan::MED_SPEED, Fan::HIGH_SPEED, Fan::MAX_SPEED};

  bool all_ok = true;
  for (Mode m : modes)
    for (Fan fan : fans)
      for (uint8_t temp = 10; temp <= 32; ++temp)
        for (uint8_t power = 0; power <= 1; ++power)
        {
          bool sv = (temp % 2) == 0;
          bool sh = (temp % 3) == 0;
          Frame f{};
          f.setPower(power != 0);
          f.setMode(m);
          f.setFan(fan);
          f.setTemperatureC(temp);
          f.setSwingVertical(sv);
          f.setSwingHorizontal(sh);

          uint16_t ticks[Frame::kMaxTicks];
          esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
          if (!f.toRaw(buf))
          {
            all_ok = false;
            continue;
          }
          esp32irpk::IRRawTickView view{buf.ticks, buf.len};
          Frame g{};
          if (!Frame::fromRaw(view, g) || !g.checksum_ok ||
              g.power() != (power != 0) || g.mode() != m || g.fan() != fan ||
              g.temperatureC() != static_cast<float>(temp) ||
              g.swingVertical() != sv || g.swingHorizontal() != sh)
            all_ok = false;
        }
  EXPECT_TRUE("daikin/state-matrix", all_ok);
}

// Toshiba AC (standard 9-byte TOSHIBA_AC): build a frame, render the burst
// (MSB-first, single frame), decode it back, and confirm the logical fields and
// the XOR checksum survive the roundtrip. Also exercises the codec's MSB-first
// path (Toshiba is the only MSB-first AC vendor). Canonical bytes are derived from
// the documented field layout; the hardware study verifies them against
// IRToshibaAC.
void testToshibaAcRoundtrip()
{
  using esp32irpk::ac::Toshiba::Fan;
  using esp32irpk::ac::Toshiba::Frame;
  using esp32irpk::ac::Toshiba::Mode;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setTemperatureC(24);
  f.setFan(Fan::AUTO);

  EXPECT_TRUE("toshiba/power-get", f.power());
  EXPECT_TRUE("toshiba/mode-get", f.mode() == Mode::COOL);
  EXPECT_EQ("toshiba/temp-get", 24u, f.temperatureC());
  EXPECT_TRUE("toshiba/fan-get", f.fan() == Fan::AUTO);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("toshiba/encode", f.toRaw(buf));
  EXPECT_TRUE("toshiba/encode-nonempty", buf.len > 0);

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("toshiba/decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("toshiba/decode-checksum", g.checksum_ok);
  EXPECT_EQ("toshiba/decode-bytelen", Frame::kBytes, g.byte_length);
  EXPECT_TRUE("toshiba/decode-power", g.power());
  EXPECT_TRUE("toshiba/decode-mode", g.mode() == Mode::COOL);
  EXPECT_EQ("toshiba/decode-temp", 24u, g.temperatureC());
  EXPECT_TRUE("toshiba/decode-fan", g.fan() == Fan::AUTO);

  // Power on / cool / 24C / fan auto, derived from the documented layout + XOR
  // checksum (byte 8). Signature F2 0D, inverted pair 03 FC, flags 01.
  static const uint8_t kCanonicalCool24[Frame::kBytes] = {
      0xF2, 0x0D, 0x03, 0xFC, 0x01, 0x70, 0x01, 0x00, 0x70};
  bool canonical_match = true;
  for (size_t i = 0; i < Frame::kBytes; ++i)
    if (g.bytes[i] != kCanonicalCool24[i])
      canonical_match = false;
  EXPECT_TRUE("toshiba/canonical-bytes", canonical_match);

  // Power off is encoded in the Mode field (== 7).
  Frame p{};
  p.setMode(Mode::HEAT);
  p.setPower(false);
  EXPECT_TRUE("toshiba/poweroff", !p.power());
  uint16_t t2[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer b2{t2, Frame::kMaxTicks, 0};
  EXPECT_TRUE("toshiba/poweroff-encode", p.toRaw(b2));
  Frame pg{};
  EXPECT_TRUE("toshiba/poweroff-decode", Frame::fromRaw(esp32irpk::IRRawTickView{b2.ticks, b2.len}, pg));
  EXPECT_TRUE("toshiba/poweroff-decode-off", !pg.power());

  // Temperature clamps to the supported range.
  Frame t{};
  t.setTemperatureC(40);
  EXPECT_EQ("toshiba/temp-clamp-high", 30u, t.temperatureC());
  t.setTemperatureC(5);
  EXPECT_EQ("toshiba/temp-clamp-low", 17u, t.temperatureC());

  // A non-Toshiba waveform (NEC) must be rejected.
  esp32irpk::IRRawTickView nec{};
  nec.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  nec.len = test_fixtures::nec_normal_00ff_34_raw_len;
  Frame nf{};
  EXPECT_TRUE("toshiba/reject-nec", !Frame::fromRaw(nec, nf));
}

// Every mode/fan/temperature/power combination must encode (setters -> toRaw ->
// fromRaw) back to itself with a valid XOR checksum, exercising the full field map
// and the MSB-first codec path.
void testToshibaAcStateMatrix()
{
  using esp32irpk::ac::Toshiba::Fan;
  using esp32irpk::ac::Toshiba::Frame;
  using esp32irpk::ac::Toshiba::Mode;

  static const Mode modes[] = {Mode::AUTO, Mode::COOL, Mode::DRY, Mode::HEAT, Mode::FAN};
  static const Fan fans[] = {Fan::AUTO, Fan::MIN_SPEED, Fan::LOW_SPEED,
                             Fan::MED_SPEED, Fan::HIGH_SPEED, Fan::MAX_SPEED};

  bool all_ok = true;
  for (Mode m : modes)
    for (Fan fan : fans)
      for (uint8_t temp = 17; temp <= 30; ++temp)
        for (uint8_t power = 0; power <= 1; ++power)
        {
          Frame f{};
          f.setMode(m);
          f.setFan(fan);
          f.setTemperatureC(temp);
          f.setPower(power != 0);

          uint16_t ticks[Frame::kMaxTicks];
          esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
          if (!f.toRaw(buf))
          {
            all_ok = false;
            continue;
          }
          esp32irpk::IRRawTickView view{buf.ticks, buf.len};
          Frame g{};
          if (!Frame::fromRaw(view, g) || !g.checksum_ok ||
              g.power() != (power != 0) || g.fan() != fan ||
              g.temperatureC() != temp)
            all_ok = false;
          // Mode is only meaningful when powered on (off masks it as field 7).
          if (power && g.mode() != m)
            all_ok = false;
        }
  EXPECT_TRUE("toshiba/state-matrix", all_ok);
}

// Samsung AC (standard 14-byte SAMSUNG_AC): build a frame, render the burst
// (LSB-first, leading header + two 7-byte sections), decode it back, and confirm
// the logical fields and the two popcount section checksums survive the roundtrip.
// Canonical bytes are derived from IRSamsungAc's documented field layout +
// checksum; the hardware study verifies them against IRSamsungAc.
void testSamsungAcRoundtrip()
{
  using esp32irpk::ac::Samsung::Fan;
  using esp32irpk::ac::Samsung::Frame;
  using esp32irpk::ac::Samsung::Mode;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setTemperatureC(24);
  f.setFan(Fan::AUTO);

  EXPECT_TRUE("samsung/power-get", f.power());
  EXPECT_TRUE("samsung/mode-get", f.mode() == Mode::COOL);
  EXPECT_EQ("samsung/temp-get", 24u, f.temperatureC());
  EXPECT_TRUE("samsung/fan-get", f.fan() == Fan::AUTO);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("samsung/encode", f.toRaw(buf));
  EXPECT_TRUE("samsung/encode-nonempty", buf.len > 0);

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("samsung/decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("samsung/decode-checksum", g.checksum_ok);
  EXPECT_EQ("samsung/decode-bytelen", Frame::kBytes, g.byte_length);
  EXPECT_TRUE("samsung/decode-power", g.power());
  EXPECT_TRUE("samsung/decode-mode", g.mode() == Mode::COOL);
  EXPECT_EQ("samsung/decode-temp", 24u, g.temperatureC());
  EXPECT_TRUE("samsung/decode-fan", g.fan() == Fan::AUTO);

  // Power on / cool / 24C / fan auto, derived from IRSamsungAc's struct layout +
  // the popcount section checksums (section 1 -> bytes 1,2; section 2 -> bytes 8,9).
  static const uint8_t kCanonicalCool24[Frame::kBytes] = {
      0x02, 0x92, 0x0F, 0x00, 0x00, 0x00, 0xF0,
      0x01, 0x02, 0xAF, 0x71, 0x80, 0x11, 0xF0};
  bool canonical_match = true;
  for (size_t i = 0; i < Frame::kBytes; ++i)
    if (g.bytes[i] != kCanonicalCool24[i])
      canonical_match = false;
  EXPECT_TRUE("samsung/canonical-bytes", canonical_match);

  // Power is two 2-bit fields; off is independent of mode.
  Frame p{};
  p.setMode(Mode::HEAT);
  p.setPower(false);
  EXPECT_TRUE("samsung/poweroff", !p.power());
  EXPECT_TRUE("samsung/poweroff-mode-kept", p.mode() == Mode::HEAT);
  uint16_t t2[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer b2{t2, Frame::kMaxTicks, 0};
  EXPECT_TRUE("samsung/poweroff-encode", p.toRaw(b2));
  Frame pg{};
  EXPECT_TRUE("samsung/poweroff-decode", Frame::fromRaw(esp32irpk::IRRawTickView{b2.ticks, b2.len}, pg));
  EXPECT_TRUE("samsung/poweroff-decode-off", !pg.power());
  EXPECT_TRUE("samsung/poweroff-decode-mode", pg.mode() == Mode::HEAT);

  // Temperature clamps to the supported range.
  Frame t{};
  t.setTemperatureC(40);
  EXPECT_EQ("samsung/temp-clamp-high", 30u, t.temperatureC());
  t.setTemperatureC(5);
  EXPECT_EQ("samsung/temp-clamp-low", 16u, t.temperatureC());

  // A non-Samsung waveform (NEC) must be rejected.
  esp32irpk::IRRawTickView nec{};
  nec.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  nec.len = test_fixtures::nec_normal_00ff_34_raw_len;
  Frame nf{};
  EXPECT_TRUE("samsung/reject-nec", !Frame::fromRaw(nec, nf));
}

// Every mode/fan/temperature/power combination must encode (setters -> toRaw ->
// fromRaw) back to itself with valid section checksums, exercising the full field
// map, the two-section framing, and the LSB-first codec path.
void testSamsungAcStateMatrix()
{
  using esp32irpk::ac::Samsung::Fan;
  using esp32irpk::ac::Samsung::Frame;
  using esp32irpk::ac::Samsung::Mode;

  static const Mode modes[] = {Mode::AUTO, Mode::COOL, Mode::DRY, Mode::FAN, Mode::HEAT};
  static const Fan fans[] = {Fan::AUTO, Fan::LOW_SPEED, Fan::MED_SPEED,
                             Fan::HIGH_SPEED, Fan::MAX_SPEED};

  bool all_ok = true;
  for (Mode m : modes)
    for (Fan fan : fans)
      for (uint8_t temp = 16; temp <= 30; ++temp)
        for (uint8_t power = 0; power <= 1; ++power)
        {
          Frame f{};
          f.setMode(m);
          f.setFan(fan);
          f.setTemperatureC(temp);
          f.setPower(power != 0);

          uint16_t ticks[Frame::kMaxTicks];
          esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
          if (!f.toRaw(buf))
          {
            all_ok = false;
            continue;
          }
          esp32irpk::IRRawTickView view{buf.ticks, buf.len};
          Frame g{};
          // Power is independent of mode, so mode is always asserted.
          if (!Frame::fromRaw(view, g) || !g.checksum_ok ||
              g.power() != (power != 0) || g.mode() != m || g.fan() != fan ||
              g.temperatureC() != temp)
            all_ok = false;
        }
  EXPECT_TRUE("samsung/state-matrix", all_ok);
}

// Sharp AC (standard 13-byte SHARP_AC): build a frame, render the burst (LSB-first,
// single frame, fixed AA 5A CF 10 header), decode it back, and confirm the logical
// fields and the nibble-folded XOR checksum survive the roundtrip. Canonical bytes
// are derived from IRSharpAc's documented layout + checksum; the hardware study
// verifies them against IRSharpAc.
void testSharpAcRoundtrip()
{
  using esp32irpk::ac::Sharp::Fan;
  using esp32irpk::ac::Sharp::Frame;
  using esp32irpk::ac::Sharp::Mode;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setTemperatureC(24);
  f.setFan(Fan::AUTO);

  EXPECT_TRUE("sharp/power-get", f.power());
  EXPECT_TRUE("sharp/mode-get", f.mode() == Mode::COOL);
  EXPECT_EQ("sharp/temp-get", 24u, f.temperatureC());
  EXPECT_TRUE("sharp/fan-get", f.fan() == Fan::AUTO);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("sharp/encode", f.toRaw(buf));
  EXPECT_TRUE("sharp/encode-nonempty", buf.len > 0);

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("sharp/decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("sharp/decode-checksum", g.checksum_ok);
  EXPECT_EQ("sharp/decode-bytelen", Frame::kBytes, g.byte_length);
  EXPECT_TRUE("sharp/decode-power", g.power());
  EXPECT_TRUE("sharp/decode-mode", g.mode() == Mode::COOL);
  EXPECT_EQ("sharp/decode-temp", 24u, g.temperatureC());
  EXPECT_TRUE("sharp/decode-fan", g.fan() == Fan::AUTO);

  // On / cool / 24C / fan auto (A907), from IRSharpAc's struct + nibble checksum.
  // Byte 4 = 0xC0 | (24-15) = 0xC9 (IRSharpAc fixes the temp byte's high bits).
  static const uint8_t kCanonicalCool24[Frame::kBytes] = {
      0xAA, 0x5A, 0xCF, 0x10, 0xC9, 0x31, 0x22,
      0x00, 0x08, 0x80, 0x00, 0xE0, 0x51};
  bool canonical_match = true;
  for (size_t i = 0; i < Frame::kBytes; ++i)
    if (g.bytes[i] != kCanonicalCool24[i])
      canonical_match = false;
  EXPECT_TRUE("sharp/canonical-bytes", canonical_match);

  // Power off (PowerSpecial = 2).
  Frame p{};
  p.setMode(Mode::HEAT);
  p.setPower(false);
  EXPECT_TRUE("sharp/poweroff", !p.power());
  uint16_t t2[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer b2{t2, Frame::kMaxTicks, 0};
  EXPECT_TRUE("sharp/poweroff-encode", p.toRaw(b2));
  Frame pg{};
  EXPECT_TRUE("sharp/poweroff-decode", Frame::fromRaw(esp32irpk::IRRawTickView{b2.ticks, b2.len}, pg));
  EXPECT_TRUE("sharp/poweroff-decode-off", !pg.power());

  // Temperature clamps to the supported range.
  Frame t{};
  t.setMode(Mode::COOL);
  t.setTemperatureC(40);
  EXPECT_EQ("sharp/temp-clamp-high", 30u, t.temperatureC());
  t.setTemperatureC(5);
  EXPECT_EQ("sharp/temp-clamp-low", 15u, t.temperatureC());

  // A non-Sharp waveform (NEC) must be rejected.
  esp32irpk::IRRawTickView nec{};
  nec.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  nec.len = test_fixtures::nec_normal_00ff_34_raw_len;
  Frame nf{};
  EXPECT_TRUE("sharp/reject-nec", !Frame::fromRaw(nec, nf));
}

// Every mode/fan/temperature/power combination must encode (setters -> toRaw ->
// fromRaw) back to itself with a valid checksum. Auto/Dry force Fan=Auto and Temp=0
// (no temp setting), so those fields are only asserted in Cool/Heat.
void testSharpAcStateMatrix()
{
  using esp32irpk::ac::Sharp::Fan;
  using esp32irpk::ac::Sharp::Frame;
  using esp32irpk::ac::Sharp::Mode;

  static const Mode modes[] = {Mode::AUTO, Mode::HEAT, Mode::COOL, Mode::DRY};
  static const Fan fans[] = {Fan::AUTO, Fan::MIN_SPEED, Fan::MED_SPEED,
                             Fan::HIGH_SPEED, Fan::MAX_SPEED};

  bool all_ok = true;
  for (Mode m : modes)
    for (Fan fan : fans)
      for (uint8_t temp = 15; temp <= 30; ++temp)
        for (uint8_t power = 0; power <= 1; ++power)
        {
          Frame f{};
          f.setMode(m);
          f.setFan(fan);
          f.setTemperatureC(temp);
          f.setPower(power != 0);

          uint16_t ticks[Frame::kMaxTicks];
          esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
          if (!f.toRaw(buf))
          {
            all_ok = false;
            continue;
          }
          esp32irpk::IRRawTickView view{buf.ticks, buf.len};
          Frame g{};
          if (!Frame::fromRaw(view, g) || !g.checksum_ok ||
              g.power() != (power != 0) || g.mode() != m || g.fan() != fan)
            all_ok = false;
          // Temperature is only meaningful in Cool/Heat (Auto/Dry carry no temp).
          const bool cool_or_heat = (m == Mode::COOL || m == Mode::HEAT);
          if (cool_or_heat && g.temperatureC() != temp)
            all_ok = false;
        }
  EXPECT_TRUE("sharp/state-matrix", all_ok);
}

// Kelvinator AC (standard 16-byte, two-block protocol): build a frame, render the
// two-block burst (header + 32 bits + B010 footer + gap + 32 bits + gap, x2) and
// decode it back. Field offsets + block checksums are derived from IRKelvinatorAC's
// documented layout; the hardware study verifies them against IRKelvinatorAC.
void testKelvinatorAcRoundtrip()
{
  using esp32irpk::ac::Kelvinator::Fan;
  using esp32irpk::ac::Kelvinator::Frame;
  using esp32irpk::ac::Kelvinator::Mode;

  Frame f{};
  f.setPower(true);
  f.setMode(Mode::COOL);
  f.setTemperatureC(24);
  f.setFan(Fan::AUTO);

  EXPECT_TRUE("kelvinator/power-get", f.power());
  EXPECT_TRUE("kelvinator/mode-get", f.mode() == Mode::COOL);
  EXPECT_EQ("kelvinator/temp-get", 24u, f.temperatureC());
  EXPECT_TRUE("kelvinator/fan-get", f.fan() == Fan::AUTO);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
  EXPECT_TRUE("kelvinator/encode", f.toRaw(buf));
  EXPECT_TRUE("kelvinator/encode-nonempty", buf.len > 0);

  esp32irpk::IRRawTickView view{buf.ticks, buf.len};
  Frame g{};
  EXPECT_TRUE("kelvinator/decode", Frame::fromRaw(view, g));
  EXPECT_TRUE("kelvinator/decode-checksum", g.checksum_ok);
  EXPECT_EQ("kelvinator/decode-bytelen", Frame::kBytes, g.byte_length);
  EXPECT_TRUE("kelvinator/decode-power", g.power());
  EXPECT_TRUE("kelvinator/decode-mode", g.mode() == Mode::COOL);
  EXPECT_EQ("kelvinator/decode-temp", 24u, g.temperatureC());
  EXPECT_TRUE("kelvinator/decode-fan", g.fan() == Fan::AUTO);

  // On / cool / 24C / fan auto, from IRKelvinatorAC's struct + block checksums.
  static const uint8_t kCanonicalCool24[Frame::kBytes] = {
      0x09, 0x08, 0x00, 0x50, 0x00, 0x00, 0x00, 0xB0,
      0x09, 0x08, 0x00, 0x70, 0x00, 0x00, 0x00, 0xB0};
  bool canonical_match = true;
  for (size_t i = 0; i < Frame::kBytes; ++i)
    if (g.bytes[i] != kCanonicalCool24[i])
      canonical_match = false;
  EXPECT_TRUE("kelvinator/canonical-bytes", canonical_match);

  // A hardware capture does not record the final trailing gap (the RX ends on
  // idle silence), so the last tick is the trailer mark with no gap after it.
  // Decode must still succeed with that final gap stripped.
  Frame ng{};
  EXPECT_TRUE("kelvinator/decode-no-trailing-gap",
              buf.len > 1 &&
                  Frame::fromRaw(esp32irpk::IRRawTickView{buf.ticks, buf.len - 1}, ng) &&
                  ng.checksum_ok && ng.mode() == Mode::COOL && ng.fan() == Fan::AUTO);

  // Power off.
  Frame p{};
  p.setMode(Mode::HEAT);
  p.setPower(false);
  EXPECT_TRUE("kelvinator/poweroff", !p.power());
  uint16_t t2[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer b2{t2, Frame::kMaxTicks, 0};
  EXPECT_TRUE("kelvinator/poweroff-encode", p.toRaw(b2));
  Frame pg{};
  EXPECT_TRUE("kelvinator/poweroff-decode", Frame::fromRaw(esp32irpk::IRRawTickView{b2.ticks, b2.len}, pg));
  EXPECT_TRUE("kelvinator/poweroff-decode-off", !pg.power());

  // Temperature clamps to the supported range.
  Frame t{};
  t.setMode(Mode::COOL);
  t.setTemperatureC(40);
  EXPECT_EQ("kelvinator/temp-clamp-high", 30u, t.temperatureC());
  t.setTemperatureC(5);
  EXPECT_EQ("kelvinator/temp-clamp-low", 16u, t.temperatureC());

  // A non-Kelvinator waveform (NEC) must be rejected.
  esp32irpk::IRRawTickView nec{};
  nec.ticks = test_fixtures::nec_normal_00ff_34_raw_ticks;
  nec.len = test_fixtures::nec_normal_00ff_34_raw_len;
  Frame nf{};
  EXPECT_TRUE("kelvinator/reject-nec", !Frame::fromRaw(nec, nf));
}

// Every mode/fan/temperature/power combination must encode -> decode back to
// itself with a valid checksum. Auto/Dry force 25C, so temperature is only
// asserted in the modes that keep it (Cool/Heat/Fan).
void testKelvinatorAcStateMatrix()
{
  using esp32irpk::ac::Kelvinator::Fan;
  using esp32irpk::ac::Kelvinator::Frame;
  using esp32irpk::ac::Kelvinator::Mode;

  static const Mode modes[] = {Mode::AUTO, Mode::COOL, Mode::DRY, Mode::FAN, Mode::HEAT};
  static const Fan fans[] = {Fan::AUTO, Fan::MIN_SPEED, Fan::LOW_SPEED,
                             Fan::MED_SPEED, Fan::HIGH_SPEED, Fan::MAX_SPEED};

  bool all_ok = true;
  for (Mode m : modes)
    for (Fan fan : fans)
      for (uint8_t temp = 16; temp <= 30; ++temp)
        for (uint8_t power = 0; power <= 1; ++power)
        {
          Frame f{};
          f.setMode(m);
          f.setFan(fan);
          f.setTemperatureC(temp);
          f.setPower(power != 0);

          uint16_t ticks[Frame::kMaxTicks];
          esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
          if (!f.toRaw(buf))
          {
            all_ok = false;
            continue;
          }
          esp32irpk::IRRawTickView view{buf.ticks, buf.len};
          Frame g{};
          if (!Frame::fromRaw(view, g) || !g.checksum_ok ||
              g.power() != (power != 0) || g.mode() != m || g.fan() != fan)
            all_ok = false;
          // Auto/Dry force 25C; the other modes keep the set temperature.
          const bool keeps_temp = (m != Mode::AUTO && m != Mode::DRY);
          if (keeps_temp && g.temperatureC() != temp)
            all_ok = false;
        }
  EXPECT_TRUE("kelvinator/state-matrix", all_ok);
}

// decodeAny must classify each vendor's own rendered frame as that vendor. This
// guards the cascade ordering -- notably Gree vs Kelvinator, whose frames share a
// header + B010 footer + block checksum (a Gree frame is one Kelvinator block), so
// the more-specific 16-byte Kelvinator must be tried before the 8-byte Gree.
void testAcDecodeAny()
{
  namespace ac = esp32irpk::ac;
  static uint16_t ticks[1024];
  auto check = [&](auto frame, ac::AcVendor expected, const char *label) {
    esp32irpk::IRRawTickBuffer buf{ticks, sizeof(ticks) / sizeof(ticks[0]), 0};
    const bool ok = frame.toRaw(buf) &&
                    ac::decodeAny(esp32irpk::IRRawTickView{buf.ticks, buf.len}) == expected;
    EXPECT_TRUE(label, ok);
  };
  check(ac::Panasonic::Frame{}, ac::AcVendor::PANASONIC, "decodeany/panasonic");
  check(ac::Gree::Frame{}, ac::AcVendor::GREE, "decodeany/gree");
  check(ac::Mitsubishi::Frame{}, ac::AcVendor::MITSUBISHI, "decodeany/mitsubishi");
  check(ac::Fujitsu::Frame{}, ac::AcVendor::FUJITSU, "decodeany/fujitsu");
  check(ac::Daikin::Frame{}, ac::AcVendor::DAIKIN, "decodeany/daikin");
  check(ac::Toshiba::Frame{}, ac::AcVendor::TOSHIBA, "decodeany/toshiba");
  check(ac::Samsung::Frame{}, ac::AcVendor::SAMSUNG, "decodeany/samsung");
  check(ac::Sharp::Frame{}, ac::AcVendor::SHARP, "decodeany/sharp");
  check(ac::Kelvinator::Frame{}, ac::AcVendor::KELVINATOR, "decodeany/kelvinator");
}

// Each vendor's toString() maps an enum value to its identifier name (used by
// printTo). Out-of-range values fall back to "?".
void testAcEnumToString()
{
  namespace P = esp32irpk::ac::Panasonic;
  namespace G = esp32irpk::ac::Gree;
  namespace M = esp32irpk::ac::Mitsubishi;
  namespace F = esp32irpk::ac::Fujitsu;
  namespace D = esp32irpk::ac::Daikin;
  namespace TO = esp32irpk::ac::Toshiba;
  namespace SA = esp32irpk::ac::Samsung;
  namespace SH = esp32irpk::ac::Sharp;
  namespace K = esp32irpk::ac::Kelvinator;
  EXPECT_TRUE("panasonic/tostring-mode", strcmp(P::toString(P::Mode::COOL), "COOL") == 0);
  EXPECT_TRUE("panasonic/tostring-fan-min", strcmp(P::toString(P::Fan::MIN_SPEED), "MIN_SPEED") == 0);
  EXPECT_TRUE("panasonic/tostring-fan-quiet", strcmp(P::toString(P::Fan::QUIET), "QUIET") == 0);
  EXPECT_TRUE("panasonic/tostring-louver", strcmp(P::toString(P::Louver::P3), "P3") == 0);
  EXPECT_TRUE("gree/tostring-fan-max", strcmp(G::toString(G::Fan::MAX_SPEED), "MAX_SPEED") == 0);
  EXPECT_TRUE("gree/tostring-swingv", strcmp(G::toString(G::SwingV::MIDDLE), "MIDDLE") == 0);
  EXPECT_TRUE("gree/tostring-swingh", strcmp(G::toString(G::SwingH::MAX_LEFT), "MAX_LEFT") == 0);
  EXPECT_TRUE("mitsubishi/tostring-vane", strcmp(M::toString(M::Vane::P3), "P3") == 0);
  EXPECT_TRUE("mitsubishi/tostring-widevane", strcmp(M::toString(M::WideVane::AUTO), "AUTO") == 0);
  EXPECT_TRUE("mitsubishi/tostring-fan-quiet", strcmp(M::toString(M::Fan::QUIET), "QUIET") == 0);
  EXPECT_TRUE("fujitsu/tostring-mode", strcmp(F::toString(F::Mode::HEAT), "HEAT") == 0);
  EXPECT_TRUE("fujitsu/tostring-fan-quiet", strcmp(F::toString(F::Fan::QUIET), "QUIET") == 0);
  EXPECT_TRUE("fujitsu/tostring-swing", strcmp(F::toString(F::Swing::BOTH), "BOTH") == 0);
  EXPECT_TRUE("daikin/tostring-mode", strcmp(D::toString(D::Mode::COOL), "COOL") == 0);
  EXPECT_TRUE("daikin/tostring-fan-quiet", strcmp(D::toString(D::Fan::QUIET), "QUIET") == 0);
  EXPECT_TRUE("daikin/tostring-fan-max", strcmp(D::toString(D::Fan::MAX_SPEED), "MAX_SPEED") == 0);
  EXPECT_TRUE("toshiba/tostring-mode", strcmp(TO::toString(TO::Mode::HEAT), "HEAT") == 0);
  EXPECT_TRUE("toshiba/tostring-fan-min", strcmp(TO::toString(TO::Fan::MIN_SPEED), "MIN_SPEED") == 0);
  EXPECT_TRUE("toshiba/tostring-fan-max", strcmp(TO::toString(TO::Fan::MAX_SPEED), "MAX_SPEED") == 0);
  EXPECT_TRUE("samsung/tostring-mode", strcmp(SA::toString(SA::Mode::HEAT), "HEAT") == 0);
  EXPECT_TRUE("samsung/tostring-fan-low", strcmp(SA::toString(SA::Fan::LOW_SPEED), "LOW_SPEED") == 0);
  EXPECT_TRUE("samsung/tostring-fan-max", strcmp(SA::toString(SA::Fan::MAX_SPEED), "MAX_SPEED") == 0);
  EXPECT_TRUE("sharp/tostring-mode", strcmp(SH::toString(SH::Mode::COOL), "COOL") == 0);
  EXPECT_TRUE("sharp/tostring-fan-min", strcmp(SH::toString(SH::Fan::MIN_SPEED), "MIN_SPEED") == 0);
  EXPECT_TRUE("sharp/tostring-fan-max", strcmp(SH::toString(SH::Fan::MAX_SPEED), "MAX_SPEED") == 0);
  EXPECT_TRUE("kelvinator/tostring-mode", strcmp(K::toString(K::Mode::HEAT), "HEAT") == 0);
  EXPECT_TRUE("kelvinator/tostring-fan-min", strcmp(K::toString(K::Fan::MIN_SPEED), "MIN_SPEED") == 0);
  EXPECT_TRUE("kelvinator/tostring-fan-max", strcmp(K::toString(K::Fan::MAX_SPEED), "MAX_SPEED") == 0);
  // Out-of-range value falls back to "?".
  EXPECT_TRUE("panasonic/tostring-unknown", strcmp(P::toString((P::Mode)99), "?") == 0);
}

// fromBytes rebuilds a frame from decoded state bytes: it matches fromRaw's
// result and round-trips bit-exactly through toRaw. Wrong length is rejected.
void testAcFromBytes()
{
  // Panasonic: full check incl. model detection, bad-length, and bit-exact replay.
  {
    using esp32irpk::ac::Panasonic::Fan;
    using esp32irpk::ac::Panasonic::Frame;
    using esp32irpk::ac::Panasonic::Mode;
    Frame a{};
    a.setPower(true);
    a.setMode(Mode::COOL);
    a.setTemperatureC(22.5f);
    a.setFan(Fan::QUIET);
    uint16_t t[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{t, Frame::kMaxTicks, 0};
    EXPECT_TRUE("panasonic/frombytes-enc", a.toRaw(buf));
    esp32irpk::IRRawTickView v{buf.ticks, buf.len};
    Frame dec{};
    EXPECT_TRUE("panasonic/frombytes-dec", Frame::fromRaw(v, dec));

    Frame fb{};
    EXPECT_TRUE("panasonic/frombytes", Frame::fromBytes(dec.bytes, Frame::kBytes, fb));
    EXPECT_TRUE("panasonic/frombytes-checksum", fb.checksum_ok);
    EXPECT_TRUE("panasonic/frombytes-model", fb.model == dec.model);
    bool eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (fb.bytes[i] != dec.bytes[i])
        eq = false;
    EXPECT_TRUE("panasonic/frombytes-eq", eq);
    Frame bad{};
    EXPECT_TRUE("panasonic/frombytes-badlen", !Frame::fromBytes(dec.bytes, Frame::kBytes - 1, bad));

    // Bit-exact replay: fromBytes -> toRaw -> fromRaw reproduces the same bytes.
    uint16_t t2[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf2{t2, Frame::kMaxTicks, 0};
    EXPECT_TRUE("panasonic/frombytes-reenc", fb.toRaw(buf2));
    esp32irpk::IRRawTickView v2{buf2.ticks, buf2.len};
    Frame dec2{};
    EXPECT_TRUE("panasonic/frombytes-redec", Frame::fromRaw(v2, dec2));
    bool replay_eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (dec2.bytes[i] != dec.bytes[i])
        replay_eq = false;
    EXPECT_TRUE("panasonic/frombytes-replay-eq", replay_eq);
  }
  // Gree
  {
    using esp32irpk::ac::Gree::Fan;
    using esp32irpk::ac::Gree::Frame;
    using esp32irpk::ac::Gree::Mode;
    using esp32irpk::ac::Gree::SwingV;
    Frame a{};
    a.setPower(true);
    a.setMode(Mode::COOL);
    a.setTemperatureC(25);
    a.setFan(Fan::MED_SPEED);
    a.setSwingV(SwingV::MIDDLE);
    uint16_t t[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{t, Frame::kMaxTicks, 0};
    EXPECT_TRUE("gree/frombytes-enc", a.toRaw(buf));
    esp32irpk::IRRawTickView v{buf.ticks, buf.len};
    Frame dec{};
    EXPECT_TRUE("gree/frombytes-dec", Frame::fromRaw(v, dec));
    Frame fb{};
    EXPECT_TRUE("gree/frombytes", Frame::fromBytes(dec.bytes, Frame::kBytes, fb));
    EXPECT_TRUE("gree/frombytes-checksum", fb.checksum_ok);
    bool eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (fb.bytes[i] != dec.bytes[i])
        eq = false;
    EXPECT_TRUE("gree/frombytes-eq", eq);
    EXPECT_TRUE("gree/frombytes-badlen", !Frame::fromBytes(dec.bytes, 0, fb));
  }
  // Mitsubishi
  {
    using esp32irpk::ac::Mitsubishi::Fan;
    using esp32irpk::ac::Mitsubishi::Frame;
    using esp32irpk::ac::Mitsubishi::Mode;
    using esp32irpk::ac::Mitsubishi::Vane;
    Frame a{};
    a.setPower(true);
    a.setMode(Mode::HEAT);
    a.setTemperatureC(22.5f);
    a.setFan(Fan::QUIET);
    a.setVane(Vane::P3);
    uint16_t t[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{t, Frame::kMaxTicks, 0};
    EXPECT_TRUE("mitsubishi/frombytes-enc", a.toRaw(buf));
    esp32irpk::IRRawTickView v{buf.ticks, buf.len};
    Frame dec{};
    EXPECT_TRUE("mitsubishi/frombytes-dec", Frame::fromRaw(v, dec));
    Frame fb{};
    EXPECT_TRUE("mitsubishi/frombytes", Frame::fromBytes(dec.bytes, Frame::kBytes, fb));
    EXPECT_TRUE("mitsubishi/frombytes-checksum", fb.checksum_ok);
    bool eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (fb.bytes[i] != dec.bytes[i])
        eq = false;
    EXPECT_TRUE("mitsubishi/frombytes-eq", eq);
    EXPECT_TRUE("mitsubishi/frombytes-badlen", !Frame::fromBytes(dec.bytes, 99, fb));
  }
  // Fujitsu (long frame): bit-exact replay through fromBytes.
  {
    using esp32irpk::ac::Fujitsu::Fan;
    using esp32irpk::ac::Fujitsu::Frame;
    using esp32irpk::ac::Fujitsu::Mode;
    using esp32irpk::ac::Fujitsu::Swing;
    Frame a{};
    a.setPower(true);
    a.setMode(Mode::HEAT);
    a.setTemperatureC(28);
    a.setFan(Fan::LOW_SPEED);
    a.setSwing(Swing::VERTICAL);
    uint16_t t[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{t, Frame::kMaxTicks, 0};
    EXPECT_TRUE("fujitsu/frombytes-enc", a.toRaw(buf));
    esp32irpk::IRRawTickView v{buf.ticks, buf.len};
    Frame dec{};
    EXPECT_TRUE("fujitsu/frombytes-dec", Frame::fromRaw(v, dec));
    Frame fb{};
    EXPECT_TRUE("fujitsu/frombytes", Frame::fromBytes(dec.bytes, Frame::kBytes, fb));
    EXPECT_TRUE("fujitsu/frombytes-checksum", fb.checksum_ok);
    bool eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (fb.bytes[i] != dec.bytes[i])
        eq = false;
    EXPECT_TRUE("fujitsu/frombytes-eq", eq);
    EXPECT_TRUE("fujitsu/frombytes-badlen", !Frame::fromBytes(dec.bytes, 99, fb));
  }
  // Daikin (classic): bit-exact replay through fromBytes.
  {
    using esp32irpk::ac::Daikin::Fan;
    using esp32irpk::ac::Daikin::Frame;
    using esp32irpk::ac::Daikin::Mode;
    Frame a{};
    a.setPower(true);
    a.setMode(Mode::HEAT);
    a.setTemperatureC(28);
    a.setFan(Fan::MAX_SPEED);
    a.setSwingVertical(true);
    a.setSwingHorizontal(false);
    uint16_t t[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{t, Frame::kMaxTicks, 0};
    EXPECT_TRUE("daikin/frombytes-enc", a.toRaw(buf));
    esp32irpk::IRRawTickView v{buf.ticks, buf.len};
    Frame dec{};
    EXPECT_TRUE("daikin/frombytes-dec", Frame::fromRaw(v, dec));
    Frame fb{};
    EXPECT_TRUE("daikin/frombytes", Frame::fromBytes(dec.bytes, Frame::kBytes, fb));
    EXPECT_TRUE("daikin/frombytes-checksum", fb.checksum_ok);
    bool eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (fb.bytes[i] != dec.bytes[i])
        eq = false;
    EXPECT_TRUE("daikin/frombytes-eq", eq);
    EXPECT_TRUE("daikin/frombytes-badlen", !Frame::fromBytes(dec.bytes, Frame::kBytes - 1, fb));
  }
  // Toshiba: bit-exact replay through fromBytes.
  {
    using esp32irpk::ac::Toshiba::Fan;
    using esp32irpk::ac::Toshiba::Frame;
    using esp32irpk::ac::Toshiba::Mode;
    Frame a{};
    a.setPower(true);
    a.setMode(Mode::HEAT);
    a.setTemperatureC(28);
    a.setFan(Fan::MAX_SPEED);
    uint16_t t[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{t, Frame::kMaxTicks, 0};
    EXPECT_TRUE("toshiba/frombytes-enc", a.toRaw(buf));
    esp32irpk::IRRawTickView v{buf.ticks, buf.len};
    Frame dec{};
    EXPECT_TRUE("toshiba/frombytes-dec", Frame::fromRaw(v, dec));
    Frame fb{};
    EXPECT_TRUE("toshiba/frombytes", Frame::fromBytes(dec.bytes, Frame::kBytes, fb));
    EXPECT_TRUE("toshiba/frombytes-checksum", fb.checksum_ok);
    bool eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (fb.bytes[i] != dec.bytes[i])
        eq = false;
    EXPECT_TRUE("toshiba/frombytes-eq", eq);
    EXPECT_TRUE("toshiba/frombytes-badlen", !Frame::fromBytes(dec.bytes, Frame::kBytes - 1, fb));
  }
  // Samsung: bit-exact replay through fromBytes.
  {
    using esp32irpk::ac::Samsung::Fan;
    using esp32irpk::ac::Samsung::Frame;
    using esp32irpk::ac::Samsung::Mode;
    Frame a{};
    a.setPower(true);
    a.setMode(Mode::HEAT);
    a.setTemperatureC(28);
    a.setFan(Fan::MAX_SPEED);
    uint16_t t[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{t, Frame::kMaxTicks, 0};
    EXPECT_TRUE("samsung/frombytes-enc", a.toRaw(buf));
    esp32irpk::IRRawTickView v{buf.ticks, buf.len};
    Frame dec{};
    EXPECT_TRUE("samsung/frombytes-dec", Frame::fromRaw(v, dec));
    Frame fb{};
    EXPECT_TRUE("samsung/frombytes", Frame::fromBytes(dec.bytes, Frame::kBytes, fb));
    EXPECT_TRUE("samsung/frombytes-checksum", fb.checksum_ok);
    bool eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (fb.bytes[i] != dec.bytes[i])
        eq = false;
    EXPECT_TRUE("samsung/frombytes-eq", eq);
    EXPECT_TRUE("samsung/frombytes-badlen", !Frame::fromBytes(dec.bytes, Frame::kBytes - 1, fb));
  }
  // Sharp: bit-exact replay through fromBytes.
  {
    using esp32irpk::ac::Sharp::Fan;
    using esp32irpk::ac::Sharp::Frame;
    using esp32irpk::ac::Sharp::Mode;
    Frame a{};
    a.setPower(true);
    a.setMode(Mode::HEAT);
    a.setTemperatureC(28);
    a.setFan(Fan::MAX_SPEED);
    uint16_t t[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{t, Frame::kMaxTicks, 0};
    EXPECT_TRUE("sharp/frombytes-enc", a.toRaw(buf));
    esp32irpk::IRRawTickView v{buf.ticks, buf.len};
    Frame dec{};
    EXPECT_TRUE("sharp/frombytes-dec", Frame::fromRaw(v, dec));
    Frame fb{};
    EXPECT_TRUE("sharp/frombytes", Frame::fromBytes(dec.bytes, Frame::kBytes, fb));
    EXPECT_TRUE("sharp/frombytes-checksum", fb.checksum_ok);
    bool eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (fb.bytes[i] != dec.bytes[i])
        eq = false;
    EXPECT_TRUE("sharp/frombytes-eq", eq);
    EXPECT_TRUE("sharp/frombytes-badlen", !Frame::fromBytes(dec.bytes, Frame::kBytes - 1, fb));
  }
  // Kelvinator: bit-exact replay through fromBytes.
  {
    using esp32irpk::ac::Kelvinator::Fan;
    using esp32irpk::ac::Kelvinator::Frame;
    using esp32irpk::ac::Kelvinator::Mode;
    Frame a{};
    a.setPower(true);
    a.setMode(Mode::HEAT);
    a.setTemperatureC(28);
    a.setFan(Fan::MAX_SPEED);
    uint16_t t[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{t, Frame::kMaxTicks, 0};
    EXPECT_TRUE("kelvinator/frombytes-enc", a.toRaw(buf));
    esp32irpk::IRRawTickView v{buf.ticks, buf.len};
    Frame dec{};
    EXPECT_TRUE("kelvinator/frombytes-dec", Frame::fromRaw(v, dec));
    Frame fb{};
    EXPECT_TRUE("kelvinator/frombytes", Frame::fromBytes(dec.bytes, Frame::kBytes, fb));
    EXPECT_TRUE("kelvinator/frombytes-checksum", fb.checksum_ok);
    bool eq = true;
    for (size_t i = 0; i < Frame::kBytes; ++i)
      if (fb.bytes[i] != dec.bytes[i])
        eq = false;
    EXPECT_TRUE("kelvinator/frombytes-eq", eq);
    EXPECT_TRUE("kelvinator/frombytes-badlen", !Frame::fromBytes(dec.bytes, Frame::kBytes - 1, fb));
  }
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
  testAcCodecRoundtrip();
  testPanasonicAcRoundtrip();
  testPanasonicAcHalfDegreeAndLouver();
  testPanasonicAcFanModes();
  testPanasonicAcCanonicalStates();
  testPanasonicAcModels();
  testPanasonicAcDecodesSkewedTiming();
  testGreeAcRoundtrip();
  testGreeAcStateMatrix();
  testGreeAcSwing();
  testMitsubishiAcRoundtrip();
  testMitsubishiAcStateMatrix();
  testMitsubishiAcVaneAndHalfDegree();
  testFujitsuAcRoundtrip();
  testFujitsuAcPowerOff();
  testFujitsuAcStateMatrix();
  testDaikinAcRoundtrip();
  testDaikinAcStateMatrix();
  testToshibaAcRoundtrip();
  testToshibaAcStateMatrix();
  testSamsungAcRoundtrip();
  testSamsungAcStateMatrix();
  testSharpAcRoundtrip();
  testSharpAcStateMatrix();
  testKelvinatorAcRoundtrip();
  testKelvinatorAcStateMatrix();
  testAcDecodeAny();
  testAcEnumToString();
  testAcFromBytes();

  Serial.print("TEST done ");
  Serial.print(g_passed);
  Serial.print("/");
  Serial.println(g_total);
}

void loop()
{
  delay(1);
}
