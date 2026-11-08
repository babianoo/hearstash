# Plan: the rolling audio buffer

The main feature of Hearstash, and the selling point for the startup
class. It grew out of the "rolling audio buffer" experiment in Japanese
Helper, and takes ideas from Yomitan and from mining cards with mpv +
mpvacious.

**Status (November 2026): phases 1–7 work on Linux and Windows.** On
Linux with PulseAudio or PipeWire, and a wlroots desktop like Hyprland
for screenshots; on Windows with WASAPI loopback, `RegisterHotKey` and
GDI. What's still to check on Windows by hand is listed under "Windows"
below.

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
length.

**Windows: done.** `ma_device_type_loopback` on WASAPI, which miniaudio
moves to the new default output by itself, so `follow_default_output`
does nothing there. WASAPI sends nothing while nothing plays, so two
more things count that time as silence: the `GapDetector` is started
when recording starts (otherwise a quiet start would vanish once the
first sound came), and `snapshot()` adds the silence since the last
chunk (otherwise a capture taken during a pause would end at the last
sound, not now). Tried with an inaudible tone: it lands where it was
played, with the quiet before and after kept as silence.

### 2. Global hotkey

New `src/ui/global_hotkey.{h,cpp}`.

- Windows: `RegisterHotKey` on the main window's HWND (`fl_xid`), and
  catch `WM_HOTKEY` with `Fl::add_system_handler` (FLTK 1.4).
- The key combination is a setting. Registering fails if another app has
  it, so that has to be reported, not ignored.
- Linux (X11: `XGrabKey`; Wayland: the GlobalShortcuts portal) can wait.
  Hearstash is Windows-first.

Done when: pressing it from any app (mpv, a fullscreen browser) opens the
capture window.

**Linux: done, differently.** Wayland lets no app grab a key, so the
desktop owns the shortcut: the user binds a key to `hearstash --capture`
(e.g. `bind = SUPER, A, exec, /path/to/hearstash --capture` in
hyprland.conf), which passes the press to the running app through a
socket in `$XDG_RUNTIME_DIR`. Settings > Buffer shows the command with
the right path. This works on X11 desktops too, so no `XGrabKey` was
needed.

**Windows: done, a little differently.** `RegisterHotKey` on a hidden
message-only window of its own instead of the main window's HWND: the
capturer starts before the main window exists, and a window's messages
still arrive during Windows' own message loops (e.g. while a window is
dragged), where thread messages are lost. The key is a setting
(src/core/shortcut.h, tested), `Win+Alt+C` by default: nothing on
Windows 11 uses it, and it has no AltGr in it, which types letters
like ć in some layouts. Settings > Buffer takes a new one from the keys
pressed in its box, and pauses the old one meanwhile. A key that's
taken is reported there. `hearstash --capture` works on Windows too,
by posting to that window, and a second copy of the app is told the
first one has the shortcut.

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

**Windows: done.** A GDI `BitBlt`
of the monitor with the foreground window, in real pixels, since FLTK
makes the app per-monitor DPI-aware when its first window opens. About
30 ms for 1920x1080. Still to try: video in a browser and in mpv, to
see whether any comes out black, which would mean DXGI Desktop
Duplication. The crop view and JPEG code are shared.

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
tested). Cards are tagged `hearstash`, and pictures are at most 480 px
tall. Each part is formatted like the user's Yomitan cards
(src/core/card_format.h): the looked-up word highlighted in the
sentence, `word【reading】`, the meanings as a yomitan-glossary list,
then `[sound:...]` and `<img alt="snapshot">`, in whichever fields the
settings say. Jisho has no pitch accents, word audio or conjugations,
so those parts of the template stay empty.

### Windows

