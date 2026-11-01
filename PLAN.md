# Plan: the rolling audio buffer

The main feature of the prototype, and the selling point for the startup
class. It grew out of the "rolling audio buffer" experiment in Japanese
Helper, and takes ideas from Yomitan and from mining cards with mpv +
mpvacious.

**Status (November 2026): the Linux side is complete.** Phases 1–7
work on Linux (PulseAudio or PipeWire, a wlroots desktop like Hyprland
for screenshots). What's left are the Windows parts, listed under
"Left for Windows" below and marked `TODO(Windows)` in the code. Until
they're done, the Windows build runs as before, and the rolling buffer
says it isn't done on Windows yet.

## What the user does

1. They're watching something: an anime in mpv, a YouTube video, a stream.
   The app runs in the background and always keeps the last N seconds of
   **what the computer plays** (not the microphone). N is a setting,
   e.g. 20, 30 or 60.
2. They hear something they didn't catch and press the **hotkey**. Two
   things are captured at once:
   - a copy of the audio buffer, which stops scrolling while they work on it
   - a **screenshot**, taken right then
3. A window opens with a **waveform** of the buffer. The last few seconds
   are already selected. They can drag the start and end, **play the
   selection back** to check it's the right line, and press Enter.
4. Only the selection goes to whisper. The **transcript** appears in the
   window.
5. They select any part of the text (kanji, kana, a whole word) and it's
   **looked up** right away, Yomitan-style.
6. Optional: **add to Anki**. The card gets the word, reading, meaning, the
   sentence, the selected **audio clip**, and the screenshot, which they
   **crop** to just the video (useful when the video isn't fullscreen and
   the screenshot also has the browser or desktop in it).

The quick path is: hotkey → Enter → read. Everything else is there for
when it's needed.

## What already exists

- `Recorder` (src/core/recorder.h): records the microphone into memory,
  16 kHz mono floats, at most 30 s, start to stop. Used by the search
  bar's mic button. Its audio thread already follows the right rules
  (memory set aside up front, no locks or allocations).
- `Transcriber` (src/core/transcriber.h): whisper.cpp off the UI thread,
  model loaded ahead of time, can be cancelled. Can be used as is.
- Jisho, DeepL and MyMemory lookups (src/core/), history, settings, the
  whisper model downloader.
- In japanese-helper/ (the old project): AnkiConnect `anki_get_decks` and
  `anki_add_note` (src/api.cpp), and a GDI screenshot (src/ocr.cpp). Its
  "global" hotkey is an FLTK handler, so it only works while the app has
  focus; that part has to be redone.

## Phases

Phases 1–5 are the demo. Phases 6–7 make it the mpvacious replacement.

### 1. Always-on buffer of the computer's audio

New `AudioBuffer` in `src/core/audio_buffer.{h,cpp}`. `Recorder` stays as
it is for the mic button.

- miniaudio `ma_device_type_loopback` on WASAPI records the default output
  device. miniaudio follows the default device when it changes (e.g.
  headphones plugged in).
- A ring buffer: the audio thread writes in a circle, and a counter of
  frames written only ever goes up. Like `Recorder`, it's allocated once
  and the audio thread never locks or allocates.
- Holds N seconds **plus a couple of seconds of margin**. Then `snapshot()`
  can copy the last N seconds while the audio thread keeps writing into
  the margin, without a lock.
- Keep the ring logic in a small class of its own, so it can be tested
  without an audio device.
- Watch out: **WASAPI loopback delivers nothing while nothing plays.**
  Without handling that, "the last 30 seconds" means the last 30 seconds
  of sound, not of time. The fix: note the time of each callback, and
  write silence for any gap.
- Sample rate: see the open questions. The simplest choice is 16 kHz mono,
  which is what whisper wants. 48 kHz mono gives the Anki clips better
  sound and is resampled for whisper.
- Memory: 60 s at 48 kHz mono floats is about 11.5 MB. Fine.

