#include <Arduino.h>

#define BUTTON 7

constexpr uint32_t DEBOUNCE_TIME_MS = 50;

volatile uint32_t interruptEvents = 0;

uint32_t acceptedCounter = 0;
uint32_t ignoredCounter = 0;

uint32_t lastAcceptedTime = 0;
uint32_t lastPrintedAcceptedCounter = 0;

void IRAM_ATTR onButtonPressed()
{
  interruptEvents++;
}

void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON, INPUT);

  attachInterrupt(
      digitalPinToInterrupt(BUTTON),
      onButtonPressed,
      FALLING);

  Serial.println("Task 2: Time-based debounce");
}

void loop()
{
  uint32_t eventsToProcess = 0;

  noInterrupts();
  eventsToProcess = interruptEvents;
  interruptEvents = 0;
  interrupts();

  for (uint32_t i = 0; i < eventsToProcess; i++)
  {
    uint32_t now = millis();

    if (now - lastAcceptedTime >= DEBOUNCE_TIME_MS)
    {
      acceptedCounter++;
      lastAcceptedTime = now;
    }
    else
    {
      ignoredCounter++;
    }
  }

  if (acceptedCounter != lastPrintedAcceptedCounter)
  {
    Serial.print("Accepted counter: ");
    Serial.print(acceptedCounter);

    Serial.print(" | Ignored bounce: ");
    Serial.println(ignoredCounter);

    lastPrintedAcceptedCounter = acceptedCounter;
  }
}