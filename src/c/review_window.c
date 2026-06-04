#include <pebble.h>
#include "review_window.h"
#include "comm.h"
#include "protocol.h"
#include "fonts.h"

static int s_scr_w = 144;     // real screen width, set in window_load (diorite 144 / emery 200)
#define COL_W     (s_scr_w - ACTION_BAR_WIDTH) // text column to the left of the bar
#define STRIP_H   14          // top context strip
#define FOOTER_H  28          // 2-line "hold to rate / Back: Again" footer (answer only)
#define LONG_ANSWER 220       // chars above which the answer shrinks to GOTHIC_18

typedef enum { RS_LOADING, RS_QUESTION, RS_ANSWER, RS_EMPTY, RS_ERROR } ReviewState;
typedef enum { GLYPH_NONE, GLYPH_SPINNER, GLYPH_CHECK, GLYPH_BANG, GLYPH_QMARK } Glyph;

static Window         *s_window;
static Layer          *s_strip;     // top context strip (deck name)
static ScrollLayer    *s_scroll;
static TextLayer      *s_text;      // scrollable Q/A body
static TextLayer      *s_msg;       // big centered message (loading/empty/error)
static TextLayer      *s_sub;       // small subline under s_msg
static TextLayer      *s_legend;    // 1-line footer, answer only
static Layer          *s_state_icon;// drawn glyph for message states
static ActionBarLayer *s_action_bar;
static GBitmap        *s_ic_reveal, *s_ic_easy, *s_ic_ok, *s_ic_hard;
static AppTimer       *s_watchdog;
static AppTimer       *s_spin_timer;

static ReviewState s_state;
static Glyph s_glyph;
static int  s_spin;            // spinner angle (deg)
static char s_deck_id[DECK_ID_LEN];
static char s_deck_name[DECK_NAME_LEN];
static char s_note_id[NOTE_ID_LEN];
static uint8_t s_card_ord;
static uint8_t s_button_count;
static char *s_q;
static char *s_a;
static bool  s_answer_ready;
static bool  s_want_reveal;

static void click_config_provider(void *context);  // fwd decl

static char *dup_str(const char *src) {
  if (!src) src = "";
  int n = strlen(src);
  char *p = malloc(n + 1);
  if (p) memcpy(p, src, n + 1);
  return p;
}

