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
  usb_pd_int_pin.Init(DaisySeed::GetPin(12), GPIO::Mode::INPUT);


  hardware.StartLog(true); 

  System::Delay(200);
  hardware.PrintLine("===========================================");
  hardware.PrintLine("                It's alive!                ");
  hardware.PrintLine("===========================================");
  hardware.PrintLine("");

  // Init tcpm
  tcpm_init(0);
  System::Delay(50);
  pd_init(0);
  System::Delay(50);

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