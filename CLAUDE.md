# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CDImage is a Qt6 C++ GUI application that burns visible pictures onto a CD-R/RW surface by generating a specially crafted Audio CD track. The image is encoded into the physical laser burn pattern using disc-specific geometric calibration parameters.

## Building

Requires Qt 6 with the `widgets` module.

```bash
qmake && make
```

Or open `cdimage.pro` in Qt Creator. There is no CMake build; the project uses qmake exclusively.

## Architecture

The application has four components wired together through Qt signals/slots:

**`Converter`** (`src/converter.h/.cpp`) — the core engine. Takes a `QImage` and a file path, writes a raw Audio CD track (~800 MB). Controlled by three disc-geometry parameters:
- `tr0` — track offset at the start (innermost spiral position)
- `dtr` — track-to-track pitch
- `r0` — physical inner radius in mm

The `delays[]` array in `converter.h` encodes CIRC (Cross-Interleaved Reed-Solomon Coding) interleaving offsets from the ECMA-130 "Red Book" standard. The `pallete[]` array defines the four achievable grayscale levels on a CD surface. Conversion emits `progressChanged(int)` and can be cancelled via `cancelConverting()`.

**`CDPreview`** (`src/cdpreview.h/.cpp`) — interactive QWidget that renders a CD with the loaded image overlaid. Supports drag (left mouse), center (double-click), and zoom (scroll wheel). `getImage()` returns the final `QImage` at the correct offset/scale for passing to `Converter`.

**`CreateTrackDialog`** (`src/createtrackdialog.h/.cpp`) — dialog that holds the hardcoded brand presets (each preset is a `{tr0, dtr}` pair in `m_presets`). The `r0` parameter is not exposed per-preset and defaults to 24.5. If adding a new disc brand, add an entry to `m_presets` in the constructor.

**`MainWindow`** (`src/mainwindow.h/.cpp`) — top-level window (multiple inheritance: `QMainWindow` + `Ui::MainWindow`). Owns a `CDPreview centralView` and a `QImage m_image`. The `Edit→Load image` action calls `loadImage()`; `Edit→Create track` opens `CreateTrackDialog`, then runs `Converter`.

## Domain Notes

- The output file must be burned as an **Audio CD** (not data), e.g. `cdrecord -audio dev=<device> <track_file>`.
- Disc geometry varies by brand/batch; wrong parameters produce no visible image. Known-good presets are in `CreateTrackDialog`'s constructor.
- The algorithm and coordinate conversion logic originates from the [CD PAINT](http://undefer.narod.ru/cdpaint/index.html) project by [unDEFER].
