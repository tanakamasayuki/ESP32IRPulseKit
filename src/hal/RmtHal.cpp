#include "RmtHal.h"
#include "../ESP32IRPulseKit.h"

#include <algorithm>
#include <vector>
#include <utility>

#if defined(ESP_PLATFORM)
#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <soc/soc_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#endif

namespace esp32irpk::hal
{

#if defined(ESP_PLATFORM)

  namespace
  {
    constexpr const char *kTag = "ESP32IRPulseKit";
    // RMT resolution: 100kHz (1 tick = 10us). Clock source is selected per SoC.
    constexpr uint32_t kRmtResolutionHz = 100000; // 1 tick = 10us (RMT resolution)
    // Fine resolution for the phase-aligned carrier path: 1MHz (1 tick = 1us),
    // so a carrier half-cycle (~13us at 38kHz) is representable as RMT symbols.
    constexpr uint32_t kRmtResolutionHzFine = 1000000; // 1 tick = 1us
    constexpr uint32_t kRmtTickUs = 10;
    constexpr uint32_t kDefaultCarrierFrequencyHz = 38000;
    constexpr size_t kMaxRxSymbols = 256;
    constexpr size_t kMaxQueuedFrames = 4;
    constexpr uint32_t kTickUs = 10; // library tick = 10us

    rmt_clock_source_t selectRmtClkSrc()
    {
#if defined(SOC_RMT_SUPPORT_REF_TICK) && SOC_RMT_SUPPORT_REF_TICK
      ESP_LOGV(kTag, "selectRmtClkSrc : RMT_CLK_SRC_REF_TICK");
      return RMT_CLK_SRC_REF_TICK;
#else
      ESP_LOGV(kTag, "selectRmtClkSrc : RMT_CLK_SRC_DEFAULT");
      return RMT_CLK_SRC_DEFAULT;
#endif
    }

    std::vector<rmt_symbol_word_t> toSymbols(const esp32irpk::IRRawTickView &raw, bool mark_high)
    {
      constexpr uint16_t kMaxDuration = 0x7FFF; // RMT duration field limit
      std::vector<rmt_symbol_word_t> syms;
      syms.reserve((raw.len + 1) / 2);
      for (size_t i = 0; i < raw.len; i += 2)
      {
        rmt_symbol_word_t s{};
        uint32_t mark_ticks = raw.ticks[i];
        uint32_t space_ticks = (i + 1 < raw.len) ? static_cast<uint32_t>(raw.ticks[i + 1]) : 0;
        if (mark_ticks > kMaxDuration)
          mark_ticks = kMaxDuration;
        if (space_ticks > kMaxDuration)
          space_ticks = kMaxDuration;
        s.level0 = mark_high ? 1 : 0;
        s.duration0 = static_cast<uint16_t>(mark_ticks);
        s.level1 = mark_high ? 0 : 1;
        s.duration1 = static_cast<uint16_t>(space_ticks);
        syms.push_back(s);
      }
      return syms;
    }

