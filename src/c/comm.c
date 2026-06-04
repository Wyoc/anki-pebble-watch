#include <pebble.h>
#include "comm.h"
#include "protocol.h"
#include "deck_window.h"
#include "review_window.h"

// ---- chunk reassembly state for the in-flight card ----
static char *s_buf[2];          // [FIELD_QUESTION], [FIELD_ANSWER]
static int   s_len[2];          // bytes accumulated (excluding NUL)
static bool  s_done[2];

static char    s_note_id[NOTE_ID_LEN];
static uint8_t s_card_ord;
static uint8_t s_button_count;
static char    s_lbl_hard[LABEL_LEN];
static char    s_lbl_ok[LABEL_LEN];
static char    s_lbl_easy[LABEL_LEN];

static void reset_card(void) {
  for (int f = 0; f < 2; f++) {
    if (s_buf[f]) { free(s_buf[f]); s_buf[f] = NULL; }
    s_len[f] = 0;
    s_done[f] = false;
  }
}

// Append a UTF-8 fragment to field f's growing buffer (realloc as we go so we
// don't need the total byte count up front). Caps at MAX_CARD_TEXT.
static void append_chunk(int f, const char *text) {
  if (!text) return;
  int add = strlen(text);
  if (s_len[f] + add > MAX_CARD_TEXT) add = MAX_CARD_TEXT - s_len[f];
  if (add <= 0) return;
  char *nb = realloc(s_buf[f], s_len[f] + add + 1);
  if (!nb) { APP_LOG(APP_LOG_LEVEL_ERROR, "OOM reassembling card"); return; }
  s_buf[f] = nb;
  memcpy(s_buf[f] + s_len[f], text, add);
  s_len[f] += add;
  s_buf[f][s_len[f]] = '\0';
}

static void handle_card_chunk(DictionaryIterator *it) {
  Tuple *t_field = dict_find(it, MESSAGE_KEY_CARD_FIELD);
  Tuple *t_idx   = dict_find(it, MESSAGE_KEY_CHUNK_INDEX);
  Tuple *t_total = dict_find(it, MESSAGE_KEY_CHUNK_TOTAL);
  Tuple *t_text  = dict_find(it, MESSAGE_KEY_CARD_TEXT);
  if (!t_field || !t_idx || !t_total) return;

  int field = t_field->value->uint8;
  int idx   = t_idx->value->uint16;
  int total = t_total->value->uint16;
  if (field != FIELD_QUESTION && field != FIELD_ANSWER) return;

  // First fragment of the question marks the start of a brand-new card.
  if (field == FIELD_QUESTION && idx == 0) {
    reset_card();
    Tuple *t_note = dict_find(it, MESSAGE_KEY_NOTE_ID);
    Tuple *t_ord  = dict_find(it, MESSAGE_KEY_CARD_ORD);
    Tuple *t_bc   = dict_find(it, MESSAGE_KEY_BUTTON_COUNT);
    Tuple *t_lh   = dict_find(it, MESSAGE_KEY_BTN_LABEL_HARD);
    Tuple *t_lo   = dict_find(it, MESSAGE_KEY_BTN_LABEL_OK);
    Tuple *t_le   = dict_find(it, MESSAGE_KEY_BTN_LABEL_EASY);
    s_note_id[0] = '\0';
    if (t_note) strncpy(s_note_id, t_note->value->cstring, NOTE_ID_LEN - 1);
    s_card_ord     = t_ord ? t_ord->value->uint8 : 0;
    s_button_count = t_bc  ? t_bc->value->uint8  : 4;
    s_lbl_hard[0] = s_lbl_ok[0] = s_lbl_easy[0] = '\0';
    if (t_lh) strncpy(s_lbl_hard, t_lh->value->cstring, LABEL_LEN - 1);
    if (t_lo) strncpy(s_lbl_ok,   t_lo->value->cstring, LABEL_LEN - 1);
    if (t_le) strncpy(s_lbl_easy, t_le->value->cstring, LABEL_LEN - 1);
  }

  // Clear this field's buffer at its first fragment so a duplicate AppMessage
  // delivery (BLE re-delivers when an ACK is delayed) can't double the text.
  if (idx == 0) {
    if (s_buf[field]) { free(s_buf[field]); s_buf[field] = NULL; }
    s_len[field] = 0;
  }

  if (t_text) append_chunk(field, t_text->value->cstring);

  if (idx >= total - 1) {           // last fragment of this field
    s_done[field] = true;
    if (field == FIELD_QUESTION) {
      review_window_show_question(s_buf[FIELD_QUESTION] ? s_buf[FIELD_QUESTION] : "",
                                  s_note_id, s_card_ord, s_button_count,
                                  s_lbl_hard, s_lbl_ok, s_lbl_easy);
    } else {
      review_window_set_answer(s_buf[FIELD_ANSWER] ? s_buf[FIELD_ANSWER] : "");
    }
  }
}

