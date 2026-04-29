# Disc Geometry Detection — Design Spec

**Date:** 2026-04-28
**Scope:** Sub-projects 1 (disc identification + local DB) and 2 (calibration wizard).
Sub-project 3 (community geometry database) is explicitly out of scope.

---

## 1. Goals

- Automatically identify an inserted disc (CD-R, CD-RW, DVD-R, DVD-RW, DVD-DL) by querying the drive.
- Look up its geometry from a bundled or user-built profile database.
- When a disc is unknown, guide the user through a calibration wizard that produces a geometry profile using either a disc photograph or drive seek-time measurement.
- Support Linux and Windows. Same feature set on both platforms.

---

## 2. Data Model

### `DiscProfile` (`src/discprofile.h`)

Replaces the three loose `double` parameters (`tr0`, `dtr`, `r0`) currently on `Converter`. Single currency type used by detection, calibration, the database, and conversion.

```cpp
enum class MediaType { CD_R, CD_RW, DVD_R, DVD_RW, DVD_DL };

struct DiscProfile {
    QString   discId;            // ATIP manufacturer ID (CD) or media code (DVD)
    QString   name;              // human-readable label
    MediaType mediaType;

    double    tr0;               // starting track offset
    double    dtr;               // track-to-track pitch
    double    r0;                // inner radius (mm)

    int       layerCount;        // 1 for CD/DVD-R/DVD-RW; 2 for DVD-DL
    qint64    layerBreakSector;  // DVD-DL only; 0 otherwise

    bool      mixColors;
};
```

Serialisation: two free functions `QJsonObject toJson(const DiscProfile&)` and `DiscProfile fromJson(const QJsonObject&)` — the only serialisation touch points in the codebase.

The four existing hardcoded presets in `CreateTrackDialog` migrate to entries in `resources/profiles/default_profiles.json`.

---

## 3. Platform Backend

### `IDiscBackend` (`src/idiscbackend.h`)

```cpp
struct RawDiscInfo {
    QString   discId;
    MediaType mediaType;
};

class IDiscBackend {
public:
    virtual ~IDiscBackend() = default;

    virtual QStringList     availableDevices()                                         = 0;
    virtual RawDiscInfo     queryDisc(const QString& devicePath)                       = 0;
    virtual bool            burnTestPattern(const QString& devicePath,
                                            const QString& trackFile)                  = 0;
    virtual QVector<qint64> measureSeekTimes(const QString& devicePath,
                                             const QVector<qint64>& sectors)           = 0;
};
```

### Platform implementations

| File | Platform | Key APIs |
|---|---|---|
| `src/linuxdiscbackend.cpp` | Linux | `ioctl(CDROM_SEND_PACKET)` for MMC READ DISC INFORMATION; `ioctl(CDROMSEEK)` + `clock_gettime` for seek timing; `cdrecord -audio` for burning |
| `src/windowsdiscbackend.cpp` | Windows | `DeviceIoControl` / SCSI Pass-Through for READ DISC INFORMATION; SPTI SEEK + `QueryPerformanceCounter` for seek timing; `cdrecord` (bundled) or IMAPI2/COM for burning |

Both backends send the same MMC command byte sequence — the OS wrapper is the only difference.

### Factory

Each platform file defines:
```cpp
IDiscBackend* createDiscBackend();
```
`MainWindow` calls this once at startup and owns the result via `QScopedPointer<IDiscBackend>`.

### `cdimage.pro` wiring
```
unix:SOURCES  += src/linuxdiscbackend.cpp
win32:SOURCES += src/windowsdiscbackend.cpp
```

---

## 4. Profile Database

### `ProfileDatabase` (`src/profiledatabase.h/.cpp`)

Two-tier storage, merged at query time:

- **Bundled** — `:/profiles/default_profiles.json` (Qt resource). Read-only; updated via releases.
- **User** — `QStandardPaths::AppDataLocation + "/profiles.json"`. Written by calibration.

User tier takes precedence: a calibrated user profile always overrides the shipped default for the same `discId`.

```cpp
class ProfileDatabase {
public:
    explicit ProfileDatabase(QObject* parent = nullptr);

    DiscProfile*       findById(const QString& discId) const;
    QList<DiscProfile> allProfiles() const;
    void               saveUserProfile(const DiscProfile&);
    void               removeUserProfile(const QString& discId);
};
```

---

## 5. Disc Detection

### `DiscDetector` (`src/discdetector.h/.cpp`)

```cpp
class DiscDetector : public QObject {
    Q_OBJECT
public:
    explicit DiscDetector(IDiscBackend*, ProfileDatabase*, QObject* parent = nullptr);
    void detectAsync(const QString& devicePath);

signals:
    void profileFound(DiscProfile);
    void profileNotFound(RawDiscInfo);  // triggers calibration wizard
    void detectionFailed(QString error);
};
```

Runs on a worker thread (disc queries can block hundreds of milliseconds).

**Flow:**
1. `backend->queryDisc(devicePath)` → `RawDiscInfo`
2. `db->findById(rawInfo.discId)`
3. Found → emit `profileFound`; not found → emit `profileNotFound` with `RawDiscInfo` so the wizard can pre-fill disc ID and media type

---

## 6. Calibration Wizard

### Test pattern

A radial gradient image: intensity increases linearly from inner to outer radius. When burned and measured, the intensity ramp provides a spatial reference for mapping coordinates to physical disc positions.