// ---- top strip (deck last-segment + 1px divider) ----
static void strip_update(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  const char *seg = s_deck_name, *p = s_deck_name, *f;
  while ((f = strstr(p, DECK_SEP)) != NULL) { seg = f + 2; p = f + 2; }
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, seg, afont_strip(),
                     GRect(4, -3, b.size.w - 6, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_line(ctx, GPoint(0, b.size.h - 1), GPoint(b.size.w - 1, b.size.h - 1));
}

// ---- drawn glyphs for loading/empty/error ----
static void state_icon_update(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  int cx = b.size.w / 2, cy = b.size.h / 2;
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 3);
  switch (s_glyph) {
    case GLYPH_SPINNER:
      graphics_fill_radial(ctx, GRect(cx - 13, cy - 13, 26, 26), GOvalScaleModeFitCircle, 5,
                           DEG_TO_TRIGANGLE(s_spin), DEG_TO_TRIGANGLE(s_spin + 70));
      break;
    case GLYPH_CHECK:
      graphics_draw_line(ctx, GPoint(cx - 12, cy + 1), GPoint(cx - 3, cy + 11));
      graphics_draw_line(ctx, GPoint(cx - 3, cy + 11), GPoint(cx + 14, cy - 11));
      break;
    case GLYPH_BANG:
    case GLYPH_QMARK:
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_circle(ctx, GPoint(cx, cy), 15);
      graphics_draw_text(ctx, s_glyph == GLYPH_BANG ? "!" : "?",
                         fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                         GRect(cx - 12, cy - 17, 24, 28),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
      break;
    default: break;
  }
}

// ---- state rendering ----
static void show_state(const char *msg, const char *sub, Glyph g) {
  layer_set_hidden(scroll_layer_get_layer(s_scroll), true);
  layer_set_hidden(text_layer_get_layer(s_legend), true);
  layer_set_hidden(s_strip, true);
  action_bar_layer_remove_from_window(s_action_bar);
  text_layer_set_text(s_msg, msg);
  layer_set_hidden(text_layer_get_layer(s_msg), false);
  text_layer_set_text(s_sub, sub ? sub : "");
  layer_set_hidden(text_layer_get_layer(s_sub), sub == NULL);
  s_glyph = g;
  layer_set_hidden(s_state_icon, g == GLYPH_NONE);
  layer_mark_dirty(s_state_icon);
}

static void show_scroll_text(const char *txt, bool answer_mode) {
  Layer *root = window_get_root_layer(s_window);
  GRect b = layer_get_bounds(root);

  layer_set_hidden(text_layer_get_layer(s_msg), true);
  layer_set_hidden(text_layer_get_layer(s_sub), true);
  layer_set_hidden(s_state_icon, true);
  layer_set_hidden(s_strip, false);
  layer_mark_dirty(s_strip);

  int footer = answer_mode ? FOOTER_H : 0;
  layer_set_frame(scroll_layer_get_layer(s_scroll),
                  GRect(0, STRIP_H, COL_W, b.size.h - STRIP_H - footer));

  // DejaVu (Latin + Cyrillic) at the user's configured size, for Q and A alike
  text_layer_set_font(s_text, afont_body());

  layer_set_frame(text_layer_get_layer(s_text), GRect(5, 2, COL_W - 6, 4000));
  text_layer_set_text(s_text, txt);
  GSize cs = text_layer_get_content_size(s_text);
  layer_set_frame(text_layer_get_layer(s_text), GRect(5, 2, COL_W - 6, cs.h + 8));
  scroll_layer_set_content_size(s_scroll, GSize(COL_W, cs.h + 10));
  scroll_layer_set_content_offset(s_scroll, GPoint(0, 0), false);
  layer_set_hidden(scroll_layer_get_layer(s_scroll), false);

  // ActionBar icons: question = reveal only; answer = the three grades
  if (answer_mode) {
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, s_ic_easy);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_SELECT, s_ic_ok);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN, s_ic_hard);
    text_layer_set_text(s_legend, "Hold to rate\nBack = Again");
  } else {
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, NULL);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_SELECT, s_ic_reveal);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN, NULL);
  }
  action_bar_layer_add_to_window(s_action_bar, s_window);
  layer_set_hidden(text_layer_get_layer(s_legend), !answer_mode);
}

// ---- click handlers ----
static void grade(uint8_t intent) {
  comm_submit_grade(intent, s_note_id, s_card_ord, s_deck_id);
  review_window_set_state(SS_LOADING, "");
}
static void click_easy(ClickRecognizerRef r, void *c)  { grade(GRADE_EASY); }
static void click_ok(ClickRecognizerRef r, void *c)    { grade(GRADE_OK); }
static void click_hard(ClickRecognizerRef r, void *c)  { grade(GRADE_HARD); }
static void click_again(ClickRecognizerRef r, void *c) { grade(GRADE_AGAIN); }
static void click_abandon(ClickRecognizerRef r, void *c) { window_stack_pop(true); }

static void click_reveal(ClickRecognizerRef r, void *c) {
  s_state = RS_ANSWER;
  if (s_answer_ready) {
    show_scroll_text(s_a ? s_a : "", true);
  } else {
    s_want_reveal = true;
    show_state("Loading answer", NULL, GLYPH_SPINNER);
  }
  window_set_click_config_provider(s_window, click_config_provider);
}

