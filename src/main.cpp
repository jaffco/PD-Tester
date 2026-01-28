#include "../libDaisy/src/daisy_seed.h"
#include "tcpm_driver.h"
#include "usb_pd.h"

// USB-C Specific - TCPM start 1
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
  {0, fusb302_I2C_SLAVE_ADDR, &fusb302_tcpm_drv},
};

// PD capabilities tracking
int pd_count = 0, pd_count_written = 0;
uint32_t *pd_src_caps = nullptr;
// USB-C Specific - TCPM end 1

// Type-C current tracking
uint32_t last_typec_current = 0;
bool typec_header_printed = false;

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
  bool pd_header_printed = false;
  while (true) {
    if (usb_pd_int_pin.Read() == false) {
      tcpc_alert(0);
    }
    
    // Print PD capabilities one at a time to avoid blocking
    if (pd_count_written < pd_count) {
      // Print header when we start receiving PDOs
      if (!pd_header_printed) {
        hardware.PrintLine("");
        hardware.PrintLine("===========================================");
        hardware.PrintLine("    USB-C Power Delivery Capabilities");
        hardware.PrintLine("===========================================");
        pd_header_printed = true;
        typec_header_printed = false;  // Reset Type-C header when PD is detected
      }
      
      uint32_t ma = 0, mv = 0;
      uint32_t pdo = pd_src_caps[pd_count_written];
      
      pd_extract_pdo_power(pdo, &ma, &mv);
      
      float voltage = (float)mv / 1000.0f;
      float current = (float)ma / 1000.0f;
      
      hardware.PrintLine("PDO %d: " FLT_FMT3 " V, " FLT_FMT3 " A", 
                        pd_count_written + 1, 
                        FLT_VAR3(voltage), 
                        FLT_VAR3(current));
      pd_count_written++;
      
      // Print footer after last PDO
      if (pd_count_written >= pd_count) {
        hardware.PrintLine("===========================================");
        hardware.PrintLine("");
      }
    } else if (pd_count == 0) {
      // No PD negotiation - check for Type-C current at 5V
      uint32_t typec_current = pd_get_typec_current_limit(0);
      
      if (typec_current > 0 && typec_current != last_typec_current) {
        // Type-C current changed
        if (!typec_header_printed) {
          hardware.PrintLine("");
          hardware.PrintLine("===========================================");
          hardware.PrintLine("   Type-C Current @ 5V (No PD Negotiation)");
          hardware.PrintLine("===========================================");
          typec_header_printed = true;
        }
        
        float current_a = (float)typec_current / 1000.0f;
        hardware.PrintLine("Type-C Current: " FLT_FMT3 " A (5.0 V)", 
                          FLT_VAR3(current_a));
        hardware.PrintLine("===========================================");
        hardware.PrintLine("");
        
        last_typec_current = typec_current;
      } else if (typec_current == 0 && last_typec_current > 0) {
        // Lost connection
        hardware.PrintLine("");
        hardware.PrintLine("=== USB-C Connection Lost ===");
        hardware.PrintLine("");
        typec_header_printed = false;
        last_typec_current = 0;
      }
    }
    
    pd_run_state_machine(0);
    System::Delay(4);
  }

  return 0;
}