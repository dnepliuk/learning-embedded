#include <Arduino.h>

#define BUTTON 7

constexpr uint32_t POLL_INTERVAL_MS = 5;
constexpr uint32_t DEBOUNCE_TIME_MS = 50;

uint32_t counter = 0;
uint32_t lastPrintedCounter = 0;

uint32_t lastPollTime = 0;
uint32_t lastDebounceTime = 0;

bool lastRawState = HIGH;
bool stableState = HIGH;
bool previousStableState = HIGH;

void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON, INPUT);

  delay(100);

  bool initialState = digitalRead(BUTTON);

  lastRawState = initialState;
  stableState = initialState;
  previousStableState = initialState;

  Serial.println("Task 4: Polling + debounce without interrupts");
}

void loop()
{
  uint32_t now = millis();

  if (now - lastPollTime < POLL_INTERVAL_MS)
  {
    return;
  }

  lastPollTime = now;

  bool rawState = digitalRead(BUTTON);

  if (rawState != lastRawState)
  {
    lastRawState = rawState;
    lastDebounceTime = now;
  }

  if (now - lastDebounceTime >= DEBOUNCE_TIME_MS)
  {
    if (rawState != stableState)
    {
      previousStableState = stableState;
      stableState = rawState;

      if (previousStableState == HIGH && stableState == LOW)
      {
        counter++;
      }
    }
  }

  if (counter != lastPrintedCounter)
  {
    Serial.print("Counter: ");
    Serial.println(counter);

    lastPrintedCounter = counter;
  }
}