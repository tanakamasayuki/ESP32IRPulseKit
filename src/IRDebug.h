#pragma once

#include "ESP32IRPulseKit.h"

// Optional Serial-formatting helpers shared by the learn/dump sketches. These
// format the core types (RAW ticks, decode candidates, decoded frames) to any
// Arduino Print, so examples and on-board studies do not each reimplement the
// same boilerplate. This header is opt-in: include <IRDebug.h> in addition to
// <ESP32IRPulseKit.h> when you want it. None of it is part of the send/receive
// contract; it is diagnostics only.

namespace esp32irpk::debug
{

  // RAW capture as a space-separated microsecond list: "raw (us): 9000 4500 ...".
  inline void printRawMicros(Print &out, const esp32irpk::IRRawTickView &raw)
  {
    out.print("raw (us):");
    for (size_t i = 0; i < raw.len; ++i)
    {
      out.print(' ');
      out.print((unsigned)(raw.ticks[i] * 10)); // 1 tick = 10us
    }
    out.println();
  }

  // 64-bit value as two 32-bit hex halves for display (no "0x", no padding).
  inline void printHexU64(Print &out, uint64_t v)
  {
    out.print((uint32_t)(v >> 32), HEX);
    out.print((uint32_t)(v & 0xFFFFFFFFu), HEX);
  }

  // 64-bit value as a valid C++ literal, e.g. 0x1234ULL (low half zero-padded
  // when a high half is present so the literal round-trips).
  inline void printHexU64Literal(Print &out, uint64_t v)
  {
    uint32_t hi = (uint32_t)(v >> 32);
    uint32_t lo = (uint32_t)(v & 0xFFFFFFFFu);
    out.print("0x");
    if (hi)
    {
      out.print(hi, HEX);
      char buf[9];
      snprintf(buf, sizeof(buf), "%08lx", (unsigned long)lo);
      out.print(buf);
    }
    else
    {
      out.print(lo, HEX);
    }
    out.print("ULL");
  }

  // One-line summary of a decode candidate:
  //   "#0 pid=1 protocol=NEC score=-12 len=32 bits=0x..... frame_type=NORMAL"
  inline void printDecodedCandidate(Print &out, unsigned index,
                                    const esp32irpk::IRDecodeCandidate &c)
  {
    const esp32irpk::IRDecodedBits &b = c.decoded;
    out.print('#');
    out.print(index);
    out.print(" pid=");
    out.print((unsigned)c.protocol_id);
    out.print(" protocol=");
    out.print(c.name);
    out.print(" score=");
    out.print((int)c.score);
    out.print(" len=");
    out.print((unsigned)b.bit_length);
    out.print(" bits=0x");
    printHexU64(out, b.bits);
    out.print(" frame_type=");
    out.println(b.frame_type == esp32irpk::IRFrameType::REPEAT ? "REPEAT" : "NORMAL");
  }