static void click_config_provider(void *context) {
  switch (s_state) {
    case RS_QUESTION:
      window_set_click_context(BUTTON_ID_UP, s_scroll);
      window_set_click_context(BUTTON_ID_DOWN, s_scroll);
      window_single_click_subscribe(BUTTON_ID_UP,   scroll_layer_scroll_up_click_handler);
      window_single_click_subscribe(BUTTON_ID_DOWN, scroll_layer_scroll_down_click_handler);
      window_single_click_subscribe(BUTTON_ID_SELECT, click_reveal);
      window_single_click_subscribe(BUTTON_ID_BACK, click_abandon);
      break;
    case RS_ANSWER:
      window_set_click_context(BUTTON_ID_UP, s_scroll);
      window_set_click_context(BUTTON_ID_DOWN, s_scroll);
      window_single_click_subscribe(BUTTON_ID_UP,   scroll_layer_scroll_up_click_handler);
      window_single_click_subscribe(BUTTON_ID_DOWN, scroll_layer_scroll_down_click_handler);
      window_long_click_subscribe(BUTTON_ID_UP,     0, click_easy, NULL);
      window_long_click_subscribe(BUTTON_ID_SELECT, 0, click_ok,   NULL);
      window_long_click_subscribe(BUTTON_ID_DOWN,   0, click_hard, NULL);
      window_single_click_subscribe(BUTTON_ID_BACK, click_again);
      window_long_click_subscribe(BUTTON_ID_BACK,   0, click_abandon, NULL);
      break;
    default:
      window_single_click_subscribe(BUTTON_ID_BACK, click_abandon);
      break;
  }
}

// ---- spinner + watchdog ----
static void spin_tick(void *data) {
  s_spin_timer = NULL;
  if (s_state != RS_LOADING) return;
  s_spin = (s_spin + 30) % 360;
  if (s_state_icon) layer_mark_dirty(s_state_icon);
  s_spin_timer = app_timer_register(90, spin_tick, NULL);
}
static void start_spinner(void) {
  if (!s_spin_timer) s_spin_timer = app_timer_register(90, spin_tick, NULL);
}
static void watchdog_fire(void *data) {
  s_watchdog = NULL;
  if (s_state == RS_LOADING) show_state("Phone unreachable", "Check Bluetooth", GLYPH_QMARK);
}
static void arm_watchdog(void) {
  if (s_watchdog) app_timer_cancel(s_watchdog);
  s_watchdog = app_timer_register(10000, watchdog_fire, NULL);
}
static void disarm_watchdog(void) {
  if (s_watchdog) { app_timer_cancel(s_watchdog); s_watchdog = NULL; }
}

// ---- public API ----
void review_window_set_state(uint8_t session_state, const char *counts) {
  if (session_state == SS_LOADING) {
    s_state = RS_LOADING;
    s_answer_ready = false; s_want_reveal = false;
    show_state("Loading", NULL, GLYPH_SPINNER);
    start_spinner();
    arm_watchdog();
  } else if (session_state == SS_EMPTY) {
    disarm_watchdog();
    s_state = RS_EMPTY;
    show_state("All caught up", (counts && counts[0]) ? counts : "No cards due", GLYPH_CHECK);
  } else {
    disarm_watchdog();
    s_state = RS_ERROR;
    show_state("Sync error", "Back to decks", GLYPH_BANG);
  }
  window_set_click_config_provider(s_window, click_config_provider);
}

void review_window_show_question(const char *text, const char *note_id,
                                 uint8_t card_ord, uint8_t button_count,
                                 const char *l_hard, const char *l_ok,
                                 const char *l_easy) {
  disarm_watchdog();
  strncpy(s_note_id, note_id ? note_id : "", NOTE_ID_LEN - 1);
  s_note_id[NOTE_ID_LEN - 1] = '\0';
  s_card_ord = card_ord;
  s_button_count = button_count;
  if (s_q) free(s_q);
  s_q = dup_str(text);
  s_answer_ready = false; s_want_reveal = false;
  s_state = RS_QUESTION;
  show_scroll_text(s_q ? s_q : "", false);
  window_set_click_config_provider(s_window, click_config_provider);
}

