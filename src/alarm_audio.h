#pragma once
#include <Arduino.h>
#include "port_bsp/i2c_bsp.h"

void alarm_audio_init(I2cMasterBus &i2c_bus);
void alarm_audio_start();
void alarm_audio_stop();
void alarm_audio_play_test();
bool alarm_audio_is_playing();
void alarm_audio_set_volume(uint8_t volume); // 0 to 100
