#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include "axp2101_bsp.h"
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

#define TAG "AXP2101"


static XPowersPMU axp2101;
static I2cMasterBus *i2cbus_ = NULL;
static i2c_master_dev_handle_t i2cPMICdev = NULL;
static uint8_t i2cPMICAddress;


static int AXP2101_SLAVE_Read(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
  int ret;
  uint8_t count = 3;
  do {
    ret = (i2cbus_->i2c_read_buff(i2cPMICdev, regAddr, data, len) == ESP_OK) ? 0 : -1;
    if (ret == 0)
      break;
    vTaskDelay(pdMS_TO_TICKS(100));
    count--;
  } while (count);
  return ret;
}

static int AXP2101_SLAVE_Write(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
  int ret;
  uint8_t count = 3;
  do {
    ret = (i2cbus_->i2c_write_buff(i2cPMICdev, regAddr, data, len) == ESP_OK) ? 0 : -1;
    if (ret == 0)
      break;
    vTaskDelay(pdMS_TO_TICKS(100));
    count--;
  } while (count);
  return ret;
}

void Custom_PmicPortInit(I2cMasterBus *i2cbus, uint8_t dev_addr) {
  if (i2cbus_ == NULL) {
    i2cbus_ = i2cbus;
  }
  if (i2cPMICdev == NULL) {
    i2c_master_bus_handle_t BusHandle = i2cbus_->Get_I2cBusHandle();
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 100000;
    dev_cfg.device_address = dev_addr;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(BusHandle, &dev_cfg, &i2cPMICdev));
    i2cPMICAddress = dev_addr;
  }
  if (axp2101.begin(i2cPMICAddress, AXP2101_SLAVE_Read, AXP2101_SLAVE_Write)) {
    ESP_LOGI(TAG, "Init PMU SUCCESS!");
  } else {
    ESP_LOGE(TAG, "Init PMU FAILED!");
  }
  Custom_PmicRegisterInit();
}

void Custom_PmicRegisterInit(void) {
  axp2101.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_2000MA);

  if (axp2101.getDC1Voltage() != 3300) {
    axp2101.setDC1Voltage(3300);
    ESP_LOGW("axp2101_init_log", "Set DCDC1 to output 3V3");
  }
  if (axp2101.getALDO1Voltage() != 3300) {
    axp2101.setALDO1Voltage(3300);
    ESP_LOGW("axp2101_init_log", "Set ALDO1 to output 3V3");
  }
  if (axp2101.getALDO2Voltage() != 3300) {
    axp2101.setALDO2Voltage(3300);
    ESP_LOGW("axp2101_init_log", "Set ALDO2 to output 3V3");
  }
  if (axp2101.getALDO3Voltage() != 3300) {
    axp2101.setALDO3Voltage(3300);
    ESP_LOGW("axp2101_init_log", "Set ALDO3 to output 3V3");
  }
  if (axp2101.getALDO4Voltage() != 3300) {
    axp2101.setALDO4Voltage(3300);
    ESP_LOGW("axp2101_init_log", "Set ALDO4 to output 3V3");
  }

  axp2101.enableALDO1(); // Power up Audio Codec (ES8311)
  axp2101.enableALDO4(); // Power up Peripherals

  axp2101.enableBattDetection();
  axp2101.enableBattVoltageMeasure();
  axp2101.enableCellbatteryCharge();

  axp2101.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
  axp2101.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
  axp2101.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_50MA);

  // Enable PEKEY (Power button) interrupt detection so PWR button wakes display / controls clock
  axp2101.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ | XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ);
  axp2101.clearIrqStatus();
}

void Axp2101_SetAldo3(uint8_t vol) {
  if(vol) {
    axp2101.enableALDO3();
  } else {
    axp2101.disableALDO3();
  }
}

void Axp2101_SetAldo2(uint8_t vol) {
  if(vol) {
    axp2101.enableALDO2();
  } else {
    axp2101.disableALDO2();
  }
}

int Axp2101_GetBattVoltage(void) {
  return axp2101.getBattVoltage();
}

bool Axp2101_IsBatteryConnected(void) {
  return axp2101.isBatteryConnect();
}

int Axp2101_GetBatteryPercent(void) {
  if (!axp2101.isBatteryConnect()) {
    return -1; // No battery connected
  }
  int percent = axp2101.getBatteryPercent();
  if (percent >= 0 && percent <= 100) {
    return percent;
  }
  // Fallback: estimate from LiPo voltage discharge curve
  int mv = axp2101.getBattVoltage();
  if (mv >= 4180) return 100;
  if (mv <= 3350) return 0;
  if (mv >= 3900) {
    return 60 + (int)((mv - 3900) * 40.0f / 280.0f);
  } else if (mv >= 3700) {
    return 25 + (int)((mv - 3700) * 35.0f / 200.0f);
  } else {
    return (int)((mv - 3350) * 25.0f / 350.0f);
  }
}

bool Axp2101_IsCharging(void) {
  return axp2101.isCharging();
}

bool Axp2101_IsVbusIn(void) {
  return axp2101.isVbusIn();
}

bool Axp2101_WasPowerButtonPressed(void) {
  axp2101.getIrqStatus();
  bool pressed = axp2101.isPekeyShortPressIrq() || axp2101.isPekeyLongPressIrq() || axp2101.isPekeyNegativeIrq();
  if (pressed) {
    axp2101.clearIrqStatus();
  }
  return pressed;
}
