#pragma once
#include <pebble.h>

// Pushes the review window for a deck and enters the loading state.
void review_window_push(const char *deck_id, const char *deck_name);

// Inbound updates from comm:
void review_window_set_state(uint8_t session_state, const char *counts);
void review_window_show_question(const char *text, const char *note_id,
                                 uint8_t card_ord, uint8_t button_count,
                                 const char *l_hard, const char *l_ok,
                                 const char *l_easy);
void review_window_set_answer(const char *text);

// Re-render the current card with the (possibly changed) font size.
void review_window_refresh(void);
