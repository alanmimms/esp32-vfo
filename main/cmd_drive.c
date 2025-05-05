#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_console.h"
#include "cmd_i2c.h"
#include "sdkconfig.h"
#include "argtable3/argtable3.h"
#include "si5351.h"


static const char *TAG = "cmd_drive";
static int driveHandler(int argc, char **argv);

static struct {
  struct arg_int *driveP;
  struct arg_int *clkP;
  struct arg_end *endP;
} args;


void registerDRIVE(void) {
  args.driveP = arg_int1(NULL, NULL, "<drive>", "Clock drive, [0=2mA, 1=4mA, 2=6mA, 3=8mA]");
  args.clkP = arg_int1(NULL, NULL, "<clk number>", "Clock output number to change");
  args.endP = arg_end(5);

  const esp_console_cmd_t cmd = {
    .command = "drive",
    .help = "Set drive strength for a specified clock output",
    .hint = NULL,
    .func = &driveHandler,
    .argtable = &args,
  };

  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd) );
}


// Translate SI5351CLK_xMA to drive strength value in mA.
static int enumToDrive(int e) {

  switch (e) {
  case SI5351CLK_2MA:
    return 2;

  case SI5351CLK_4MA:
    return 4;

  case SI5351CLK_6MA:
    return 6;

  case SI5351CLK_8MA:
    return 8;

  default:
    printf("ERROR: Bad enumToDrive enum %d\n", e);
    return -1;
  }

}


/* 'drive' command */
static int driveHandler(int argc, char **argv) {

  if (arg_parse(argc, argv, (void **) &args) != 0) {
    arg_print_errors(stderr, args.endP, argv[0]);
    return 1;
  }

  printf("%s: drive=%d (%dmA) clk=%d\n", TAG,
	 args.driveP->ival[0], enumToDrive(args.driveP->ival[0]), args.clkP->ival[0]);
  SI5351SetDrive(args.clkP->ival[0], args.driveP->ival[0]);
  return 0;
}
