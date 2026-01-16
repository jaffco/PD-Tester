/*
 * tcpm_driver.h
 *
 * Created: 11/11/2017 18:42:39
 *  Author: jason
 */ 


#ifndef TCPM_DRIVER_H_
#define TCPM_DRIVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// USB-C Stuff
#include "tcpm.h"
#include "FUSB302.h"
#define CONFIG_USB_PD_PORT_COUNT 1

#ifdef __cplusplus
}

// Global I2C handle for Daisy (C++ side)
#include "../libDaisy/src/per/i2c.h"
extern daisy::I2CHandle* g_i2c_handle;

#else
#endif

#endif /* TCPM_DRIVER_H_ */
