# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CDImage is a Qt6 C++ GUI application that burns visible pictures onto a CD-R/RW surface by generating a specially crafted Audio CD track. The image is encoded into the physical laser burn pattern using disc-specific geometric calibration parameters.

## Building

Requires Qt 6 with the `widgets`, `concurrent`, and `testlib` modules.

```bash
qmake && make
```

Or open `cdimage.pro` in Qt Creator. There is no CMake build; the project uses qmake exclusively.

## Tests

QTest-based suite in `tests/`. Build and run separately:

```bash
cd tests && qmake && make && ./cdimage_tests
```

Set `CDIMAGE_SKIP_LONG=1` to skip the WAV-header tests in `test_converter_wav.cpp` — each writes ~800 MB and takes minutes. CI runs with skip on; release verification should run without it. CI also sets `QT_QPA_PLATFORM=offscreen` so headless runners don't try to open a display.

## Architecture

Components wired through Qt signals/slots:

**`Converter`** (`src/converter.h/.cpp`) — core engine. Takes a `QImage` and a file path, writes a standard PCM WAV file (~800 MB; the audio data is byte-identical to a full Audio CD's sector stream). The 44-byte RIFF/WAVE header (44.1 kHz, 16-bit, stereo) is written first as a placeholder, then patched with final size fields after the data loop. Controlled by three disc-geometry parameters:
- `tr0` — track offset at the start (innermost spiral position)
- `dtr` — track-to-track pitch
- `r0` — physical inner radius in mm

The `delays[]` array in `converter.h` encodes CIRC (Cross-Interleaved Reed-Solomon Coding) interleaving offsets from ECMA-130. The `pallete[]` array defines the four achievable grayscale levels on a CD surface. Conversion emits `progressChanged(int)` and can be cancelled via `cancelConverting()`.

**`CDPreview`** (`src/cdpreview.h/.cpp`) — interactive QWidget that renders a CD with the loaded image overlaid. Drag (left mouse), center (double-click), zoom (scroll wheel). `getImage()` returns the final `QImage` for `Converter`.

**`ProfileDatabase`** (`src/profiledatabase.h/.cpp`) — two-tier disc profile store. Bundled presets live in `:/profiles/default_profiles.json` (Qt resource); user-calibrated profiles persist to `QStandardPaths::writableLocation(AppDataLocation)/profiles.json`. `userProfiles()` and `bundledProfiles()` are exposed separately so UI can label/group them. `saveUserProfile` returns `bool` and emits `saveFailed(QString)` on persist failure (mkpath, open, or short-write); on failure the in-memory list is rolled back to its pre-save snapshot to keep RAM and disk in sync. App identity (`setOrganizationName`/`setApplicationName` in `main.cpp`) is what makes `AppDataLocation` resolve predictably.

**`IDiscBackend`** (`src/idiscbackend.h`) — interface implemented by `LinuxDiscBackend` and `WindowsDiscBackend`. Reads disc identity (MMC `READ DISC INFORMATION` + `READ TOC/PMA/ATIP` for the manufacturer code) and shells out to `cdrecord` for burns. `burnTestPattern` returns a `BurnResult` struct (started/finished/exitCode/errorMessage/stderrText) so callers can distinguish "not in PATH" from "crashed" from "non-zero exit" — the previous `bool` return reported success when `cdrecord` never started.

**`DiscDetector`** (`src/discdetector.h/.cpp`) — async wrapper that calls `IDiscBackend::queryDisc()` on a worker, then emits `profileFound`/`profileNotFound`/`detectionFailed`.

**`CalibrationWizard`** (`src/calibrationwizard.h/.cpp`) — multi-page wizard launched when an unknown disc is inserted. Burns a rings test pattern, then measures geometry via either photo analysis (`PhotoCalibration`) or drive read-back timing (`DriveReadbackCalibration`). The `ResultPage` saves via Qt's canonical `validatePage()` (returns `false` to keep the wizard open if save fails), shows success/failure dialogs, and mints a synthetic `discId` (`user:<mediaType>:<name>`) when ATIP didn't yield one — without that, `findById` would never match the saved profile on re-insertion.

**`CreateTrackDialog`** (`src/createtrackdialog.h/.cpp`) — track-output dialog. Pre-selects the current disc profile if one was passed in, labels combo entries `[Local]`/`[Bundled]`. `discId` is the authoritative match key; `name` is fallback only when `discId` is empty.

**`MainWindow`** (`src/mainwindow.h/.cpp`) — top-level window. Owns `m_currentProfile` (the most recently detected/calibrated profile); passes it to `CreateTrackDialog` for pre-selection.

## Domain Notes

- Output is a standard PCM WAV file. Burn it as an **Audio CD** with any burning software (Nero, ImgBurn, Windows Media Player, or `cdrecord -audio dev=<device> <track.wav>`).
- Disc geometry varies by brand/batch; wrong parameters produce no visible image. Bundled profiles are in `resources/profiles/default_profiles.json`; user-calibrated profiles persist via `ProfileDatabase`.
- The algorithm and coordinate conversion logic originates from the [CD PAINT](http://undefer.narod.ru/cdpaint/index.html) project by [unDEFER].
