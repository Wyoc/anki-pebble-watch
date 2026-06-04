#pragma once
#include <pebble.h>

// Custom DejaVu Sans fonts (Latin + Cyrillic) so non-Latin decks render instead
// of showing tofu boxes. The body size is user-configurable (0=S,1=M,2=L) and
// persisted on the watch.
void  afont_init(void);
void  afont_deinit(void);
void  afont_set_size(int idx);   // reload body font + persist
int   afont_size_idx(void);
GFont afont_body(void);          // Q/A + deck titles (configurable size)
GFont afont_strip(void);         // small deck-name strip / subtitles (fixed 14)
int   afont_cell_height(void);   // deck-menu row height for the current size
