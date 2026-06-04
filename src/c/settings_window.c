#include <pebble.h>
#include "settings_window.h"
#include "fonts.h"
#include "deck_window.h"

static Window        *s_window;
static MenuLayer     *s_menu;
static StatusBarLayer *s_status;

static const char *NAMES[3] = { "Small", "Medium", "Large" };

static uint16_t get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) { return 3; }
static int16_t  get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return afont_cell_height(); }
static int16_t  get_header_height(MenuLayer *ml, uint16_t section, void *ctx) { return 22; }
static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *ctx2) {
  menu_cell_basic_header_draw(ctx, cell, "Font size");
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *c) {
  GRect b = layer_get_bounds(cell);
  MenuIndex sel = menu_layer_get_selected_index(s_menu);
  bool hl = (sel.row == idx->row);
  graphics_context_set_fill_color(ctx, hl ? GColorBlack : GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, hl ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, NAMES[idx->row], afont_body(),
                     GRect(6, -2, b.size.w - 30, b.size.h),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  if ((int)idx->row == afont_size_idx())   // mark the current size
    graphics_draw_text(ctx, "*", afont_body(),
                       GRect(b.size.w - 26, -2, 20, b.size.h),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
}

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  afont_set_size(idx->row);
  menu_layer_reload_data(s_menu);
  deck_window_refresh();
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  s_status = status_bar_layer_create();
  layer_add_child(root, status_bar_layer_get_layer(s_status));
  s_menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, b.size.w, b.size.h - STATUS_BAR_LAYER_HEIGHT));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .draw_row = draw_row,
    .select_click = select_click,
  });
  menu_layer_set_click_config_onto_window(s_menu, window);
#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_menu, GColorBlack, GColorWhite);
#endif
  menu_layer_set_selected_index(s_menu, (MenuIndex){0, afont_size_idx()}, MenuRowAlignCenter, false);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu); s_menu = NULL;
  status_bar_layer_destroy(s_status); s_status = NULL;
  s_window = NULL;
}

void settings_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load, .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
