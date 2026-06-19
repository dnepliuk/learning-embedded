#include <Arduino.h>

enum class LedState : uint8_t {
  Off,
  On
};

enum class LedMode : uint8_t {
  Blinking,
  AlwaysOn,
  AlwaysOff
};

class AppConfig final {
public:
  // constexpr для основних параметрів
  static constexpr uint8_t LED_PIN = 5;
  static constexpr uint8_t BUTTON_PIN = 4;
  static constexpr uint32_t BLINK_INTERVAL_MS = 500;

  // static const для додаткових налаштувань
  static const uint32_t SERIAL_BAUD = 115200;
  static const uint32_t BUTTON_DEBOUNCE_MS = 200;
  static const uint32_t LOOP_REPORT_EVERY = 1000;

  // true — якщо LED підключений до GND через резистор
  // false — якщо LED активний по LOW
  static const bool LED_ACTIVE_HIGH = true;

private:
  AppConfig() = delete;
};

class Led {
public:
  Led(uint8_t pin, bool activeHigh)
    : pin_(pin), activeHigh_(activeHigh) {}

  void init() {
    pinMode(pin_, OUTPUT);
    set(LedState::Off);
  }

  void set(LedState state) {
    state_ = state;

    bool outputLevel;

    if (state_ == LedState::On) {
      outputLevel = activeHigh_;
    } else {
      outputLevel = !activeHigh_;
    }

    digitalWrite(pin_, outputLevel ? HIGH : LOW);
  }

  LedState state() const {
    return state_;
  }

  void toggle() {
    if (state_ == LedState::On) {
      set(LedState::Off);
    } else {
      set(LedState::On);
    }
  }

private:
  uint8_t pin_;
  bool activeHigh_;
  LedState state_ = LedState::Off;
};

// Потрібна для ISR, тому залишаємо глобальною
volatile bool buttonPressed = false;

Led& getLed() {
  static Led led(AppConfig::LED_PIN, AppConfig::LED_ACTIVE_HIGH);
  return led;
}

void IRAM_ATTR onButtonInterrupt() {
  buttonPressed = true;
}

LedMode getNextMode(LedMode currentMode) {
  switch (currentMode) {
    case LedMode::Blinking:
      return LedMode::AlwaysOn;

    case LedMode::AlwaysOn:
      return LedMode::AlwaysOff;

    case LedMode::AlwaysOff:
      return LedMode::Blinking;

    default:
      return LedMode::Blinking;
  }
}

const char* getModeName(LedMode mode) {
  switch (mode) {
    case LedMode::Blinking:
      return "Blinking";

    case LedMode::AlwaysOn:
      return "Always On";

    case LedMode::AlwaysOff:
      return "Always Off";

    default:
      return "Unknown";
  }
}

bool consumeButtonPress(uint32_t currentTimeMs) {
  static uint32_t lastButtonHandledMs = 0;

  bool wasPressed = false;

  noInterrupts();

  if (buttonPressed) {
    buttonPressed = false;
    wasPressed = true;
  }

  interrupts();

  if (!wasPressed) {
    return false;
  }

  if (currentTimeMs - lastButtonHandledMs < AppConfig::BUTTON_DEBOUNCE_MS) {
    return false;
  }

  lastButtonHandledMs = currentTimeMs;
  return true;
}

void reportSuperloopTime(uint32_t loopStartUs) {
  static uint32_t iterationCounter = 0;
  static uint64_t totalLoopTimeUs = 0;
  static uint32_t maxLoopTimeUs = 0;

  uint32_t loopTimeUs = micros() - loopStartUs;

  totalLoopTimeUs += loopTimeUs;

  if (loopTimeUs > maxLoopTimeUs) {
    maxLoopTimeUs = loopTimeUs;
  }

  iterationCounter++;

  if (iterationCounter >= AppConfig::LOOP_REPORT_EVERY) {
    uint32_t averageLoopTimeUs = totalLoopTimeUs / iterationCounter;

    Serial.print("Loop avg: ");
    Serial.print(averageLoopTimeUs);
    Serial.print(" us, max: ");
    Serial.print(maxLoopTimeUs);
    Serial.println(" us");

    iterationCounter = 0;
    totalLoopTimeUs = 0;
    maxLoopTimeUs = 0;
  }
}

void setup() {
  Serial.begin(AppConfig::SERIAL_BAUD);

  getLed().init();

  pinMode(AppConfig::BUTTON_PIN, INPUT);

  attachInterrupt(
    digitalPinToInterrupt(AppConfig::BUTTON_PIN),
    onButtonInterrupt,
    FALLING
  );

  Serial.println("ESP32 Embedded C++ Blink started");
}

void loop() {
  uint32_t loopStartUs = micros();
  uint32_t currentTimeMs = millis();

  static LedMode currentMode = LedMode::Blinking;
  static uint32_t lastBlinkTimeMs = 0;

  if (consumeButtonPress(currentTimeMs)) {
    currentMode = getNextMode(currentMode);

    Serial.print("Mode changed to: ");
    Serial.println(getModeName(currentMode));

    if (currentMode == LedMode::AlwaysOn) {
      getLed().set(LedState::On);
    } else if (currentMode == LedMode::AlwaysOff) {
      getLed().set(LedState::Off);
    } else {
      lastBlinkTimeMs = currentTimeMs;
      getLed().set(LedState::Off);
    }
  }

  if (currentMode == LedMode::Blinking) {
    if (currentTimeMs - lastBlinkTimeMs >= AppConfig::BLINK_INTERVAL_MS) {
      lastBlinkTimeMs = currentTimeMs;
      getLed().toggle();
    }
  }

  reportSuperloopTime(loopStartUs);
}