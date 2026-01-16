#include "platform.h"
#include "../libDaisy/src/daisy_seed.h"

using namespace daisy;

extern "C" {

void platform_usleep(uint32_t us) {
  System::DelayUs(us);
}

void delayMicroseconds(uint32_t us) {
  System::DelayUs(us);
}

uint32_t millis(void) {
  return System::GetNow();
}

uint32_t micros(void) {
  return System::GetNow() * 1000;
}

}