void review_window_refresh(void) {
  if (!s_window) return;
  layer_mark_dirty(s_strip);
  if (s_state == RS_QUESTION) show_scroll_text(s_q ? s_q : "", false);
  else if (s_state == RS_ANSWER) show_scroll_text(s_a ? s_a : "", true);
  window_set_click_config_provider(s_window, click_config_provider);
}

void review_window_set_answer(const char *text) {
  if (s_a) free(s_a);
  s_a = dup_str(text);
  s_answer_ready = true;
  if (s_state == RS_ANSWER && s_want_reveal) {
    s_want_reveal = false;
    show_scroll_text(s_a ? s_a : "", true);
    window_set_click_config_provider(s_window, click_config_provider);
  }
}

// ---- window lifecycle ----
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  s_scr_w = b.size.w;          // adapt to the real screen (diorite 144 / emery 200)

  s_ic_reveal = gbitmap_create_with_resource(RESOURCE_ID_IMG_REVEAL);
  s_ic_easy   = gbitmap_create_with_resource(RESOURCE_ID_IMG_GRADE_EASY);
  s_ic_ok     = gbitmap_create_with_resource(RESOURCE_ID_IMG_GRADE_OK);
  s_ic_hard   = gbitmap_create_with_resource(RESOURCE_ID_IMG_GRADE_HARD);

  s_strip = layer_create(GRect(0, 0, COL_W, STRIP_H));
  layer_set_update_proc(s_strip, strip_update);
  layer_add_child(root, s_strip);

  s_scroll = scroll_layer_create(GRect(0, STRIP_H, COL_W, b.size.h - STRIP_H));
  scroll_layer_set_shadow_hidden(s_scroll, true);
  s_text = text_layer_create(GRect(5, 2, COL_W - 6, 4000));
  text_layer_set_font(s_text, afont_body());
  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_text));
  layer_add_child(root, scroll_layer_get_layer(s_scroll));

  s_state_icon = layer_create(GRect(0, 38, b.size.w, 34));
  layer_set_update_proc(s_state_icon, state_icon_update);
  layer_add_child(root, s_state_icon);

  s_msg = text_layer_create(GRect(4, 82, b.size.w - 8, 34));
  text_layer_set_font(s_msg, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_msg, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_msg));

  s_sub = text_layer_create(GRect(4, 118, b.size.w - 8, 22));
  text_layer_set_font(s_sub, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_sub, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_sub));

  s_legend = text_layer_create(GRect(0, b.size.h - FOOTER_H, COL_W, FOOTER_H));
  text_layer_set_font(s_legend, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_legend, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_legend));

  s_action_bar = action_bar_layer_create();

  review_window_set_state(SS_LOADING, "");
}

static void window_unload(Window *window) {
  disarm_watchdog();
  if (s_spin_timer) { app_timer_cancel(s_spin_timer); s_spin_timer = NULL; }
  action_bar_layer_destroy(s_action_bar);
  gbitmap_destroy(s_ic_reveal); gbitmap_destroy(s_ic_easy);
  gbitmap_destroy(s_ic_ok); gbitmap_destroy(s_ic_hard);
  text_layer_destroy(s_legend);
  text_layer_destroy(s_sub);
  text_layer_destroy(s_msg);
  layer_destroy(s_state_icon);
  text_layer_destroy(s_text);
  scroll_layer_destroy(s_scroll);
  layer_destroy(s_strip);
  if (s_q) { free(s_q); s_q = NULL; }
  if (s_a) { free(s_a); s_a = NULL; }
  s_window = NULL;
}

void review_window_push(const char *deck_id, const char *deck_name) {
  strncpy(s_deck_id, deck_id ? deck_id : "", DECK_ID_LEN - 1);
  s_deck_id[DECK_ID_LEN - 1] = '\0';
  strncpy(s_deck_name, deck_name ? deck_name : "", DECK_NAME_LEN - 1);
  s_deck_name[DECK_NAME_LEN - 1] = '\0';
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load, .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
