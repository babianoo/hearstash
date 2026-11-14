# Hearstash

Turn words you hear into flashcards.

![A line from a video is captured with Win+Alt+C, selected on the waveform, transcribed, looked up on Jisho and added to Anki](docs/capture.gif)

A desktop app for learning Japanese from what you watch. It keeps the last
few seconds of whatever your computer is playing (anime in mpv, a YouTube
video, a stream). When you miss a line, press a shortcut and you can:

1. see the audio as a waveform and select the line, then play it back to
   check it,
2. transcribe the selection with [whisper.cpp](https://github.com/ggml-org/whisper.cpp),
   which runs on your own machine,
3. select any word in the transcript to look it up in the dictionary that
   comes with the app, inflected or not (悩んでた finds 悩む),
4. add it to Anki as a card with the sentence, the audio clip and a
   cropped screenshot of the moment.

The fast path is **shortcut → Enter → read**. Everything else is optional.

If you'd rather not stop the show, a third shortcut **stashes** the line
instead: it's saved without opening anything, with a short chime, and the
stash button in the main window opens it after the episode, exactly as a
capture would have. It can also be transcribed straight away, so the
stash shows what each line says.

For games and visual novels, a second shortcut reads the text on the
screen instead (OCR, with [PaddleOCR](https://github.com/PaddlePaddle/PaddleOCR)'s
models, also on your own machine), into the same kind of window: look
words up, translate, and add cards the same way.

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
- zlib, to read the dictionary's zip and pack the app's own dictionary file
- pkg-config
- On Linux: libpng, for screenshots from the desktop's screenshot portal
- Optional, on Linux: `wayland-client`, for a clipboard that works under
  Wayland
- Optional, on Linux: libsystemd (its sd-bus), for screenshots on KDE,
  GNOME and X11. Without it, screenshots only work with grim
- Optional: Vulkan, to run whisper on the GPU (NVIDIA, AMD or Intel),
  about 5 times faster. Building it takes the Vulkan headers and loader,
  `glslc` and SPIRV-Headers (Arch: `vulkan-headers`, `vulkan-icd-loader`,
  `shaderc`, `spirv-headers`). It's used when they're all found; pass
  `-DHEARSTASH_GPU=OFF` to build for the CPU only, or `ON` to fail without
  them. The app then needs the Vulkan loader to start, which GPU drivers
  install. With no GPU, or not enough free video memory for the model,
  it transcribes on the CPU
- Optional: [onnxruntime](https://onnxruntime.ai/), to read text on the
  screen (Arch: `onnxruntime-cpu`; elsewhere Microsoft's prebuilt package,
  passed to CMake as `-DONNXRUNTIME_ROOT=<its folder>`). Without it, the
  screen capture says it can't read

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

That build is tuned for your own CPU. To make an `.exe` you can send to
other people, turn off `GGML_NATIVE` and target the AVX2 instruction set
instead. Any x64 PC from about 2013 or later supports it. `-s` strips the
debug symbols:

```bash
cmake -B build-dist -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-mingw-static -DVCPKG_HOST_TRIPLET=x64-mingw-static -DCMAKE_PREFIX_PATH=C:/deps/fltk "-DCMAKE_EXE_LINKER_FLAGS=-static -s" -DGGML_NATIVE=OFF -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON
```

```bash
cmake --build build-dist -j --target hearstash
```

`build-dist/hearstash.exe` then runs on its own. It only uses DLLs that
come with Windows 10 and 11.

### The dictionary

Configuring also fetches the latest [Jitendex](https://jitendex.org) zip
(about 40 MB) into the build folder, and the build converts it to
`jitendex.hsdict` next to the program, which takes about half a minute.
That file is the dictionary the app ships with: **hand it out together
with the program**, in the same folder. It's the one part of a build
that isn't Hearstash's code, and it's under its own licence (see
[Sources](#sources)).

- The zip is fetched again once the copy in the build folder is a week
  old, so a build to hand out has a current dictionary.
- `-DHEARSTASH_DICTIONARY_ZIP=<zip>` converts a zip you already have
  instead, e.g. where the build has no network.
- `-DHEARSTASH_DICTIONARY=OFF` builds none. The app then looks words up
  on jisho.org until it has downloaded the dictionary itself.
- On Windows, the vcpkg triplet needs `zlib` too, and a build with
  `--target hearstash` needs `--target dictionary` as well.

### Tests

The parts that don't need an audio device or a window have plain test
executables:

```bash
ctest --test-dir build
```

## Setting it up

Everything is in **Settings**:

- **General**: your DeepL key (free or Pro). It can also come from the
  `DEEPL_API_KEY` environment variable, which is never saved. With a key,
  transcripts and screen text are translated as soon as they're there;
  untick **Translate automatically** to only translate with the Translate
  button (Ctrl+T), e.g. to save the key's quota. Also how many searches the
  history keeps, clearing it, and the mouse wheel's scroll speed.
- **Speech-to-Text**: download a whisper model and choose it. `small` is a
  good start; `large-v3-turbo-q5_0` picks kanji much better but is slower.
  Downloads are checked against their SHA-1. It runs on the GPU unless you
  untick **Transcribe on the GPU**, e.g. to leave a game all the video
  memory. You can also choose the microphone here.
- **Recording**: turn the rolling buffer on or off, set how many seconds it
  keeps (and how much of the end is selected when a capture opens), and
  what stashing a line does.
- **Shortcuts**: the capture, screen capture and stash shortcuts.
- **Anki**: choose the deck and note type, and which field gets the word,
  reading, meaning, sentence, audio and picture. The note type is
  Hearstash's own unless you pick another: the sentence on the front, the
  word, meaning, audio and picture on the back, in large Japanese type. It's
  added to Anki with your first card. With another note type, fields are
  guessed from their names. Needs Anki running with the
  [AnkiConnect](https://ankiweb.net/shared/info/2055492159) add-on.

Hover over a setting for more on what it does.

### The capture shortcuts

There are three: the capture of what the computer played, the screen
capture, which reads the text on the screen, and the stash, which saves
a capture for later.

- **Windows**: `Win+Alt+C`, `Win+Alt+X` and `Win+Alt+Z` by default.
  Change them in Settings > Shortcuts by pressing the new keys in their
  box.
- **Linux**: Wayland doesn't let apps grab keys, so bind keys in your
  desktop to `hearstash --capture`, `hearstash --read-screen` and
  `hearstash --stash`, which tell the running app what to do. Settings >
  Shortcuts shows the commands with the right path. For Hyprland:

  ```
  bind = SUPER, A, exec, /path/to/hearstash --capture
  bind = SUPER, S, exec, /path/to/hearstash --read-screen
  bind = SUPER, Z, exec, /path/to/hearstash --stash
  ```

  On KDE, add each command in System Settings > Shortcuts > Add New >
  Command or Script.

### Screenshots on Linux

On wlroots desktops such as Hyprland or Sway, screenshots use
[grim](https://sr.ht/~emersion/grim/) if it's installed. On KDE,
Hearstash asks KWin itself for the monitor with the focus, which takes a
few hundredths of a second. KWin decides which programs may: if it says
no, Hearstash prints so once, and adding
`X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2` to the
`.desktop` file that starts Hearstash lets it.

Elsewhere (GNOME, X11, or a KWin that says no) screenshots go through
the desktop's screenshot portal (xdg-desktop-portal), which:

- may ask once whether Hearstash can take screenshots,
- takes all your monitors, not just the one with the video, so crop to
  it as usual (on KDE it's cut down to the monitor with the focus, with
  `kscreen-doctor`),
- is slower: about 2 s on KDE before the capture window opens.

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

## Using the stash

1. While watching, press the stash shortcut. Nothing opens; a rising
   chime says the line was kept, a low one that it wasn't (e.g. with the
   buffer off; the stash window then says why). The whole buffer is kept,
   with a screenshot.
2. Afterwards, press the stash button (the tray, left of History) in the
   main window. It shows how many lines are waiting, and lists them newest
   first, with their screenshot, when, and how long.
3. Double-click one, or press **Enter**, to open it in the capture
   window, then go on as with a capture. Adding a card from it takes it
   out of the stash. **Delete** removes one, **Clear All** all of them.

With **Transcribe stashed lines right away** in Settings > Recording,
each line's selection is transcribed in the background as soon as it's
stashed: the stash shows what it says, and opening it shows the
transcript without pressing Enter. It's off by default, since whisper then
runs while you watch, which can slow down a game.

## Using a screen capture

1. Press the screen capture shortcut in the game. The window opens with a
   screenshot, and the text on it is read. The first time, the models
   that read it are downloaded (about 30 MB).
2. Drag around the text box in the screenshot to read only that. It's
   kept for the game, so the next screen capture there reads only it too.
   **Whole screen** goes back to all of it.
3. Select text to look it up, and **Add to Anki** as with a capture. The
   card has no audio.

![The capture window: the selected line's waveform, its transcript with 映画 selected, and Jisho's entries for it](docs/lookup.png)

![Add to Anki: the word, reading, meaning and sentence filled in, with the screenshot to crop](docs/card.png)

## Where files go

| What | Windows | Linux |
| --- | --- | --- |
| `settings.json` | `%APPDATA%\Hearstash` | `$XDG_CONFIG_HOME/Hearstash` (`~/.config/Hearstash`) |
| `history.json` | `%APPDATA%\Hearstash` | `$XDG_STATE_HOME/Hearstash` (`~/.local/state/Hearstash`) |
| Stashed lines | `%APPDATA%\Hearstash\stash` | `$XDG_STATE_HOME/Hearstash/stash` (`~/.local/state/Hearstash/stash`) |
| Whisper models | `%LOCALAPPDATA%\Hearstash\models` | `$XDG_DATA_HOME/Hearstash/models` (`~/.local/share/Hearstash/models`) |
| Screen reading models | the same, in `ocr` | the same, in `ocr` |
| The dictionary it shipped with | `jitendex.hsdict`, next to `hearstash.exe` | next to `hearstash`, or in `../share/hearstash` from it |
| A dictionary it updated to | `%LOCALAPPDATA%\Hearstash\dictionary` | `$XDG_DATA_HOME/Hearstash/dictionary` |
| The frequency list, once downloaded | `frequencies.hsfreq`, in the same folder | the same |

An unreadable `settings.json` is renamed to `settings.json.bad` rather
than overwritten.

## Privacy

The app listens to everything your computer plays, so:

- The buffer stays in memory and is never written to disk, except a clip
  you add to a card, and lines you stash. A stashed line (its audio and
  screenshot, about 3 MB for 30 seconds) stays on disk until you make a
  card from it or delete it, and only your user can read it.
- Transcription and reading the screen run locally. Nothing is sent anywhere except what you
  choose to look up (to DeepL or MyMemory, or to Jisho when the
  dictionary on your computer has nothing for it) and cards you add to
  your own Anki. Words found in that dictionary are looked up without
  sending anything.
- When the dictionary is over 30 days old, the app asks jitendex.org
  whether there's a newer one, which sends nothing but the request.
- The one exception: with a DeepL key, each transcript and each text read
  on the screen is sent to DeepL to translate, without asking. Settings >
  General turns that off.
- Turning the buffer off in Settings > Recording stops it recording.

## Known gaps

- On GNOME, and on KDE when KWin doesn't let Hearstash take screenshots
  itself, the screenshot portal makes the capture window take about 2 s
  to open.
- On Windows, screenshots use GDI; some hardware-accelerated video may
  come out black. DRM-protected video (Netflix and the like) always does.
- No tray icon or start-with-Windows yet.
- Pictures in dictionary entries (a few hundred words have one) aren't
  shown or put on cards.
- Cards have no word audio or pitch accent: the dictionary has neither.

See [PLAN.md](PLAN.md) for the rest.

## Sources

Hearstash looks words up in **Jitendex**, and its cards carry Jitendex's
entries. The same credits are in the app, under Settings > Sources, with
the dictionary's age and a button to get the latest one.

- [Jitendex](https://jitendex.org), © Stephen Kraus, is used under the
  [Creative Commons Attribution-ShareAlike 4.0 licence](https://creativecommons.org/licenses/by-sa/4.0/).
  Hearstash converts it into one file, `jitendex.hsdict`, rearranged so a
  word is found quickly; its text isn't changed. That file is under the
  same licence, isn't encrypted, and its layout is described in
  [src/core/dictionary.h](src/core/dictionary.h) and
  [dictionary.cpp](src/core/dictionary.cpp), so you may copy, read and
  share it on the same terms.
- Jitendex is built from the [JMdict](https://www.edrdg.org/wiki/index.php/JMdict-EDICT_Dictionary_Project)
  dictionary files. These files are the property of the
  [Electronic Dictionary Research and Development Group](https://www.edrdg.org/),
  and are used in conformance with the Group's
  [licence](https://www.edrdg.org/edrdg/licence.html).
- Its example sentences are from [Tatoeba](https://tatoeba.org), under
  [CC BY 2.0 FR](https://creativecommons.org/licenses/by/2.0/fr/). On a
  card, each sentence links to its page on Tatoeba, which names who
  wrote it.
- Which reading goes over which kanji is from
  [JmdictFurigana](https://github.com/Doublevil/JmdictFurigana), under
  CC BY-SA 4.0.

The JMdict licence asks that software using the data keeps it up to
date. A build ships with the Jitendex that was current when it was made,
and the app looks for a newer one whenever the one it has is over 30
days old, offering to download it. Settings > Sources does the same on
demand.

Without the dictionary file, or for a search it has nothing for (in
English, say), words are looked up on [jisho.org](https://jisho.org)
instead.

### Word frequency

Settings > Sources can also download **JPDBv2㋕**, a frequency list in
Yomitan's format from
[Kuuuube's yomitan-dictionaries](https://github.com/Kuuuube/yomitan-dictionaries),
built from [jpdb.io](https://jpdb.io)'s frequency list by jpdb, Kuuube
and Gecko. Each word is then shown with it the way Yomitan shows it, e.g.
`JPDBv2㋕ 312, 19896㋕` for 読む: its rank among the words in jpdb.io's
corpus (lower is more common) and, marked ㋕, its rank when it's written
in kana. Its licence isn't stated, so it isn't shipped with Hearstash:
the app downloads it (about 6 MB, checked against its SHA-1) only when
you press Download, and converts it to `frequencies.hsfreq`, whose layout
is described in [src/core/frequency_list.h](src/core/frequency_list.h).