  // Per-protocol named-field decode for the generic (non-AC) protocols: turns
  // IRDecodedBits into the matching frames::*Frame and prints its fields. A
  // protocol with no named frame prints nothing.
  inline void printDecodedFrame(Print &out, const esp32irpk::IRDecodedBits &b)
  {
    switch (b.protocol_id)
    {
    case esp32irpk::IRProtocolID::NEC:
    {
      esp32irpk::frames::NECFrame f = esp32irpk::frames::NECFrame::fromBits(b);
      if (f.is_repeat)
      {
        out.println("  frame: NEC REPEAT");
        break;
      }
      out.print("  frame: NEC addr=0x");
      out.print(f.address, HEX);
      out.print(" cmd=0x");
      out.println(f.command, HEX);
      break;
    }
    case esp32irpk::IRProtocolID::AEHA:
    {
      esp32irpk::frames::AEHAFrame f = esp32irpk::frames::AEHAFrame::fromBits(b);
      if (f.is_repeat)
      {
        out.println("  frame: AEHA REPEAT");
        break;
      }
      out.print("  frame: AEHA bits=");
      out.print(f.bit_length);
      out.print(" data=0x");
      out.println((uint32_t)(f.data & 0xFFFFFFFFu), HEX);
      break;
    }
    case esp32irpk::IRProtocolID::SONY12:
    {
      esp32irpk::frames::Sony12Frame f = esp32irpk::frames::Sony12Frame::fromBits(b);
      if (f.is_repeat)
      {
        out.println("  frame: SONY12 REPEAT");
        break;
      }
      out.print("  frame: SONY12 data=0x");
      out.println(f.data, HEX);
      break;
    }
    case esp32irpk::IRProtocolID::SONY15:
    {
      esp32irpk::frames::Sony15Frame f = esp32irpk::frames::Sony15Frame::fromBits(b);
      if (f.is_repeat)
      {
        out.println("  frame: SONY15 REPEAT");
        break;
      }
      out.print("  frame: SONY15 data=0x");
      out.println(f.data, HEX);
      break;
    }
    case esp32irpk::IRProtocolID::SONY20:
    {
      esp32irpk::frames::Sony20Frame f = esp32irpk::frames::Sony20Frame::fromBits(b);
      if (f.is_repeat)
      {
        out.println("  frame: SONY20 REPEAT");
        break;
      }
      out.print("  frame: SONY20 data=0x");
      out.println(f.data, HEX);
      break;
    }
    case esp32irpk::IRProtocolID::SAMSUNG32:
    {
      esp32irpk::frames::Samsung32Frame f = esp32irpk::frames::Samsung32Frame::fromBits(b);
      if (f.is_repeat)
      {
        out.println("  frame: SAMSUNG32 REPEAT");
        break;
      }
      out.print("  frame: SAMSUNG32 addr=0x");
      out.print(f.address, HEX);
      out.print(" cmd=0x");
      out.println(f.command, HEX);
      break;
    }
    case esp32irpk::IRProtocolID::SAMSUNG36:
    {
      esp32irpk::frames::Samsung36Frame f = esp32irpk::frames::Samsung36Frame::fromBits(b);
      if (f.is_repeat)
      {
        out.println("  frame: SAMSUNG36 REPEAT");
        break;
      }
      out.print("  frame: SAMSUNG36 addr=0x");
      out.print(f.address, HEX);
      out.print(" cmd=0x");
      out.println(f.command, HEX);
      break;
    }
    case esp32irpk::IRProtocolID::JVC:
    {
      esp32irpk::frames::JVCFrame f = esp32irpk::frames::JVCFrame::fromBits(b);
      if (f.is_repeat)
      {
        out.println("  frame: JVC REPEAT");
        break;
      }
      out.print("  frame: JVC addr=0x");
      out.print(f.address, HEX);
      out.print(" cmd=0x");
      out.println(f.command, HEX);
      break;
    }
    case esp32irpk::IRProtocolID::RC5:
    {
      esp32irpk::frames::RC5Frame f = esp32irpk::frames::RC5Frame::fromBits(b);
      out.print("  frame: RC5 data=0x");
      out.println(f.data, HEX);
      break;
    }
    case esp32irpk::IRProtocolID::RC6_M0_16:
    {
      esp32irpk::frames::RC6M0Frame f = esp32irpk::frames::RC6M0Frame::fromBits(b);
      out.print("  frame: RC6_M0_16 data=0x");
      out.println(f.data, HEX);
      break;
    }
    case esp32irpk::IRProtocolID::RC6_M6_32:
    {
      esp32irpk::frames::RC6M6Frame f = esp32irpk::frames::RC6M6Frame::fromBits(b);
      out.print("  frame: RC6_M6_32 data=0x");
      out.println((uint32_t)(f.data & 0xFFFFFFFFu), HEX);
      break;
    }
    default:
      break;
    }
  }

  // Copy-paste C++ that re-sends a decoded candidate via IRDecodedBits. Uniform
  // across protocols and reproduces the exact decoded payload.
  inline void printBitsSendSnippet(Print &out, const esp32irpk::IRDecodeCandidate &c)
  {
    const esp32irpk::IRDecodedBits &b = c.decoded;
    out.println("// send code (decoded):");
    out.println("esp32irpk::IRDecodedBits bits{};");
    out.print("bits.protocol_id = esp32irpk::IRProtocolID::");
    out.print(c.name); // spec name matches the enum id
    out.println(";");
    out.print("bits.frame_type = esp32irpk::IRFrameType::");
    out.println(b.frame_type == esp32irpk::IRFrameType::REPEAT ? "REPEAT;" : "NORMAL;");
    out.print("bits.bit_length = ");
    out.print((unsigned)b.bit_length);
    out.println(";");
    out.print("bits.bits = ");
    printHexU64Literal(out, b.bits);
    out.println(";");
    out.println("tx.send(bits);");
  }

  // Copy-paste C++ that re-sends the captured RAW as a tick array. Works for any
  // waveform (including long AC bursts). The caller prints any protocol-specific
  // note (e.g. the AC phase-aligned-carrier reminder) before calling this.
  inline void printRawSendSnippet(Print &out, const esp32irpk::IRRawTickView &raw)
  {
    out.println("// send code (raw replay):");
    out.print("const uint16_t ticks[] = {");
    for (size_t i = 0; i < raw.len; ++i)
    {
      if (i)
        out.print(", ");
      out.print((unsigned)raw.ticks[i]); // 1 tick = 10us
    }
    out.println("};");
    out.print("tx.send({ticks, ");
    out.print((unsigned)raw.len);
    out.println("});");
  }

} // namespace esp32irpk::debug
