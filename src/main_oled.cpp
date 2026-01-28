#include "../libDaisy/src/daisy_seed.h"
#include "dev/oled_ssd1312.h"
#include "util/oled_fonts.h"
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
bool info_displayed = false;
bool pd_displayed = false;  // Track if we've already displayed PD info for this connection
uint32_t startup_time = 0;
const uint32_t PD_NEGOTIATION_TIMEOUT_MS = 2000;  // Wait 2 seconds for PD before showing Type-C

// Saved best PDO values (so they don't change when source sends new caps)
uint32_t saved_best_voltage_mv = 0;
uint32_t saved_best_current_ma = 0;
int saved_best_pdo_index = 0;

using namespace daisy;
using DisplayType = OledDisplay<SSD13124WireSpi128x32Driver>;

static DaisySeed hardware;
static DisplayType display;
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

void InitOLED() {
  DisplayType::Config disp_cfg;
  disp_cfg.driver_config.transport_config.pin_config.dc = DaisySeed::GetPin(9);   // D9
  disp_cfg.driver_config.transport_config.spi_config.nss = SpiHandle::Config::NSS::SOFT;
  disp_cfg.driver_config.transport_config.pin_config.reset = DaisySeed::GetPin(7); // D7
  display.Init(disp_cfg);
  display.Fill(false);
  display.Update();
}

void DisplayPDValues(uint8_t pdo_index, uint32_t voltage_mv, uint32_t current_ma) {
  // Clear display with double-buffer approach
  display.Fill(false);
  display.Update();
  System::Delay(20);
  display.Fill(false);
  
  // Title
  display.SetCursor(0, 0);
  display.WriteString("USB-C Power Delivery", Font_5x8, true);
  
  // PDO info - use integer math
  display.SetCursor(0, 11);
  char buf[24];
  int volts_int = voltage_mv / 1000;
  int volts_dec = (voltage_mv % 1000) / 100;
  int amps_int = current_ma / 1000;
  int amps_dec = (current_ma % 1000) / 10;
  snprintf(buf, sizeof(buf), "PDO%d: %d.%dV %d.%02dA", pdo_index, volts_int, volts_dec, amps_int, amps_dec);
  display.WriteString(buf, Font_5x8, true);
  
  // Power calculation (mV * mA / 1000000 = W), with rounding
  uint32_t power_mw = (voltage_mv / 1000) * current_ma;  // Simplified: V * mA = mW
  int power_w = (power_mw + 500) / 1000;  // Round to nearest watt
  display.SetCursor(0, 22);
  snprintf(buf, sizeof(buf), "Power: %dW", power_w);
  display.WriteString(buf, Font_5x8, true);
  
  display.Update();
  System::Delay(50);
}

void DisplayPDCapability(uint8_t pdo_index, uint32_t pdo) {
  uint32_t ma = 0, mv = 0;
  pd_extract_pdo_power(pdo, &ma, &mv);
  DisplayPDValues(pdo_index, mv, ma);
}

void DisplayTypeCCurrent(uint32_t current_ma) {
  // Clear display with double-buffer approach
  display.Fill(false);
  display.Update();
  System::Delay(20);
  display.Fill(false);
  
  // Title
  display.SetCursor(0, 0);
  display.WriteString("Type-C (No PD)", Font_5x8, true);
  
  // Current info - use integer math
  display.SetCursor(0, 11);
  char buf[24];
  int amps_int = current_ma / 1000;
  int amps_dec = (current_ma % 1000) / 10;  // Two decimal places
  snprintf(buf, sizeof(buf), "5.0V @ %d.%02dA", amps_int, amps_dec);
  display.WriteString(buf, Font_5x8, true);
  
  // Power calculation
  int power_mw = 5 * current_ma;  // 5V * mA = mW
  int power_w = power_mw / 1000;
  int power_dec = (power_mw % 1000) / 100;
  display.SetCursor(0, 22);
  snprintf(buf, sizeof(buf), "Power: %d.%dW", power_w, power_dec);
  display.WriteString(buf, Font_5x8, true);
  
  display.Update();
  System::Delay(50);
}

void DisplayWaiting() {
  // Clear display with double-buffer approach
  display.Fill(false);
  display.Update();
  System::Delay(20);
  display.Fill(false);
  
  display.SetCursor(8, 8);
  display.WriteString("Waiting for", Font_5x8, true);
  
  display.SetCursor(8, 18);
  display.WriteString("USB-C device...", Font_5x8, true);
  
  display.Update();
  System::Delay(50);
}

