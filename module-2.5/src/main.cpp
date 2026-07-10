#include <Arduino.h>
#include <esp_timer.h>

namespace Config
{
  constexpr uint8_t RELAY_CTRL_PIN = 4;

  // Для схеми керування через BC547:
  // GPIO HIGH -> реле увімкнене
  // GPIO LOW  -> реле вимкнене
  constexpr uint8_t RELAY_ON_LEVEL = HIGH;
  constexpr uint8_t RELAY_OFF_LEVEL = LOW;

  // Тестовий режим:
  // повний цикл — 60 секунд;
  // вентилятор працює 15 секунд;
  constexpr uint64_t FAN_PERIOD_MS = 60ULL * 1000ULL;
  constexpr uint64_t FAN_ON_TIME_MS = 15ULL * 1000ULL;

  // Реальний режим:
  // constexpr uint64_t FAN_PERIOD_MS =
  //     60ULL * 60ULL * 1000ULL; // 1 година
  //
  // constexpr uint64_t FAN_ON_TIME_MS =
  //     15ULL * 60ULL * 1000ULL; // 15 хвилин

  constexpr uint64_t FAN_OFF_TIME_MS =
      FAN_PERIOD_MS - FAN_ON_TIME_MS;
}

static_assert(
    Config::FAN_ON_TIME_MS < Config::FAN_PERIOD_MS,
    "Fan ON time must be shorter than the full period");

enum class FanState
{
  Off,
  On
};

esp_timer_handle_t fanTimer = nullptr;
FanState fanState = FanState::Off;

uint64_t millisecondsToMicroseconds(uint64_t milliseconds)
{
  return milliseconds * 1000ULL;
}

void setRelay(bool enabled)
{
  digitalWrite(
      Config::RELAY_CTRL_PIN,
      enabled
          ? Config::RELAY_ON_LEVEL
          : Config::RELAY_OFF_LEVEL);
}

bool startTimer(uint64_t durationMs)
{
  const esp_err_t result = esp_timer_start_once(
      fanTimer,
      millisecondsToMicroseconds(durationMs));

  if (result != ESP_OK)
  {
    Serial.print("[ERROR] Cannot start timer. Error code: ");
    Serial.println(static_cast<int>(result));
    return false;
  }

  return true;
}

void turnFanOn()
{
  fanState = FanState::On;
  setRelay(true);

  Serial.println("[FAN] ON");

  // Після завершення часу роботи callback вимкне вентилятор.
  startTimer(Config::FAN_ON_TIME_MS);
}

void turnFanOff()
{
  fanState = FanState::Off;
  setRelay(false);

  Serial.println("[FAN] OFF");

  // Чекаємо решту години:
  // 60 хвилин - 15 хвилин = 45 хвилин.
  startTimer(Config::FAN_OFF_TIME_MS);
}

void onFanTimer(void *argument)
{
  if (fanState == FanState::On)
  {
    turnFanOff();
  }
  else
  {
    turnFanOn();
  }
}

bool createFanTimer()
{
  const esp_timer_create_args_t timerArguments = {
      .callback = &onFanTimer,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "fan_timer",
      .skip_unhandled_events = false};

  const esp_err_t result = esp_timer_create(
      &timerArguments,
      &fanTimer);

  if (result != ESP_OK)
  {
    Serial.print("[ERROR] Cannot create timer. Error code: ");
    Serial.println(static_cast<int>(result));
    return false;
  }

  return true;
}

void setup()
{
  Serial.begin(115200);

  pinMode(Config::RELAY_CTRL_PIN, OUTPUT);
  setRelay(false);

  Serial.println();
  Serial.println("=== Exhaust fan timer ===");

  Serial.print("Full period: ");
  Serial.print(Config::FAN_PERIOD_MS);
  Serial.println(" ms");

  Serial.print("Fan ON time: ");
  Serial.print(Config::FAN_ON_TIME_MS);
  Serial.println(" ms");

  Serial.print("Fan OFF time: ");
  Serial.print(Config::FAN_OFF_TIME_MS);
  Serial.println(" ms");

  if (!createFanTimer())
  {
    Serial.println("[ERROR] Project stopped");
    return;
  }

  // Перший запуск одразу після завантаження ESP32.
  turnFanOn();
}

void loop()
{
  // Порожній навмисно.
  // Керування вентилятором повністю виконує esp_timer.
}