Done when: it runs from startup, `snapshot()` returns the last N seconds,
and silence shows up as silence.

**Linux: done.** It records the default output's monitor device through
PulseAudio (PipeWire's PulseAudio server works too), and checks every 3 s
whether the default output changed, moving to the new one. The ring is
`SampleRing` (src/core/sample_ring.h) with `GapDetector` for the silence,
tested in tests/sample_ring_test.cpp. `Capturer` (src/ui/capturer.h)
runs it from startup; Settings > Buffer turns it on or off and sets the
length. **Windows: left** (WASAPI loopback in `AudioBuffer::open_device`).

### 2. Global hotkey

New `src/ui/global_hotkey.{h,cpp}`.

- Windows: `RegisterHotKey` on the main window's HWND (`fl_xid`), and
  catch `WM_HOTKEY` with `Fl::add_system_handler` (FLTK 1.4).
- The key combination is a setting. Registering fails if another app has
  it, so that has to be reported, not ignored.
- Linux (X11: `XGrabKey`; Wayland: the GlobalShortcuts portal) can wait.
  The prototype is Windows-first.

Done when: pressing it from any app (mpv, a fullscreen browser) opens the
capture window.

**Linux: done, differently.** Wayland lets no app grab a key, so the
desktop owns the shortcut: the user binds a key to `prototype --capture`
(e.g. `bind = SUPER, A, exec, /path/to/prototype --capture` in
hyprland.conf), which passes the press to the running app through a
socket in `$XDG_RUNTIME_DIR`. Settings > Buffer shows the command with
the right path. This works on X11 desktops too, so no `XGrabKey` was
needed. **Windows: left** (`RegisterHotKey`, src/ui/global_hotkey.cpp,
and a setting for the key combination).

### 3. Capture window: waveform, selection, playback

New `src/ui/capture_window.{h,cpp}` and `src/ui/waveform.{h,cpp}`.

- Waveform widget: for each pixel column, the min and max sample in that
  slice, drawn as a vertical line. Recomputed only on resize, since the
  snapshot doesn't change.
- Selection: drag on the waveform to make one, drag its edges to adjust.
  It opens with the last ~8 s selected (a setting, maybe).
- Playback: a miniaudio playback device that plays the selection, with a
  moving playhead. Space plays and stops.
- A time axis in seconds, ending at "now".
- Enter (or a Transcribe button) moves on to phase 4.

Done when: you can select a line, hear it, and adjust the edges.

**Done** (no platform code): src/ui/waveform.h, src/ui/capture_window.h
and src/core/clip_player.h.

### 4. Transcribe the selection

- Resample the selection to 16 kHz if the buffer isn't, and hand it to
  `Transcriber` on a worker thread, the way `MainWindow::start_recording`
  does.
- Shown in the same window, below the waveform, in a text widget whose
  text can be selected.
- Load the model at startup or on the first hotkey press, so the first
  transcription doesn't also wait for loading. (Keeping a large model
  loaded uses memory all the time, which is a tradeoff to look at.)
- Later: return whisper's segments with their timestamps instead of one
  string, so a sentence can be matched to its audio.

Done when: hotkey → Enter shows the right text, and Esc cancels.

**Done** (no platform code). The model starts loading when a capture
opens. `SharedTranscriber` (src/ui/shared_transcriber.h) keeps one model
in memory for both the mic button and captures. Whisper's segments with
timestamps are still to do.

### 5. Look up what's selected

- When the mouse button is released over selected text, search for it
  with the existing Jisho code, and show the result in a panel next to
  the transcript.
- Jisho handles some inflected forms (食べた → 食べる), not all.
- Ignore selections that are only punctuation or spaces.

Done when: selecting a word shows its reading and meaning. **This is the
demo.**

**Done** (no platform code). Spaces and punctuation around the selection
are dropped first (src/core/lookup_text.h, tested).

