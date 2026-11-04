# Hearstash

Turn words you hear into flashcards.

A desktop app for learning Japanese from what you watch. It keeps the last
few seconds of whatever your computer is playing (anime in mpv, a YouTube
video, a stream). When you miss a line, press a shortcut and you can:

1. see the audio as a waveform and select the line, then play it back to
   check it,
2. transcribe the selection with [whisper.cpp](https://github.com/ggml-org/whisper.cpp),
   which runs on your own machine,
3. select any word in the transcript to look it up on Jisho,
4. add it to Anki as a card with the sentence, the audio clip and a
   cropped screenshot of the moment.

The fast path is **shortcut → Enter → read**. Everything else is optional.

The main window is also a lookup tool: type or paste text, or record it
from the microphone (Record Input in the **+** menu), and search Jisho, DeepL or MyMemory. Successful
searches are kept in a history window.

It's written in C++23 with [FLTK](https://www.fltk.org/), and runs on
Windows and Linux. [PLAN.md](PLAN.md) has the design, what's done, and
what's still to try.

## Building

### Dependencies

Installed on the system:

- CMake 3.21 or newer, and a C++23 compiler (GCC or MinGW)
- FLTK (1.4 recommended)
- libcurl
- nlohmann_json 3.11 or newer
- libjpeg-turbo
- pkg-config
- Optional, on Linux: `wayland-client`, for a clipboard that works under
  Wayland

Built with the app, as git submodules in `external/`:

- [whisper.cpp](https://github.com/ggml-org/whisper.cpp), for speech to text
- [miniaudio](https://github.com/mackron/miniaudio), for recording and playback

Fetch the submodules first:

```bash
git submodule update --init
```

### Linux

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

### Windows (MinGW + vcpkg)

Install curl, nlohmann-json and libjpeg-turbo with vcpkg's
`x64-mingw-static` triplet, and point `CMAKE_PREFIX_PATH` at an FLTK
build. `-static` links everything into one `.exe`:

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-mingw-static -DVCPKG_HOST_TRIPLET=x64-mingw-static -DCMAKE_PREFIX_PATH=C:/deps/fltk -DCMAKE_EXE_LINKER_FLAGS=-static
```

```bash
cmake --build build -j
```

### Tests

The parts that don't need an audio device or a window have plain test
executables:

```bash
ctest --test-dir build
```

## Setting it up

Everything is in **Settings**:

- **Speech-to-Text**: download a whisper model and choose it. `small` is a good
  start; `large-v3-turbo-q5_0` picks kanji much better but is slower.
  Downloads are checked against their SHA-1. You can also choose the
  microphone here.
- **Buffer**: turn the rolling buffer on or off, set how many seconds it
  keeps (and how much of the end is selected when a capture opens), and
  set the capture shortcut.
- **Anki**: choose the deck and note type, and which field gets the word,
  reading, meaning, sentence, audio and picture. Fields are guessed from
  their names. Needs Anki running with the
  [AnkiConnect](https://ankiweb.net/shared/info/2055492159) add-on.
- **API**: your DeepL key (free or Pro). It can also come from the
  `DEEPL_API_KEY` environment variable, which is never saved.
- **History**: how many searches to keep, and clearing them.

### The capture shortcut

- **Windows**: `Win+Alt+C` by default. Change it in Settings > Buffer by
  pressing the new keys in its box.
- **Linux**: Wayland doesn't let apps grab keys, so bind a key in your
  desktop to `hearstash --capture`, which tells the running app to
  capture. Settings > Buffer shows the command with the right path. For
  Hyprland:

  ```
  bind = SUPER, A, exec, /path/to/hearstash --capture
  ```

### Screenshots on Linux

Screenshots use [grim](https://sr.ht/~emersion/grim/), so they only work
on wlroots desktops such as Hyprland or Sway. GNOME, KDE and X11 aren't
supported yet.

## Using a capture

1. Press the shortcut. The window opens with the end of the buffer
   selected.
2. Drag on the waveform to change the selection. **Space** plays and
   stops it.
3. **Enter** transcribes the selection. **Esc** cancels a transcription
   that's running.
4. Select text in the transcript to look it up.
5. To make a card, press **Add to Anki**, check the fields, crop the
   screenshot to the video (the last crop is remembered), and press
   **Add**. Cards are tagged `hearstash`.

## Where files go

| What | Windows | Linux |
| --- | --- | --- |
| `settings.json` | `%APPDATA%\Hearstash` | `$XDG_CONFIG_HOME/Hearstash` (`~/.config/Hearstash`) |
| `history.json` | `%APPDATA%\Hearstash` | `$XDG_STATE_HOME/Hearstash` (`~/.local/state/Hearstash`) |
| Whisper models | `%LOCALAPPDATA%\Hearstash\models` | `$XDG_DATA_HOME/Hearstash/models` (`~/.local/share/Hearstash/models`) |

An unreadable `settings.json` is renamed to `settings.json.bad` rather
than overwritten.

## Privacy

The app listens to everything your computer plays, so:

- The buffer stays in memory and is never written to disk, except a clip
  you add to a card.
- Transcription runs locally. Nothing is sent anywhere except what you
  choose to look up (to Jisho, DeepL or MyMemory) and cards you add to
  your own Anki.
- Turning the buffer off in Settings > Buffer stops it recording.

## Known gaps

- Adding a card to a running Anki hasn't been tried by hand yet.
- On Windows, screenshots use GDI; some hardware-accelerated video may
  come out black. DRM-protected video (Netflix and the like) always does.
- No tray icon or start-with-Windows yet.
- Jisho handles some inflected forms, not all. A local JMdict dictionary
  with deinflection is planned.

See [PLAN.md](PLAN.md) for the rest.
