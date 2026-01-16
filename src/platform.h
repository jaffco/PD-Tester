#ifndef PLATFORM_H_
#define PLATFORM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void platform_usleep(uint32_t us);
void delayMicroseconds(uint32_t us);
uint32_t millis(void);
uint32_t micros(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H_ */
