#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
  struct arg_rex *freqP;
  struct arg_int *clkP;
  struct arg_end *endP;
} args;


void registerFREQ(void) {
  args.freqP = arg_rex1(NULL, NULL,
			"[0-9.]+[kmKM]?", NULL, 0,
			"Clock frequency in Hz or followed by K (kHz) or M (MHz).");
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


// Return nonzero if stringP ends with ch - case insensitively.
static int endsWith(const char *stringP, char ch) {
  int len = strlen(stringP);
  return (len > 0 && tolower(stringP[len-1]) == tolower(ch));
}


/* 'freq' command */
static int freqHandler(int argc, char **argv) {

  if (arg_parse(argc, argv, (void **) &args) != 0) {
    arg_print_errors(stderr, args.endP, argv[0]);
    return 1;
  }

  const char *fp = args.freqP->sval[0];
  int multiplier;

  if (endsWith(fp, 'k'))
    multiplier = 1000;
  else if (endsWith(fp, 'm'))
    multiplier = 1000*1000;
  else
    multiplier = 1;

  int freq = (int) (atof(fp) * multiplier);
  printf("  %s: freq=%s (%dHz) clk=%d\n", TAG, fp, freq, args.clkP->ival[0]);
  SI5351SetFreq(args.clkP->ival[0], freq);
  return 0;
}
