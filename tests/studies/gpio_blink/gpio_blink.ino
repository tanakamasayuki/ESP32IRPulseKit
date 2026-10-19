// Slow GPIO blink on the IR TX pin -- no IR library involved.
//
// Isolates the TX wiring from ESP32IRPulseKit: if the IR LED does not follow this
// slow, plain GPIO toggle (watch it through a phone camera), the problem is the
// pin or the LED drive circuit, not the library. The pin is configured
// input+output so every level change also reports what the pad actually reads;
// a mismatch means the load (or a wrong/input-only pin) is holding the level.
//
// Serial commands:
//   READY         -> GPIO_READY ...
//   PING          -> PONG
//   ON / OFF      hold the LED on / off (honours IR_TX_INVERTED)
//   BLINK <ms>    toggle every <ms> (default 1000)
//   DRIVE <0-3>   set the pad drive strength (GPIO_DRIVE_CAP_0..3)
// Every level change prints: GPIO set=<0|1> read=<0|1> led=<on|off>
#include <driver/gpio.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kGpio = atoi(IR_TX_GPIO);
const bool kInverted = atoi(IR_TX_INVERTED) != 0;

namespace
{
bool g_ok = false;
bool g_blink = true;
uint32_t g_periodMs = 1000;
uint32_t g_nextToggleMs = 0;
bool g_ledOn = false;

int driveCap()
{
  gpio_drive_cap_t cap = GPIO_DRIVE_CAP_DEFAULT;
  gpio_get_drive_capability(static_cast<gpio_num_t>(kGpio), &cap);
  return static_cast<int>(cap);
}

void sendReady()
{
  Serial.print("GPIO_READY gpio=");
  Serial.print(kGpio);
  Serial.print(" inverted=");
  Serial.print(kInverted ? 1 : 0);
  Serial.print(" output_capable=");
  Serial.print(GPIO_IS_VALID_OUTPUT_GPIO(kGpio) ? 1 : 0);
  Serial.print(" drive=");
  Serial.println(driveCap());
}

void setLed(bool on)
{
  g_ledOn = on;
  const int level = (on != kInverted) ? 1 : 0;
  gpio_set_level(static_cast<gpio_num_t>(kGpio), level);
  delayMicroseconds(50); // let the pad settle under load before reading back
  Serial.print("GPIO set=");
  Serial.print(level);
  Serial.print(" read=");
  Serial.print(gpio_get_level(static_cast<gpio_num_t>(kGpio)));
  Serial.print(" led=");
  Serial.println(on ? "on" : "off");
}

bool readLine(String &line)
{
  if (!Serial.available())
  {
    return false;
  }
  line = Serial.readStringUntil('\n');
  line.trim();
  return line.length() > 0;
}

void handleCommand(const String &line)
{
  if (line == "READY")
  {
    sendReady();
  }
  else if (line == "PING")
  {
    Serial.println("PONG");
  }
  else if (line == "ON" || line == "OFF")
  {
    g_blink = false;
    setLed(line == "ON");
  }
  else if (line == "BLINK" || line.startsWith("BLINK "))
  {
    long ms = line.length() > 5 ? line.substring(6).toInt() : 1000;
    if (ms < 50 || ms > 10000)
    {
      Serial.println("GPIO_ERROR invalid_period");
      return;
    }
    g_periodMs = static_cast<uint32_t>(ms);
    g_blink = true;
    g_nextToggleMs = 0;
    Serial.print("GPIO_BLINK ");
    Serial.println(g_periodMs);
  }
  else if (line.startsWith("DRIVE "))
  {
    long cap = line.substring(6).toInt();
    if (cap < 0 || cap > 3 ||
        gpio_set_drive_capability(static_cast<gpio_num_t>(kGpio), static_cast<gpio_drive_cap_t>(cap)) != ESP_OK)
    {
      Serial.println("GPIO_ERROR invalid_drive");
      return;
    }
    Serial.print("GPIO_DRIVE ");
    Serial.println(driveCap());
  }
  else
  {
    Serial.print("GPIO_ERROR unknown_command ");
    Serial.println(line);
  }
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(2000);
  if (!GPIO_IS_VALID_OUTPUT_GPIO(kGpio))
  {
    Serial.print("GPIO_ERROR not_output_capable gpio=");
    Serial.println(kGpio);
    return;
  }
  pinMode(kGpio, OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(kGpio), GPIO_MODE_INPUT_OUTPUT);
  g_ok = true;
  setLed(false);
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
  {
    if (g_ok)
    {
      handleCommand(line);
    }
    else
    {
      Serial.print("GPIO_ERROR not_output_capable gpio=");
      Serial.println(kGpio);
    }
  }

  if (g_ok && g_blink)
  {
    uint32_t now = millis();
    if (g_nextToggleMs == 0 || static_cast<int32_t>(now - g_nextToggleMs) >= 0)
    {
      setLed(!g_ledOn);
      g_nextToggleMs = now + g_periodMs;
    }
  }
  delay(1);
}