    // Phase-aligned carrier path. Channel runs at 1us resolution. Each mark is
    // expanded into an integer number of full carrier cycles starting at phase 0
    // (level high then low), so the demodulated mark length is deterministic and
    // free of the free-running-carrier ±1-cycle wobble. Spaces (and solid marks
    // when carrier_hz == 0) are emitted as level runs. Durations are in 1us ticks.
    std::vector<rmt_symbol_word_t> toCarrierSymbols(const esp32irpk::IRRawTickView &raw,
                                                    uint32_t carrier_hz, float duty,
                                                    bool mark_high)
    {
      constexpr uint32_t kMaxDur = 0x7FFF; // RMT duration field limit (1us units)
      std::vector<rmt_symbol_word_t> syms;
      const uint8_t mark_lvl = mark_high ? 1 : 0;
      const uint8_t space_lvl = mark_high ? 0 : 1;

      // Carrier cell in 1us ticks (duty = high fraction). 0 -> solid marks.
      uint32_t period = (carrier_hz > 0) ? (1000000u + carrier_hz / 2) / carrier_hz : 0;
      uint16_t hi = 0, lo = 0;
      if (period > 0)
      {
        if (period > 2 * kMaxDur)
          period = 2 * kMaxDur;
        uint32_t high = static_cast<uint32_t>(period * duty + 0.5f);
        if (high < 1)
          high = 1;
        if (high >= period)
          high = period - 1;
        hi = static_cast<uint16_t>(high);
        lo = static_cast<uint16_t>(period - high);
      }

      // Emit a single-level run of `us` microseconds, packing two halves per
      // symbol and never emitting a zero duration mid-stream (which the RMT HW
      // treats as an end marker).
      auto pushRun = [&](uint8_t level, uint32_t us)
      {
        while (us > 0)
        {
          uint32_t d0 = std::min<uint32_t>(us, kMaxDur);
          us -= d0;
          uint32_t d1 = std::min<uint32_t>(us, kMaxDur);
          us -= d1;
          if (d1 == 0 && d0 > 1)
          {
            d1 = d0 / 2;
            d0 -= d1;
          }
          rmt_symbol_word_t s{};
          s.level0 = level;
          s.duration0 = static_cast<uint16_t>(d0);
          s.level1 = level;
          s.duration1 = static_cast<uint16_t>(d1 ? d1 : d0);
          syms.push_back(s);
        }
      };

      for (size_t i = 0; i < raw.len; i += 2)
      {
        uint32_t mark_us = static_cast<uint32_t>(raw.ticks[i]) * kRmtTickUs;
        if (period > 0)
        {
          uint32_t cycles = (mark_us + period / 2) / period;
          if (cycles < 1)
            cycles = 1;
          for (uint32_t c = 0; c < cycles; ++c)
          {
            rmt_symbol_word_t s{};
            s.level0 = mark_lvl;
            s.duration0 = hi;
            s.level1 = space_lvl;
            s.duration1 = lo;
            syms.push_back(s);
          }
        }
        else
        {
          pushRun(mark_lvl, mark_us);
        }
        if (i + 1 < raw.len)
        {
          uint32_t space_us = static_cast<uint32_t>(raw.ticks[i + 1]) * kRmtTickUs;
          if (space_us > 0)
            pushRun(space_lvl, space_us);
        }
      }
      return syms;
    }
  } // namespace

  bool RmtTx::setCarrierMode(TxCarrierMode mode)
  {
    if (begun_)
      return false; // resolution is fixed at channel creation
    carrier_mode_ = mode;
    return true;
  }

  bool RmtTx::setTxMemBlocks(uint8_t blocks)
  {
    if (begun_)
      return false; // channel memory is fixed at channel creation
    tx_mem_blocks_ = blocks;
    return true;
  }

