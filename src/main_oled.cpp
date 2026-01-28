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

// BC1.2 detection types
enum BC12Type {
  BC12_NONE = 0,
  BC12_SDP,   // Standard Downstream Port - 500mA
  BC12_CDP,   // Charging Downstream Port - 1500mA
  BC12_DCP    // Dedicated Charging Port - 1500mA+
};

// BC1.2 state tracking
bool bc12_detected = false;
BC12Type bc12_type = BC12_NONE;
uint32_t bc12_current_ma = 0;

// FUSB302 register definitions for direct access
#define FUSB302_REG_STATUS0     0x40
#define FUSB302_STATUS0_VBUSOK  (1<<7)
#define FUSB302_I2C_ADDR        0x22

using namespace daisy;
using DisplayType = OledDisplay<SSD13124WireSpi128x32Driver>;

static DaisySeed hardware;
static DisplayType display;
GPIO usb_pd_int_pin;
GPIO dp_pin;     // D30 - D+ for BC1.2 detection
GPIO dm_pin;     // D29 - D- for BC1.2 detection
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

void InitBC12Pins() {
  // Initialize D+ and D- with pull-down for BC1.2 detection
  dp_pin.Init(DaisySeed::GetPin(30), GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
  dm_pin.Init(DaisySeed::GetPin(29), GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
}

// Read VBUS status from FUSB302's STATUS0 register (VBUSOK bit)
// This is more reliable than using D28 GPIO which may have hardware issues
bool ReadVBus() {
  uint8_t status0 = 0;
  I2CHandle::Result result = i2c4.ReadDataAtAddress(FUSB302_I2C_ADDR, FUSB302_REG_STATUS0, 1, &status0, 1, 100);
  if (result != I2CHandle::Result::OK) {
    return false;
  }
  return (status0 & FUSB302_STATUS0_VBUSOK) != 0;
}

// BC1.2 detection - based on USB Battery Charging Specification
// Uses D+ (D30) and D- (D29) pins
BC12Type DetectBC12() {
  hardware.PrintLine("Starting BC1.2 detection...");
  hardware.PrintLine("D+ on D30, D- on D29");
  
  // CRITICAL: Verify VBUS is actually present before detection
  // BC1.2 requires VBUS to be present for valid detection
  bool vbusPresent = ReadVBus();
  if (!vbusPresent) {
    hardware.PrintLine("ERROR: BC1.2 called but VBUS NOT present!");
    hardware.PrintLine("Cannot perform BC1.2 detection without VBUS.");
    return BC12_NONE;
  }
  hardware.PrintLine("VBUS confirmed present via FUSB302");
  
  // Ensure both pins start as inputs (high-Z)
  dp_pin.Init(DaisySeed::GetPin(30), GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
  dm_pin.Init(DaisySeed::GetPin(29), GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
  System::Delay(5);
  
  // Primary Detection: Drive D+ HIGH, measure D-
  hardware.PrintLine("Primary Detection: Drive D+ HIGH, measure D-");
  
  dp_pin.Init(DaisySeed::GetPin(30), GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
  dp_pin.Write(true);  // Drive D+ HIGH
  
  dm_pin.Init(DaisySeed::GetPin(29), GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
  System::Delay(1);
  
  bool dm_response = dm_pin.Read();
  hardware.PrintLine("  D- response: %s", dm_response ? "HIGH" : "LOW");
  
  // Release D+
  dp_pin.Init(DaisySeed::GetPin(30), GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
  
  if (!dm_response) {
    // D- did NOT respond -> Standard Downstream Port (SDP)
    hardware.PrintLine("Result: SDP (Standard Downstream Port) - 500mA");
    return BC12_SDP;
  }
  
  // Secondary Detection: Drive D- HIGH, measure D+
  hardware.PrintLine("Secondary Detection: Drive D- HIGH, measure D+");
  
  dm_pin.Init(DaisySeed::GetPin(29), GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
  dm_pin.Write(true);  // Drive D- HIGH
  
  dp_pin.Init(DaisySeed::GetPin(30), GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
  System::Delay(1);
  
  bool dp_response = dp_pin.Read();
  hardware.PrintLine("  D+ response: %s", dp_response ? "HIGH" : "LOW");
  
  // Release D-
  dm_pin.Init(DaisySeed::GetPin(29), GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
  
  if (dp_response) {
    // D+ responds -> Dedicated Charging Port (DCP)
    hardware.PrintLine("Result: DCP (Dedicated Charging Port) - 1500mA+");
    return BC12_DCP;
  } else {
    // D+ does not respond -> Charging Downstream Port (CDP)
    hardware.PrintLine("Result: CDP (Charging Downstream Port) - 1500mA");
    return BC12_CDP;
  }
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

void DisplayBC12Power(BC12Type type, uint32_t current_ma) {
  // Clear display with double-buffer approach
  display.Fill(false);
  display.Update();
  System::Delay(20);
  display.Fill(false);
  
  // Title based on port type
  display.SetCursor(0, 0);
  switch (type) {
    case BC12_SDP:
      display.WriteString("USB-A SDP Port", Font_5x8, true);
      break;
    case BC12_CDP:
      display.WriteString("USB-A CDP Port", Font_5x8, true);
      break;
    case BC12_DCP:
      display.WriteString("USB-A DCP Charger", Font_5x8, true);
      break;
    default:
      display.WriteString("USB-A (Unknown)", Font_5x8, true);
      break;
  }
  
  // Current info
  display.SetCursor(0, 11);
  char buf[24];
  int amps_int = current_ma / 1000;
  int amps_dec = (current_ma % 1000) / 10;
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

int main() {
  hardware.Init();
  
  // Wait for power and hardware to stabilize
  System::Delay(1000);
  
  // Initialize I2C for FUSB302 first
  InitI2C4();
  g_i2c_handle = &i2c4;
  
  // Initialize BC1.2 detection pins (VBUS, D+, D-)
  InitBC12Pins();
  
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
  uint32_t no_cc_start_time = 0;  // When we first saw no CC connection
  bool waiting_for_pd = false;
  bool usb_a_mode = false;  // USB-A adapter detected (VBUS but no CC)
  bool waiting_for_usb_a = false;  // Waiting to confirm USB-A (no CC but VBUS)
  
  // Track how long we've been in "idle/waiting" state with no USB-C connection
  // Only allow USB-A detection after being idle for a while (prevents false triggers after USB-C disconnect)
  uint32_t idle_start_time = System::GetNow();
  const uint32_t USB_A_IDLE_REQUIRED_MS = 3000;  // Must be idle for 3 seconds before USB-A detection
  bool last_vbus_state = ReadVBus();
  bool vbus_was_present_at_startup = last_vbus_state;  // Track if VBUS was present at boot
  bool vbus_transitioned_on = false;  // True only if we saw VBUS go OFF then ON
  
  if (vbus_was_present_at_startup) {
    hardware.PrintLine("VBUS present at startup - waiting for VBUS cycle before USB-A detection");
  } else {
    hardware.PrintLine("No VBUS at startup - USB-A detection ready");
  }
  
  while (true) {
    if (usb_pd_int_pin.Read() == false) {
      tcpc_alert(0);
    }
    
    // Check for Type-C current to detect connection
    uint32_t typec_current = pd_get_typec_current_limit(0);
    uint32_t now = System::GetNow();
    
    // Check for VBUS (for USB-A adapter detection)
    bool vbus_present = ReadVBus();
    
    // Track VBUS transitions - if VBUS goes OFF, we can trust the next ON transition
    if (!vbus_present && last_vbus_state) {
      // VBUS went OFF - next time it goes ON, we know it's a real connection
      vbus_transitioned_on = false;
      hardware.PrintLine("VBUS went OFF");
    } else if (vbus_present && !last_vbus_state) {
      // VBUS went ON - this is a real connection (not board power)
      vbus_transitioned_on = true;
      hardware.PrintLine("VBUS went ON - USB-A detection enabled");
    }
    last_vbus_state = vbus_present;
    
    // If we detect USB-C connection, immediately exit USB-A mode and reset idle timer
    if (typec_current > 0 && usb_a_mode) {
      hardware.PrintLine("USB-C detected, exiting USB-A mode");
      usb_a_mode = false;
      waiting_for_usb_a = false;
      bc12_detected = false;
      bc12_type = BC12_NONE;
      bc12_current_ma = 0;
    }
    
    // Reset idle timer and vbus_transitioned_on whenever we have any USB-C activity
    if (typec_current > 0 || waiting_for_pd || pd_count > 0) {
      idle_start_time = now;  // Reset - we're not idle
      vbus_transitioned_on = false;  // Reset - need fresh VBUS cycle for USB-A
    }
    
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
    } else if (typec_current == 0 && vbus_present && !usb_a_mode && vbus_transitioned_on && (now - idle_start_time) > USB_A_IDLE_REQUIRED_MS) {
      // No CC connection + VBUS present + VBUS transitioned OFF->ON + been idle long enough
      // This means we have a real USB-A adapter connection
      // Wait for additional timeout before confirming (to avoid false positives)
      if (!waiting_for_usb_a) {
        // Start waiting
        no_cc_start_time = now;
        waiting_for_usb_a = true;
        hardware.PrintLine("VBUS transitioned ON while idle, waiting to confirm USB-A...");
      } else if ((now - no_cc_start_time) > PD_NEGOTIATION_TIMEOUT_MS) {
        // Waited long enough - this is a USB-A adapter!
        hardware.PrintLine("Confirmed USB-A adapter mode");
        usb_a_mode = true;
        waiting_for_usb_a = false;
        
        // Perform BC1.2 detection
        bc12_type = DetectBC12();
        bc12_detected = true;
        
        // Set current based on BC1.2 type
        switch (bc12_type) {
          case BC12_SDP:
            bc12_current_ma = 500;
            break;
          case BC12_CDP:
          case BC12_DCP:
            bc12_current_ma = 1500;
            break;
          default:
            bc12_current_ma = 500;
            break;
        }
        
        // Display BC1.2 power info
        DisplayBC12Power(bc12_type, bc12_current_ma);
        info_displayed = true;
      }
    } else if (usb_a_mode && !vbus_present) {
      // USB-A adapter disconnected
      hardware.PrintLine("USB-A Connection Lost");
      usb_a_mode = false;
      waiting_for_usb_a = false;
      bc12_detected = false;
      bc12_type = BC12_NONE;
      bc12_current_ma = 0;
      info_displayed = false;
      idle_start_time = now;  // Reset idle timer
      DisplayWaiting();
    } else if (!vbus_present && !usb_a_mode) {
      // No connection (no VBUS and no CC)
      waiting_for_usb_a = false;  // Reset USB-A waiting state
      if (last_typec_current > 0 || waiting_for_pd || pd_count > 0) {
        // Lost connection - add delay to let things settle before updating display
        System::Delay(200);
        
        DisplayWaiting();
        hardware.PrintLine("USB-C Connection Lost");
        last_typec_current = 0;
        info_displayed = false;
        waiting_for_pd = false;
        idle_start_time = System::GetNow();  // Reset idle timer after disconnect
        
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
