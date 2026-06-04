// ============================================================================
// PebbleKit-JS for the Anki watchapp.
// Production role: none (the Android companion provides all data; font size is
// set on the watch). The DEV_MOCK block below is a stand-in for the companion so
// the review flow can be exercised in the emulator — MUST be false for real-watch
// builds (a live mock would fight the real companion).
// ============================================================================

var DEV_MOCK = false;  // <<< set FALSE before deploying to a real watch (true only for emulator)

if (DEV_MOCK) (function () {
  var MT_REQUEST_DECKS = 1, MT_SELECT_DECK = 2, MT_REQUEST_NEXT = 3, MT_SUBMIT_GRADE = 4;
  var MT_DECK_ITEM = 10, MT_CARD_CHUNK = 11, MT_SESSION_STATE = 12;
  var FIELD_QUESTION = 0, FIELD_ANSWER = 1;
  var SS_EMPTY = 2;
  var CHUNK = 64;

  var DECKS = [
    { name: "French",                    id: "1001", counts: "5n 3l 20d" },
    { name: "Spanish",                   id: "2000", counts: "1n 0l 2d"  },
    { name: "Spanish::Verbs",            id: "2001", counts: "0n 0l 6d"  },
    { name: "Spanish::Verbs::Irregular", id: "2002", counts: "0n 0l 4d"  },
    { name: "Spanish::Nouns",            id: "2003", counts: "2n 0l 1d"  },
    { name: "Русский",                   id: "5000", counts: "2n 0l 3d"  },
    { name: "Русский::Животные",         id: "5001", counts: "1n 0l 2d"  },
    { name: "Code de la route",          id: "1004", counts: "2n 0l 4d"  },
    { name: "Geography",                 id: "3001", counts: "1n 0l 3d"  },
    { name: "Mathematics",               id: "3003", counts: "4n 0l 2d"  },
    { name: "Empty Deck",                id: "1003", counts: "0n 0l 0d"  }
  ];
  var LONG = "The mitochondrion is a double-membrane-bound organelle found in most " +
    "eukaryotic cells. It generates most of the cell's supply of ATP. This is a " +
    "deliberately long answer so the watch must reassemble several chunks and scroll.";
  var CARDS = {
    "1001": [{ q: "bonjour", a: "hello", note: "9001", ord: 0, bc: 4, lbl: { hard: "<10m", ok: "1d", easy: "4d" } },
             { q: "merci", a: "thank you", note: "9002", ord: 0, bc: 3, lbl: { hard: "", ok: "<10m", easy: "1d" } }],
    "5000": [{ q: "кошка", a: "cat (the animal)", note: "5901", ord: 0, bc: 4, lbl: { hard: "<10m", ok: "2d", easy: "5d" } }],
    "5001": [{ q: "собака", a: "собака = dog", note: "5902", ord: 0, bc: 4, lbl: { hard: "<10m", ok: "2d", easy: "5d" } }],
    "1004": [{ q: "Describe the mitochondrion (long).", a: LONG, note: "9201", ord: 0, bc: 4, lbl: { hard: "<10m", ok: "2d", easy: "5d" } }]
  };
  var pos = {}, queue = [], sending = false;

  function enqueue(d) { queue.push(d); pump(); }
  function pump() {
    if (sending || !queue.length) return;
    sending = true;
    Pebble.sendAppMessage(queue.shift(),
      function () { sending = false; pump(); },
      function () { sending = false; pump(); });
  }
  function sendDecks() {
    DECKS.forEach(function (d, i) {
      enqueue({ "MSG_TYPE": MT_DECK_ITEM, "DECK_NAME": d.name, "DECK_ID": d.id,
                "DECK_INDEX": i, "DECK_TOTAL": DECKS.length, "DECK_COUNTS": d.counts });
    });
  }
  function sendField(field, text, meta) {
    var n = Math.max(1, Math.ceil(text.length / CHUNK));
    for (var i = 0; i < n; i++) {
      var d = { "MSG_TYPE": MT_CARD_CHUNK, "CARD_FIELD": field, "CHUNK_INDEX": i,
                "CHUNK_TOTAL": n, "CARD_TEXT": text.substr(i * CHUNK, CHUNK) };
      if (field === FIELD_QUESTION && i === 0 && meta) {
        d["NOTE_ID"] = meta.note; d["CARD_ORD"] = meta.ord; d["BUTTON_COUNT"] = meta.bc;
        d["BTN_LABEL_HARD"] = meta.lbl.hard; d["BTN_LABEL_OK"] = meta.lbl.ok; d["BTN_LABEL_EASY"] = meta.lbl.easy;
      }
      enqueue(d);
    }
  }
  function sendNextCard(deckId) {
    var list = CARDS[deckId] || [], i = pos[deckId] || 0;
    if (i >= list.length) { enqueue({ "MSG_TYPE": MT_SESSION_STATE, "SESSION_STATE": SS_EMPTY, "STATE_COUNTS": "" }); return; }
    sendField(FIELD_QUESTION, list[i].q, list[i]);
    sendField(FIELD_ANSWER, list[i].a, null);
  }

  Pebble.addEventListener("ready", function () { sendDecks(); });
  Pebble.addEventListener("appmessage", function (e) {
    var p = e.payload, t = p["MSG_TYPE"];
    if (t === undefined) return;  // not a mock message (e.g. config echo)
    if (t === MT_REQUEST_DECKS) sendDecks();
    else if (t === MT_SELECT_DECK) pos[p["DECK_ID"]] = 0;
    else if (t === MT_REQUEST_NEXT) sendNextCard(p["DECK_ID"]);
    else if (t === MT_SUBMIT_GRADE) { var d = p["DECK_ID"]; pos[d] = (pos[d] || 0) + 1; sendNextCard(d); }
  });
})();