  bool RmtTx::begin(int gpio, bool inverted)
  {
    if (begun_)
      return false;
    gpio_ = gpio;
    inverted_ = inverted;

    const bool phase_aligned = (carrier_mode_ == TxCarrierMode::PhaseAligned);

    // One RMT block = SOC_RMT_MEM_WORDS_PER_CHANNEL symbols (64 on ESP32/S2,
    // 48 elsewhere). PhaseAligned streams ~1 symbol per carrier cycle, so it
    // wants more blocks for refill headroom; the hardware path keeps one block.
    // setTxMemBlocks() overrides the default but stays within the shared TX pool.
    uint32_t mem_words;
    if (tx_mem_blocks_ > 0)
      mem_words = static_cast<uint32_t>(tx_mem_blocks_) * SOC_RMT_MEM_WORDS_PER_CHANNEL;
    else
      mem_words = (phase_aligned ? 2u : 1u) * SOC_RMT_MEM_WORDS_PER_CHANNEL;

    rmt_tx_channel_config_t cfg = {};
    cfg.clk_src = selectRmtClkSrc();
    cfg.gpio_num = static_cast<gpio_num_t>(gpio_);
    cfg.mem_block_symbols = mem_words;
    cfg.resolution_hz = phase_aligned ? kRmtResolutionHzFine : kRmtResolutionHz;
    cfg.trans_queue_depth = 2;
    cfg.flags.invert_out = inverted_;

    if (rmt_new_tx_channel(&cfg, reinterpret_cast<rmt_channel_handle_t *>(&tx_channel_)) != ESP_OK)
      return false;

    // PhaseAligned generates the carrier in the symbol stream, so the HW carrier
    // (rmt_apply_carrier) stays disabled; we only track the requested frequency.
    if (phase_aligned)
    {
      carrier_hz_ = kDefaultCarrierFrequencyHz;
    }
    else if (!applyCarrierHz(kDefaultCarrierFrequencyHz))
    {
      rmt_del_channel(reinterpret_cast<rmt_channel_handle_t>(tx_channel_));
      tx_channel_ = nullptr;
      carrier_hz_ = 0;
      return false;
    }

    rmt_copy_encoder_config_t enc_cfg = {};
    if (rmt_new_copy_encoder(&enc_cfg, reinterpret_cast<rmt_encoder_handle_t *>(&tx_encoder_)) != ESP_OK)
    {
      rmt_del_channel(reinterpret_cast<rmt_channel_handle_t>(tx_channel_));
      tx_channel_ = nullptr;
      carrier_hz_ = 0;
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
    sending_ = false;
    carrier_hz_ = 0;
    begun_ = false;
  }

  bool RmtTx::applyCarrierHz(uint32_t carrier_hz)
  {
    if (!tx_channel_)
      return false;
    // PhaseAligned encodes the carrier in the symbol stream; just track the
    // requested frequency (0 = solid marks) and skip the HW carrier.
    if (carrier_mode_ == TxCarrierMode::PhaseAligned)
    {
      carrier_hz_ = carrier_hz;
      return true;
    }
    // Re-apply when the frequency changes, or when the duty changed while a
    // carrier is active (same frequency but a new duty from setCarrierDuty()).
    if (carrier_hz == carrier_hz_ &&
        (carrier_hz == 0 || carrier_duty_ == applied_duty_))
      return true;

    rmt_carrier_config_t carrier_cfg = {};
    const rmt_carrier_config_t *cfg_ptr = nullptr;
    if (carrier_hz != 0)
    {
      carrier_cfg.frequency_hz = carrier_hz;
      carrier_cfg.duty_cycle = carrier_duty_;
      carrier_cfg.flags.polarity_active_low = false;
      carrier_cfg.flags.always_on = false;
      cfg_ptr = &carrier_cfg;
    }
    // carrier_hz == 0 -> pass NULL, which disables carrier modulation
    // (solid marks), e.g. for wired-loopback / raw-waveform testing.
    if (rmt_apply_carrier(reinterpret_cast<rmt_channel_handle_t>(tx_channel_), cfg_ptr) != ESP_OK)
      return false;

    carrier_hz_ = carrier_hz;
    applied_duty_ = (carrier_hz != 0) ? carrier_duty_ : -1.0f;
    return true;
  }

  bool RmtTx::setCarrierDuty(float duty)
  {
    if (!(duty > 0.0f && duty < 1.0f))
      return false;
    carrier_duty_ = duty; // applied on the next applyCarrierHz() (i.e. next send)
    return true;
  }

  bool RmtTx::send(const esp32irpk::IRRawTickView &raw, int8_t repeat_count, uint32_t carrier_hz)
  {
    if (!begun_)
      return false;
    if (!raw.ticks || raw.len == 0)
      return false;
    if (!applyCarrierHz(carrier_hz))
      return false;

    auto syms = (carrier_mode_ == TxCarrierMode::PhaseAligned)
                    ? toCarrierSymbols(raw, carrier_hz, carrier_duty_, /*mark_high=*/true)
                    : toSymbols(raw, /*mark_high=*/true);
    if (syms.empty())
      return false;

    rmt_transmit_config_t tcfg = {};
    uint32_t loops = repeat_count < 0 ? 1U : static_cast<uint32_t>(repeat_count) + 1U;
    tcfg.loop_count = loops;

    sending_ = true;
    esp_err_t err = rmt_transmit(reinterpret_cast<rmt_channel_handle_t>(tx_channel_),
                                 reinterpret_cast<rmt_encoder_handle_t>(tx_encoder_),
                                 syms.data(),
                                 syms.size() * sizeof(rmt_symbol_word_t),
                                 &tcfg);
    if (err != ESP_OK)
    {
      sending_ = false;
      return false;
    }

    err = rmt_tx_wait_all_done(reinterpret_cast<rmt_channel_handle_t>(tx_channel_), -1);
    sending_ = false;
    return err == ESP_OK;
  }

  bool RmtRx::begin(int gpio, bool inverted, uint32_t idle_threshold_us)
  {
    if (begun_)
      return false;
    gpio_ = gpio;
    inverted_ = inverted;
    idle_threshold_us_ = idle_threshold_us;
    ESP_LOGV(kTag, "RMT RX begin: gpio=%d inverted=%d idle_threshold_us=%u tick_us=%u",
             gpio_, inverted_ ? 1 : 0, idle_threshold_us_, kRmtTickUs);

    rmt_rx_channel_config_t cfg = {};
    cfg.clk_src = selectRmtClkSrc();
    cfg.gpio_num = static_cast<gpio_num_t>(gpio_);
    cfg.mem_block_symbols = 64;
    cfg.resolution_hz = kRmtResolutionHz;
    cfg.flags.invert_in = inverted_;
    cfg.flags.with_dma = false;

    if (rmt_new_rx_channel(&cfg, reinterpret_cast<rmt_channel_handle_t *>(&rx_channel_)) != ESP_OK)
      return false;

    rmt_rx_event_callbacks_t cbs = {};
    cbs.on_recv_done = [](rmt_channel_handle_t channel,
                          const rmt_rx_done_event_data_t *edata,
                          void *user_ctx) -> bool
    {
      (void)channel;
      auto *self = static_cast<RmtRx *>(user_ctx);
      return self ? self->handleRecvDone(edata) : false;
    };
    if (rmt_rx_register_event_callbacks(reinterpret_cast<rmt_channel_handle_t>(rx_channel_), &cbs, this) != ESP_OK)
    {
      rmt_del_channel(reinterpret_cast<rmt_channel_handle_t>(rx_channel_));
      rx_channel_ = nullptr;
      return false;
    }

    if (rmt_enable(reinterpret_cast<rmt_channel_handle_t>(rx_channel_)) != ESP_OK)
    {
      end();
      return false;
    }

    rx_sem_ = xSemaphoreCreateBinary();
    if (!rx_sem_)
    {
      end();
      return false;
    }

    last_truncated_ = false;
    last_overflow_ = false;
    sym_count_ = 0;
    has_frame_ = false;
    queue_.clear();
    current_ = RxFrame{};
    queue_overflow_count_ = 0;
    if (!startReceiveInternal())
    {
      end();
      return false;
    }
    begun_ = true;
    return true;
  }

  void RmtRx::end()
  {
    if (rx_channel_)
    {
      rmt_disable(reinterpret_cast<rmt_channel_handle_t>(rx_channel_));
      rmt_del_channel(reinterpret_cast<rmt_channel_handle_t>(rx_channel_));
      rx_channel_ = nullptr;
    }
    if (rx_sem_)
    {
      vSemaphoreDelete(static_cast<SemaphoreHandle_t>(rx_sem_));
      rx_sem_ = nullptr;
    }
    sym_count_ = 0;
    has_frame_ = false;
    receiving_ = false;
    queue_.clear();
    current_ = RxFrame{};
    queue_overflow_count_ = 0;
    last_truncated_ = false;
    last_overflow_ = false;
    begun_ = false;
  }

  bool RmtRx::read(/*out*/ esp32irpk::IRRawTickView &raw)
  {
    raw = {};
    if (!begun_)
      return false;

    drainRxQueue();
    if (!hasCurrent())
    {
      if (!loadNextFrame())
        return false;
    }

    raw.ticks = current_.ticks.data() + current_.start;
    raw.len = current_.ticks.size() - current_.start;
    return raw.len > 0;
  }

  bool RmtRx::startReceive()
  {
    return startReceiveInternal();
  }

  void RmtRx::drainRxQueue()
  {
    if (!rx_sem_)
      return;

    while (xSemaphoreTake(static_cast<SemaphoreHandle_t>(rx_sem_), 0) == pdTRUE)
    {
      has_frame_ = false;
      if (sym_count_ == 0)
      {
        startReceiveInternal();
        continue;
      }

      RxFrame frame{};
      frame.truncated = last_truncated_;
      frame.overflow = last_overflow_;
      last_truncated_ = false;
      last_overflow_ = false;

      auto *sym = reinterpret_cast<const rmt_symbol_word_t *>(sym_buf_.data());
      frame.ticks.reserve(sym_count_ * 2);
      for (size_t i = 0; i < sym_count_; ++i)
      {
        const auto &s = sym[i];
        uint32_t t0 = static_cast<uint32_t>(s.duration0);
        if (t0 > 0xFFFF)
          t0 = 0xFFFF;
        frame.ticks.push_back(static_cast<uint16_t>(t0));
        if (s.duration1 > 0)
        {
          uint32_t t1 = static_cast<uint32_t>(s.duration1);
          if (t1 > 0xFFFF)
            t1 = 0xFFFF;
          frame.ticks.push_back(static_cast<uint16_t>(t1));
        }
      }
      sym_count_ = 0;

      ESP_LOGV(kTag, "RMT RX: symbols=%zu ticks=%zu truncated=%d overflow=%d",
               frame.ticks.size() / 2 + frame.ticks.size() % 2,
               frame.ticks.size(),
               frame.truncated ? 1 : 0,
               frame.overflow ? 1 : 0);

      if (queue_.size() >= kMaxQueuedFrames)
      {
        queue_.pop_front();
        ++queue_overflow_count_;
      }
      queue_.push_back(std::move(frame));

      startReceiveInternal();
    }
  }

  bool RmtRx::hasCurrent() const
  {
    return current_.start < current_.ticks.size();
  }

  bool RmtRx::loadNextFrame()
  {
    if (queue_.empty())
      return false;
    current_ = std::move(queue_.front());
    queue_.pop_front();
    if (current_.start >= current_.ticks.size())
      return loadNextFrame();
    return true;
  }

  bool RmtRx::consumeTruncatedFlag()
  {
    bool v = current_.truncated;
    current_.truncated = false;
    return v;
  }

  bool RmtRx::consumeOverflowFlag()
  {
    bool v = current_.overflow;
    current_.overflow = false;
    return v;
  }

  void RmtRx::consume(size_t ticks)
  {
    if (!hasCurrent() || ticks == 0)
      return;
    size_t remaining = current_.ticks.size() - current_.start;
    size_t adv = ticks > remaining ? remaining : ticks;
    current_.start += adv;
    if (current_.start > current_.ticks.size())
      current_.start = current_.ticks.size();
  }

  uint32_t RmtRx::consumeQueueOverflowCount()
  {
    uint32_t v = queue_overflow_count_;
    queue_overflow_count_ = 0;
    return v;
  }

  bool RmtRx::startReceiveInternal()
  {
    if (!rx_channel_)
      return false;
    receiving_ = false;
    sym_buf_.resize(kMaxRxSymbols);
    rmt_receive_config_t rcfg = {};
    rcfg.signal_range_min_ns = 0;
    rcfg.signal_range_max_ns = idle_threshold_us_ * 1000;

    esp_err_t err = rmt_receive(reinterpret_cast<rmt_channel_handle_t>(rx_channel_),
                                sym_buf_.data(),
                                sym_buf_.size() * sizeof(uint32_t),
                                &rcfg);
    receiving_ = (err == ESP_OK);
    if (!receiving_)
      last_overflow_ = true;
    return receiving_;
  }

  bool IRAM_ATTR RmtRx::handleRecvDone(const rmt_rx_done_event_data_t *edata)
  {
    sym_count_ = edata->num_symbols;
    if (sym_count_ > kMaxRxSymbols)
    {
      sym_count_ = kMaxRxSymbols;
      last_truncated_ = true;
    }
    has_frame_ = true;
    BaseType_t hp_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(rx_sem_), &hp_task_woken);
    return hp_task_woken == pdTRUE;
  }

#else // !ESP_PLATFORM

