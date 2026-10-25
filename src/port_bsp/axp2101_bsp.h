#pragma once


#include "i2c_bsp.h"

void Custom_PmicRegisterInit(void);
void Custom_PmicPortInit(I2cMasterBus *i2cbus, uint8_t dev_addr);

void Axp2101_SetAldo2(uint8_t vol);
void Axp2101_SetAldo3(uint8_t vol);
int Axp2101_GetBattVoltage(void);
int Axp2101_GetBatteryPercent(void);
bool Axp2101_IsBatteryConnected(void);
bool Axp2101_IsCharging(void);
bool Axp2101_IsVbusIn(void);
bool Axp2101_WasPowerButtonPressed(void);
