#include "../libDaisy/src/daisy_seed.h"

using namespace daisy;

static DaisySeed hardware;

int main() {
  hardware.Init();

  hardware.StartLog(); 
  // hardware.StartLog(true); // waits for connection before proceeding

  System::Delay(200);
  hardware.PrintLine("===========================================");
  hardware.PrintLine("                It's alive!                ");
  hardware.PrintLine("===========================================");
  hardware.PrintLine("");

  // blink
  while (true) {
    static bool ledState = false;
    ledState = !ledState;
    hardware.SetLed(ledState);
    hardware.PrintLine("LED is now %s", ledState ? "ON" : "OFF");
    System::Delay(500);
  }

  return 0;
}