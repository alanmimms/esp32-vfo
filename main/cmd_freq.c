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


static const char *TAG = "cmd_freq";
static int freqHandler(int argc, char **argv);

static struct {
  struct arg_int *freqP;
  struct arg_int *clkP;
  struct arg_end *endP;
} args;


void registerFREQ(void) {
  args.freqP = arg_int1(NULL, NULL, "<freq>", "Clock frequency, Hz");
  args.clkP = arg_int1(NULL, NULL, "<clk number>", "Clock output number to change");
  args.endP = arg_end(5);

  const esp_console_cmd_t cmd = {
    .command = "freq",
    .help = "Set frequency for a specified clock output",
    .hint = NULL,
    .func = &freqHandler,
    .argtable = &args,
  };

  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd) );
}


/* 'freq' command */
static int freqHandler(int argc, char **argv) {

  if (arg_parse(argc, argv, (void **) &args) != 0) {
    arg_print_errors(stderr, args.endP, argv[0]);
    return 1;
  }

  printf("%s: freq=%d clk=%d\n", TAG, args.freqP->ival[0], args.clkP->ival[0]);
  SI5351SetFreq(args.clkP->ival[0], args.freqP->ival[0]);
  SI5351SetDrive(args.clkP->ival[0], SI5351CLK_8MA);
  return 0;
}
