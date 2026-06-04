#include <pebble.h>
#include "deck_window.h"
#include "comm.h"
#include "protocol.h"
#include "review_window.h"
#include "fonts.h"
#include "settings_window.h"

typedef struct {
  char name[DECK_NAME_LEN];   // full Anki path, e.g. "Spanish::Verbs"
  char id[DECK_ID_LEN];
  char counts[DECK_COUNTS_LEN];
} DeckEntry;

typedef enum { VA_DRILL, VA_REVIEW } ViewAction;

typedef struct {
  char label[VIEW_LABEL_LEN]; // one path segment, or "Review all"
  ViewAction action;
  int deck_index;             // index into s_decks for VA_REVIEW
  bool folder;
  char counts[DECK_COUNTS_LEN];
} ViewRow;

static Window *s_window;
static MenuLayer *s_menu;
static StatusBarLayer *s_status;
static DeckEntry s_decks[MAX_DECKS];
static int s_deck_count;
static char s_prefix[DECK_NAME_LEN];   // current drill path ("" = top, else ends in "::")
static ViewRow s_view[MAX_VIEW];
static int s_view_count;
static AppTimer *s_retry;
static int s_retry_n;

static void retry_fire(void *data) {
  s_retry = NULL;
  if (s_deck_count > 0) return;
  comm_request_decks();
  if (++s_retry_n < 8) s_retry = app_timer_register(1500, retry_fire, NULL);
}

static int find_deck_exact(const char *name) {
  for (int i = 0; i < s_deck_count; i++)
    if (strcmp(s_decks[i].name, name) == 0) return i;
  return -1;
}

// Recompute the rows shown for the current hierarchy level from the flat deck list.
static void rebuild_view(void) {
  s_view_count = 0;
  int plen = strlen(s_prefix);

  // "Review all" when inside a folder whose parent path is itself a real deck
  if (plen > 0) {
    char parent[DECK_NAME_LEN];
    strncpy(parent, s_prefix, DECK_NAME_LEN - 1); parent[DECK_NAME_LEN - 1] = '\0';
    int pl = strlen(parent);
    if (pl >= 2) parent[pl - 2] = '\0';   // strip trailing "::"
    int pidx = find_deck_exact(parent);
    if (pidx >= 0 && s_view_count < MAX_VIEW) {
      ViewRow *v = &s_view[s_view_count++];
      strncpy(v->label, "Review all", VIEW_LABEL_LEN - 1); v->label[VIEW_LABEL_LEN - 1] = '\0';
      v->action = VA_REVIEW; v->deck_index = pidx; v->folder = false;
      strncpy(v->counts, s_decks[pidx].counts, DECK_COUNTS_LEN - 1); v->counts[DECK_COUNTS_LEN - 1] = '\0';
    }
  }

  int child_start = s_view_count;
  for (int i = 0; i < s_deck_count; i++) {
    if (strncmp(s_decks[i].name, s_prefix, plen) != 0) continue;
    const char *rest = s_decks[i].name + plen;
    if (rest[0] == '\0') continue;                 // the parent deck itself
    const char *sep = strstr(rest, DECK_SEP);
    int seglen = sep ? (int)(sep - rest) : (int)strlen(rest);
    if (seglen <= 0) continue;
    char seg[VIEW_LABEL_LEN];
    int n = seglen < VIEW_LABEL_LEN - 1 ? seglen : VIEW_LABEL_LEN - 1;
    memcpy(seg, rest, n); seg[n] = '\0';
    bool is_folder = (sep != NULL);
    bool is_exact = (sep == NULL);

    int found = -1;
    for (int k = child_start; k < s_view_count; k++)
      if (strcmp(s_view[k].label, seg) == 0) { found = k; break; }

    if (found >= 0) {
      if (is_folder) { s_view[found].folder = true; s_view[found].action = VA_DRILL; }
      if (is_exact && !s_view[found].folder) {
        s_view[found].action = VA_REVIEW; s_view[found].deck_index = i;
        strncpy(s_view[found].counts, s_decks[i].counts, DECK_COUNTS_LEN - 1);
        s_view[found].counts[DECK_COUNTS_LEN - 1] = '\0';
      }
    } else if (s_view_count < MAX_VIEW) {
      ViewRow *v = &s_view[s_view_count++];
      strncpy(v->label, seg, VIEW_LABEL_LEN - 1); v->label[VIEW_LABEL_LEN - 1] = '\0';
      v->folder = is_folder;
      if (is_folder) { v->action = VA_DRILL; v->deck_index = -1; v->counts[0] = '\0'; }
      else {
        v->action = VA_REVIEW; v->deck_index = i;
        strncpy(v->counts, s_decks[i].counts, DECK_COUNTS_LEN - 1); v->counts[DECK_COUNTS_LEN - 1] = '\0';
      }
    }
  }
}

static void start_review(int deck_index) {
  if (deck_index < 0 || deck_index >= s_deck_count) return;
  review_window_push(s_decks[deck_index].id, s_decks[deck_index].name);
  comm_request_next(s_decks[deck_index].id);
}

static void handle_select(int row) {
  if (s_deck_count == 0 || row < 0 || row >= s_view_count) return;
  ViewRow *v = &s_view[row];
  if (v->action == VA_DRILL) {
    int pl = strlen(s_prefix);
    snprintf(s_prefix + pl, DECK_NAME_LEN - pl, "%s%s", v->label, DECK_SEP);
    rebuild_view();
    menu_layer_set_selected_index(s_menu, (MenuIndex){0, 0}, MenuRowAlignTop, false);
    menu_layer_reload_data(s_menu);
  } else {
    start_review(v->deck_index);
  }
}

