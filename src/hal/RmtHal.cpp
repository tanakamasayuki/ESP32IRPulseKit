#include "RmtHal.h"
#include "../ESP32IRPulseKit.h"

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
    constexpr uint32_t kRmtTickUs = 10;
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
  } // namespace

  bool RmtTx::begin(int gpio, bool inverted)
  {
    if (begun_)
      return false;
    gpio_ = gpio;
    inverted_ = inverted;

    rmt_tx_channel_config_t cfg = {};
    cfg.clk_src = selectRmtClkSrc();
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

  bool RmtTx::send(const esp32irpk::IRRawTickView &raw, int8_t repeat_count)
  {
    if (!begun_)
      return false;
    if (!raw.ticks || raw.len == 0)
      return false;

    auto syms = toSymbols(raw, /*mark_high=*/true);
    if (syms.empty())
      return false;

    rmt_transmit_config_t tcfg = {};
    uint32_t loops = repeat_count < 0 ? 1U : static_cast<uint32_t>(repeat_count) + 1U;
    tcfg.loop_count = loops;

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
    begun_ = true;
    return true;
  }

  void RmtTx::end()
  {
    begun_ = false;
  }

  bool RmtTx::send(const esp32irpk::IRRawTickView &raw, int8_t repeat_count)
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

#if !defined(ESP_PLATFORM)

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

  bool RmtTx::send(const esp32irpk::IRRawTickView &raw, int8_t repeat_count)
  {
    (void)raw;
    (void)repeat_count;
    return false;
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
    queue_.clear();
    current_ = RxFrame{};
  }

  bool RmtRx::read(esp32irpk::IRRawTickView &raw)
  {
    raw = {};
    return false;
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
    (void)ticks;
  }

  uint32_t RmtRx::consumeQueueOverflowCount()
  {
    return 0;
  }

  void RmtRx::drainRxQueue() {}

  bool RmtRx::hasCurrent() const
  {
    return false;
  }

  bool RmtRx::loadNextFrame()
  {
    return false;
  }

  bool RmtRx::startReceive()
  {
    return false;
  }

#endif // !ESP_PLATFORM

} // namespace esp32irpk::hal
