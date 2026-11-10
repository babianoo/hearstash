# Transcription tests (2026-11-10)

The **50 audio tests** from the startup roadmap (Phase 1: "run the prototype on 50 short clips … record how many sentences are transcribed correctly per model size"): how accurate and fast whisper is on real anime lines, per model, on the CPU and a GPU. The same write-up is in the entrepreneurship repo, for the course.

Two shows were tested: a loud comedy with a lot of shouting, and a calmer drama with harder words.

## Short answer
- **Accurate enough? Yes.** With the app's default model:
  - **the comedy:** 35 of 50 lines come out right or nearly right, with 8% of the sounds wrong. Most of what's left is character names and shouted or stammered gag lines.
  - **the calmer show:** 40 of 49, with 3.5% wrong.
- **Fast enough? On a desktop CPU, yes.** The app's default model takes a median of **4.4 s** for a 4.7 s line on our i3-12100F.
- **The *small* model is 4 times faster (1 s) but not always as good.** It kept up on the comedy (8.8% wrong), but fell behind on the calmer show (6.2% against 3.5%), missing exactly the harder words a learner looks up. It's the fallback for weak laptops, not the default.
- **A GPU makes it about 5 times faster**, even a 2016 one: **0.9 s** a line with the default model, at the same accuracy.
- **The tests found a bug in the app**, and it's fixed now. A line over 5 s could come out as nonsense in several languages, depending on what was transcribed before it. See [The bug the tests found](#the-bug-the-tests-found).

## How we tested
### The first show: a loud comedy
- **Clips.** 50 lines from all 12 episodes of *The 100 Girlfriends Who Really, Really, Really, Really, Really Love You*: 4 per episode, plus one more from 2 random episodes. They were picked at random by a script, from lines:
  - 1.5 to 10 s long (median 4.7 s, 2.1 to 10.5 s with padding)
  - with nobody else talking within 0.6 s before or after
  - not part of the opening song
- **Like a learner's selection.** Subtitle events that follow each other with almost no gap (one sentence split in two) are joined into one line, and each clip gets 0.25 s extra on each side.
- **Reference text.** The Japanese subtitles of the fansub release (Airota & Nekomoe kissaten). They are close to what's said, but not guaranteed to be word for word.

### The second show: a calmer drama
- **Clips.** 49 lines from episodes 1 and 2 of *The Apothecary Diaries* season 3, 25 and 24, picked at random by the same script. They're from lines of 1.5 to 10 s (median 3.5 s, 2.0 to 8.2 s with padding). More talking, less shouting, and more formal and old-fashioned words.
- **Reference text.** Japanese TV closed captions. They were timed to another recording than our video, so each episode's captions were shifted to match (36.0 and 37.7 s). The shift was found by lining them up with the video's own English subtitles, and stays the same through each episode.
- **Cleaned up:**
  - speaker names and sounds in brackets, like （高順） or （蝗の羽音）, are taken out
  - a caption that is only a sound is skipped
  - captions continued with ➡ are joined into one line
- **Left out:**
  - captions with two people's lines at once: the second person often speaks after the caption's time is up
  - singing (♬)
  - lines that are mostly laughter
  - 2 lines of only eating sounds, taken out by hand
- **Padding.** A TV caption stays up until the next one, so captions touch. A clip gets the 0.25 s of padding only on a side where no other caption is that close. Spot checks showed no words cut off at the start or end.

