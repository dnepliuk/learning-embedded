#include <Arduino.h>
#include <array>

#define LED_1 4
#define LED_2 5
#define LED_3 6
#define BUTTON 7

std::array<uint8_t, 3> ledArray = {LED_1, LED_2, LED_3};

int8_t current = 0;
hw_timer_t *timer = NULL;

uint32_t lastButtonPressed = 0;
const uint32_t debounceTime = 100;

bool isForward = true;
bool waitingForRelease = false;

volatile bool isButtonPressed = false;
volatile bool isNext = false;

void IRAM_ATTR onButtonPressed()
{
  isButtonPressed = true;
}

void IRAM_ATTR onTimer()
{
  isNext = true;
}

void turnOffAllLeds()
{
  digitalWrite(LED_1, LOW);
  digitalWrite(LED_2, LOW);
  digitalWrite(LED_3, LOW);
}

void setup()
{
  Serial.begin(115200);

  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(LED_3, OUTPUT);

  pinMode(BUTTON, INPUT);
  // Якщо це проста кнопка між GPIO і GND без модуля, краще:
  // pinMode(BUTTON, INPUT_PULLUP);

  attachInterrupt(
      digitalPinToInterrupt(BUTTON),
      onButtonPressed,
      FALLING);

  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 500000, true);
  timerAlarmEnable(timer);
}

void loop()
{
  if (waitingForRelease)
  {
    if (digitalRead(BUTTON) == HIGH)
    {
      waitingForRelease = false;
      lastButtonPressed = millis();
    }
  }

  if (isButtonPressed)
  {
    isButtonPressed = false;

    if (!waitingForRelease)
    {
      uint32_t now = millis();

      if (now - lastButtonPressed >= debounceTime)
      {
        if (digitalRead(BUTTON) == LOW)
        {
          lastButtonPressed = now;
          isForward = !isForward;
          waitingForRelease = true;
        }
      }
    }
  }

  if (isNext)
  {
    isNext = false;

    turnOffAllLeds();
    digitalWrite(ledArray[current], HIGH);

    if (isForward)
    {
      if (current + 1 == ledArray.size())
      {
        current = 0;
      }
      else
      {
        current++;
      }
    }
    else
    {
      if (current == 0)
      {
        current = ledArray.size() - 1;
      }
      else
      {
        current--;
      }
    }
  }
}