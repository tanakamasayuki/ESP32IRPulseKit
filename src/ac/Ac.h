#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"
#include "Panasonic.h"
#include "Gree.h"
#include "Mitsubishi.h"
#include "Fujitsu.h"
#include "Daikin.h"

// Air-conditioner support layer. AC frames are multi-byte vendor state that
// does not fit the generic 64-bit IRDecodedBits codec, so this layer works on
// the RAW tick path instead. See SPEC §11.

namespace esp32irpk::ac
{

  enum class AcVendor : uint16_t
  {
    UNKNOWN = 0,
    PANASONIC = 1,
    GREE = 2,
    MITSUBISHI = 3,
    FUJITSU = 4,
    DAIKIN = 5,
    // further vendors added incrementally
  };

  // One-call send: encode any vendor Frame into a stack buffer sized by
  // Frame::kMaxTicks and transmit it. Returns false on encode or send failure.
  // Configure the sender's carrier mode (setPhaseAlignedCarrier(false) for long
  // frames) separately, as usual.
  template <class Frame>
  bool send(esp32irpk::IRSender &tx, const Frame &frame)
  {
    uint16_t ticks[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{ticks, Frame::kMaxTicks, 0};
    if (!frame.toRaw(buf))
      return false;
    return tx.send(esp32irpk::IRRawTickView{buf.ticks, buf.len});
  }

  // Try each built-in AC vendor against the RAW capture, in registration order,
  // and return which one matched (UNKNOWN if none). When `out` is non-null the
  // matched frame is also dumped via its printTo() (and a no-match note is
  // printed). This centralizes the vendor list so a learn/dump path picks up new
  // vendors automatically. It reports only the vendor identity, not the decoded
  // frame, because the per-vendor Frames are heterogeneous types — decode the
  // specific vendor (Vendor::Frame::fromRaw) when you need its fields or to
  // re-encode.
  inline AcVendor decodeAny(const esp32irpk::IRRawTickView &raw, Print *out = nullptr)
  {
    Panasonic::Frame pf;
    if (Panasonic::Frame::fromRaw(raw, pf))
    {
      if (out)
        pf.printTo(*out);
      return AcVendor::PANASONIC;
    }
    Gree::Frame gf;
    if (Gree::Frame::fromRaw(raw, gf))
    {
      if (out)
        gf.printTo(*out);
      return AcVendor::GREE;
    }
    Mitsubishi::Frame mf;
    if (Mitsubishi::Frame::fromRaw(raw, mf))
    {
      if (out)
        mf.printTo(*out);
      return AcVendor::MITSUBISHI;
    }
    Fujitsu::Frame ff;
    if (Fujitsu::Frame::fromRaw(raw, ff))
    {
      if (out)
        ff.printTo(*out);
      return AcVendor::FUJITSU;
    }
    Daikin::Frame df;
    if (Daikin::Frame::fromRaw(raw, df))
    {
      if (out)
        df.printTo(*out);
      return AcVendor::DAIKIN;
    }
    if (out)
      out->println("// decoded: no AC vendor matched (raw replay still works)");
    return AcVendor::UNKNOWN;
  }

  // Decode the matching vendor and print copy-paste C++ that rebuilds the frame
  // from its decoded state bytes (the compact, bit-exact alternative to a RAW
  // tick dump). Returns the matched vendor, or UNKNOWN if none matched (in which
  // case nothing is printed — fall back to a RAW snippet). Mirrors decodeAny so
  // the vendor list stays in one place.
  inline AcVendor printSendSnippet(const esp32irpk::IRRawTickView &raw, Print &out)
  {
    Panasonic::Frame pf;
    if (Panasonic::Frame::fromRaw(raw, pf))
    {
      printAcStateSnippet(out, "Panasonic", "esp32irpk::ac::Panasonic::Frame",
                          pf.bytes, Panasonic::Frame::kBytes);
      return AcVendor::PANASONIC;
    }
    Gree::Frame gf;
    if (Gree::Frame::fromRaw(raw, gf))
    {
      printAcStateSnippet(out, "Gree", "esp32irpk::ac::Gree::Frame",
                          gf.bytes, Gree::Frame::kBytes);
      return AcVendor::GREE;
    }
    Mitsubishi::Frame mf;
    if (Mitsubishi::Frame::fromRaw(raw, mf))
    {
      printAcStateSnippet(out, "Mitsubishi", "esp32irpk::ac::Mitsubishi::Frame",
                          mf.bytes, Mitsubishi::Frame::kBytes);
      return AcVendor::MITSUBISHI;
    }
    Fujitsu::Frame ff;
    if (Fujitsu::Frame::fromRaw(raw, ff))
    {
      // Dump byte_length, not kBytes: a power-off capture is a 7-byte short frame.
      printAcStateSnippet(out, "Fujitsu", "esp32irpk::ac::Fujitsu::Frame",
                          ff.bytes, ff.byte_length);
      return AcVendor::FUJITSU;
    }
    Daikin::Frame df;
    if (Daikin::Frame::fromRaw(raw, df))
    {
      printAcStateSnippet(out, "Daikin", "esp32irpk::ac::Daikin::Frame",
                          df.bytes, Daikin::Frame::kBytes);
      return AcVendor::DAIKIN;
    }
    return AcVendor::UNKNOWN;
  }

  // Decode the matching vendor and print an editable setter template (see each
  // Frame's printSetterSnippet) — lossy but easy to tweak, the counterpart to
  // the bit-exact printSendSnippet. Returns the matched vendor (UNKNOWN if none;
  // nothing printed). Mirrors decodeAny so the vendor list stays in one place.
  inline AcVendor printSetterSnippet(const esp32irpk::IRRawTickView &raw, Print &out)
  {
    Panasonic::Frame pf;
    if (Panasonic::Frame::fromRaw(raw, pf))
    {
      pf.printSetterSnippet(out);
      return AcVendor::PANASONIC;
    }
    Gree::Frame gf;
    if (Gree::Frame::fromRaw(raw, gf))
    {
      gf.printSetterSnippet(out);
      return AcVendor::GREE;
    }
    Mitsubishi::Frame mf;
    if (Mitsubishi::Frame::fromRaw(raw, mf))
    {
      mf.printSetterSnippet(out);
      return AcVendor::MITSUBISHI;
    }
    Fujitsu::Frame ff;
    if (Fujitsu::Frame::fromRaw(raw, ff))
    {
      ff.printSetterSnippet(out);
      return AcVendor::FUJITSU;
    }
    Daikin::Frame df;
    if (Daikin::Frame::fromRaw(raw, df))
    {
      df.printSetterSnippet(out);
      return AcVendor::DAIKIN;
    }
    return AcVendor::UNKNOWN;
  }

} // namespace esp32irpk::ac