Done (November 2026), and tried on Windows 11 with MinGW and vcpkg
(x64-mingw-static). Building takes libjpeg-turbo (vcpkg's
`libjpeg-turbo`, or MSYS2's `mingw-w64-x86_64-libjpeg-turbo`) for CMake
to find, but an FLTK built with its bundled libjpeg puts that one's
headers first, and the app then uses it. The two declare
`jpeg_mem_dest`'s size differently, which src/core/image.cpp allows for.

Still to try by hand:

- A capture of a real line, with sound playing: the waveform, the
  transcript, and a card in a running Anki (also untried on Linux).
- Screenshots of video in a browser and in mpv: if any is black, switch
  to DXGI Desktop Duplication.
- Plugging in headphones while it records, to see miniaudio follow the
  new default output.

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

### Possible: read game and VN text (OCR)

For games and visual novels, where the line is on screen, not (only)
heard. Not built yet. The first step is a comparison on real screenshots.

Why: the capture already has what it needs. The hotkey takes a
screenshot, and the crop view remembers a rectangle. Reading the text in
the crop gives the exact sentence, where whisper only guesses. In a
voiced VN the audio buffer also holds the voice line, so a card gets the
sentence from the screen, its audio from the buffer and the screenshot,
with no whisper at all. Same hotkey as for video.

How it works:

- The capture opens with the screenshot and the remembered crop drawn on
  it, and the text already read into the lookup panel. Enter accepts.
  Dragging the crop reads it again when the mouse is released. This is
  the same pattern as the waveform's selection and the card's crop:
  remembered, shown, easy to change, never required.
- The crop is remembered **per game**, keyed by the focused window
  (`hyprctl` on Hyprland, the foreground window on Windows), since every
  game puts its text box somewhere else.
- Everything runs on the computer, like whisper. The models come from the
  model downloader on first use. Screenshots aren't sent to a cloud OCR,
  which would break the privacy promise.

Two steps:

1. **Detection**: finding each line inside the crop. The crop says where
   the dialogue box is; what's in it changes every time (one line or
   three, furigana above kanji, a "next" arrow, the speaker's name).
2. **Reading**: turning the image of one line, or one block, into text.

Candidates:

- **manga-ocr** reads a whole block, horizontal or vertical, so the crop
  alone might do, with no detection. But it shrinks every image to a
  224×224 square: fine for a speech bubble, poor for a wide box of three
  long lines. Probably the best reader for Japanese.
- **A row-counting line splitter**: count the text-coloured pixels in each
  pixel row; lines are peaks, the gaps between them valleys, furigana a
  small peak above a line. About 50 lines of C++ and no model. It works
  when the box is plain and readable, and fails on see-through boxes over
  busy art or on outlined text.
- **PaddleOCR (PP-OCRv5)**: a detection model and a reading model, with
  official ONNX exports, in a small "mobile" and a larger "server" size.
  Its reader takes exactly one line and covers Chinese, English and
  Japanese. Its own benchmark scores it about 60 on Japanese against 86
  on Chinese, so manga-ocr probably reads better. Its detection is what's
  worth having: it copes with loose crops, even the whole window. That
  matters for full-screen narration pages, choice menus and pop-ups,
  where the dialogue box crop is wrong.
- Not Tesseract (poor on Japanese game fonts), and not Windows' own OCR
  (Windows only, and WinRT from MinGW is painful).

The comparison: about 20 screenshots from real VNs, including full-screen
narration pages and choice menus, not only dialogue. Each goes through:

1. manga-ocr on the whole crop.
2. The row-counting splitter, then manga-ocr per line.
3. Paddle detection, then manga-ocr per line.
4. Paddle detection and Paddle's own reader.

It answers two questions: which reader is good enough, and how often the
remembered crop is wrong in practice. If the games keep their text in
one box, 1 or 2 is enough and Paddle isn't needed. If text moves around
a lot, Paddle's detection is worth its download.

Things to solve:

- onnxruntime, for manga-ocr and Paddle alike. On Linux it's packaged.
  Microsoft's Windows DLLs are built with MSVC, so linking them from
  MinGW through onnxruntime's C API has to be checked first.
- Detection without OpenCV: Paddle's reference code uses contours and
  rotated rectangles, but game text is horizontal or vertical, so
  grouping pixels into plain rectangles is enough. That's small and
  testable.
- Reading order: horizontal lines top to bottom, vertical ones right to
  left.
- Furigana: drop lines much shorter than their neighbours, or better,
  keep them as the reading.
- Size: about 80 MB for Paddle's server reader, plus its detector, plus
  a few hundred MB for manga-ocr.
- Speed: not measured. It should be far below whisper's, but it needs a
  benchmark like whisper got.

## Settings to add

To `src/core/settings.h`, as new categories in the settings window:

- **Buffer**: length in seconds (with limits, e.g. 10–120), on/off, the
  hotkey, the default selection length, dictation mode on/off.
- **Anki**: deck, note type, field mapping, the last crop rectangle.

Done, except dictation mode (not built).

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
  cost about as much as a long one. The encoder now looks at the clip
  plus 5 s, at least 10 s (`audio_context` in src/core/transcriber.cpp):
  large-v3-turbo-q5_0, which picks kanji far better than small, went from
  31-35 s to 10-13 s for a 4 s line, and from 32 s to 17-20 s for a 10 s
  one, with the same text. A window cut closer to the clip breaks the
  output. Measured with espeak's voice, so still to check on real anime
  lines; and turbo is still slow on this CPU.