static void inbox_received(DictionaryIterator *it, void *context) {
  Tuple *t_type = dict_find(it, MESSAGE_KEY_MSG_TYPE);
  if (!t_type) return;
  switch (t_type->value->uint8) {
    case MT_DECK_ITEM: {
      Tuple *t_name  = dict_find(it, MESSAGE_KEY_DECK_NAME);
      Tuple *t_id    = dict_find(it, MESSAGE_KEY_DECK_ID);
      Tuple *t_index = dict_find(it, MESSAGE_KEY_DECK_INDEX);
      Tuple *t_total = dict_find(it, MESSAGE_KEY_DECK_TOTAL);
      Tuple *t_cnts  = dict_find(it, MESSAGE_KEY_DECK_COUNTS);
      deck_window_add_item(t_name ? t_name->value->cstring : "?",
                           t_id   ? t_id->value->cstring   : "",
                           t_index ? t_index->value->uint16 : 0,
                           t_total ? t_total->value->uint16 : 0,
                           t_cnts ? t_cnts->value->cstring : "");
      break;
    }
    case MT_CARD_CHUNK:
      handle_card_chunk(it);
      break;
    case MT_SESSION_STATE: {
      Tuple *t_ss   = dict_find(it, MESSAGE_KEY_SESSION_STATE);
      Tuple *t_cnts = dict_find(it, MESSAGE_KEY_STATE_COUNTS);
      review_window_set_state(t_ss ? t_ss->value->uint8 : SS_ERROR,
                              t_cnts ? t_cnts->value->cstring : "");
      break;
    }
    default:
      break;
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", reason);
}
static void outbox_failed(DictionaryIterator *it, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", reason);
}
static void outbox_sent(DictionaryIterator *it, void *context) {}

void comm_init(void) {
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_register_outbox_sent(outbox_sent);

  // Request the largest buffers the firmware will give us. Log the negotiated
  // sizes -- this is the Spike A measurement (visible in `pebble logs`).
  uint32_t in_max  = app_message_inbox_size_maximum();
  uint32_t out_max = app_message_outbox_size_maximum();
  APP_LOG(APP_LOG_LEVEL_INFO, "AppMessage max sizes: inbox=%lu outbox=%lu", in_max, out_max);
  uint32_t inbox  = in_max  > 2048 ? 2048 : in_max;   // cap for Pebble 2 heap
  uint32_t outbox = out_max > 512  ? 512  : out_max;
  AppMessageResult r = app_message_open(inbox, outbox);
  APP_LOG(APP_LOG_LEVEL_INFO, "app_message_open(%lu,%lu) -> %d", inbox, outbox, r);
}

// ---- outbound helpers ----
static void send_now(DictionaryIterator *it) {
  AppMessageResult r = app_message_outbox_send();
  if (r != APP_MSG_OK) APP_LOG(APP_LOG_LEVEL_WARNING, "outbox_send -> %d", r);
}

void comm_request_decks(void) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_MSG_TYPE, MT_REQUEST_DECKS);
  send_now(it);
}

void comm_select_deck(const char *deck_id) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_MSG_TYPE, MT_SELECT_DECK);
  dict_write_cstring(it, MESSAGE_KEY_DECK_ID, deck_id);
  send_now(it);
}

void comm_request_next(const char *deck_id) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_MSG_TYPE, MT_REQUEST_NEXT);
  dict_write_cstring(it, MESSAGE_KEY_DECK_ID, deck_id);
  send_now(it);
}

void comm_submit_grade(uint8_t grade_intent, const char *note_id,
                       uint8_t card_ord, const char *deck_id) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_MSG_TYPE, MT_SUBMIT_GRADE);
  dict_write_uint8(it, MESSAGE_KEY_EASE, grade_intent);
  dict_write_cstring(it, MESSAGE_KEY_NOTE_ID, note_id);
  dict_write_uint8(it, MESSAGE_KEY_CARD_ORD, card_ord);
  dict_write_cstring(it, MESSAGE_KEY_DECK_ID, deck_id);
  send_now(it);
}