int main() {
  hardware.Init();
  
  // Wait for power and hardware to stabilize
  System::Delay(1000);
  
  // Initialize I2C for FUSB302 first
  InitI2C4();
  g_i2c_handle = &i2c4;
  
  // Initialize OLED
  System::Delay(100);
  InitOLED();
  System::Delay(200);
  DisplayWaiting();
  
  // Start logging (don't wait for serial connection)
  hardware.StartLog(false);
  
  hardware.PrintLine("===========================================");
  hardware.PrintLine("      USB-C Power Display - OLED Version");
  hardware.PrintLine("===========================================");
  hardware.PrintLine("");
  hardware.PrintLine("OLED initialized.");

  hardware.PrintLine("Initializing USB-PD TCPM...");
  // Initialize PD interrupt pin
  usb_pd_int_pin.Init(DaisySeed::GetPin(27), GPIO::Mode::INPUT);
  hardware.PrintLine("USB-PD TCPM initialized.");

  hardware.PrintLine("Starting USB-PD TCPM state machine...");
  // Init tcpm
  tcpm_init(0);
  System::Delay(50);
  pd_init(0);
  System::Delay(50);
  hardware.PrintLine("USB-PD TCPM state machine started.");
  hardware.PrintLine("");
  
  // Track when we start looking for connections
  startup_time = System::GetNow();
  uint32_t last_connection_time = 0;
  bool waiting_for_pd = false;
  
  while (true) {
    if (usb_pd_int_pin.Read() == false) {
      tcpc_alert(0);
    }
    
    // Check for Type-C current to detect connection
    uint32_t typec_current = pd_get_typec_current_limit(0);
    uint32_t now = System::GetNow();
    
    // Print and display PD capabilities (PD takes priority)
    if (pd_count > 0 && !pd_displayed) {
      // Wait a bit for all PDOs to arrive before processing
      System::Delay(500);
      
      // Find the best 5V PDO (highest current at 5V)
      int best_5v_index = -1;
      uint32_t best_5v_current = 0;
      
      for (int i = 0; i < pd_count; i++) {
        uint32_t ma = 0, mv = 0;
        pd_extract_pdo_power(pd_src_caps[i], &ma, &mv);
        
        // Only consider 5V PDOs (allow 4.5V-5.5V range for tolerance)
        if (mv >= 4500 && mv <= 5500) {
          if (ma > best_5v_current) {
            best_5v_current = ma;
            best_5v_index = i;
          }
        }
        
        // Print all PDOs to serial for reference
        float voltage = (float)mv / 1000.0f;
        float current = (float)ma / 1000.0f;
        hardware.PrintLine("PDO %d: " FLT_FMT3 " V, " FLT_FMT3 " A", 
                          i + 1, 
                          FLT_VAR3(voltage), 
                          FLT_VAR3(current));
      }
      
      // Save and display the best 5V PDO on OLED
      if (best_5v_index >= 0) {
        uint32_t ma = 0, mv = 0;
        pd_extract_pdo_power(pd_src_caps[best_5v_index], &ma, &mv);
        saved_best_voltage_mv = mv;
        saved_best_current_ma = ma;
        saved_best_pdo_index = best_5v_index + 1;
        DisplayPDValues(saved_best_pdo_index, saved_best_voltage_mv, saved_best_current_ma);
        hardware.PrintLine("Selected: PDO %d (best 5V) - %dmV %dmA", saved_best_pdo_index, mv, ma);
      } else {
        // No 5V PDO found, show first one as fallback
        uint32_t ma = 0, mv = 0;
        pd_extract_pdo_power(pd_src_caps[0], &ma, &mv);
        saved_best_voltage_mv = mv;
        saved_best_current_ma = ma;
        saved_best_pdo_index = 1;
        hardware.PrintLine("No 5V PDO found!");
        DisplayPDValues(saved_best_pdo_index, saved_best_voltage_mv, saved_best_current_ma);
      }
      
      pd_count_written = pd_count;  // Mark all as processed
      info_displayed = true;
      pd_displayed = true;  // Don't display again for this connection
      waiting_for_pd = false;
    } else if (pd_count > 0 && typec_current > 0) {
      // PD negotiation complete, we have PDOs and still connected - stay on last PDO
    } else if (typec_current > 0) {
      // Connection detected but no PD yet
      if (!waiting_for_pd) {
        // Just connected - start waiting for PD negotiation
        last_connection_time = now;
        waiting_for_pd = true;
        hardware.PrintLine("Connection detected, waiting for PD...");
      } else if ((now - last_connection_time) > PD_NEGOTIATION_TIMEOUT_MS) {
        // Waited long enough - this is a Type-C only source
        if (typec_current != last_typec_current) {
          DisplayTypeCCurrent(typec_current);
          
          float current_a = (float)typec_current / 1000.0f;
          hardware.PrintLine("Type-C Current: " FLT_FMT3 " A (5.0 V)", 
                            FLT_VAR3(current_a));
          
          last_typec_current = typec_current;
          info_displayed = true;
        }
      }
    } else {
      // No connection
      if (last_typec_current > 0 || waiting_for_pd || pd_count > 0) {
        // Lost connection - add delay to let things settle before updating display
        System::Delay(200);
        
        DisplayWaiting();
        hardware.PrintLine("USB-C Connection Lost");
        last_typec_current = 0;
        info_displayed = false;
        waiting_for_pd = false;
        
        // Reset PD state for next connection
        pd_count = 0;
        pd_count_written = 0;
        pd_displayed = false;
      }
    }
    
    pd_run_state_machine(0);
    System::Delay(50);
  }

  return 0;
}
