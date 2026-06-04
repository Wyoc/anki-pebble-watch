#include <pebble.h>
#include "fonts.h"

#define PK_FONT_SIZE 1   // persist key

static const uint32_t BODY_RES[3] = {
  RESOURCE_ID_DEJAVU_18, RESOURCE_ID_DEJAVU_24, RESOURCE_ID_DEJAVU_28
};
static const int CELL_H[3] = { 38, 44, 54 };

static int   s_idx = 1;          // default Medium
static GFont s_body;
static GFont s_strip;

static void load_body(void) {
  if (s_body) fonts_unload_custom_font(s_body);
  s_body = fonts_load_custom_font(resource_get_handle(BODY_RES[s_idx]));
}

void afont_init(void) {
  if (persist_exists(PK_FONT_SIZE)) s_idx = persist_read_int(PK_FONT_SIZE);
  if (s_idx < 0 || s_idx > 2) s_idx = 1;
  s_strip = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DEJAVU_14));
  load_body();
}

void afont_deinit(void) {
  if (s_body)  { fonts_unload_custom_font(s_body);  s_body = NULL; }
  if (s_strip) { fonts_unload_custom_font(s_strip); s_strip = NULL; }
}

void afont_set_size(int idx) {
  if (idx < 0 || idx > 2 || idx == s_idx) return;
  s_idx = idx;
  persist_write_int(PK_FONT_SIZE, s_idx);
  load_body();
}

int   afont_size_idx(void)   { return s_idx; }
GFont afont_body(void)       { return s_body; }
GFont afont_strip(void)      { return s_strip; }
int   afont_cell_height(void){ return CELL_H[s_idx]; }