### 6. Screenshot at the hotkey, and cropping

New `src/core/screenshot.{h,cpp}` (Windows) and `src/ui/crop_view.{h,cpp}`.

- Taken **at the moment the hotkey is pressed**, before the capture window
  opens and covers anything.
- Capture the whole monitor the foreground window is on, so the crop can
  pick the video out of anything on it.
- Start from the GDI capture in japanese-helper/src/ocr.cpp (fixed: it
  currently captures from the corner to the mouse position). Test it on
  YouTube in a browser and on mpv. If video comes out black, which GDI
  does with some hardware-accelerated video, switch to DXGI Desktop
  Duplication.
- DRM-protected video (Netflix and the like) comes out black whatever the
  method. That's the platform's choice, and the app doesn't try to get
  around it.
- Crop view: the screenshot scaled to fit, with a rectangle to drag and
  resize. **The last crop is remembered**, since the video is usually in
  the same place the next time.
- Saved as JPEG (or WebP) to keep the Anki collection small:
  `stb_image_write`, one header like miniaudio, does JPEG.

Done when: the screenshot shows the video, not a black box, and the crop
is kept between cards.

**Linux: done.** grim takes the screenshot (on Hyprland, of the monitor
`hyprctl` says has the focus), which is only on wlroots desktops; GNOME
and KDE would need the screenshot portal. Two changes from the plan:
JPEG goes through libjpeg-turbo, which FLTK already uses, instead of
`stb_image_write` (fetching stb as a new submodule was blocked in the
session that built this; either works), and the shrinking before saving
is a small averaging filter in src/core/image.cpp. The crop is kept as
fractions of the screenshot, so it still fits after a resolution change.
**Windows: left** (the GDI capture in `take_screenshot`,
src/core/screenshot.cpp). The crop view and JPEG code are shared.

### 7. Add to Anki

New `src/core/anki_connect.{h,cpp}`, ported from japanese-helper's
src/api.cpp.

- AnkiConnect at `http://127.0.0.1:8765`: `deckNames`, `modelNames`,
  `modelFieldNames`, `addNote`. `addNote` can take the audio and picture
  as base64 and store them, so no separate upload step.
- Settings say which deck, which note type, and which field gets what
  (word, reading, meaning, sentence, audio, image), like mpvacious does.
- The audio clip is the selection from phase 3, as WAV (simplest: a
  44-byte header and the samples). Opus or MP3 later for smaller files.
- A card window: the fields filled in and editable, and the crop view.
  The card is added only when the user presses Add.
- If Anki or AnkiConnect isn't running, say so plainly.

Done when: a card shows up in Anki with sound that plays and the cropped
picture.

**Done** (no platform code), apart from adding a card to a running Anki,
which still has to be tried by hand: Anki wasn't open while it was
built, so only "Anki isn't open" was tested. Picking a note type in
Settings > Anki guesses the fields from their names (src/core/anki_fields.h,
tested). Cards are tagged `prototype`, and pictures are at most 480 px
tall. Each part is formatted like the user's Yomitan cards
(src/core/card_format.h): the looked-up word highlighted in the
sentence, `word【reading】`, the meanings as a yomitan-glossary list,
then `[sound:...]` and `<img alt="snapshot">`, in whichever fields the
settings say. Jisho has no pitch accents, word audio or conjugations,
so those parts of the template stay empty.

### Left for Windows

Each is marked `TODO(Windows)` in the code:

- `AudioBuffer::open_device` (src/core/audio_buffer.cpp):
  `ma_device_type_loopback` on WASAPI, which follows the default output
  by itself, so `follow_default_output` can stay a no-op there.
- `GlobalHotkey` (src/ui/global_hotkey.cpp): `RegisterHotKey` and
  `WM_HOTKEY`, plus a setting for the key combination on the Buffer
  page, which shows the `--capture` command only on Linux.
  `send_hotkey_press` and `capture_command` aren't needed there.
