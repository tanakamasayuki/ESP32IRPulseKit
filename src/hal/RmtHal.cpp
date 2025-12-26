#include "RmtHal.h"
#include "../ESP32IRPulseKit.h"

#include <vector>

#if defined(ESP_PLATFORM)
#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#endif

namespace esp32irpk::hal
{

#if defined(ESP_PLATFORM)

  namespace
  {
    constexpr uint32_t kRmtResolutionHz = 100000; // 1 tick = 10us

    std::vector<rmt_symbol_word_t> toSymbols(const esp32irpk::IRRawTickView &raw, bool mark_high)
    {
      std::vector<rmt_symbol_word_t> syms;
      syms.reserve((raw.len + 1) / 2);
      for (size_t i = 0; i < raw.len; i += 2)
      {
        rmt_symbol_word_t s{};
        uint16_t mark_ticks = raw.ticks[i];
        uint16_t space_ticks = (i + 1 < raw.len) ? raw.ticks[i + 1] : 0;
        s.level0 = mark_high ? 1 : 0;
        s.duration0 = mark_ticks;
        s.level1 = mark_high ? 0 : 1;
        s.duration1 = space_ticks;
        syms.push_back(s);
      }
      return syms;
    }
  } // namespace

  bool RmtTx::begin(int gpio, bool inverted)
  {
    if (begun_)
      return false;
    gpio_ = gpio;
    inverted_ = inverted;

    rmt_tx_channel_config_t cfg = {};
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.gpio_num = static_cast<gpio_num_t>(gpio_);
    cfg.mem_block_symbols = 64;
    cfg.resolution_hz = kRmtResolutionHz;
    cfg.trans_queue_depth = 2;
    cfg.flags.invert_out = inverted_;

    if (rmt_new_tx_channel(&cfg, reinterpret_cast<rmt_channel_handle_t *>(&tx_channel_)) != ESP_OK)
      return false;

    rmt_copy_encoder_config_t enc_cfg = {};
    if (rmt_new_copy_encoder(&enc_cfg, reinterpret_cast<rmt_encoder_handle_t *>(&tx_encoder_)) != ESP_OK)
    {
      rmt_del_channel(reinterpret_cast<rmt_channel_handle_t>(tx_channel_));
      tx_channel_ = nullptr;
      return false;
    }

    if (rmt_enable(reinterpret_cast<rmt_channel_handle_t>(tx_channel_)) != ESP_OK)
    {
      end();
      return false;
    }

    begun_ = true;
    return true;
  }

  void RmtTx::end()
  {
    if (tx_channel_)
    {
      rmt_disable(reinterpret_cast<rmt_channel_handle_t>(tx_channel_));
      rmt_del_channel(reinterpret_cast<rmt_channel_handle_t>(tx_channel_));
      tx_channel_ = nullptr;
    }
    if (tx_encoder_)
    {
      rmt_del_encoder(reinterpret_cast<rmt_encoder_handle_t>(tx_encoder_));
      tx_encoder_ = nullptr;
    }
    begun_ = false;
  }

  bool RmtTx::send(const esp32irpk::IRRawTickView &raw, uint8_t repeat_count)
  {
    if (!begun_)
      return false;
    if (!raw.ticks || raw.len == 0)
      return false;

    auto syms = toSymbols(raw, /*mark_high=*/true);
    if (syms.empty())
      return false;

    rmt_transmit_config_t tcfg = {};
    tcfg.loop_count = static_cast<uint32_t>(repeat_count) + 1U;

    esp_err_t err = rmt_transmit(reinterpret_cast<rmt_channel_handle_t>(tx_channel_),
                                 reinterpret_cast<rmt_encoder_handle_t>(tx_encoder_),
                                 syms.data(),
                                 syms.size() * sizeof(rmt_symbol_word_t),
                                 &tcfg);
    if (err != ESP_OK)
      return false;

    err = rmt_tx_wait_all_done(reinterpret_cast<rmt_channel_handle_t>(tx_channel_), -1);
    return err == ESP_OK;
  }

  bool RmtTx::encode(const esp32irpk::IRDecodedBits &decoded, esp32irpk::IRRawTickBuffer &out_raw)
  {
    (void)decoded;
    (void)out_raw;
    return false;
  }

  bool RmtRx::begin(int gpio, bool inverted, uint32_t idle_threshold_us)
  {
    gpio_ = gpio;
    inverted_ = inverted;
    idle_threshold_us_ = idle_threshold_us;
    begun_ = true;
    return true;
  }

  void RmtRx::end()
  {
    begun_ = false;
  }

  bool RmtRx::read(/*out*/ esp32irpk::IRRawTickView &raw)
  {
    (void)raw;
    if (!begun_)
      return false;
    return false;
  }

#else // !ESP_PLATFORM

  bool RmtTx::begin(int gpio, bool inverted)
  {
    gpio_ = gpio;
    inverted_ = inverted;
    begun_ = true;
    return true;
  }

  void RmtTx::end()
  {
    begun_ = false;
  }

  bool RmtTx::send(const esp32irpk::IRRawTickView &raw, uint8_t repeat_count)
  {
    (void)raw;
    (void)repeat_count;
    if (!begun_)
      return false;
    return false;
  }

  bool RmtTx::encode(const esp32irpk::IRDecodedBits &decoded, esp32irpk::IRRawTickBuffer &out_raw)
  {
    (void)decoded;
    (void)out_raw;
    if (!begun_)
      return false;
    return false;
  }

  bool RmtRx::begin(int gpio, bool inverted, uint32_t idle_threshold_us)
  {
    gpio_ = gpio;
    inverted_ = inverted;
    idle_threshold_us_ = idle_threshold_us;
    begun_ = true;
    return true;
  }

  void RmtRx::end()
  {
    begun_ = false;
  }

  bool RmtRx::read(/*out*/ esp32irpk::IRRawTickView &raw)
  {
    (void)raw;
    if (!begun_)
      return false;
    return false;
  }

#endif // ESP_PLATFORM

} // namespace esp32irpk::hal
