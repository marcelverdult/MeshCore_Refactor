#include <Arduino.h>
#include "target.h"

ESP32Board board;

SensorManager sensors;

#ifndef LORA_CR
  #define LORA_CR      5
#endif
