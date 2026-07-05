#include <Arduino.h>

#define BUTTON 7

constexpr uint32_t PRESS_CHECK_MS = 5;
constexpr uint32_t RELEASE_CHECK_MS = 50;

volatile bool buttonEvent = false;

uint32_t counter = 0;
uint32_t lastPrintedCounter = 0;

enum class ButtonState
{
  Released,
  PressCheck,
  Pressed,
  ReleaseCheck
};

ButtonState buttonState = ButtonState::Released;
uint32_t stateStartTime = 0;

void IRAM_ATTR onButtonPressed()
{
  buttonEvent = true;
}

void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON, INPUT);

  delay(100);

  noInterrupts();
  buttonEvent = false;
  interrupts();

  attachInterrupt(
      digitalPinToInterrupt(BUTTON),
      onButtonPressed,
      FALLING);

  Serial.println("Task 3: State-based debounce");
}

void loop()
{
  bool eventDetected = false;

  noInterrupts();
  if (buttonEvent)
  {
    eventDetected = true;
    buttonEvent = false;
  }
  interrupts();

  uint32_t now = millis();

  switch (buttonState)
  {
  case ButtonState::Released:
    if (eventDetected)
    {
      buttonState = ButtonState::PressCheck;
      stateStartTime = now;
    }
    break;

  case ButtonState::PressCheck:
    if (now - stateStartTime >= PRESS_CHECK_MS)
    {
      if (digitalRead(BUTTON) == LOW)
      {
        counter++;
        buttonState = ButtonState::Pressed;
      }
      else
      {
        buttonState = ButtonState::Released;
      }
    }
    break;

  case ButtonState::Pressed:
    if (digitalRead(BUTTON) == HIGH)
    {
      buttonState = ButtonState::ReleaseCheck;
      stateStartTime = now;
    }
    break;

  case ButtonState::ReleaseCheck:
    if (digitalRead(BUTTON) == LOW)
    {
      buttonState = ButtonState::Pressed;
    }
    else if (now - stateStartTime >= RELEASE_CHECK_MS)
    {
      buttonState = ButtonState::Released;

      noInterrupts();
      buttonEvent = false;
      interrupts();
    }
    break;
  }

  if (counter != lastPrintedCounter)
  {
    Serial.print("Counter: ");
    Serial.println(counter);
    lastPrintedCounter = counter;
  }
}