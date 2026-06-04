#pragma once
#include <pebble.h>

// AppMessage transport. Opens the inbox/outbox, dispatches inbound messages to
// the deck/review windows, and exposes outbound request helpers.
void comm_init(void);

// Watch -> Phone requests.
void comm_request_decks(void);
void comm_select_deck(const char *deck_id);
void comm_request_next(const char *deck_id);
void comm_submit_grade(uint8_t grade_intent, const char *note_id,
                       uint8_t card_ord, const char *deck_id);
