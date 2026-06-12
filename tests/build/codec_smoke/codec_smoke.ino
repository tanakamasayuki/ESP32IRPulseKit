#include <ESP32IRPulseKit.h>
#include <codec/Encoder.h>

namespace
{
  bool checkNecEncodeDecode()
  {
    esp32irpk::frames::NECFrame frame{};
    frame.address = 0x00ff;
    frame.command = 0x34;

    esp32irpk::IRDecodedBits bits = frame.toBits();

    uint16_t ticks[96]{};
    esp32irpk::IRRawTickBuffer raw{};
    raw.ticks = ticks;
    raw.capacity = sizeof(ticks) / sizeof(ticks[0]);

    const esp32irpk::IRProtocolSpec specs[] = {esp32irpk::specs::NEC};
    if (!esp32irpk::codec::encodeBitsToRaw(bits, specs, 1, raw))
    {
      return false;
    }

    esp32irpk::IRRawTickView view{};
    view.ticks = raw.ticks;
    view.len = raw.len;

    esp32irpk::IRReceiveResult<4> result{};
    if (!esp32irpk::codec::decodeRawToBits(view, specs, 1, 4, 0, result))
    {
      return false;
    }

    return result.count == 1 &&
           result.candidates[0].protocol_id == esp32irpk::IRProtocolID::NEC &&
           result.candidates[0].decoded.bit_length == bits.bit_length &&
           result.candidates[0].decoded.bits == bits.bits;
  }
} // namespace

void setup()
{
  Serial.begin(115200);
  Serial.println(checkNecEncodeDecode() ? "NEC_COMPILE_SMOKE_OK" : "NEC_COMPILE_SMOKE_FAIL");
}

void loop()
{
}
