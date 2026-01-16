#include "../libDaisy/src/daisy_seed.h"
#include "tcpm_driver.h"
#include "usb_pd.h"

// USB-C Specific - TCPM start 1
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
  {0, fusb302_I2C_SLAVE_ADDR, &fusb302_tcpm_drv},
};
// USB-C Specific - TCPM end 1

using namespace daisy;
static DaisySeed hardware;
GPIO usb_pd_int_pin;
I2CHandle i2c4;

// Make i2c4 accessible to tcpm_driver
extern I2CHandle* g_i2c_handle;

void InitI2C4() {
  I2CHandle::Config i2c_config;
  i2c_config.periph = I2CHandle::Config::Peripheral::I2C_4;
  i2c_config.speed  = I2CHandle::Config::Speed::I2C_400KHZ;
  i2c_config.mode   = I2CHandle::Config::Mode::I2C_MASTER;
  i2c_config.pin_config.scl = DaisySeed::GetPin(13); // D13 - SCL
  i2c_config.pin_config.sda = DaisySeed::GetPin(14); // D14 - SDA
  i2c4.Init(i2c_config);
}

int main() {
  hardware.Init();
  usb_pd_int_pin.Init(DaisySeed::GetPin(27), GPIO::Mode::INPUT);
  // Initialize I2C and set global handle for USB-PD driver
  InitI2C4();
  g_i2c_handle = &i2c4;
  hardware.StartLog(true); 

  System::Delay(200);
  hardware.PrintLine("===========================================");
  hardware.PrintLine("                It's alive!                ");
  hardware.PrintLine("===========================================");
  hardware.PrintLine("");

  hardware.PrintLine("Initializing USB-PD TCPM...");
  // Initialize PD interrupt pin && I2C4 for TCPM driver
  usb_pd_int_pin.Init(DaisySeed::GetPin(27), GPIO::Mode::INPUT);  
  InitI2C4();
  g_i2c_handle = &i2c4;
  hardware.PrintLine("USB-PD TCPM initialized.");

  hardware.PrintLine("Starting USB-PD TCPM state machine...");
  // Init tcpm
  tcpm_init(0);
  System::Delay(50);
  pd_init(0);
  System::Delay(50);
  hardware.PrintLine("USB-PD TCPM state machine started.");

  // blink
  while (true) {
    if (usb_pd_int_pin.Read() == false) {
      tcpc_alert(0);
    }
    pd_run_state_machine(0);
    System::Delay(4);
  }

  return 0;
}