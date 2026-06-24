#include <Arduino.h>

namespace Config
{
  constexpr uint8_t RELAY_CTRL_PIN = 4;    // GPIO для керування реле
  constexpr uint8_t RELAY_CONTACT_PIN = 5; // GPIO для зчитування контакту

  constexpr uint8_t MEASUREMENTS_COUNT = 10;

  constexpr uint32_t RELAY_ON_PAUSE_MS = 300;
  constexpr uint32_t RELAY_OFF_PAUSE_MS = 1000;
  constexpr uint32_t MEASUREMENT_TIMEOUT_MS = 1000;
}

enum class TestState
{
  StartMeasurement,
  WaitingForContact,
  RelayOnPause,
  RelayOffPause,
  Finished
};

volatile bool waitingForContact = false;
volatile bool contactCaptured = false;

volatile uint32_t startTimeMs = 0;
volatile uint32_t capturedDelayMs = 0;

uint32_t measurements[Config::MEASUREMENTS_COUNT];

uint8_t measurementIndex = 0;
TestState state = TestState::StartMeasurement;

uint32_t stateStartedAt = 0;

void setRelay(bool enabled)
{
  // Через BC547:
  // GPIO HIGH -> BC547 відкритий -> IN реле-модуля притягнутий до GND -> реле ON
  // GPIO LOW  -> BC547 закритий -> реле OFF
  digitalWrite(Config::RELAY_CTRL_PIN, enabled ? HIGH : LOW);
}

bool isRelayContactClosed()
{
  // COM підключений до GND.
  // NO підключений до GPIO5 з INPUT_PULLUP.
  // Коли реле спрацювало, NO замикається з COM, тому GPIO5 читає LOW.
  return digitalRead(Config::RELAY_CONTACT_PIN) == LOW;
}

void IRAM_ATTR onRelayContactClosed()
{
  if (!waitingForContact || contactCaptured)
  {
    return;
  }

  capturedDelayMs = millis() - startTimeMs;
  contactCaptured = true;
  waitingForContact = false;
}

void printResults()
{
  uint32_t sum = 0;

  Serial.println();
  Serial.println("=== Results ===");

  for (uint8_t i = 0; i < Config::MEASUREMENTS_COUNT; i++)
  {
    sum += measurements[i];

    Serial.print("Measurement ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(measurements[i]);
    Serial.println(" ms");
  }

  const float average = static_cast<float>(sum) / Config::MEASUREMENTS_COUNT;

  Serial.println("----------------");
  Serial.print("Average relay response time: ");
  Serial.print(average, 2);
  Serial.println(" ms");
}

void setup()
{
  Serial.begin(115200);

  pinMode(Config::RELAY_CTRL_PIN, OUTPUT);
  setRelay(false);

  pinMode(Config::RELAY_CONTACT_PIN, INPUT_PULLUP);

  attachInterrupt(
      digitalPinToInterrupt(Config::RELAY_CONTACT_PIN),
      onRelayContactClosed,
      FALLING);

  Serial.println("Relay response time test started");
  Serial.println("Relay module: Songle SRD-05VDC-SL-C 5V");
  Serial.println("Control: ESP32 GPIO4 -> BC547 -> relay IN");
  Serial.println("Contact read: COM -> GND, NO -> GPIO5 INPUT_PULLUP");
  Serial.println();
}

void loop()
{
  const uint32_t now = millis();

  switch (state)
  {
  case TestState::StartMeasurement:
  {
    if (measurementIndex >= Config::MEASUREMENTS_COUNT)
    {
      state = TestState::Finished;
      printResults();
      return;
    }

    if (isRelayContactClosed())
    {
      Serial.println("Contact is already closed. Waiting for relay release...");
      setRelay(false);
      stateStartedAt = now;
      state = TestState::RelayOffPause;
      return;
    }

    noInterrupts();
    contactCaptured = false;
    waitingForContact = true;
    startTimeMs = millis();
    capturedDelayMs = 0;
    interrupts();

    setRelay(true);

    Serial.print("Measurement ");
    Serial.print(measurementIndex + 1);
    Serial.print(": ");

    state = TestState::WaitingForContact;
    break;
  }

  case TestState::WaitingForContact:
  {
    if (contactCaptured)
    {
      uint32_t result;

      noInterrupts();
      result = capturedDelayMs;
      interrupts();

      measurements[measurementIndex] = result;
      measurementIndex++;

      Serial.print(result);
      Serial.println(" ms");

      stateStartedAt = now;
      state = TestState::RelayOnPause;
    }

    if (now - startTimeMs > Config::MEASUREMENT_TIMEOUT_MS)
    {
      noInterrupts();
      waitingForContact = false;
      interrupts();

      Serial.println("timeout, relay contact was not detected");

      setRelay(false);
      stateStartedAt = now;
      state = TestState::RelayOffPause;
    }

    break;
  }

  case TestState::RelayOnPause:
  {
    if (now - stateStartedAt >= Config::RELAY_ON_PAUSE_MS)
    {
      setRelay(false);
      stateStartedAt = now;
      state = TestState::RelayOffPause;
    }

    break;
  }

  case TestState::RelayOffPause:
  {
    if (now - stateStartedAt >= Config::RELAY_OFF_PAUSE_MS)
    {
      state = TestState::StartMeasurement;
    }

    break;
  }

  case TestState::Finished:
  {
    setRelay(false);
    break;
  }
  }
}