static bool go_up(void) {
  int len = strlen(s_prefix);
  if (len == 0) return false;
  if (len >= 2) s_prefix[len - 2] = '\0';     // drop trailing "::"
  char *last = NULL; char *p = s_prefix;
  while ((p = strstr(p, DECK_SEP)) != NULL) { last = p; p += 2; }
  if (last) last[2] = '\0'; else s_prefix[0] = '\0';
  rebuild_view();
  menu_layer_set_selected_index(s_menu, (MenuIndex){0, 0}, MenuRowAlignTop, false);
  menu_layer_reload_data(s_menu);
  return true;
}

// ---- menu callbacks ----
static uint16_t get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  if (s_deck_count == 0) return 1;
  return s_view_count > 0 ? s_view_count : 1;
}
static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  return afont_cell_height();   // tracks the configured font size
}

// Custom cell draw (instead of menu_cell_basic_draw) so deck titles can use the
// Cyrillic-capable DejaVu font and respect the configured size.
static void draw_row(GContext *gctx, const Layer *cell, MenuIndex *idx, void *ctx) {
  GRect b = layer_get_bounds(cell);
  MenuIndex sel = menu_layer_get_selected_index(s_menu);
  bool hl = (sel.section == idx->section && sel.row == idx->row);
  graphics_context_set_fill_color(gctx, hl ? GColorBlack : GColorWhite);
  graphics_fill_rect(gctx, b, 0, GCornerNone);
  graphics_context_set_text_color(gctx, hl ? GColorWhite : GColorBlack);

  const char *title = "(empty)";
  const char *sub = NULL;
  if (s_deck_count == 0) { title = "Loading decks..."; }
  else if (idx->row < s_view_count) {
    ViewRow *v = &s_view[idx->row];
    title = v->label;
    sub = (v->action == VA_DRILL) ? "open  >" : (v->counts[0] ? v->counts : NULL);
  }
  int sub_h = sub ? 16 : 0;
  graphics_draw_text(gctx, title, afont_body(),
                     GRect(4, -2, b.size.w - 8, b.size.h - sub_h + 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (sub)
    graphics_draw_text(gctx, sub, afont_strip(),
                       GRect(4, b.size.h - sub_h - 2, b.size.w - 8, sub_h + 2),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ---- custom clicks (so Back navigates up the hierarchy) ----
static void click_up(ClickRecognizerRef r, void *c) { menu_layer_set_selected_next(s_menu, true, MenuRowAlignCenter, true); }
static void click_down(ClickRecognizerRef r, void *c) { menu_layer_set_selected_next(s_menu, false, MenuRowAlignCenter, true); }
static void click_select(ClickRecognizerRef r, void *c) { handle_select(menu_layer_get_selected_index(s_menu).row); }
static void open_settings(ClickRecognizerRef r, void *c) { settings_window_push(); }
static void click_back(ClickRecognizerRef r, void *c) {
  if (s_prefix[0] == '\0') window_stack_pop(true);   // top level -> exit app
  else go_up();
}
static void click_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, click_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, click_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, click_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, open_settings, NULL);  // hold Select = settings
  window_single_click_subscribe(BUTTON_ID_BACK, click_back);
}

void deck_window_add_item(const char *name, const char *id,
                          uint16_t index, uint16_t total, const char *counts) {
  if (index == 0) { s_deck_count = 0; s_prefix[0] = '\0'; }   // fresh stream -> clear stale decks
  if (index >= MAX_DECKS) return;
  DeckEntry *d = &s_decks[index];
  strncpy(d->name, name, DECK_NAME_LEN - 1);   d->name[DECK_NAME_LEN - 1] = '\0';
  strncpy(d->id, id, DECK_ID_LEN - 1);         d->id[DECK_ID_LEN - 1] = '\0';
  strncpy(d->counts, counts, DECK_COUNTS_LEN - 1); d->counts[DECK_COUNTS_LEN - 1] = '\0';
  if (index + 1 > s_deck_count) s_deck_count = index + 1;
  if (s_retry) { app_timer_cancel(s_retry); s_retry = NULL; }
  rebuild_view();
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_status = status_bar_layer_create();
  layer_add_child(root, status_bar_layer_get_layer(s_status));

  GRect mb = GRect(b.origin.x, b.origin.y + STATUS_BAR_LAYER_HEIGHT,
                   b.size.w, b.size.h - STATUS_BAR_LAYER_HEIGHT);
  s_menu = menu_layer_create(mb);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .draw_row = draw_row,
    .get_cell_height = get_cell_height,
  });
  window_set_click_config_provider_with_context(window, click_provider, s_menu);
#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_menu, GColorBlack, GColorWhite);
#endif
  layer_add_child(root, menu_layer_get_layer(s_menu));

  s_retry_n = 0;
  comm_request_decks();
  s_retry = app_timer_register(1500, retry_fire, NULL);
}

static void window_unload(Window *window) {
  if (s_retry) { app_timer_cancel(s_retry); s_retry = NULL; }
  menu_layer_destroy(s_menu); s_menu = NULL;
  status_bar_layer_destroy(s_status); s_status = NULL;
}

void deck_window_refresh(void) {
  if (s_menu) menu_layer_reload_data(s_menu);
}

void deck_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load, .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