  bool RmtTx::begin(int gpio, bool inverted)
  {
    gpio_ = gpio;
    inverted_ = inverted;
    carrier_hz_ = 38000;
    begun_ = true;
    return true;
  }

  void RmtTx::end()
  {
    sending_ = false;
    carrier_hz_ = 0;
    begun_ = false;
  }

  bool RmtTx::setCarrierMode(TxCarrierMode mode)
  {
    if (begun_)
      return false;
    carrier_mode_ = mode;
    return true;
  }

  bool RmtTx::setTxMemBlocks(uint8_t blocks)
  {
    if (begun_)
      return false;
    tx_mem_blocks_ = blocks;
    return true;
  }

  bool RmtTx::applyCarrierHz(uint32_t carrier_hz)
  {
    if (!begun_)
      return false;
    carrier_hz_ = carrier_hz;
    return true;
  }

  bool RmtTx::setCarrierDuty(float duty)
  {
    if (!(duty > 0.0f && duty < 1.0f))
      return false;
    carrier_duty_ = duty;
    return true;
  }

  bool RmtTx::send(const esp32irpk::IRRawTickView &raw, int8_t repeat_count, uint32_t carrier_hz)
  {
    (void)raw;
    (void)repeat_count;
    (void)carrier_hz;
    if (!begun_)
      return false;
    return false;
  }

