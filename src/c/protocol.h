#pragma once
// Anki-Pebble AppMessage protocol contract.
// Shared by the watch (C), the dev JS mock (src/pkjs), and the Android companion.
// AppMessage *keys* are the auto-generated MESSAGE_KEY_<name> macros (see package.json
// messageKeys). The constants below are the *values* carried in those keys.

// ---- MSG_TYPE values (discriminator on every message) ----
// Watch -> Phone
#define MT_REQUEST_DECKS   1   // (no payload)
#define MT_SELECT_DECK     2   // + DECK_ID
#define MT_REQUEST_NEXT    3   // + DECK_ID
#define MT_SUBMIT_GRADE    4   // + EASE + NOTE_ID + CARD_ORD + DECK_ID
// Phone -> Watch
#define MT_DECK_ITEM      10   // + DECK_NAME + DECK_ID + DECK_INDEX + DECK_TOTAL + DECK_COUNTS
#define MT_CARD_CHUNK     11   // + CARD_FIELD + CHUNK_INDEX + CHUNK_TOTAL + CARD_TEXT
                               //   (field 0 / chunk 0 also carries card meta + button labels)
#define MT_SESSION_STATE  12   // + SESSION_STATE (+ STATE_COUNTS)

// ---- CARD_FIELD values ----
#define FIELD_QUESTION  0
#define FIELD_ANSWER    1

// ---- EASE values (grade intent, watch -> phone) ----
// The companion remaps these onto a real Anki ease using the card's button_count.
#define GRADE_AGAIN  1
#define GRADE_HARD   2
#define GRADE_OK     3
#define GRADE_EASY   4

// ---- SESSION_STATE values (phone -> watch) ----
#define SS_LOADING  0
#define SS_EMPTY    2
#define SS_ERROR    3

// ---- sizing limits (watch RAM is tight) ----
#define MAX_DECKS         80
#define DECK_NAME_LEN     80   // full Anki deck path, e.g. "Spanish::Verbs::Present"
#define DECK_ID_LEN       24   // Anki ids are 64-bit -> up to 20 digits; sent as strings
#define DECK_COUNTS_LEN   24
#define MAX_VIEW          82   // rows shown at one hierarchy level (+ special rows)
#define VIEW_LABEL_LEN    64   // a single path segment
#define NOTE_ID_LEN       24
#define LABEL_LEN         24
#define MAX_CARD_TEXT   4096   // hard cap on reassembled question/answer

#define DECK_SEP "::"          // Anki deck-hierarchy separator
