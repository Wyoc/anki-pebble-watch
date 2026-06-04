# anki-pebble-watch

A Pebble watchapp (C) for running [AnkiDroid](https://github.com/ankidroid/Anki-Android)
review sessions from your wrist: pick a deck, read the question, reveal the
answer, grade it.

Because AnkiDroid only exposes its data through an Android `ContentProvider`
(which the Pebble JS sandbox can't reach), this is a **3-part system** — this
repo is the **watch** half:

```
[anki-pebble-watch]  ⇄  Android companion (Kotlin, PebbleKitAndroid2)  ⇄  AnkiDroid
   (this repo)            github.com/Wyoc/anki-pebble-companion
```

- Companion app: **[Wyoc/anki-pebble-companion](https://github.com/Wyoc/anki-pebble-companion)** (required — provides all the deck/card data)
- De-risking spikes: [Wyoc/anki-pebble-spikes](https://github.com/Wyoc/anki-pebble-spikes)

## What it does

- Hierarchical deck menu (parses Anki `::` paths into folders/decks, with due counts)
- Question screen → reveal → answer screen, with scrollable long cards
- Grades via AnkiDroid's v3 scheduler (Again / Hard / OK / Easy), chunk reassembly over BLE
- On-watch font-size settings (Small / Medium / Large), Cyrillic/Latin rendering via bundled DejaVu Sans

## Button map

**Question screen:** Up/Down = scroll · Select = reveal answer · Back = abandon to deck.

**Answer screen:** Up/Down tap = scroll · **hold Up/Select/Down = Easy / OK / Hard** ·
Back = Again · **hold Back = abandon**.

**Deck menu:** Select = open/drill in · Back = up a level · **hold Select = font settings**.

## Build

Built for **emery** (Pebble Time 2, 200×228) and **diorite** (Pebble 2, 144×168);
both render fine in black & white. Toolchain is the Core Devices `pebble-tool` +
Pebble SDK.

```sh
# one-time toolchain setup
uv tool install pebble-tool --python 3.13   # pebble binary → ~/.local/bin
export PATH="$HOME/.local/bin:$PATH"
pebble sdk install latest                    # installs Pebble SDK 4.9.x

# build → build/watch.pbw
pebble build

# run headless in the emulator
pebble install --emulator emery
pebble screenshot --emulator emery out.png
pebble emu-button --emulator emery click select        # drive the UI
pebble send-app-message --emulator emery --uint 10000=1 # inject an AppMessage
```

There are **no npm dependencies**; `pebble build` bundles the phone-side JS itself.

To run on real hardware, sideload `build/watch.pbw` through the Core Devices app
(no port-9000 dev connection is available in the current app).

## Project layout

```
src/c/            C source — main, deck_window, review_window, settings_window, comm, fonts
src/pkjs/         PebbleKit JS (phone-side bridge stub; the companion does the real work)
resources/        DejaVu Sans font + 1-bit grade/reveal icons
package.json      UUID, target platforms, message keys, resources
wscript           Build rules (rarely edited)
```

The watch↔companion wire protocol is defined in `src/c/protocol.h`.

## CI

`.github/workflows/build.yml` builds the `.pbw` on every push and attaches it to
a GitHub Release on `v*` tags.

## License

GPL-3.0. Copyright (C) 2026 Wyoc. See [LICENSE](LICENSE).
