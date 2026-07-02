#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"
#include "Panasonic.h"
#include "Gree.h"
#include "Mitsubishi.h"
#include "Fujitsu.h"
#include "Daikin.h"
#include "Toshiba.h"
#include "Samsung.h"
#include "Sharp.h"
#include "Kelvinator.h"
#include "Midea.h"
#include "Carrier.h"
#include "Hitachi.h"
#include "Haier.h"
#include "MitsubishiHeavy.h"
#include "Tcl.h"

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
    TOSHIBA = 6,
    SAMSUNG = 7,
    SHARP = 8,
    KELVINATOR = 9,
    MIDEA = 10,
    CARRIER = 11,
    HITACHI = 12,
    HAIER = 13,
    MITSUBISHI_HEAVY = 14,
    TCL = 15,
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
    // Kelvinator (16-byte, two blocks) before Gree: a Gree frame is structurally
    // one Kelvinator block (same header + B010 footer + block checksum), so Gree's
    // fromRaw would otherwise greedily match a Kelvinator frame's first block.
    // Kelvinator requires the full two-block form, so a real Gree frame fails it
    // and falls through to Gree below.
    Kelvinator::Frame kf;
    if (Kelvinator::Frame::fromRaw(raw, kf))
    {
      if (out)
        kf.printTo(*out);
      return AcVendor::KELVINATOR;
    }
    Gree::Frame gf;
    if (Gree::Frame::fromRaw(raw, gf))
    {
      if (out)
        gf.printTo(*out);
      return AcVendor::GREE;
    }
    // Carrier (CARRIER_AC64) shares the ~9010/4505 Kelvinator/Gree header, so try it
    // here with them; its fixed 0x84 0x55 signature keeps it unambiguous.
    Carrier::Frame cf;
    if (Carrier::Frame::fromRaw(raw, cf))
    {
      if (out)
        cf.printTo(*out);
      return AcVendor::CARRIER;
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
    Toshiba::Frame tf;
    if (Toshiba::Frame::fromRaw(raw, tf))
    {
      if (out)
        tf.printTo(*out);
      return AcVendor::TOSHIBA;
    }
    Samsung::Frame sf;
    if (Samsung::Frame::fromRaw(raw, sf))
    {
      if (out)
        sf.printTo(*out);
      return AcVendor::SAMSUNG;
    }
    Sharp::Frame shf;
    if (Sharp::Frame::fromRaw(raw, shf))
    {
      if (out)
        shf.printTo(*out);
      return AcVendor::SHARP;
    }
    // Midea (48-bit double transmission, second copy bit-inverted). Its header
    // (4480/4480) matches Toshiba's, but the inverted-copy + bit-count gates make
    // it unambiguous, so ordering here is not load-bearing.
    Midea::Frame mif;
    if (Midea::Frame::fromRaw(raw, mif))
    {
      if (out)
        mif.printTo(*out);
      return AcVendor::MIDEA;
    }
    // Hitachi (28-byte) shares the ~3300/1700 header with several vendors above, so
    // it is last; its fixed 9-byte framing prefix keeps it unambiguous.
    Hitachi::Frame hf;
    if (Hitachi::Frame::fromRaw(raw, hf))
    {
      if (out)
        hf.printTo(*out);
      return AcVendor::HITACHI;
    }
    // Mitsubishi Heavy (152-bit) gates on its fixed 5-byte AD 51 3C E5 1A
    // signature, so its ~3140/1630 header does not collide with the other
    // ~3400/1700-header vendors.
    MitsubishiHeavy::Frame mhf;
    if (MitsubishiHeavy::Frame::fromRaw(raw, mhf))
    {
      if (out)
        mhf.printTo(*out);
      return AcVendor::MITSUBISHI_HEAVY;
    }
    // TCL (14-byte TCL112AC) gates on its fixed 23 CB 26 signature; its 3000/1650
    // header would otherwise overlap the ~3000-header group.
    Tcl::Frame tcf;
    if (Tcl::Frame::fromRaw(raw, tcf))
    {
      if (out)
        tcf.printTo(*out);
      return AcVendor::TCL;
    }
    // Haier (9-byte) has a distinctive 3000/3000 double-header pre-header, so it is
    // unambiguous; appended last.
    Haier::Frame haf;
    if (Haier::Frame::fromRaw(raw, haf))
    {
      if (out)
        haf.printTo(*out);
      return AcVendor::HAIER;
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
    // Kelvinator before Gree (see decodeAny): a Gree frame is one Kelvinator block.
    Kelvinator::Frame kf;
    if (Kelvinator::Frame::fromRaw(raw, kf))
    {
      printAcStateSnippet(out, "Kelvinator", "esp32irpk::ac::Kelvinator::Frame",
                          kf.bytes, Kelvinator::Frame::kBytes);
      return AcVendor::KELVINATOR;
    }
    Gree::Frame gf;
    if (Gree::Frame::fromRaw(raw, gf))
    {
      printAcStateSnippet(out, "Gree", "esp32irpk::ac::Gree::Frame",
                          gf.bytes, Gree::Frame::kBytes);
      return AcVendor::GREE;
    }
    Carrier::Frame cf;
    if (Carrier::Frame::fromRaw(raw, cf))
    {
      printAcStateSnippet(out, "Carrier", "esp32irpk::ac::Carrier::Frame",
                          cf.bytes, Carrier::Frame::kBytes);
      return AcVendor::CARRIER;
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
    Toshiba::Frame tf;
    if (Toshiba::Frame::fromRaw(raw, tf))
    {
      printAcStateSnippet(out, "Toshiba", "esp32irpk::ac::Toshiba::Frame",
                          tf.bytes, Toshiba::Frame::kBytes);
      return AcVendor::TOSHIBA;
    }
    Samsung::Frame sf;
    if (Samsung::Frame::fromRaw(raw, sf))
    {
      printAcStateSnippet(out, "Samsung", "esp32irpk::ac::Samsung::Frame",
                          sf.bytes, Samsung::Frame::kBytes);
      return AcVendor::SAMSUNG;
    }
    Sharp::Frame shf;
    if (Sharp::Frame::fromRaw(raw, shf))
    {
      printAcStateSnippet(out, "Sharp", "esp32irpk::ac::Sharp::Frame",
                          shf.bytes, Sharp::Frame::kBytes);
      return AcVendor::SHARP;
    }
    Midea::Frame mif;
    if (Midea::Frame::fromRaw(raw, mif))
    {
      printAcStateSnippet(out, "Midea", "esp32irpk::ac::Midea::Frame",
                          mif.bytes, Midea::Frame::kBytes);
      return AcVendor::MIDEA;
    }
    Hitachi::Frame hf;
    if (Hitachi::Frame::fromRaw(raw, hf))
    {
      printAcStateSnippet(out, "Hitachi", "esp32irpk::ac::Hitachi::Frame",
                          hf.bytes, Hitachi::Frame::kBytes);
      return AcVendor::HITACHI;
    }
    MitsubishiHeavy::Frame mhf;
    if (MitsubishiHeavy::Frame::fromRaw(raw, mhf))
    {
      printAcStateSnippet(out, "MitsubishiHeavy", "esp32irpk::ac::MitsubishiHeavy::Frame",
                          mhf.bytes, MitsubishiHeavy::Frame::kBytes);
      return AcVendor::MITSUBISHI_HEAVY;
    }
    Tcl::Frame tcf;
    if (Tcl::Frame::fromRaw(raw, tcf))
    {
      printAcStateSnippet(out, "TCL", "esp32irpk::ac::Tcl::Frame",
                          tcf.bytes, Tcl::Frame::kBytes);
      return AcVendor::TCL;
    }
    Haier::Frame haf;
    if (Haier::Frame::fromRaw(raw, haf))
    {
      printAcStateSnippet(out, "Haier", "esp32irpk::ac::Haier::Frame",
                          haf.bytes, Haier::Frame::kBytes);
      return AcVendor::HAIER;
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
    // Kelvinator before Gree (see decodeAny): a Gree frame is one Kelvinator block.
    Kelvinator::Frame kf;
    if (Kelvinator::Frame::fromRaw(raw, kf))
    {
      kf.printSetterSnippet(out);
      return AcVendor::KELVINATOR;
    }
    Gree::Frame gf;
    if (Gree::Frame::fromRaw(raw, gf))
    {
      gf.printSetterSnippet(out);
      return AcVendor::GREE;
    }
    Carrier::Frame cf;
    if (Carrier::Frame::fromRaw(raw, cf))
    {
      cf.printSetterSnippet(out);
      return AcVendor::CARRIER;
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
    Toshiba::Frame tf;
    if (Toshiba::Frame::fromRaw(raw, tf))
    {
      tf.printSetterSnippet(out);
      return AcVendor::TOSHIBA;
    }
    Samsung::Frame sf;
    if (Samsung::Frame::fromRaw(raw, sf))
    {
      sf.printSetterSnippet(out);
      return AcVendor::SAMSUNG;
    }
    Sharp::Frame shf;
    if (Sharp::Frame::fromRaw(raw, shf))
    {
      shf.printSetterSnippet(out);
      return AcVendor::SHARP;
    }
    Midea::Frame mif;
    if (Midea::Frame::fromRaw(raw, mif))
    {
      mif.printSetterSnippet(out);
      return AcVendor::MIDEA;
    }
    Hitachi::Frame hf;
    if (Hitachi::Frame::fromRaw(raw, hf))
    {
      hf.printSetterSnippet(out);
      return AcVendor::HITACHI;
    }
    MitsubishiHeavy::Frame mhf;
    if (MitsubishiHeavy::Frame::fromRaw(raw, mhf))
    {
      mhf.printSetterSnippet(out);
      return AcVendor::MITSUBISHI_HEAVY;
    }
    Tcl::Frame tcf;
    if (Tcl::Frame::fromRaw(raw, tcf))
    {
      tcf.printSetterSnippet(out);
      return AcVendor::TCL;
    }
    Haier::Frame haf;
    if (Haier::Frame::fromRaw(raw, haf))
    {
      haf.printSetterSnippet(out);
      return AcVendor::HAIER;
    }
    return AcVendor::UNKNOWN;
  }

} // namespace esp32irpk::ac
