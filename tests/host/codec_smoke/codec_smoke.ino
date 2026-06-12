#include <ESP32IRPulseKit.h>
#include <codec/Encoder.h>

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
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);

  testNecEncodeDecodeRoundtrip();
  testNecRejectsUndersizedBuffer();

  Serial.print("TEST done ");
  Serial.print(g_passed);
  Serial.print("/");
  Serial.println(g_total);
}

void loop()
{
  delay(1);
}
