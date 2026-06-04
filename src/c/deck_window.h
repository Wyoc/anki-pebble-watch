#pragma once
#include <pebble.h>

void deck_window_push(void);
void deck_window_refresh(void);
// Called by comm when a DECK_ITEM message arrives (decks stream in one per msg).
void deck_window_add_item(const char *name, const char *id,
                          uint16_t index, uint16_t total, const char *counts);
