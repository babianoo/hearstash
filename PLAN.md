# Plan: the rolling audio buffer

The main feature of the prototype, and the selling point for the startup
class. It grew out of the "rolling audio buffer" experiment in Japanese
Helper, and takes ideas from Yomitan and from mining cards with mpv +
mpvacious.

Nothing here is built yet. This is the order to build it in, and what
still needs deciding.

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

### 5. Look up what's selected

- When the mouse button is released over selected text, search for it
  with the existing Jisho code, and show the result in a panel next to
  the transcript.
- Jisho handles some inflected forms (食べた → 食べる), not all.
- Ignore selections that are only punctuation or spaces.

Done when: selecting a word shows its reading and meaning. **This is the
demo.**

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

### Later

- Local dictionary (JMdict) with deinflection: works offline, is faster,
  and allows Yomitan-style lookup by hovering instead of selecting.
- Whisper segment timestamps, so selecting a word can pick its sentence
  and that sentence's audio by itself.
- Linux: the PulseAudio/PipeWire monitor source for audio, plus the
  hotkey and the screenshot.
- Running in the background: start with Windows, a tray icon, closing to
  the tray.

## Settings to add

To `src/core/settings.h`, as new categories in the settings window:

- **Buffer**: length in seconds (with limits, e.g. 10–120), on/off, the
  hotkey, the default selection length.
- **Anki**: deck, note type, field mapping, the last crop rectangle.

## Privacy

The app records everything the computer plays, all the time, so this has
to be clear and safe, and it's a point worth making in the pitch:

- Audio stays in memory and is never written to disk, except a clip the
  user adds to a card.
- A visible sign that it's listening (tray icon), and a way to pause it.
- Nothing is sent anywhere except what the user chooses to look up.

## Open questions

- **Sample rate of the buffer**: 16 kHz (simplest, but clips sound
  muffled) or 48 kHz (better clips, one resample before whisper)?
  Suggestion: 48 kHz mono.
- **Where cropping happens**: in the capture window right away, or only
  in the card window? Suggestion: the card window, with the last crop
  already applied, so people who only want to read the transcript skip it.
- **Which monitor**: the one with the foreground window, or the one with
  the mouse? They're usually the same.
- **Whisper model**: which size is fast enough on a typical student
  laptop for a 5–10 s clip? Needs measuring for the demo.
- **One window or two**: waveform, transcript and lookup in one window
  (suggested), or a separate window for each step?
- **Hotkey while the window is open**: take a new capture, or bring the
  window back?
