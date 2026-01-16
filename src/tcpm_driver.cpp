/*
 * tcpm_driver.c
 *
 * Created: 11/11/2017 18:42:26
 *  Author: jason
 */ 

#include "tcpm_driver.h"
#include "../libDaisy/src/daisy_seed.h"
using namespace daisy;

// Global I2C handle pointer - set this from main
I2CHandle* g_i2c_handle = nullptr;

extern const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT];

extern "C" {

/* I2C wrapper functions using Daisy's I2CHandle */
int tcpc_write(int port, int reg, int val)
{
  if (!g_i2c_handle) return -1;
  
  uint8_t data[2] = {(uint8_t)(reg & 0xFF), (uint8_t)(val & 0xFF)};
  I2CHandle::Result result = g_i2c_handle->TransmitBlocking(
    fusb302_I2C_SLAVE_ADDR, data, 2, 100);
  
  return (result == I2CHandle::Result::OK) ? 0 : -1;
}

int tcpc_write16(int port, int reg, int val)
{
  if (!g_i2c_handle) return -1;
  
  uint8_t data[3] = {
    (uint8_t)(reg & 0xFF),
    (uint8_t)(val & 0xFF),
    (uint8_t)((val >> 8) & 0xFF)
  };
  I2CHandle::Result result = g_i2c_handle->TransmitBlocking(
    fusb302_I2C_SLAVE_ADDR, data, 3, 100);
  
  return (result == I2CHandle::Result::OK) ? 0 : -1;
}

int tcpc_read(int port, int reg, int *val)
{
  if (!g_i2c_handle) return -1;
  
  uint8_t reg_addr = reg & 0xFF;
  uint8_t data;
  
  // Write register address
  I2CHandle::Result result = g_i2c_handle->TransmitBlocking(
    fusb302_I2C_SLAVE_ADDR, &reg_addr, 1, 100);
  if (result != I2CHandle::Result::OK) return -1;
  
  // Read data
  result = g_i2c_handle->ReceiveBlocking(
    fusb302_I2C_SLAVE_ADDR, &data, 1, 100);
  if (result != I2CHandle::Result::OK) return -1;
  
  *val = data;
  return 0;
}

int tcpc_read16(int port, int reg, int *val)
{
  if (!g_i2c_handle) return -1;
  
  uint8_t reg_addr = reg & 0xFF;
  uint8_t data[2];
  
  // Write register address
  I2CHandle::Result result = g_i2c_handle->TransmitBlocking(
    fusb302_I2C_SLAVE_ADDR, &reg_addr, 1, 100);
  if (result != I2CHandle::Result::OK) return -1;
  
  // Read data
  result = g_i2c_handle->ReceiveBlocking(
    fusb302_I2C_SLAVE_ADDR, data, 2, 100);
  if (result != I2CHandle::Result::OK) return -1;
  
  *val = data[0] | (data[1] << 8);
  return 0;
}

int tcpc_xfer(int port,
  const uint8_t *out, int out_size,
  uint8_t *in, int in_size,
  int flags)
{
  if (!g_i2c_handle) return -1;
  
  I2CHandle::Result result;
  
  if (out_size) {
    result = g_i2c_handle->TransmitBlocking(
      fusb302_I2C_SLAVE_ADDR, (uint8_t*)out, out_size, 100);
    if (result != I2CHandle::Result::OK) return -1;
  }
  
  if (in_size) {
    result = g_i2c_handle->ReceiveBlocking(
      fusb302_I2C_SLAVE_ADDR, in, in_size, 100);
    if (result != I2CHandle::Result::OK) return -1;
  }
  
  return 0;
}
} // extern "C"