- `take_screenshot` (src/core/screenshot.cpp): the GDI capture, then
  DXGI if video comes out black.
- Building: libjpeg-turbo is a new dependency (MSYS2:
  `mingw-w64-x86_64-libjpeg-turbo`).

### Later

- Local dictionary (JMdict) with deinflection: works offline, is faster,
  and allows Yomitan-style lookup by hovering instead of selecting.
- Whisper segment timestamps, so selecting a word can pick its sentence
  and that sentence's audio by itself.
- Linux: screenshots on GNOME and KDE (the screenshot portal), and on X11.
- Running in the background: start with Windows, a tray icon, closing to
  the tray.

### Possible: dictation mode ("type what you heard")

An optional mode, off by default, for people who want to practise
listening instead of only reading the transcript. Not built yet.

Why: the quick path (hotkey → Enter → read) skips the part where the
user tries to hear the line, and that struggle is where listening
practice happens. Learners also tend to feel they've learned more from a
smooth answer than they have. This keeps the struggle in, the way a
tutor gives hints instead of answers. It also answers the obvious
question in the pitch: "doesn't this remove the learning?" The app
removes the busywork (rewinding, retyping, filling in cards), not the
listening.

How it works, after phase 4:

- The transcript stays hidden. The user plays the selection as many
  times as they like and types what they heard in hiragana (with the
  Windows IME).
- Enter checks it against whisper's transcript and shows both, with the
  parts that differ highlighted. Then the window works as usual: lookup,
  add to Anki.
- A key to give up and show the transcript without typing anything.

Things to solve:

- Whisper writes kanji, so the transcript has to be turned into kana
  before comparing. That needs readings for the words: a tokenizer with a
  dictionary (MeCab or Sudachi), or the JMdict work above. This is the
  biggest part.
- Compare loosely: katakana and hiragana count as the same, and
  punctuation, spaces and ー vs. a vowel are ignored.
- Whisper is sometimes wrong, especially on anime. So it shows the
  differences rather than marking the answer wrong, and the user decides.
  A mismatch can mean whisper misheard.
- Maybe later: keep a score, or add a card on a miss.

## Settings to add

To `src/core/settings.h`, as new categories in the settings window:

- **Buffer**: length in seconds (with limits, e.g. 10–120), on/off, the
  hotkey, the default selection length, dictation mode on/off.
- **Anki**: deck, note type, field mapping, the last crop rectangle.

Done, except the hotkey (Windows) and dictation mode (not built).

## Privacy

The app records everything the computer plays, all the time, so this has
to be clear and safe, and it's a point worth making in the pitch:

- Audio stays in memory and is never written to disk, except a clip the
  user adds to a card.
- A visible sign that it's listening (tray icon), and a way to pause it.
  (Pausing is the on/off setting. The tray icon is still to do, with the
  rest of running in the background.)
- Nothing is sent anywhere except what the user chooses to look up.

## Open questions

Decided while building the Linux side, all as suggested:

- **Sample rate of the buffer**: 48 kHz mono, resampled to 16 kHz for
  whisper (src/core/resample.h).
- **Where cropping happens**: the card window, with the last crop
  already applied.
- **Which monitor**: the one with the focused window.
- **One window or two**: one capture window for the waveform, transcript
  and lookup; the card is a second window, since it's optional.
- **Hotkey while the window is open**: a new capture, replacing the one
  shown, since it's most likely a new line.

Still open:

- **Whisper model**: which size is fast enough on a typical student
  laptop for a 5–10 s clip? Measured on the 8-core laptop it was built
  on: about 6 s with base and 7 s with small for an 8 s clip, with the
  model already loaded. Whisper pads every clip to 30 s, so a short clip
  costs about as much as a long one; setting `audio_ctx` to fit the clip
  could make it several times faster, at some cost in accuracy.
  Worth trying before the demo.