  bool RmtRx::begin(int gpio, bool inverted, uint32_t idle_threshold_us)
  {
    gpio_ = gpio;
    inverted_ = inverted;
    idle_threshold_us_ = idle_threshold_us;
    queue_.clear();
    current_ = RxFrame{};
    queue_overflow_count_ = 0;
    begun_ = true;
    return true;
  }

  void RmtRx::end()
  {
    queue_.clear();
    current_ = RxFrame{};
    queue_overflow_count_ = 0;
    begun_ = false;
  }

  bool RmtRx::read(/*out*/ esp32irpk::IRRawTickView &raw)
  {
    raw = {};
    if (hasCurrent())
    {
      raw.ticks = current_.ticks.data() + current_.start;
      raw.len = current_.ticks.size() - current_.start;
      return raw.len > 0;
    }
    if (!begun_)
      return false;
    return false;
  }

  bool RmtRx::startReceive()
  {
    return false;
  }

  void RmtRx::drainRxQueue() {}

  bool RmtRx::hasCurrent() const
  {
    return current_.start < current_.ticks.size();
  }

  bool RmtRx::loadNextFrame()
  {
    if (queue_.empty())
      return false;
    current_ = std::move(queue_.front());
    queue_.pop_front();
    return hasCurrent();
  }

  bool RmtRx::consumeTruncatedFlag()
  {
    return false;
  }

  bool RmtRx::consumeOverflowFlag()
  {
    return false;
  }

  void RmtRx::consume(size_t ticks)
  {
    if (!hasCurrent() || ticks == 0)
      return;
    size_t remaining = current_.ticks.size() - current_.start;
    size_t adv = ticks > remaining ? remaining : ticks;
    current_.start += adv;
  }

  uint32_t RmtRx::consumeQueueOverflowCount()
  {
    uint32_t v = queue_overflow_count_;
    queue_overflow_count_ = 0;
    return v;
  }

#endif // ESP_PLATFORM

} // namespace esp32irpk::hal
