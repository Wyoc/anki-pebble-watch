#include <pebble.h>
#include "comm.h"
#include "deck_window.h"
#include "fonts.h"

static void init(void) {
  afont_init();         // load fonts (persisted size) before any window renders
  comm_init();
  deck_window_push();   // requests the deck list (with retry) on load
}

static void deinit(void) {
  afont_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
