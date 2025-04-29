/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* I2C commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_console.h"
#include "cmd_i2c.h"
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "cmd_i2c";
extern i2c_master_dev_handle_t i2cDevH;

static void registerDUMP(void);


void register_i2c(void) {
  registerDUMP();
}


/* 'dump' command */
static int dumpI2C(int argc, char **argv) {
  uint8_t startRegN = 0;
  uint8_t regs[256];
  memset(regs, 0xFF, sizeof(regs));
  i2c_master_transmit_receive(i2cDevH, &startRegN, 1, regs, sizeof(regs), 1000 / portTICK_PERIOD_MS);
  
  printf("\n%s SI5351 Registers:", TAG);

  for (int k=0; k < 256; ++k) {
    if (k % 16 == 0) printf("\n%02X: ", k);
    printf("%02X ", regs[k]);
  }

  printf("\n");
  return 0;
}

static void registerDUMP(void)
{
  const esp_console_cmd_t cmd = {
    .command = "dump",
    .help = "Dump I2C device registers",
    .hint = NULL,
    .func = &dumpI2C,
  };
  ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}