### `ICalibrationMethod` (`src/icalibrationmethod.h`)

```cpp
class ICalibrationMethod : public QObject {
    Q_OBJECT
public:
    virtual void start(const RawDiscInfo&) = 0;

signals:
    void progressChanged(int pct);
    void finished(DiscProfile);
    void failed(QString error);
};
```

### `PhotoCalibration` (`src/photocalibration.h/.cpp`)

1. Load user-supplied disc photo as `QImage`
2. Detect disc boundary via radial scan from image centre — find first pixel row where average intensity shifts sharply (implementable with `QImage::pixel()`, no OpenCV dependency required)
3. Map radial intensity profile of photo against the known gradient pattern
4. Least-squares fit to solve for `tr0`, `dtr`, `r0`

### `DriveReadbackCalibration` (`src/drivereadbackcalibration.h/.cpp`)

1. Call `backend->measureSeekTimes()` for a set of evenly-spaced sectors across the disc
2. Seek time between two sectors is proportional to their radial/angular separation
3. Fit measured timings against ECMA-130 / ECMA-267 standard track pitch constants as the anchor to extract `tr0` and `dtr`

Both implementations emit `finished(DiscProfile)` when done.

### `CalibrationWizard` (`src/calibrationwizard.h/.cpp`, `src/calibrationwizard.ui`)

Constructor: `CalibrationWizard(IDiscBackend*, ProfileDatabase*, const RawDiscInfo&, QWidget* parent)`. The backend is needed for burn (Page 2) and seek-time read-back (Page 4b); the database is needed to save the result (Page 6).

```
Page 1: Welcome       — detected disc ID + media type, explains process
Page 2: Burn pattern  — generates test track, calls burnTestPattern(), progress bar
Page 3: Method select — "Photograph the disc" vs "Let the drive re-read it"
  ├─ Page 4a: Photo   — instructions + file picker
  └─ Page 4b: Readback— triggers measureSeekTimes(), progress bar
Page 5: Analysis      — runs ICalibrationMethod, progress bar
Page 6: Result        — computed DiscProfile params, editable name field, Save button
```

On save: calls `ProfileDatabase::saveUserProfile()`.

---

## 7. Changes to Existing Code

### `Converter`

Add a constructor accepting `DiscProfile`; extracts `tr0`/`dtr`/`r0` from it. Existing constructors remain. Conversion algorithm is untouched.

```cpp
Converter(QObject* parent, const DiscProfile& profile);
```

### `CreateTrackDialog`

Replace `QMap<QString, QVector<double>> m_presets` with a `ProfileDatabase` reference. Combo box populated from `db.allProfiles()`. Manual `tr0`/`dtr` fields remain. On accept, constructs a `DiscProfile` from selected preset or manual input.

### `MainWindow`

New members:
```cpp
QScopedPointer<IDiscBackend>   m_backend;
QScopedPointer<DiscDetector>   m_detector;
QScopedPointer<ProfileDatabase> m_profileDb;
DiscProfile                    m_currentProfile;
```

New toolbar action: **"Detect Disc"** → calls `m_detector->detectAsync(selectedDevice)`.

New slots: `onProfileFound(DiscProfile)`, `onProfileNotFound(RawDiscInfo)` (opens `CalibrationWizard`), `onDetectionFailed(QString)`.

No changes to `CDPreview` or `main.cpp`.

### `cdimage.pro` (full additions)

```
HEADERS += src/discprofile.h src/idiscbackend.h src/icalibrationmethod.h \
           src/discdetector.h src/profiledatabase.h \
           src/photocalibration.h src/drivereadbackcalibration.h \
           src/calibrationwizard.h

SOURCES += src/discdetector.cpp src/profiledatabase.cpp \
           src/photocalibration.cpp src/drivereadbackcalibration.cpp \
           src/calibrationwizard.cpp

unix:SOURCES  += src/linuxdiscbackend.cpp
win32:SOURCES += src/windowsdiscbackend.cpp

FORMS    += src/calibrationwizard.ui

RESOURCES += resources/profiles.qrc
```

---

## 8. File Summary

| File | Status | Purpose |
|---|---|---|
| `src/discprofile.h` | New | Unified disc geometry data model |
| `src/idiscbackend.h` | New | Platform backend interface |
| `src/linuxdiscbackend.cpp` | New | Linux disc querying (MMC/ioctl) |
| `src/windowsdiscbackend.cpp` | New | Windows disc querying (SPTI/DeviceIoControl) |
| `src/discdetector.h/.cpp` | New | Orchestrates detection flow |
| `src/profiledatabase.h/.cpp` | New | Two-tier JSON profile store |
| `src/icalibrationmethod.h` | New | Calibration strategy interface |
| `src/photocalibration.h/.cpp` | New | Photo-based geometry computation |
| `src/drivereadbackcalibration.h/.cpp` | New | Seek-time geometry computation |
| `src/calibrationwizard.h/.cpp/.ui` | New | QWizard UI |
| `resources/profiles/default_profiles.json` | New | Bundled presets (migrated from CreateTrackDialog) |
| `resources/profiles.qrc` | New | Qt resource file |
| `src/converter.h/.cpp` | Modified | Add DiscProfile constructor |
| `src/createtrackdialog.h/.cpp` | Modified | Use ProfileDatabase |
| `src/mainwindow.h/.cpp` | Modified | Add backend, detector, profile members + toolbar action |
| `cdimage.pro` | Modified | New files + platform split |