### Both shows
- **Same settings as the app.** A small test program calls whisper.cpp with the exact settings from `src/core/transcriber.cpp`:
  - Japanese, greedy decoding, no context from the previous clip, non-speech tokens suppressed
  - flash attention off (see [the bug](#the-bug-the-tests-found))
  - the app's **shortened window**: whisper looks at the clip + 5 s, at least 10 s, instead of its usual 30 s. "Full window" rows use the 30 s instead.
- **All clips in a row, with the model kept loaded,** like a learner transcribing line after line.
- **Machine.** Intel i3-12100F (4 cores, 8 threads, a 2022 desktop CPU), 16 GB RAM. CPU only, with 4 threads, which is what the app uses. The app's own whisper.cpp build has no GPU support. A GTX 1050 Ti (2016, 4 GB) is used for the GPU runs, through Vulkan.
- **Time** is the time to transcribe one clip, without loading the model (the app loads it ahead of time). One warm-up clip is run first and not counted.

### Scoring
Spaces and punctuation are ignored.
- **Reading errors:** both texts are turned into katakana readings first (with the UniDic dictionary, plus the readings of the characters' names and of a few words UniDic reads differently from how they're said, like 明日 as あした). So 可愛い and かわいい, 分かる and わかる, or ２冊 and 二冊 count as the same. This is the main number: it says whether whisper **heard** the words. The learner looks the word up either way.
- **Text errors:** the characters as written, so other spellings count as wrong.
- **Correct:** no reading errors at all. **Close:** at most 10% of the sounds wrong, which is about one wrong sound in a short sentence. A close line is still useful: the learner sees the line and replays the clip.
- **Caveat:** a wrong word that sounds the same still counts as right. Example: 愛情 ("love") written for the name 愛城 (Aijō).

## Results: the comedy
All 50 clips, on the CPU.

| Model | Size | Window | Correct | Close | Reading errors | Text errors | Median time | Slowest | Time ÷ clip length |
|---|---|---|---|---|---|---|---|---|---|
| tiny | 75 MiB | app | 5 | 17 | 33.4% | 47.8% | 0.2 s | 5.3 s | 0.03 |
| base | 142 MiB | app | 12 | 23 | 19.9% | 31.6% | 0.3 s | 4.4 s | 0.06 |
| **small** | 466 MiB | app | 18 | 36 | 8.8% | 18.1% | **1.0 s** | 1.8 s | 0.21 |
| small | 466 MiB | full | 21 | 37 | 7.7% | 16.7% | 3.4 s | 11.3 s | 0.73 |
| medium | 1.5 GiB | app | 25 | 38 | 7.0% | 15.3% | 3.2 s | 5.6 s | 0.68 |
| medium | 1.5 GiB | full | 24 | 38 | 7.8% | 15.3% | 10.3 s | 12.1 s | 2.15 |
| large-v3-turbo | 1.5 GiB | app | 25 | 38 | 6.4% | 13.7% | 5.5 s | 8.2 s | 1.08 |
| **large-v3-turbo-q5_0** (app default) | 547 MiB | app | 23 | 35 | 8.0% | 15.1% | **4.4 s** | 7.7 s | 0.93 |
| large-v3-turbo-q5_0 | 547 MiB | full | 26 | 40 | 5.2% | 11.6% | 16.5 s | 18.7 s | 3.46 |
| large-v3 | 2.9 GiB | app | 28 | 38 | 6.3% | 13.7% | 6.8 s | 57.9 s | 1.49 |
| large-v3 | 2.9 GiB | full | 29 | 39 | 5.3% | 12.5% | 19.9 s | 51.4 s | 4.27 |

"Time ÷ clip length" under 1 means it's faster than playing the clip.

### What the table says
- **From small up, the models are close.** small to large-v3 are all between 6% and 9% reading errors. With 50 clips, a difference of 2 or 3 lines between two models isn't meaningful.
- **tiny and base are not good enough** for Japanese: a fifth to a third of the sounds wrong.
- **The shortened window costs little.** It's 1 to 3 points worse than the full window, and 3 to 4 times faster.
- **large-v3 is the most accurate, but slow and uneven on a CPU.** 7 of its 50 clips took over 10 s, one 58 s, when whisper wasn't sure of a line and tried again.
- **small with the app's window looks like a bargain here:** 8.8% wrong, in 1 s per line, never over 2 s. The calmer show says otherwise (below).

## Results: the calmer show
All 49 clips. The models that fit on the GPU were run there: accuracy is the same as on the CPU. large-v3, which doesn't fit, ran on the CPU, as did the default model for its CPU time.

| Model | Window | Correct | Close | Reading errors | Text errors | Median time (GPU) | Median time (CPU) |
|---|---|---|---|---|---|---|---|
| tiny | app | 9 | 15 | 21.3% | 33.6% | 0.1 s | |
| base | app | 13 | 24 | 11.9% | 23.0% | 0.1 s | |
| small | app | 20 | 32 | 6.2% | 16.3% | 0.4 s | |
| small | full | 20 | 33 | 6.6% | 16.2% | 1.0 s | |
| medium | app | 32 | 39 | 3.6% | 13.7% | 1.1 s | |
| large-v3-turbo | app | 34 | 40 | 3.4% | 11.7% | 1.6 s | |
| **large-v3-turbo-q5_0** (app default) | app | 33 | 40 | **3.5%** | 12.2% | **0.8 s** | 4.3 s |
| large-v3-turbo-q5_0 | full | 33 | 38 | 3.7% | 11.2% | 3.0 s | |
| large-v3 | app | 36 | 41 | 3.1% | 11.9% | | 6.1 s |
| large-v3 | full | 37 | 41 | 2.7% | 11.6% | | 19.2 s |

### What the table says
- **Calmer speech halves the errors** of every model from medium up: 3 to 4% of sounds wrong, against 6 to 8% on the comedy.
- **small falls behind.** 6.2% wrong, against 3.5% for the default model. What it misses is the vocabulary:

  | Said | small heard |
  |---|---|
  | 拒む ("refuse") | 小バム |
  | 観念 ("give up") | 関連 ("connection") |
  | 家賃 ("rent") | あっちに |
  | 天女 ("celestial maiden") | 天虚 |
  | 嬢ちゃん ("young lady") | 女ちゃん |

  These are the words a learner looks up. Both turbo models and large-v3 got all five; medium missed two (拒む and 家賃).
- **The shortened window costs nothing here.** The app's window matches the full one, or beats it.
- **large-v3 is a little better again** (3.1%), at 1.5 times the default model's CPU time.

## The bug the tests found
The first run gave much worse results for the app's window, and they changed with the order of the clips. Clip 14 came out right on its own, and as nonsense right after clip 08:

| | |
|---|---|
| Subtitle | よーし　いい度胸ね　怒ってやろうじゃないの　とっくに怒ってるじゃないですか |
| After clip 08 | 終わーし!良いドır曳念! 心の auf getting贈 |
| On its own, or with the fix | よーし!いい度胸ね、怒ってやろうじゃないの!とっくに怒ってるじゃないですか… |

**The cause: a bug in whisper.cpp.**
- Its flash attention (on by default) keeps what it computed from the audio in blocks of 256 frames (5.12 s).
- The decoder reads the whole last block, even past the end of the window. Those frames weren't written for this clip, and still hold the previous one's.
- The app's window ends exactly on a block only for lines up to 5.12 s. Any longer line could read leftovers from the line before.

**It affected the app too.** The app keeps the model loaded between transcriptions, so a learner could get nonsense for a line over 5 s, depending on what they transcribed before it.

**Fixed:** flash attention is now off in [`transcriber.cpp`](../src/core/transcriber.cpp) (`Transcriber::load`). Then the decoder reads exactly the window. On the CPU it's no slower. With the bug, the first run had:
- 7 of 50 lines as nonsense with the app's default model
- 13 of 50 lines as one or two characters ("ア", "。") with large-v3

## What still goes wrong
With the bug fixed, what's left is whisper mishearing.

### 1. Names
Whisper doesn't know the characters, so it writes their names by sound, often as other words:

| Name | Written as |
|---|---|
| 唐音 (Karane) | カラネ, からね, 彼, 金 |
| 羽香里 (Hakari) | はかり, 計り, あかり, 明かり |
| 楠莉 (Kusuri) | 薬 ("medicine") |
| 恋太郎 (Rentarō) | レンタロウ, 蓮太郎, レントラ |

The calmer show has the same problem: 趙迂 (Chōu) came out as 町, 長男 ("eldest son") or 超尾.

For a learner this matters less: names aren't what they look up, and they'll recognise them.

### 2. Shouting, stammering and gags
Every model gets these wrong:
- **Clip 24:** べべ別に　んなわけ (stammered, 1.9 s), written as うちに、のき、で, ブーブー or 無事に脳却
- **Clip 05:** かわいいかわいいかわいい, shouted. With the app's window, tiny, small, medium and both turbo models hear 怖い怖い怖い ("scary"). With the full window, every model hears it right: this is what the shorter window costs.
- **Clip 26:** a gag line ending in a fake retching sound (オエー), which whisper drops or turns into words

### 3. Rare words
Even the default model misses a few harder words on the calmer show:
- 湯あみ ("a bath") heard as 弱み ("a weakness"), by every model
- 数年 ("several years") heard as 終年

The learner sees a word that doesn't fit, and can replay the line.

### 4. Stammers and laughs
Whisper leaves out stammers (わっ… わかった, こっ… これは) and laughs (ハハハッ). They count as errors in the score, but don't matter for lookup.

### 5. Repeats
Once in a while a line gets repeated at the end, e.g. 一人だけ走ってばかり twice (clip 23, app's default model). The learner sees it right away.

## Speed

### CPU
- **On our desktop, every model from small to large-v3-turbo is fine:** 1 to 5.5 s for a median 4.7 s line, with the app's window. The learner has already paused to replay and select the line.
- **Not measured on a weak laptop.** The comment in `transcriber.cpp` gives one data point. On an i7-8565U (a 2018 laptop CPU), a 4 s line took 10 to 13 s with the app's window and default model. That's roughly 2.5× slower than our desktop. Scaled the same way:
  - the default model would take around 11 s
  - small around 2.5 s
- **Loading the model** takes under 1 s for everything except large-v3 (6 s). The app loads it ahead of time anyway.

### GPU
The same 50 clips on a **GTX 1050 Ti** (2016, 4 GB), through whisper.cpp's Vulkan backend: a separate build, since the app's has no GPU support. Flash attention is off, like in the app.

| Model | Window | Correct | Close | Reading errors | Median time (GPU) | Median time (CPU) | Slowest (GPU) |
|---|---|---|---|---|---|---|---|
| tiny | app | 5 | 17 | 32.7% | 0.1 s | 0.2 s | 9.3 s |
| base | app | 12 | 23 | 19.9% | 0.2 s | 0.3 s | 7.7 s |
| small | app | 18 | 36 | 8.8% | 0.5 s | 1.0 s | 1.1 s |
| small | full | 21 | 38 | 7.6% | 1.3 s | 3.4 s | 10.4 s |
| medium | app | 25 | 38 | 7.0% | 1.6 s | 3.2 s | 3.2 s |
| large-v3-turbo | app | 25 | 38 | 6.4% | 2.2 s | 5.5 s | 3.6 s |
| **large-v3-turbo-q5_0** (app default) | app | 23 | 35 | 8.3% | **0.9 s** | 4.4 s | 3.5 s |
| large-v3-turbo-q5_0 | full | 27 | 41 | 5.1% | 3.8 s | 16.5 s | 4.6 s |

- **The same accuracy as on the CPU**, give or take one clip.
- **The default model is about 5 times faster:** 0.9 s a line instead of 4.4 s. The full window, the most accurate setting, comes down from 16.5 s to 3.8 s.
- **large-v3 doesn't fit.** It needs about 3.1 GB, and the desktop's own programs (KWin, Discord, VS Code, …) were using 1.6 GB of the 4 GB. Loading it failed, and whisper.cpp crashed instead of reporting the error. A GPU build of the app would have to check the free memory, or fall back to the CPU.
- **Flash attention isn't worth much here:** about 20% faster with the full window (3.1 s instead of 3.8 s). With the app's window it has the same bug on the GPU as on the CPU, so it stays off.

**For a server:** a GPU from 2016 already transcribes the default model's line in under a second. That's roughly 4,000 lines an hour on one card, with the app's window. A current server GPU would do many more. What one costs per hour is for the money side (Giorgi) to look up. Dividing it by the lines per hour gives the cost per line.

## Answers to the questions

### Is it fast enough?
- **On a desktop CPU: yes,** with any model from small to large-v3-turbo (1 to 5.5 s a line).
- **On a weak laptop: probably only with small** (around 2.5 s), which is worse on harder words. The default model would take around 11 s.
- **With a GPU: yes, easily.** Even a GTX 1050 Ti does the default model in 0.9 s, or the most accurate setting in 3.8 s.

### Is it compatible enough?
- **Runs on any x86-64 CPU, with no GPU needed.**
- **Model files are 75 MiB to 2.9 GiB**, so the model can be chosen to suit the computer. small (466 MiB) is good enough for slow ones, with more mistakes on harder words.
- **A GPU build through Vulkan** would cover NVIDIA, AMD and Intel GPUs, with nothing extra to install. It helps even on an old card: 5 times faster on a GTX 1050 Ti.
- **The catch is video memory.** large-v3 didn't fit next to the desktop's programs on a 4 GB card, so a GPU build has to check the free memory or fall back to the CPU.

### Is it accurate enough?
- **Yes.** With the default model:
  - **the comedy:** 35 of 50 lines are right or nearly right (8% of sounds wrong). With large-v3 it's 38 of 50 (6%).
  - **the calmer show:** 40 of 49 (3.5%). With large-v3 it's 41 of 49 (3%).
- **What's wrong is mostly names, shouted gag lines and a few rare words.** The learner sees the text next to the clip they just heard, and can replay it.

### How well does it work with different types of audio? Games?
- **Only anime was tested,** but two very different shows. Both have music and sound effects under a lot of the lines. The clips weren't sorted by how loud the music is, so this test can't say how much music hurts.
- **Calm talking is easy; shouting, stammering and very short exclamations are the weak spot.** Errors on the calm show are about half those on the loud comedy.
- **Harder words need a bigger model.** small did nearly as well as the default on the comedy's casual talk, but clearly worse on the calmer show's vocabulary.
- **Games, streams and YouTube aren't tested yet.** Games are likely close to anime (voice actors, music), streams harder (worse microphones, talking over each other).

### Local or on our servers? What would each cost?
- **Local:** costs us nothing per use, and it's accurate and fast enough on a normal desktop.
- **Server:** the most a server would add is large-v3 with the full window. On the comedy that's **about 3 points better** than the app's default model (5.3% vs 8.0% of sounds wrong). On the calmer show it's under 1 point (2.7% vs 3.5%). The startup's licensing plan's subscription only works if local isn't good enough for many users. For accuracy, these results say it is good enough. A server would mostly help **people with weak laptops**, for speed.
- **Server cost:** one 2016 GPU handles roughly 4,000 lines an hour with the default model. A current server GPU would do many more. The cost per line is the GPU's hourly price divided by that, which is for the money side (Giorgi) to work out.

## What to change
- [x] **Fix the flash attention bug** (done: flash attention off in `transcriber.cpp`).
- [ ] **Keep large-v3-turbo-q5_0 as the default, and offer small for slow computers.** small is 4 times faster and kept up on the comedy, but missed the harder words on the calmer show. The settings could say so.
- [ ] **Test the other audio types:** a game, a stream and a YouTube video.
- [ ] **Test on a weak laptop.**
- [x] **Consider a GPU build** (Vulkan). It needs a fallback to the CPU when the model doesn't fit in video memory. (Done: the app builds with Vulkan when it can, checks the free video memory before loading, and falls back to the CPU. Setting: Speech-to-Text > Transcribe on the GPU.)
- [ ] **Report the bug to whisper.cpp:** the flash-attention decoder reads past the end of a shortened window.

## Limits of this test
- **Two shows, one kind of audio.** Anime only: no games, streams or YouTube.
- **The reference texts aren't word for word.** The first show's is a fansub's subtitles, the second's TV captions. Both may leave out short sounds (あ, え) or tidy up what was said.
- **The second show's captions were timed by hand to another recording,** and shifted to fit. Spot checks found no words cut off, but a line could start or end a little early or late.
- **Only 2 episodes of the second show**, so few voices and scenes.
- **Lines with someone else talking right before or after were left out**, so overlapping speech isn't tested.
- **Same-sounding wrong words count as right** in the reading score. The text score is stricter.
- **Speed was measured on one desktop.**
- **About 50 clips per show is a small sample:** a difference of 2 or 3 lines between two models isn't meaningful.

## Files
The scripts and clips are in the Hearstash repo under `testing/audio-tests/`. That folder isn't committed: the clips come from copyrighted episodes.

| File | What it does |
|---|---|
| `select_clips.py` | Picks the lines and cuts the clips into `clips/`, with the reference text in `truth.json`. The second show's are in `sets/apothecary/` |
| `sync_check.py` | Checks how far subtitles are shifted from a video, per 4-minute section |
| `run_whisper.cpp` | The test program, with the app's whisper settings. Built against the app's whisper.cpp for the CPU, and against `build-vulkan/` for the GPU |
| `run_all.sh` | Runs each model on all the clips, into `results/` |
| `score.py` | Scores the results into `scores.json` |
| `results-flash-attn-bug/` | The first run, with the bug |
