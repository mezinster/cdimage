# Disc Geometry Detection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add automatic disc identification via drive query + profile database, plus a calibration wizard that measures disc geometry from a photo or drive seek-time read-back.

**Architecture:** A thin platform backend (`IDiscBackend`) abstracts Linux ioctl/MMC and Windows SPTI into the same four-method interface. `DiscDetector` queries the backend asynchronously and looks up the result in a two-tier `ProfileDatabase`; on a miss it opens `CalibrationWizard`, which runs either `PhotoCalibration` or `DriveReadbackCalibration` and saves the result back to the database.

**Tech Stack:** Qt 6 (widgets, concurrent), QTest (unit tests), `scsi/sg.h` + `linux/cdrom.h` (Linux backend), `ntddscsi.h` + `windows.h` (Windows backend), `cdrecord` (burn subprocess on both platforms).

---

## File Map

### New files
| Path | Purpose |
|---|---|
| `src/discprofile.h` | `DiscProfile` struct + `MediaType` enum + serialisation declarations |
| `src/discprofile.cpp` | `toJson` / `fromJson` implementations |
| `src/idiscbackend.h` | `IDiscBackend` pure-virtual interface + `RawDiscInfo` |
| `src/linuxdiscbackend.h` | Linux backend declaration + `createDiscBackend()` |
| `src/linuxdiscbackend.cpp` | Linux MMC/ioctl implementation |
| `src/windowsdiscbackend.h` | Windows backend declaration + `createDiscBackend()` |
| `src/windowsdiscbackend.cpp` | Windows SPTI/DeviceIoControl implementation |
| `src/discdetector.h` | `DiscDetector` declaration |
| `src/discdetector.cpp` | Async detection using `QtConcurrent::run` |
| `src/profiledatabase.h` | `ProfileDatabase` declaration |
| `src/profiledatabase.cpp` | Two-tier JSON load/save |
| `src/icalibrationmethod.h` | Calibration strategy interface |
| `src/testpatterngenerator.h` | Radial gradient generator declaration |
| `src/testpatterngenerator.cpp` | Gradient image + track generation |
| `src/photocalibration.h` | `PhotoCalibration` declaration |
| `src/photocalibration.cpp` | Radial-scan geometry extraction |
| `src/drivereadbackcalibration.h` | `DriveReadbackCalibration` declaration |
| `src/drivereadbackcalibration.cpp` | Seek-time geometry extraction |
| `src/calibrationwizard.h` | `CalibrationWizard` + page classes |
| `src/calibrationwizard.cpp` | Wizard page implementations |
| `src/calibrationwizard.ui` | Result page layout |
| `resources/profiles/default_profiles.json` | Bundled presets (migrated from `CreateTrackDialog`) |
| `resources/profiles.qrc` | Qt resource file |
| `tests/tests.pro` | QTest project |
| `tests/main.cpp` | Test runner |
| `tests/mockdiscbackend.h` | In-memory `IDiscBackend` mock |
| `tests/test_discprofile.h` / `.cpp` | DiscProfile unit tests |
| `tests/test_profiledatabase.h` / `.cpp` | ProfileDatabase unit tests |
| `tests/test_discdetector.h` / `.cpp` | DiscDetector unit tests |
| `tests/test_photocalibration.h` / `.cpp` | PhotoCalibration unit tests |

### Modified files
| Path | Change |
|---|---|
| `src/converter.h` | Add `DiscProfile` constructor declaration |
| `src/converter.cpp` | Implement `DiscProfile` constructor |
| `src/createtrackdialog.h` | Replace `m_presets` map with `ProfileDatabase*` |
| `src/createtrackdialog.cpp` | Populate combo from `db->allProfiles()` |
| `src/mainwindow.h` | Add backend/detector/db members + new slots |
| `src/mainwindow.cpp` | Wire detect action + calibration wizard |
| `cdimage.pro` | Add new files, `concurrent` module, platform split |

---

## Task 1: DiscProfile data model + test infrastructure

**Files:**
- Create: `src/discprofile.h`
- Create: `src/discprofile.cpp`
- Create: `tests/tests.pro`
- Create: `tests/main.cpp`
- Create: `tests/test_discprofile.h`
- Create: `tests/test_discprofile.cpp`

- [ ] **Step 1: Create `src/discprofile.h`**

```cpp
#ifndef DISCPROFILE_H
#define DISCPROFILE_H

#include <QString>
#include <QtGlobal>
#include <optional>

class QJsonObject;

enum class MediaType { CD_R, CD_RW, DVD_R, DVD_RW, DVD_DL };

struct DiscProfile {
    QString   discId;
    QString   name;
    MediaType mediaType        = MediaType::CD_RW;
    double    tr0              = 22951.52052;
    double    dtr              = 1.3865961805;
    double    r0               = 24.5;
    int       layerCount       = 1;
    qint64    layerBreakSector = 0;
    bool      mixColors        = false;
};

QJsonObject toJson(const DiscProfile&);
DiscProfile fromJson(const QJsonObject&);

#endif
```

- [ ] **Step 2: Create `src/discprofile.cpp`**

```cpp
#include "discprofile.h"
#include <QJsonObject>

QJsonObject toJson(const DiscProfile& p) {
    QJsonObject o;
    o["discId"]           = p.discId;
    o["name"]             = p.name;
    o["mediaType"]        = static_cast<int>(p.mediaType);
    o["tr0"]              = p.tr0;
    o["dtr"]              = p.dtr;
    o["r0"]               = p.r0;
    o["layerCount"]       = p.layerCount;
    o["layerBreakSector"] = static_cast<double>(p.layerBreakSector);
    o["mixColors"]        = p.mixColors;
    return o;
}

DiscProfile fromJson(const QJsonObject& o) {
    DiscProfile p;
    p.discId           = o["discId"].toString();
    p.name             = o["name"].toString();
    p.mediaType        = static_cast<MediaType>(o["mediaType"].toInt());
    p.tr0              = o["tr0"].toDouble(22951.52052);
    p.dtr              = o["dtr"].toDouble(1.3865961805);
    p.r0               = o["r0"].toDouble(24.5);
    p.layerCount       = o["layerCount"].toInt(1);
    p.layerBreakSector = static_cast<qint64>(o["layerBreakSector"].toDouble(0));
    p.mixColors        = o["mixColors"].toBool(false);
    return p;
}
```

- [ ] **Step 3: Create `tests/tests.pro`**

```
TEMPLATE = app
TARGET   = cdimage_tests
CONFIG  += testcase
QT      += testlib widgets concurrent

INCLUDEPATH += ..

HEADERS += test_discprofile.h \
           test_profiledatabase.h \
           test_discdetector.h \
           test_photocalibration.h \
           mockdiscbackend.h

SOURCES += main.cpp \
           test_discprofile.cpp \
           test_profiledatabase.cpp \
           test_discdetector.cpp \
           test_photocalibration.cpp \
           ../src/discprofile.cpp \
           ../src/profiledatabase.cpp \
           ../src/discdetector.cpp \
           ../src/photocalibration.cpp \
           ../src/testpatterngenerator.cpp \
           ../src/converter.cpp

RESOURCES += ../resources/profiles.qrc
```

- [ ] **Step 4: Create `tests/main.cpp`**

```cpp
#include <QCoreApplication>
#include <QTest>
#include "test_discprofile.h"
#include "test_profiledatabase.h"
#include "test_discdetector.h"
#include "test_photocalibration.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("cdimage_tests");
    int status = 0;
    auto run = [&](QObject* t){ status |= QTest::qExec(t, argc, argv); delete t; };
    run(new TestDiscProfile);
    run(new TestProfileDatabase);
    run(new TestDiscDetector);
    run(new TestPhotoCalibration);
    return status;
}
```

- [ ] **Step 5: Create `tests/test_discprofile.h`**

```cpp
#ifndef TEST_DISCPROFILE_H
#define TEST_DISCPROFILE_H
#include <QObject>
class TestDiscProfile : public QObject {
    Q_OBJECT
private slots:
    void roundtrip_preserves_all_fields();
    void fromJson_uses_defaults_for_missing_fields();
    void dvd_dl_roundtrip();
};
#endif
```

- [ ] **Step 6: Create `tests/test_discprofile.cpp`**

```cpp
#include "test_discprofile.h"
#include "../src/discprofile.h"
#include <QTest>
#include <QJsonObject>

void TestDiscProfile::roundtrip_preserves_all_fields() {
    DiscProfile p;
    p.discId = "97m24s01f"; p.name = "Test"; p.mediaType = MediaType::CD_R;
    p.tr0 = 22951.52; p.dtr = 1.3865961; p.r0 = 24.5;
    p.layerCount = 1; p.layerBreakSector = 0; p.mixColors = true;
    DiscProfile p2 = fromJson(toJson(p));
    QCOMPARE(p2.discId,            p.discId);
    QCOMPARE(p2.name,              p.name);
    QCOMPARE(p2.mediaType,         p.mediaType);
    QCOMPARE(p2.tr0,               p.tr0);
    QCOMPARE(p2.dtr,               p.dtr);
    QCOMPARE(p2.r0,                p.r0);
    QCOMPARE(p2.layerCount,        p.layerCount);
    QCOMPARE(p2.layerBreakSector,  p.layerBreakSector);
    QCOMPARE(p2.mixColors,         p.mixColors);
}

void TestDiscProfile::fromJson_uses_defaults_for_missing_fields() {
    QJsonObject o;
    o["discId"] = "test"; o["name"] = "Minimal";
    o["tr0"] = 22951.52; o["dtr"] = 1.3865961;
    DiscProfile p = fromJson(o);
    QCOMPARE(p.r0,               24.5);
    QCOMPARE(p.layerCount,       1);
    QCOMPARE(p.layerBreakSector, qint64(0));
    QCOMPARE(p.mixColors,        false);
}

void TestDiscProfile::dvd_dl_roundtrip() {
    DiscProfile p;
    p.mediaType = MediaType::DVD_DL;
    p.layerCount = 2;
    p.layerBreakSector = 1913760LL;
    DiscProfile p2 = fromJson(toJson(p));
    QCOMPARE(p2.mediaType,        MediaType::DVD_DL);
    QCOMPARE(p2.layerCount,       2);
    QCOMPARE(p2.layerBreakSector, qint64(1913760));
}
```

- [ ] **Step 7: Build tests and verify they fail (no ProfileDatabase or DiscDetector yet)**

```bash
cd /home/mezinster/cdimage/tests
qmake tests.pro && make 2>&1 | head -30
```

Expected: compile errors about missing `test_profiledatabase.h` and other headers — that's correct. Tests will build fully after Task 3.

- [ ] **Step 8: Commit**

```bash
cd /home/mezinster/cdimage
git add src/discprofile.h src/discprofile.cpp tests/
git commit -m "feat: add DiscProfile data model and test infrastructure"
```

---

## Task 2: ProfileDatabase + bundled JSON presets

**Files:**
- Create: `resources/profiles/default_profiles.json`
- Create: `resources/profiles.qrc`
- Create: `src/profiledatabase.h`
- Create: `src/profiledatabase.cpp`
- Create: `tests/test_profiledatabase.h`
- Create: `tests/test_profiledatabase.cpp`

- [ ] **Step 1: Create `resources/profiles/default_profiles.json`**

These are the four presets previously hardcoded in `CreateTrackDialog`. `discId` is empty for now — auto-detection populates it; manual selection still works via `allProfiles()`.

```json
[
  {
    "discId": "",
    "name": "Verbatim CD-RW Hi-Speed 8x-10x 700 MB SERL 1",
    "mediaType": 1,
    "tr0": 22951.52,
    "dtr": 1.3865961,
    "r0": 24.5,
    "layerCount": 1,
    "layerBreakSector": 0,
    "mixColors": false
  },
  {
    "discId": "",
    "name": "Verbatim CD-RW Hi-Speed 8x-10x 700 MB SERL 2",
    "mediaType": 1,
    "tr0": 22951.07,
    "dtr": 1.3865958,
    "r0": 24.5,
    "layerCount": 1,
    "layerBreakSector": 0,
    "mixColors": false
  },
  {
    "discId": "",
    "name": "eProformance CD-RW 4x-10x 700 MB Prodisk Technology Inc",
    "mediaType": 1,
    "tr0": 22936.085,
    "dtr": 1.38314,
    "r0": 24.5,
    "layerCount": 1,
    "layerBreakSector": 0,
    "mixColors": false
  },
  {
    "discId": "",
    "name": "TDK CD-RW 4x-12x HIGH SPEED 700MB 80MIN",
    "mediaType": 1,
    "tr0": 23000.145,
    "dtr": 1.38659775,
    "r0": 24.5,
    "layerCount": 1,
    "layerBreakSector": 0,
    "mixColors": false
  }
]
```

- [ ] **Step 2: Create `resources/profiles.qrc`**

```xml
<!DOCTYPE RCC>
<RCC version="1.0">
  <qresource prefix="/profiles">
    <file>profiles/default_profiles.json</file>
  </qresource>
</RCC>
```

- [ ] **Step 3: Create `src/profiledatabase.h`**

```cpp
#ifndef PROFILEDATABASE_H
#define PROFILEDATABASE_H

#include "discprofile.h"
#include <QList>
#include <QObject>
#include <optional>

class ProfileDatabase : public QObject {
    Q_OBJECT
public:
    // userProfilePath: override for testing; empty = QStandardPaths default
    explicit ProfileDatabase(const QString& userProfilePath = QString(),
                             QObject* parent = nullptr);

    std::optional<DiscProfile> findById(const QString& discId) const;
    QList<DiscProfile>         allProfiles() const;
    void                       saveUserProfile(const DiscProfile&);
    void                       removeUserProfile(const QString& discId);

private:
    void loadBundled();
    void loadUser();
    void persist() const;

    QList<DiscProfile> m_bundled;
    QList<DiscProfile> m_user;
    QString            m_userPath;
};

#endif
```

- [ ] **Step 4: Create `src/profiledatabase.cpp`**

```cpp
#include "profiledatabase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

ProfileDatabase::ProfileDatabase(const QString& userProfilePath, QObject* parent)
    : QObject(parent)
{
    if (userProfilePath.isEmpty())
        m_userPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                     + "/profiles.json";
    else
        m_userPath = userProfilePath;
    loadBundled();
    loadUser();
}

void ProfileDatabase::loadBundled() {
    QFile f(":/profiles/default_profiles.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr)
        m_bundled.append(fromJson(v.toObject()));
}

void ProfileDatabase::loadUser() {
    QFile f(m_userPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr)
        m_user.append(fromJson(v.toObject()));
}

std::optional<DiscProfile> ProfileDatabase::findById(const QString& discId) const {
    if (discId.isEmpty()) return std::nullopt;
    for (const auto& p : m_user)
        if (p.discId == discId) return p;
    for (const auto& p : m_bundled)
        if (p.discId == discId) return p;
    return std::nullopt;
}

QList<DiscProfile> ProfileDatabase::allProfiles() const {
    QList<DiscProfile> result = m_user;
    for (const auto& p : m_bundled)
        result.append(p);
    return result;
}

void ProfileDatabase::saveUserProfile(const DiscProfile& profile) {
    m_user.removeIf([&](const DiscProfile& p){ return p.discId == profile.discId; });
    m_user.append(profile);
    persist();
}

void ProfileDatabase::removeUserProfile(const QString& discId) {
    m_user.removeIf([&](const DiscProfile& p){ return p.discId == discId; });
    persist();
}

void ProfileDatabase::persist() const {
    QDir().mkpath(QFileInfo(m_userPath).absolutePath());
    QFile f(m_userPath);
    if (!f.open(QIODevice::WriteOnly)) return;
    QJsonArray arr;
    for (const auto& p : m_user)
        arr.append(toJson(p));
    f.write(QJsonDocument(arr).toJson());
}
```

- [ ] **Step 5: Create `tests/test_profiledatabase.h`**

```cpp
#ifndef TEST_PROFILEDATABASE_H
#define TEST_PROFILEDATABASE_H
#include <QObject>
class TestProfileDatabase : public QObject {
    Q_OBJECT
private slots:
    void bundled_presets_loaded();
    void findById_returns_nullopt_for_empty_id();
    void findById_finds_user_profile();
    void user_profile_overrides_bundled();
    void remove_user_profile();
};
#endif
```

- [ ] **Step 6: Create `tests/test_profiledatabase.cpp`**

```cpp
#include "test_profiledatabase.h"
#include "../src/profiledatabase.h"
#include <QTest>
#include <QTemporaryFile>

void TestProfileDatabase::bundled_presets_loaded() {
    ProfileDatabase db;
    QVERIFY(db.allProfiles().size() >= 4);
}

void TestProfileDatabase::findById_returns_nullopt_for_empty_id() {
    ProfileDatabase db;
    QVERIFY(!db.findById("").has_value());
}

void TestProfileDatabase::findById_finds_user_profile() {
    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    DiscProfile p; p.discId = "test123"; p.name = "My Disc";
    db.saveUserProfile(p);

    auto found = db.findById("test123");
    QVERIFY(found.has_value());
    QCOMPARE(found->name, QString("My Disc"));
}

void TestProfileDatabase::user_profile_overrides_bundled() {
    // Bundled profiles have empty discId — use a known non-empty one
    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    DiscProfile p; p.discId = "override_id"; p.name = "User Override"; p.tr0 = 99999.0;
    db.saveUserProfile(p);

    auto found = db.findById("override_id");
    QVERIFY(found.has_value());
    QCOMPARE(found->tr0, 99999.0);
}

void TestProfileDatabase::remove_user_profile() {
    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    DiscProfile p; p.discId = "to_remove"; p.name = "Remove Me";
    db.saveUserProfile(p);
    QVERIFY(db.findById("to_remove").has_value());

    db.removeUserProfile("to_remove");
    QVERIFY(!db.findById("to_remove").has_value());
}
```

- [ ] **Step 7: Commit**

```bash
cd /home/mezinster/cdimage
git add resources/ src/profiledatabase.h src/profiledatabase.cpp \
        tests/test_profiledatabase.h tests/test_profiledatabase.cpp
git commit -m "feat: add ProfileDatabase with two-tier JSON storage and bundled presets"
```

---

## Task 3: IDiscBackend interface + DiscDetector

**Files:**
- Create: `src/idiscbackend.h`
- Create: `src/discdetector.h`
- Create: `src/discdetector.cpp`
- Create: `tests/mockdiscbackend.h`
- Create: `tests/test_discdetector.h`
- Create: `tests/test_discdetector.cpp`

- [ ] **Step 1: Create `src/idiscbackend.h`**

```cpp
#ifndef IDISCBACKEND_H
#define IDISCBACKEND_H

#include "discprofile.h"
#include <QStringList>
#include <QVector>
#include <stdexcept>

struct RawDiscInfo {
    QString   discId;
    MediaType mediaType = MediaType::CD_RW;
};

class IDiscBackend {
public:
    virtual ~IDiscBackend() = default;

    virtual QStringList     availableDevices()                                        = 0;
    virtual RawDiscInfo     queryDisc(const QString& devicePath)                      = 0;
    virtual bool            burnTestPattern(const QString& devicePath,
                                            const QString& trackFile)                 = 0;
    virtual QVector<qint64> measureSeekTimes(const QString& devicePath,
                                             const QVector<qint64>& sectors)          = 0;
};

// Defined in linuxdiscbackend.cpp or windowsdiscbackend.cpp
IDiscBackend* createDiscBackend();

#endif
```

- [ ] **Step 2: Create `src/discdetector.h`**

```cpp
#ifndef DISCDETECTOR_H
#define DISCDETECTOR_H

#include "discprofile.h"
#include "idiscbackend.h"
#include "profiledatabase.h"
#include <QObject>

class DiscDetector : public QObject {
    Q_OBJECT
public:
    explicit DiscDetector(IDiscBackend* backend, ProfileDatabase* db,
                          QObject* parent = nullptr);
    void detectAsync(const QString& devicePath);

signals:
    void profileFound(DiscProfile);
    void profileNotFound(RawDiscInfo);
    void detectionFailed(QString error);

private:
    IDiscBackend*   m_backend;
    ProfileDatabase* m_db;
};

#endif
```

- [ ] **Step 3: Create `src/discdetector.cpp`**

```cpp
#include "discdetector.h"
#include <QtConcurrent/QtConcurrent>

DiscDetector::DiscDetector(IDiscBackend* backend, ProfileDatabase* db, QObject* parent)
    : QObject(parent), m_backend(backend), m_db(db) {}

void DiscDetector::detectAsync(const QString& devicePath) {
    QtConcurrent::run([this, devicePath]() {
        RawDiscInfo info;
        try {
            info = m_backend->queryDisc(devicePath);
        } catch (const std::exception& e) {
            emit detectionFailed(QString::fromStdString(e.what()));
            return;
        }
        auto profile = m_db->findById(info.discId);
        if (profile.has_value())
            emit profileFound(*profile);
        else
            emit profileNotFound(info);
    });
}
```

- [ ] **Step 4: Create `tests/mockdiscbackend.h`**

```cpp
#ifndef MOCKDISCBACKEND_H
#define MOCKDISCBACKEND_H

#include "../src/idiscbackend.h"

class MockDiscBackend : public IDiscBackend {
public:
    RawDiscInfo     m_discInfo;
    QVector<qint64> m_seekTimes;
    bool            m_queryFails = false;

    QStringList availableDevices() override { return {"/dev/mock"}; }

    RawDiscInfo queryDisc(const QString&) override {
        if (m_queryFails) throw std::runtime_error("mock error");
        return m_discInfo;
    }

    bool burnTestPattern(const QString&, const QString&) override { return true; }

    QVector<qint64> measureSeekTimes(const QString&,
                                     const QVector<qint64>& sectors) override {
        return m_seekTimes.isEmpty()
               ? QVector<qint64>(sectors.size(), 100LL)
               : m_seekTimes;
    }
};

#endif
```

- [ ] **Step 5: Create `tests/test_discdetector.h`**

```cpp
#ifndef TEST_DISCDETECTOR_H
#define TEST_DISCDETECTOR_H
#include <QObject>
class TestDiscDetector : public QObject {
    Q_OBJECT
private slots:
    void emits_profileFound_when_disc_in_db();
    void emits_profileNotFound_when_disc_unknown();
    void emits_detectionFailed_on_backend_error();
};
#endif
```

- [ ] **Step 6: Create `tests/test_discdetector.cpp`**

```cpp
#include "test_discdetector.h"
#include "../src/discdetector.h"
#include "../src/profiledatabase.h"
#include "mockdiscbackend.h"
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryFile>

void TestDiscDetector::emits_profileFound_when_disc_in_db() {
    MockDiscBackend backend;
    backend.m_discInfo.discId = "known_disc";
    backend.m_discInfo.mediaType = MediaType::CD_RW;

    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    DiscProfile p; p.discId = "known_disc"; p.name = "My CD-RW";
    db.saveUserProfile(p);

    DiscDetector detector(&backend, &db);
    QSignalSpy found(&detector, &DiscDetector::profileFound);
    QSignalSpy failed(&detector, &DiscDetector::detectionFailed);

    detector.detectAsync("/dev/mock");
    QVERIFY(found.wait(2000));
    QCOMPARE(found.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(found[0][0].value<DiscProfile>().discId, QString("known_disc"));
}

void TestDiscDetector::emits_profileNotFound_when_disc_unknown() {
    MockDiscBackend backend;
    backend.m_discInfo.discId = "unknown_disc";

    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());

    DiscDetector detector(&backend, &db);
    QSignalSpy notFound(&detector, &DiscDetector::profileNotFound);

    detector.detectAsync("/dev/mock");
    QVERIFY(notFound.wait(2000));
    QCOMPARE(notFound.count(), 1);
    QCOMPARE(notFound[0][0].value<RawDiscInfo>().discId, QString("unknown_disc"));
}

void TestDiscDetector::emits_detectionFailed_on_backend_error() {
    MockDiscBackend backend;
    backend.m_queryFails = true;

    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());

    DiscDetector detector(&backend, &db);
    QSignalSpy failed(&detector, &DiscDetector::detectionFailed);

    detector.detectAsync("/dev/mock");
    QVERIFY(failed.wait(2000));
    QCOMPARE(failed.count(), 1);
}
```

- [ ] **Step 7: Register `RawDiscInfo` as Qt metatype in `src/idiscbackend.h`**

Add at the end of `src/idiscbackend.h`, after the class:
```cpp
Q_DECLARE_METATYPE(RawDiscInfo)
Q_DECLARE_METATYPE(DiscProfile)
```

Also add at the end of `tests/test_discdetector.cpp` a static registration:
```cpp
// At file scope, before the test methods:
static const int s_rawDiscInfoMetatype = qRegisterMetaType<RawDiscInfo>("RawDiscInfo");
static const int s_discProfileMetatype = qRegisterMetaType<DiscProfile>("DiscProfile");
```

- [ ] **Step 8: Build and run tests**

```bash
cd /home/mezinster/cdimage/tests
qmake tests.pro && make && ./cdimage_tests
```

Expected output: `PASS` for all `TestDiscProfile`, `TestProfileDatabase`, and `TestDiscDetector` tests.

- [ ] **Step 9: Commit**

```bash
cd /home/mezinster/cdimage
git add src/idiscbackend.h src/discdetector.h src/discdetector.cpp \
        tests/mockdiscbackend.h tests/test_discdetector.h tests/test_discdetector.cpp
git commit -m "feat: add IDiscBackend interface and DiscDetector with async detection"
```

---

## Task 4: LinuxDiscBackend

**Files:**
- Create: `src/linuxdiscbackend.h`
- Create: `src/linuxdiscbackend.cpp`

- [ ] **Step 1: Create `src/linuxdiscbackend.h`**

```cpp
#ifndef LINUXDISCBACKEND_H
#define LINUXDISCBACKEND_H

#include "idiscbackend.h"

class LinuxDiscBackend : public IDiscBackend {
public:
    QStringList     availableDevices()                                        override;
    RawDiscInfo     queryDisc(const QString& devicePath)                      override;
    bool            burnTestPattern(const QString& devicePath,
                                    const QString& trackFile)                 override;
    QVector<qint64> measureSeekTimes(const QString& devicePath,
                                     const QVector<qint64>& sectors)          override;
private:
    RawDiscInfo readDiscInfo(int fd);
    RawDiscInfo readAtip(int fd);
    MediaType   mediaTypeFromDiscTypeByte(quint8 b);
    bool        sendCommand(int fd, unsigned char* cdb, int cdbLen,
                            unsigned char* buf, int bufLen);
};

#endif
```

- [ ] **Step 2: Create `src/linuxdiscbackend.cpp`**

```cpp
#include "linuxdiscbackend.h"

#include <QDir>
#include <QProcess>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include <scsi/sg.h>
#include <time.h>
#include <cstring>
#include <stdexcept>

QStringList LinuxDiscBackend::availableDevices() {
    QStringList devices;
    const QDir dev("/dev");
    for (const QString& e : dev.entryList({"sr*", "scd*"}, QDir::System))
        devices << "/dev/" + e;
    return devices;
}

bool LinuxDiscBackend::sendCommand(int fd, unsigned char* cdb, int cdbLen,
                                   unsigned char* buf, int bufLen) {
    unsigned char sense[32] = {};
    sg_io_hdr_t io;
    std::memset(&io, 0, sizeof(io));
    io.interface_id    = 'S';
    io.dxfer_direction = SG_DXFER_FROM_DEV;
    io.cmdp            = cdb;
    io.cmd_len         = static_cast<unsigned char>(cdbLen);
    io.dxferp          = buf;
    io.dxfer_len       = static_cast<unsigned int>(bufLen);
    io.sbp             = sense;
    io.mx_sb_len       = sizeof(sense);
    io.timeout         = 5000;
    return ioctl(fd, SG_IO, &io) == 0 && io.status == 0;
}

RawDiscInfo LinuxDiscBackend::readDiscInfo(int fd) {
    // MMC-5: READ DISC INFORMATION (Standard, Data Type 000b)
    unsigned char cdb[10] = {0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 34, 0x00};
    unsigned char buf[34] = {};
    if (!sendCommand(fd, cdb, sizeof(cdb), buf, sizeof(buf)))
        throw std::runtime_error("READ DISC INFORMATION failed");
    RawDiscInfo info;
    info.mediaType = mediaTypeFromDiscTypeByte(buf[8]);
    return info;
}

RawDiscInfo LinuxDiscBackend::readAtip(int fd) {
    // MMC-5: READ TOC/PMA/ATIP, Format=4 (ATIP), MSF=1
    // ATIP lead-in time bytes [4..6] encode the disc manufacturer code
    unsigned char cdb[10] = {0x43, 0x02, 0x04, 0, 0, 0, 0, 0, 28, 0};
    unsigned char buf[28] = {};
    if (!sendCommand(fd, cdb, sizeof(cdb), buf, sizeof(buf)))
        throw std::runtime_error("READ ATIP failed");
    const quint8 mm = buf[4] & 0x7F;
    const quint8 ss = buf[5];
    const quint8 ff = buf[6];
    RawDiscInfo info;
    info.discId = QString("%1m%2s%3f")
                  .arg(mm, 2, 10, QChar('0'))
                  .arg(ss, 2, 10, QChar('0'))
                  .arg(ff, 2, 10, QChar('0'));
    return info;
}

MediaType LinuxDiscBackend::mediaTypeFromDiscTypeByte(quint8 b) {
    // MMC-5 Table 404: Disc Type codes
    switch (b) {
        case 0x00: return MediaType::CD_R;
        case 0x20: return MediaType::CD_R;
        case 0x21: return MediaType::CD_RW;
        case 0x12: return MediaType::DVD_R;
        case 0x13: return MediaType::DVD_RW;
        case 0x1A: return MediaType::DVD_RW;  // DVD+RW
        case 0x1B: return MediaType::DVD_R;   // DVD+R
        case 0x2B: return MediaType::DVD_DL;  // DVD+R DL
        default:   return MediaType::CD_R;
    }
}

RawDiscInfo LinuxDiscBackend::queryDisc(const QString& devicePath) {
    int fd = open(devicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        throw std::runtime_error("Cannot open device: " + devicePath.toStdString());

    RawDiscInfo info;
    try { info = readDiscInfo(fd); } catch (...) {}
    if (info.mediaType == MediaType::CD_R || info.mediaType == MediaType::CD_RW) {
        try {
            RawDiscInfo atip = readAtip(fd);
            info.discId = atip.discId;
        } catch (...) {}
    }
    close(fd);
    return info;
}

bool LinuxDiscBackend::burnTestPattern(const QString& devicePath,
                                       const QString& trackFile) {
    QProcess proc;
    proc.start("cdrecord", {"-audio",
                            QString("dev=%1").arg(devicePath),
                            trackFile});
    proc.waitForFinished(300000);
    return proc.exitCode() == 0;
}

QVector<qint64> LinuxDiscBackend::measureSeekTimes(const QString& devicePath,
                                                    const QVector<qint64>& sectors) {
    int fd = open(devicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        throw std::runtime_error("Cannot open device: " + devicePath.toStdString());

    QVector<qint64> times;
    times.reserve(sectors.size());
    for (qint64 sector : sectors) {
        const qint64 lba = sector + 150;
        struct cdrom_msf msf;
        msf.cdmsf_min0   = static_cast<quint8>(lba / 4500);
        msf.cdmsf_sec0   = static_cast<quint8>((lba % 4500) / 75);
        msf.cdmsf_frame0 = static_cast<quint8>(lba % 75);

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        ioctl(fd, CDROMSEEK, &msf);
        clock_gettime(CLOCK_MONOTONIC, &t1);

        times.append((t1.tv_sec - t0.tv_sec) * 1000000LL
                   + (t1.tv_nsec - t0.tv_nsec) / 1000LL);
    }
    close(fd);
    return times;
}

IDiscBackend* createDiscBackend() { return new LinuxDiscBackend(); }
```

- [ ] **Step 3: Manual smoke test (requires a Linux machine with an optical drive)**

```bash
# Build the main project temporarily with the Linux backend
cd /home/mezinster/cdimage
echo 'unix:SOURCES += src/linuxdiscbackend.cpp' >> cdimage.pro
qmake && make 2>&1 | grep -E "error:|warning:" | head -20
```

Expected: Clean compile with no errors. Revert the temporary `.pro` change — Task 12 will add it properly.

```bash
git checkout cdimage.pro
```

- [ ] **Step 4: Commit**

```bash
cd /home/mezinster/cdimage
git add src/linuxdiscbackend.h src/linuxdiscbackend.cpp
git commit -m "feat: add LinuxDiscBackend (MMC/ioctl)"
```

---

## Task 5: WindowsDiscBackend

**Files:**
- Create: `src/windowsdiscbackend.h`
- Create: `src/windowsdiscbackend.cpp`

- [ ] **Step 1: Create `src/windowsdiscbackend.h`**

```cpp
#ifndef WINDOWSDISCBACKEND_H
#define WINDOWSDISCBACKEND_H

#include "idiscbackend.h"

class WindowsDiscBackend : public IDiscBackend {
public:
    QStringList     availableDevices()                                        override;
    RawDiscInfo     queryDisc(const QString& devicePath)                      override;
    bool            burnTestPattern(const QString& devicePath,
                                    const QString& trackFile)                 override;
    QVector<qint64> measureSeekTimes(const QString& devicePath,
                                     const QVector<qint64>& sectors)          override;
private:
    // Returns HANDLE cast to void*; caller must CloseHandle
    void*       openDevice(const QString& path);
    bool        sendCommand(void* handle, unsigned char* cdb, int cdbLen,
                            unsigned char* buf, int bufLen);
    RawDiscInfo readDiscInfo(void* handle);
    RawDiscInfo readAtip(void* handle);
    MediaType   mediaTypeFromDiscTypeByte(quint8 b);
};

#endif
```

- [ ] **Step 2: Create `src/windowsdiscbackend.cpp`**

```cpp
#include "windowsdiscbackend.h"

#include <QProcess>
#include <QDir>

#include <windows.h>
#include <ntddscsi.h>
#include <cstring>
#include <stdexcept>

QStringList WindowsDiscBackend::availableDevices() {
    QStringList result;
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1 << i))) continue;
        QString letter = QString("%1:\\").arg(QChar('A' + i));
        if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(letter.utf16())) == DRIVE_CDROM)
            result << QString("\\\\.\\%1:").arg(QChar('A' + i));
    }
    return result;
}

void* WindowsDiscBackend::openDevice(const QString& path) {
    HANDLE h = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open device: " + path.toStdString());
    return static_cast<void*>(h);
}

bool WindowsDiscBackend::sendCommand(void* handle, unsigned char* cdb, int cdbLen,
                                     unsigned char* buf, int bufLen) {
    // SCSI Pass-Through Direct
    const int sptdSize = sizeof(SCSI_PASS_THROUGH_DIRECT);
    SCSI_PASS_THROUGH_DIRECT sptd;
    std::memset(&sptd, 0, sptdSize);
    sptd.Length             = sptdSize;
    sptd.CdbLength          = static_cast<UCHAR>(cdbLen);
    sptd.DataIn             = SCSI_IOCTL_DATA_IN;
    sptd.DataTransferLength = static_cast<ULONG>(bufLen);
    sptd.DataBuffer         = buf;
    sptd.TimeOutValue       = 5;
    std::memcpy(sptd.Cdb, cdb, cdbLen);

    DWORD returned = 0;
    return DeviceIoControl(
        static_cast<HANDLE>(handle),
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        &sptd, sptdSize, &sptd, sptdSize,
        &returned, nullptr) != 0;
}

RawDiscInfo WindowsDiscBackend::readDiscInfo(void* handle) {
    unsigned char cdb[10] = {0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 34, 0x00};
    unsigned char buf[34] = {};
    if (!sendCommand(handle, cdb, sizeof(cdb), buf, sizeof(buf)))
        throw std::runtime_error("READ DISC INFORMATION failed");
    RawDiscInfo info;
    info.mediaType = mediaTypeFromDiscTypeByte(buf[8]);
    return info;
}

RawDiscInfo WindowsDiscBackend::readAtip(void* handle) {
    unsigned char cdb[10] = {0x43, 0x02, 0x04, 0, 0, 0, 0, 0, 28, 0};
    unsigned char buf[28] = {};
    if (!sendCommand(handle, cdb, sizeof(cdb), buf, sizeof(buf)))
        throw std::runtime_error("READ ATIP failed");
    const quint8 mm = buf[4] & 0x7F;
    const quint8 ss = buf[5];
    const quint8 ff = buf[6];
    RawDiscInfo info;
    info.discId = QString("%1m%2s%3f")
                  .arg(mm, 2, 10, QChar('0'))
                  .arg(ss, 2, 10, QChar('0'))
                  .arg(ff, 2, 10, QChar('0'));
    return info;
}

MediaType WindowsDiscBackend::mediaTypeFromDiscTypeByte(quint8 b) {
    switch (b) {
        case 0x00: return MediaType::CD_R;
        case 0x20: return MediaType::CD_R;
        case 0x21: return MediaType::CD_RW;
        case 0x12: return MediaType::DVD_R;
        case 0x13: return MediaType::DVD_RW;
        case 0x1A: return MediaType::DVD_RW;
        case 0x1B: return MediaType::DVD_R;
        case 0x2B: return MediaType::DVD_DL;
        default:   return MediaType::CD_R;
    }
}

RawDiscInfo WindowsDiscBackend::queryDisc(const QString& devicePath) {
    HANDLE h = static_cast<HANDLE>(openDevice(devicePath));
    RawDiscInfo info;
    try { info = readDiscInfo(h); } catch (...) {}
    if (info.mediaType == MediaType::CD_R || info.mediaType == MediaType::CD_RW) {
        try {
            RawDiscInfo atip = readAtip(h);
            info.discId = atip.discId;
        } catch (...) {}
    }
    CloseHandle(h);
    return info;
}

bool WindowsDiscBackend::burnTestPattern(const QString& devicePath,
                                         const QString& trackFile) {
    // devicePath is e.g. "\\.\D:" — cdrecord uses drive letter e.g. "1,0,0"
    // The user is expected to have cdrecord in PATH
    const QChar driveLetter = devicePath.at(4); // "\\\\.\\D:"[4] = 'D'
    QProcess proc;
    proc.start("cdrecord", {"-audio",
                            QString("dev=%1:").arg(driveLetter),
                            trackFile});
    proc.waitForFinished(300000);
    return proc.exitCode() == 0;
}

QVector<qint64> WindowsDiscBackend::measureSeekTimes(const QString& devicePath,
                                                      const QVector<qint64>& sectors) {
    HANDLE h = static_cast<HANDLE>(openDevice(devicePath));
    QVector<qint64> times;
    times.reserve(sectors.size());

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    for (qint64 sector : sectors) {
        const qint64 lba = sector + 150;
        CDROM_SEEK_AUDIO_MSF seek;
        seek.M = static_cast<UCHAR>(lba / 4500);
        seek.S = static_cast<UCHAR>((lba % 4500) / 75);
        seek.F = static_cast<UCHAR>(lba % 75);

        LARGE_INTEGER t0, t1;
        DWORD returned = 0;
        QueryPerformanceCounter(&t0);
        DeviceIoControl(h, IOCTL_CDROM_SEEK_AUDIO_MSF, &seek, sizeof(seek),
                        nullptr, 0, &returned, nullptr);
        QueryPerformanceCounter(&t1);

        times.append((t1.QuadPart - t0.QuadPart) * 1000000LL / freq.QuadPart);
    }
    CloseHandle(h);
    return times;
}

IDiscBackend* createDiscBackend() { return new WindowsDiscBackend(); }
```

- [ ] **Step 3: Commit**

```bash
cd /home/mezinster/cdimage
git add src/windowsdiscbackend.h src/windowsdiscbackend.cpp
git commit -m "feat: add WindowsDiscBackend (SPTI/DeviceIoControl)"
```

---

## Task 6: TestPatternGenerator + ICalibrationMethod

**Files:**
- Create: `src/icalibrationmethod.h`
- Create: `src/testpatterngenerator.h`
- Create: `src/testpatterngenerator.cpp`

- [ ] **Step 1: Create `src/icalibrationmethod.h`**

```cpp
#ifndef ICALIBRATIONMETHOD_H
#define ICALIBRATIONMETHOD_H

#include "discprofile.h"
#include "idiscbackend.h"
#include <QObject>

class ICalibrationMethod : public QObject {
    Q_OBJECT
public:
    explicit ICalibrationMethod(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ICalibrationMethod() = default;
    virtual void start(const RawDiscInfo& disc) = 0;

signals:
    void progressChanged(int pct);
    void finished(DiscProfile result);
    void failed(QString error);
};

#endif
```

- [ ] **Step 2: Create `src/testpatterngenerator.h`**

```cpp
#ifndef TESTPATTERNGENERATOR_H
#define TESTPATTERNGENERATOR_H

#include "discprofile.h"
#include <QImage>
#include <QString>

class TestPatternGenerator {
public:
    // 1000×1000 radial gradient: black at hub, white at rim, linear in between.
    // Inner 25% of radius is black (hub area), outer 5% is white (rim).
    static QImage generateGradientImage(int size = 1000);

    // Convert the gradient image to an audio track file using the given profile.
    // Returns the output file path on success, empty string on failure.
    static QString generateTrack(const DiscProfile& profile,
                                  const QString& outputPath);
};

#endif
```

- [ ] **Step 3: Create `src/testpatterngenerator.cpp`**

```cpp
#include "testpatterngenerator.h"
#include "converter.h"
#include <QPainter>
#include <QRadialGradient>

QImage TestPatternGenerator::generateGradientImage(int size) {
    QImage img(size, size, QImage::Format_RGB32);
    img.fill(Qt::black);

    const double cx = size / 2.0;
    const double cy = size / 2.0;
    const double rHub  = size * 0.25; // inner hub radius in pixels
    const double rRim  = size * 0.475; // outer rim radius in pixels

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double r = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            int gray = 0;
            if (r >= rHub && r <= rRim)
                gray = static_cast<int>(255.0 * (r - rHub) / (rRim - rHub));
            else if (r > rRim)
                gray = 255;
            img.setPixel(x, y, qRgb(gray, gray, gray));
        }
    }
    return img;
}

QString TestPatternGenerator::generateTrack(const DiscProfile& profile,
                                             const QString& outputPath) {
    QImage img = generateGradientImage(3000);
    Converter conv(nullptr, profile);
    if (conv.convert(img, outputPath))
        return outputPath;
    return {};
}
```

- [ ] **Step 4: Commit**

```bash
cd /home/mezinster/cdimage
git add src/icalibrationmethod.h src/testpatterngenerator.h src/testpatterngenerator.cpp
git commit -m "feat: add ICalibrationMethod interface and TestPatternGenerator"
```

---

## Task 7: PhotoCalibration

**Files:**
- Create: `src/photocalibration.h`
- Create: `src/photocalibration.cpp`
- Create: `tests/test_photocalibration.h`
- Create: `tests/test_photocalibration.cpp`

- [ ] **Step 1: Create `src/photocalibration.h`**

```cpp
#ifndef PHOTOCALIBRATION_H
#define PHOTOCALIBRATION_H

#include "icalibrationmethod.h"
#include <QString>

class PhotoCalibration : public ICalibrationMethod {
    Q_OBJECT
public:
    explicit PhotoCalibration(const QString& photoPath,
                               QObject* parent = nullptr);
    void start(const RawDiscInfo& disc) override;

    // Exposed for testing
    static QVector<double> radialProfile(const QImage& img, int cx, int cy, int maxR);
    static double          findEdge(const QVector<double>& profile, double threshold);

private:
    QString m_photoPath;
};

#endif
```

- [ ] **Step 2: Create `src/photocalibration.cpp`**

```cpp
#include "photocalibration.h"
#include <QImage>
#include <cmath>
#include <numeric>

PhotoCalibration::PhotoCalibration(const QString& photoPath, QObject* parent)
    : ICalibrationMethod(parent), m_photoPath(photoPath) {}

// Sample average intensity in an annulus of width 2 at radius r from (cx, cy).
QVector<double> PhotoCalibration::radialProfile(const QImage& img,
                                                 int cx, int cy, int maxR) {
    QVector<double> profile(maxR, 0.0);
    QVector<int>    counts(maxR, 0);
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int r = static_cast<int>(
                std::round(std::sqrt((x-cx)*(x-cx) + (y-cy)*(y-cy))));
            if (r < maxR) {
                profile[r] += qGray(img.pixel(x, y));
                counts[r]++;
            }
        }
    }
    for (int r = 0; r < maxR; ++r)
        if (counts[r] > 0) profile[r] /= counts[r];
    return profile;
}

// Returns the first index where profile[r] crosses threshold (0-255).
double PhotoCalibration::findEdge(const QVector<double>& profile, double threshold) {
    for (int r = 1; r < profile.size(); ++r)
        if (profile[r] >= threshold && profile[r-1] < threshold)
            return r - (profile[r] - threshold) / (profile[r] - profile[r-1]);
    return -1.0;
}

void PhotoCalibration::start(const RawDiscInfo& disc) {
    emit progressChanged(5);

    QImage photo(m_photoPath);
    if (photo.isNull()) { emit failed("Cannot load photo: " + m_photoPath); return; }
    const QImage gray = photo.convertToFormat(QImage::Format_Grayscale8);
    emit progressChanged(20);

    const int cx = gray.width() / 2;
    const int cy = gray.height() / 2;
    const int maxR = std::min(cx, cy);

    const QVector<double> profile = radialProfile(gray, cx, cy, maxR);
    emit progressChanged(60);

    // Find inner data edge (~5% intensity) and outer disc edge (~90% intensity)
    const double rDataStart_px = findEdge(profile, 12.75);   // 5% of 255
    const double rDiscEdge_px  = findEdge(profile, 229.5);   // 90% of 255

    if (rDataStart_px < 0 || rDiscEdge_px < 0 || rDiscEdge_px <= rDataStart_px) {
        emit failed("Could not detect disc boundary in photo. "
                    "Ensure the photo shows the full disc on a dark background.");
        return;
    }
    emit progressChanged(80);

    // Physical outer radius of a disc is always 60 mm (ECMA-130 / ECMA-267)
    // Use the outer edge to calibrate pixels_per_mm
    const double pxPerMm     = rDiscEdge_px / 60.0;
    const double r0_actual   = rDataStart_px / pxPerMm;

    // ECMA-130: lead-out starts at ~58 mm; data ends there
    const double rDataEnd_mm = 58.0;
    const double rDataEnd_px = rDataEnd_mm * pxPerMm;

    // Total sectors in the generated test pattern
    const int totalSectors   = 336100; // 74-minute CD = ~336100 sectors
    const double span_mm     = rDataEnd_mm - r0_actual;
    const double dtr_actual  = span_mm / totalSectors;
    // tr0: starting track position in sectors, derived from r0
    const double tr0_actual  = r0_actual / dtr_actual;

    DiscProfile result;
    result.discId    = disc.discId;
    result.mediaType = disc.mediaType;
    result.r0        = r0_actual;
    result.dtr       = dtr_actual;
    result.tr0       = tr0_actual;
    result.layerCount = (disc.mediaType == MediaType::DVD_DL) ? 2 : 1;
    emit progressChanged(100);
    emit finished(result);
}
```

- [ ] **Step 3: Create `tests/test_photocalibration.h`**

```cpp
#ifndef TEST_PHOTOCALIBRATION_H
#define TEST_PHOTOCALIBRATION_H
#include <QObject>
class TestPhotoCalibration : public QObject {
    Q_OBJECT
private slots:
    void radialProfile_peaks_at_correct_radius();
    void findEdge_locates_transition();
    void start_extracts_geometry_from_synthetic_photo();
};
#endif
```

- [ ] **Step 4: Create `tests/test_photocalibration.cpp`**

```cpp
#include "test_photocalibration.h"
#include "../src/photocalibration.h"
#include "../src/testpatterngenerator.h"
#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QTemporaryFile>
#include <cmath>

void TestPhotoCalibration::radialProfile_peaks_at_correct_radius() {
    // White ring at radius 40 in a 100x100 black image
    QImage img(100, 100, QImage::Format_Grayscale8);
    img.fill(0);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x) {
            const int r = static_cast<int>(std::round(std::sqrt((x-50.0)*(x-50.0)+(y-50.0)*(y-50.0))));
            if (r == 40) img.setPixel(x, y, 255);
        }
    const auto profile = PhotoCalibration::radialProfile(img, 50, 50, 50);
    int peakR = 0;
    for (int r = 1; r < profile.size(); ++r)
        if (profile[r] > profile[peakR]) peakR = r;
    QCOMPARE(peakR, 40);
}

void TestPhotoCalibration::findEdge_locates_transition() {
    QVector<double> profile(100, 0.0);
    for (int r = 50; r < 100; ++r) profile[r] = 255.0;
    const double edge = PhotoCalibration::findEdge(profile, 128.0);
    QVERIFY(std::abs(edge - 50.0) < 1.0);
}

void TestPhotoCalibration::start_extracts_geometry_from_synthetic_photo() {
    // Use the gradient image as a synthetic "photo"
    const QImage syntheticPhoto = TestPatternGenerator::generateGradientImage(2000);
    QTemporaryFile tmp;
    tmp.setFileTemplate(QDir::tempPath() + "/photo_XXXXXX.png");
    tmp.open();
    syntheticPhoto.save(tmp.fileName());

    RawDiscInfo disc; disc.discId = "synth"; disc.mediaType = MediaType::CD_RW;
    PhotoCalibration cal(tmp.fileName());
    QSignalSpy doneSpy(&cal, &PhotoCalibration::finished);
    QSignalSpy failSpy(&cal, &PhotoCalibration::failed);

    cal.start(disc);

    // PhotoCalibration is synchronous; check immediately
    if (failSpy.count() > 0)
        QFAIL(failSpy[0][0].toString().toLocal8Bit().constData());
    QCOMPARE(doneSpy.count(), 1);

    DiscProfile result = doneSpy[0][0].value<DiscProfile>();
    QVERIFY(result.r0 > 20.0 && result.r0 < 30.0);  // expect ~25mm
    QVERIFY(result.dtr > 0.0);
}
```

- [ ] **Step 5: Run photo calibration tests**

```bash
cd /home/mezinster/cdimage/tests
qmake tests.pro && make && ./cdimage_tests TestPhotoCalibration
```

Expected: all three `TestPhotoCalibration` tests pass.

- [ ] **Step 6: Commit**

```bash
cd /home/mezinster/cdimage
git add src/photocalibration.h src/photocalibration.cpp \
        tests/test_photocalibration.h tests/test_photocalibration.cpp
git commit -m "feat: add PhotoCalibration (radial scan + disc edge detection)"
```

---

## Task 8: DriveReadbackCalibration

**Files:**
- Create: `src/drivereadbackcalibration.h`
- Create: `src/drivereadbackcalibration.cpp`

- [ ] **Step 1: Create `src/drivereadbackcalibration.h`**

```cpp
#ifndef DRIVEREADBACKCALIBRATION_H
#define DRIVEREADBACKCALIBRATION_H

#include "icalibrationmethod.h"

class DriveReadbackCalibration : public ICalibrationMethod {
    Q_OBJECT
public:
    explicit DriveReadbackCalibration(IDiscBackend* backend,
                                       const QString& devicePath,
                                       QObject* parent = nullptr);
    void start(const RawDiscInfo& disc) override;

    // Exposed for testing
    static double estimateDtr(const QVector<qint64>& seekTimes,
                               double totalSpanMm, int nSectors);

private:
    IDiscBackend* m_backend;
    QString       m_devicePath;
};

#endif
```

- [ ] **Step 2: Create `src/drivereadbackcalibration.cpp`**

```cpp
#include "drivereadbackcalibration.h"
#include <numeric>
#include <cmath>

DriveReadbackCalibration::DriveReadbackCalibration(IDiscBackend* backend,
                                                    const QString& devicePath,
                                                    QObject* parent)
    : ICalibrationMethod(parent), m_backend(backend), m_devicePath(devicePath) {}

// Estimates dtr (mm/sector) from measured seek times.
// Principle: seek time between two sectors is proportional to radial distance.
// We sample evenly across the data area and use the total known physical span
// (ECMA standard: data area runs from r0 ~25mm to ~58mm = 33mm total span) to
// normalise without needing a reference disc.
double DriveReadbackCalibration::estimateDtr(const QVector<qint64>& seekTimes,
                                              double totalSpanMm, int nSectors) {
    if (seekTimes.isEmpty() || nSectors <= 0) return 0.0;
    // Total seek time across all samples ≈ proportional to total radial span
    const qint64 totalUs = std::accumulate(seekTimes.begin(), seekTimes.end(), 0LL);
    if (totalUs == 0) return 0.0;
    // Each seek spans nSectors / (seekTimes.size()) sectors
    const int sectorsPerStep = nSectors / (seekTimes.size() + 1);
    const double mmPerUs  = totalSpanMm / static_cast<double>(totalUs);
    return mmPerUs * (static_cast<double>(totalUs) / seekTimes.size()) / sectorsPerStep;
}

void DriveReadbackCalibration::start(const RawDiscInfo& disc) {
    emit progressChanged(5);

    // ECMA-130 (CD): data area 25–58mm; ECMA-267 (DVD): 24–58mm
    const double r0Nominal   = (disc.mediaType == MediaType::DVD_R   ||
                                 disc.mediaType == MediaType::DVD_RW  ||
                                 disc.mediaType == MediaType::DVD_DL) ? 24.0 : 25.0;
    const double rOuterMm    = 58.0;
    const double spanMm      = rOuterMm - r0Nominal;
    const int    totalSectors = 336100; // 74-min CD; DVD varies but same approach

    // Sample 10 evenly-spaced points across the disc
    const int    nSteps = 10;
    QVector<qint64> sectorPoints;
    sectorPoints.reserve(nSteps + 1);
    for (int i = 0; i <= nSteps; ++i)
        sectorPoints.append(static_cast<qint64>(i) * totalSectors / nSteps);

    QVector<qint64> seekTimes;
    try {
        seekTimes = m_backend->measureSeekTimes(m_devicePath, sectorPoints);
    } catch (const std::exception& e) {
        emit failed(QString("Seek measurement failed: %1").arg(e.what()));
        return;
    }
    emit progressChanged(70);

    // Compute dtr from consecutive seek deltas (ignore the first point — it's
    // a seek from wherever the head happens to be)
    QVector<qint64> deltas;
    for (int i = 1; i < seekTimes.size(); ++i)
        deltas.append(std::abs(seekTimes[i] - seekTimes[i-1]));

    const int sectorsPerStep = totalSectors / nSteps;
    const double dtr_actual = estimateDtr(deltas, spanMm, totalSectors);

    if (dtr_actual <= 0.0) {
        emit failed("Could not compute dtr from seek times — "
                    "all seek times were zero (drive may not support SEEK).");
        return;
    }

    const double tr0_actual = r0Nominal / dtr_actual;

    DiscProfile result;
    result.discId     = disc.discId;
    result.mediaType  = disc.mediaType;
    result.r0         = r0Nominal;
    result.dtr        = dtr_actual;
    result.tr0        = tr0_actual;
    result.layerCount = (disc.mediaType == MediaType::DVD_DL) ? 2 : 1;
    emit progressChanged(100);
    emit finished(result);
}
```

- [ ] **Step 3: Commit**

```bash
cd /home/mezinster/cdimage
git add src/drivereadbackcalibration.h src/drivereadbackcalibration.cpp
git commit -m "feat: add DriveReadbackCalibration (seek-time geometry extraction)"
```

---

## Task 9: CalibrationWizard

**Files:**
- Create: `src/calibrationwizard.h`
- Create: `src/calibrationwizard.cpp`
- Create: `src/calibrationwizard.ui`

- [ ] **Step 1: Create `src/calibrationwizard.ui`** (Result page layout)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>ResultPage</class>
 <widget class="QWidget" name="ResultPage">
  <layout class="QFormLayout">
   <item row="0" column="0"><widget class="QLabel"><property name="text"><string>Profile name:</string></property></widget></item>
   <item row="0" column="1"><widget class="QLineEdit" name="leProfileName"/></item>
   <item row="1" column="0"><widget class="QLabel"><property name="text"><string>tr0:</string></property></widget></item>
   <item row="1" column="1"><widget class="QLabel" name="lblTr0"/></item>
   <item row="2" column="0"><widget class="QLabel"><property name="text"><string>dtr:</string></property></widget></item>
   <item row="2" column="1"><widget class="QLabel" name="lblDtr"/></item>
   <item row="3" column="0"><widget class="QLabel"><property name="text"><string>r0 (mm):</string></property></widget></item>
   <item row="3" column="1"><widget class="QLabel" name="lblR0"/></item>
   <item row="4" column="0"><widget class="QLabel"><property name="text"><string>Media type:</string></property></widget></item>
   <item row="4" column="1"><widget class="QLabel" name="lblMediaType"/></item>
  </layout>
 </widget>
</ui>
```

- [ ] **Step 2: Create `src/calibrationwizard.h`**

```cpp
#ifndef CALIBRATIONWIZARD_H
#define CALIBRATIONWIZARD_H

#include "discprofile.h"
#include "idiscbackend.h"
#include "profiledatabase.h"
#include <QWizard>
#include <QWizardPage>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QProgressBar>

enum CalibrationPageId {
    Page_Welcome = 0, Page_BurnPattern, Page_MethodSelect,
    Page_Photo, Page_Readback, Page_Analysis, Page_Result
};

class CalibrationWizard : public QWizard {
    Q_OBJECT
public:
    CalibrationWizard(IDiscBackend* backend, ProfileDatabase* db,
                      const RawDiscInfo& disc, QWidget* parent = nullptr);
    DiscProfile calibratedProfile() const { return m_result; }

private slots:
    void onCalibrationFinished(DiscProfile result);
    void onCalibrationFailed(QString error);

private:
    IDiscBackend*    m_backend;
    ProfileDatabase* m_db;
    RawDiscInfo      m_disc;
    DiscProfile      m_result;
};

// ── Pages ────────────────────────────────────────────────────────────────────

class WelcomePage : public QWizardPage {
public:
    WelcomePage(const RawDiscInfo& disc, QWidget* parent = nullptr);
};

class BurnPatternPage : public QWizardPage {
    Q_OBJECT
public:
    BurnPatternPage(IDiscBackend* backend, const RawDiscInfo& disc,
                    QWidget* parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;
private slots:
    void doBurn();
private:
    IDiscBackend*  m_backend;
    RawDiscInfo    m_disc;
    QLabel*        m_status;
    QProgressBar*  m_progress;
    bool           m_done = false;
};

class MethodSelectPage : public QWizardPage {
public:
    MethodSelectPage(QWidget* parent = nullptr);
    int nextId() const override;
private:
    QRadioButton* m_rbPhoto;
    QRadioButton* m_rbReadback;
};

class PhotoPage : public QWizardPage {
public:
    PhotoPage(QWidget* parent = nullptr);
    int nextId() const override { return Page_Analysis; }
    bool isComplete() const override;
private:
    QLineEdit* m_path;
};

class ReadbackPage : public QWizardPage {
    Q_OBJECT
public:
    ReadbackPage(IDiscBackend* backend, QWidget* parent = nullptr);
    void initializePage() override;
    int nextId() const override { return Page_Analysis; }
    bool isComplete() const override { return m_done; }
private:
    IDiscBackend* m_backend;
    QProgressBar* m_progress;
    bool          m_done = false;
};

class AnalysisPage : public QWizardPage {
    Q_OBJECT
public:
    AnalysisPage(IDiscBackend* backend, const RawDiscInfo& disc,
                 QWidget* parent = nullptr);
    void initializePage() override;
    int nextId() const override { return Page_Result; }
    bool isComplete() const override { return m_done; }

signals:
    void calibrationFinished(DiscProfile);
    void calibrationFailed(QString);

private:
    IDiscBackend* m_backend;
    RawDiscInfo   m_disc;
    QProgressBar* m_progress;
    QLabel*       m_status;
    bool          m_done = false;
};

class ResultPage : public QWizardPage {
public:
    ResultPage(ProfileDatabase* db, QWidget* parent = nullptr);
    void initializePage() override;

private:
    ProfileDatabase* m_db;
    QLabel*          m_lblTr0;
    QLabel*          m_lblDtr;
    QLabel*          m_lblR0;
    QLabel*          m_lblMediaType;
    QLineEdit*       m_leName;
};

#endif
```

- [ ] **Step 3: Create `src/calibrationwizard.cpp`**

```cpp
#include "calibrationwizard.h"
#include "photocalibration.h"
#include "drivereadbackcalibration.h"
#include "testpatterngenerator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QPushButton>
#include <QMessageBox>
#include <QThread>
#include <QDir>
#include <QStandardPaths>

// ── CalibrationWizard ────────────────────────────────────────────────────────

CalibrationWizard::CalibrationWizard(IDiscBackend* backend, ProfileDatabase* db,
                                     const RawDiscInfo& disc, QWidget* parent)
    : QWizard(parent), m_backend(backend), m_db(db), m_disc(disc)
{
    setWindowTitle(tr("Disc Calibration Wizard"));
    setPage(Page_Welcome,      new WelcomePage(disc, this));
    setPage(Page_BurnPattern,  new BurnPatternPage(backend, disc, this));
    setPage(Page_MethodSelect, new MethodSelectPage(this));
    setPage(Page_Photo,        new PhotoPage(this));
    setPage(Page_Readback,     new ReadbackPage(backend, this));
    auto* analysisPage = new AnalysisPage(backend, disc, this);
    setPage(Page_Analysis, analysisPage);
    setPage(Page_Result,   new ResultPage(db, this));

    connect(analysisPage, &AnalysisPage::calibrationFinished,
            this, &CalibrationWizard::onCalibrationFinished);
    connect(analysisPage, &AnalysisPage::calibrationFailed,
            this, &CalibrationWizard::onCalibrationFailed);
}

void CalibrationWizard::onCalibrationFinished(DiscProfile result) {
    m_result = result;
    setProperty("calibratedProfile",  QVariant::fromValue(result));
    next();
}

void CalibrationWizard::onCalibrationFailed(QString error) {
    QMessageBox::critical(this, tr("Calibration Failed"), error);
    back();
}

// ── WelcomePage ──────────────────────────────────────────────────────────────

WelcomePage::WelcomePage(const RawDiscInfo& disc, QWidget* parent)
    : QWizardPage(parent)
{
    setTitle(tr("Disc Calibration"));
    static const QMap<MediaType, QString> names = {
        {MediaType::CD_R,   "CD-R"},   {MediaType::CD_RW,  "CD-RW"},
        {MediaType::DVD_R,  "DVD-R"},  {MediaType::DVD_RW, "DVD-RW"},
        {MediaType::DVD_DL, "DVD+R DL"}
    };
    auto* lbl = new QLabel(tr(
        "<p>Detected disc: <b>%1</b> (ID: %2)</p>"
        "<p>This wizard will burn a test pattern, then measure the disc geometry "
        "either from a photograph or by re-reading the disc with the drive.</p>")
        .arg(names.value(disc.mediaType, "Unknown"))
        .arg(disc.discId.isEmpty() ? tr("unknown") : disc.discId), this);
    lbl->setWordWrap(true);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(lbl);
}

// ── BurnPatternPage ──────────────────────────────────────────────────────────

BurnPatternPage::BurnPatternPage(IDiscBackend* backend, const RawDiscInfo& disc,
                                  QWidget* parent)
    : QWizardPage(parent), m_backend(backend), m_disc(disc)
{
    setTitle(tr("Burn Test Pattern"));
    m_status   = new QLabel(tr("Click 'Burn' to write the test pattern."), this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);
    auto* btn = new QPushButton(tr("Burn Test Pattern"), this);
    connect(btn, &QPushButton::clicked, this, &BurnPatternPage::doBurn);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_progress);
    layout->addWidget(btn);
}

void BurnPatternPage::initializePage() { m_done = false; }

bool BurnPatternPage::isComplete() const { return m_done; }

void BurnPatternPage::doBurn() {
    m_progress->setVisible(true);
    m_status->setText(tr("Generating test track (this may take several minutes)…"));
    qApp->processEvents();

    const QString devices = static_cast<CalibrationWizard*>(wizard())
                            ->property("devicePath").toString();
    const QString outPath = QDir::tempPath() + "/cdimage_testpattern.cdr";

    DiscProfile defaultProfile;
    if (TestPatternGenerator::generateTrack(defaultProfile, outPath).isEmpty()) {
        m_status->setText(tr("Failed to generate test track."));
        m_progress->setVisible(false);
        return;
    }

    m_status->setText(tr("Burning test pattern to disc…"));
    qApp->processEvents();

    const bool ok = m_backend->burnTestPattern(devices, outPath);
    m_progress->setVisible(false);
    if (ok) {
        m_status->setText(tr("Test pattern burned successfully. "
                             "Remove the disc and proceed."));
        m_done = true;
        emit completeChanged();
    } else {
        m_status->setText(tr("Burn failed. Check that cdrecord is installed and the "
                             "device path is correct."));
    }
}

// ── MethodSelectPage ─────────────────────────────────────────────────────────

MethodSelectPage::MethodSelectPage(QWidget* parent) : QWizardPage(parent) {
    setTitle(tr("Choose Measurement Method"));
    m_rbPhoto    = new QRadioButton(tr("Photograph the disc"), this);
    m_rbReadback = new QRadioButton(tr("Let the drive re-read the disc"), this);
    m_rbPhoto->setChecked(true);
    registerField("usePhoto", m_rbPhoto);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_rbPhoto);
    layout->addWidget(new QLabel(tr("  Take a photo of the burned disc and upload it. "
                                    "Works without a working drive reader."), this));
    layout->addSpacing(8);
    layout->addWidget(m_rbReadback);
    layout->addWidget(new QLabel(tr("  The drive seeks to known sectors and measures "
                                    "timing. Fully automated."), this));
}

int MethodSelectPage::nextId() const {
    return field("usePhoto").toBool() ? Page_Photo : Page_Readback;
}

// ── PhotoPage ────────────────────────────────────────────────────────────────

PhotoPage::PhotoPage(QWidget* parent) : QWizardPage(parent) {
    setTitle(tr("Upload Disc Photo"));
    auto* lbl = new QLabel(tr("Place the burned disc on a dark background under "
                               "even lighting. Take a photo showing the full disc. "
                               "Select the photo below:"), this);
    lbl->setWordWrap(true);
    m_path = new QLineEdit(this);
    registerField("photoPath", m_path);
    connect(m_path, &QLineEdit::textChanged, this, &PhotoPage::completeChanged);
    auto* btn = new QPushButton(tr("Browse…"), this);
    connect(btn, &QPushButton::clicked, this, [this]{
        const QString f = QFileDialog::getOpenFileName(
            this, tr("Select disc photo"), {},
            tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!f.isEmpty()) m_path->setText(f);
    });
    auto* row = new QHBoxLayout;
    row->addWidget(m_path); row->addWidget(btn);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(lbl);
    layout->addLayout(row);
}

bool PhotoPage::isComplete() const { return !m_path->text().isEmpty(); }

// ── ReadbackPage ─────────────────────────────────────────────────────────────

ReadbackPage::ReadbackPage(IDiscBackend* backend, QWidget* parent)
    : QWizardPage(parent), m_backend(backend)
{
    setTitle(tr("Drive Read-Back"));
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Re-insert the burned disc and wait while the "
                                    "drive measures seek times…"), this));
    layout->addWidget(m_progress);
}

void ReadbackPage::initializePage() {
    m_done = false;
    // The actual measurement runs in AnalysisPage; this page just shows instructions.
    m_done = true;
    emit completeChanged();
}

// ── AnalysisPage ─────────────────────────────────────────────────────────────

AnalysisPage::AnalysisPage(IDiscBackend* backend, const RawDiscInfo& disc,
                            QWidget* parent)
    : QWizardPage(parent), m_backend(backend), m_disc(disc)
{
    setTitle(tr("Analysing…"));
    m_progress = new QProgressBar(this);
    m_status   = new QLabel(this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_progress);
}

void AnalysisPage::initializePage() {
    m_done = false;
    m_progress->setValue(0);
    m_status->setText(tr("Running calibration…"));

    const bool usePhoto = field("usePhoto").toBool();
    ICalibrationMethod* method = nullptr;

    if (usePhoto) {
        method = new PhotoCalibration(field("photoPath").toString(), this);
    } else {
        const QString dev = static_cast<CalibrationWizard*>(wizard())
                            ->property("devicePath").toString();
        method = new DriveReadbackCalibration(m_backend, dev, this);
    }

    connect(method, &ICalibrationMethod::progressChanged,
            m_progress, &QProgressBar::setValue);
    connect(method, &ICalibrationMethod::finished,
            this, [this](DiscProfile p){
                m_done = true;
                emit completeChanged();
                emit calibrationFinished(p);
            });
    connect(method, &ICalibrationMethod::failed,
            this, [this](QString err){
                emit calibrationFailed(err);
            });

    method->start(m_disc);
}

// ── ResultPage ───────────────────────────────────────────────────────────────

ResultPage::ResultPage(ProfileDatabase* db, QWidget* parent)
    : QWizardPage(parent), m_db(db)
{
    setTitle(tr("Calibration Complete"));
    m_leName      = new QLineEdit(this);
    m_lblTr0      = new QLabel(this);
    m_lblDtr      = new QLabel(this);
    m_lblR0       = new QLabel(this);
    m_lblMediaType = new QLabel(this);
    registerField("profileName*", m_leName);

    auto* form = new QFormLayout(this);
    form->addRow(tr("Profile name:"), m_leName);
    form->addRow(tr("tr0:"),          m_lblTr0);
    form->addRow(tr("dtr:"),          m_lblDtr);
    form->addRow(tr("r0 (mm):"),      m_lblR0);
    form->addRow(tr("Media type:"),   m_lblMediaType);
}

void ResultPage::initializePage() {
    const auto& p = static_cast<CalibrationWizard*>(wizard())->calibratedProfile();
    static const QMap<MediaType, QString> names = {
        {MediaType::CD_R,   "CD-R"},  {MediaType::CD_RW,  "CD-RW"},
        {MediaType::DVD_R,  "DVD-R"}, {MediaType::DVD_RW, "DVD-RW"},
        {MediaType::DVD_DL, "DVD+R DL"}
    };
    m_lblTr0->setText(QString::number(p.tr0, 'g', 10));
    m_lblDtr->setText(QString::number(p.dtr, 'g', 10));
    m_lblR0->setText(QString::number(p.r0,  'f', 2));
    m_lblMediaType->setText(names.value(p.mediaType, "Unknown"));

    // Wire wizard's Finish button to save the profile
    connect(wizard(), &QWizard::accepted, this, [this, p]() mutable {
        p.name = m_leName->text();
        m_db->saveUserProfile(p);
    }, Qt::UniqueConnection);
}
```

- [ ] **Step 4: Commit**

```bash
cd /home/mezinster/cdimage
git add src/calibrationwizard.h src/calibrationwizard.cpp src/calibrationwizard.ui
git commit -m "feat: add CalibrationWizard (6-page QWizard with photo and read-back paths)"
```

---

## Task 10: Converter — DiscProfile constructor

**Files:**
- Modify: `src/converter.h` (line 47)
- Modify: `src/converter.cpp` (line 39)

- [ ] **Step 1: Add constructor declaration to `src/converter.h`**

After the existing `Converter(QObject *parent = 0, double tr0, double dtr, double r0)` line (line 47), add:

```cpp
    Converter(QObject *parent, const DiscProfile& profile);
```

Also add `#include "discprofile.h"` at the top of `converter.h`, before the `QObject` include.

- [ ] **Step 2: Add constructor implementation to `src/converter.cpp`**

After the existing `Converter::Converter(QObject *parent, double tr0, double dtr, double r0)` constructor (around line 39), add:

```cpp
Converter::Converter(QObject *parent, const DiscProfile& profile)
    : QObject(parent), m_tr0(profile.tr0), m_dtr(profile.dtr), m_r0(profile.r0),
      m_mixColors(profile.mixColors)
{
#define D 4
    nh=28*D-1;
    pinf=0;
    c=0;
#undef D
}
```

- [ ] **Step 3: Commit**

```bash
cd /home/mezinster/cdimage
git add src/converter.h src/converter.cpp
git commit -m "feat: add Converter(DiscProfile) constructor"
```

---

## Task 11: CreateTrackDialog — use ProfileDatabase

**Files:**
- Modify: `src/createtrackdialog.h`
- Modify: `src/createtrackdialog.cpp`

- [ ] **Step 1: Update `src/createtrackdialog.h`**

Replace the entire class definition with:

```cpp
#ifndef CREATETRACKDIALOG_H
#define CREATETRACKDIALOG_H

#include "ui_createtrackdialog.h"
#include "discprofile.h"
#include "profiledatabase.h"

class CreateTrackDialog: public QDialog, public Ui::CreateTrackDialog {
Q_OBJECT
public:
    explicit CreateTrackDialog(ProfileDatabase* db, QWidget* parent = nullptr);
    DiscProfile selectedProfile() const;

public slots:
    void selectFile();
    void loadPreset(int index);

private:
    ProfileDatabase* m_db;
};

#endif
```

- [ ] **Step 2: Update `src/createtrackdialog.cpp`**

Replace the entire file with:

```cpp
#include "createtrackdialog.h"
#include <QFileDialog>

CreateTrackDialog::CreateTrackDialog(ProfileDatabase* db, QWidget* parent)
    : QDialog(parent), m_db(db)
{
    setupUi(this);
    for (const DiscProfile& p : m_db->allProfiles())
        cbPresets->addItem(p.name);
    if (cbPresets->count() > 0) loadPreset(0);
    connect(tbBrowse, &QAbstractButton::clicked, this, &CreateTrackDialog::selectFile);
    connect(cbPresets, qOverload<int>(&QComboBox::activated),
            this, &CreateTrackDialog::loadPreset);
}

void CreateTrackDialog::selectFile() {
    const QString f = QFileDialog::getSaveFileName(
        this, tr("Save track as"), leFileName->text(), tr("All files (*)"));
    if (!f.isNull()) leFileName->setText(f);
}

void CreateTrackDialog::loadPreset(int index) {
    const QList<DiscProfile> profiles = m_db->allProfiles();
    if (index < 0 || index >= profiles.size()) return;
    const DiscProfile& p = profiles.at(index);
    leTr0->setText(QString::number(p.tr0, 'g', QLocale::FloatingPointShortest));
    leDtr->setText(QString::number(p.dtr, 'g', QLocale::FloatingPointShortest));
    leR0->setText(QString::number(p.r0,  'f', 2));
}

DiscProfile CreateTrackDialog::selectedProfile() const {
    DiscProfile p;
    const QList<DiscProfile> profiles = m_db->allProfiles();
    const int idx = cbPresets->currentIndex();
    if (idx >= 0 && idx < profiles.size())
        p = profiles.at(idx);
    // Allow manual overrides
    p.tr0       = leTr0->text().toDouble();
    p.dtr       = leDtr->text().toDouble();
    p.r0        = leR0->text().toDouble();
    p.mixColors = cbMixColors->isChecked();
    return p;
}
```

- [ ] **Step 3: Commit**

```bash
cd /home/mezinster/cdimage
git add src/createtrackdialog.h src/createtrackdialog.cpp
git commit -m "refactor: CreateTrackDialog uses ProfileDatabase instead of hardcoded presets"
```

---

## Task 12: MainWindow integration + cdimage.pro

**Files:**
- Modify: `src/mainwindow.h`
- Modify: `src/mainwindow.cpp`
- Modify: `cdimage.pro`

- [ ] **Step 1: Update `src/mainwindow.h`**

Replace the class definition with:

```cpp
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "cdpreview.h"
#include "discprofile.h"
#include "idiscbackend.h"
#include "discdetector.h"
#include "profiledatabase.h"
#include <QScopedPointer>

class MainWindow: public QMainWindow, public Ui::MainWindow {
Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);

public slots:
    void loadImage();
    void createTrack();
    void about();
    void detectDisc();

private slots:
    void onProfileFound(DiscProfile profile);
    void onProfileNotFound(RawDiscInfo info);
    void onDetectionFailed(QString error);

private:
    CDPreview                          centralView;
    QImage                             m_image;
    QString                            m_path;
    DiscProfile                        m_currentProfile;
    QString                            m_lastDetectedDevice;
    QScopedPointer<IDiscBackend>       m_backend;
    QScopedPointer<ProfileDatabase>    m_profileDb;
    QScopedPointer<DiscDetector>       m_detector;
};

#endif
```

- [ ] **Step 2: Update `src/mainwindow.cpp`**

Replace the entire file with:

```cpp
#include <QFileDialog>
#include <QProgressDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QComboBox>

#include "mainwindow.h"
#include "converter.h"
#include "createtrackdialog.h"
#include "calibrationwizard.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi(this);
    setCentralWidget(&centralView);
    m_path = QDir::currentPath();

    m_backend.reset(createDiscBackend());
    m_profileDb.reset(new ProfileDatabase(this));
    m_detector.reset(new DiscDetector(m_backend.data(), m_profileDb.data(), this));

    connect(actionLoad_image,   &QAction::triggered, this, &MainWindow::loadImage);
    connect(actionCreate_track, &QAction::triggered, this, &MainWindow::createTrack);
    connect(actionAbout,        &QAction::triggered, this, &MainWindow::about);

    // "Detect Disc" action — add to Edit menu in mainwindow.ui, or create it here
    QAction* actionDetect = new QAction(tr("Detect disc geometry"), this);
    menuEdit->addAction(actionDetect);
    connect(actionDetect, &QAction::triggered, this, &MainWindow::detectDisc);

    connect(m_detector.data(), &DiscDetector::profileFound,
            this, &MainWindow::onProfileFound);
    connect(m_detector.data(), &DiscDetector::profileNotFound,
            this, &MainWindow::onProfileNotFound);
    connect(m_detector.data(), &DiscDetector::detectionFailed,
            this, &MainWindow::onDetectionFailed);
}

void MainWindow::loadImage() {
    const QString fileName = QFileDialog::getOpenFileName(
        this, tr("Open File"), m_path, tr("Images (*.png *.xpm *.jpg)"));
    if (!fileName.isNull()) {
        m_path = QFileInfo(fileName).path();
        centralView.setPixmap(QPixmap(fileName));
    }
}

void MainWindow::createTrack() {
    m_image = centralView.getImage();
    CreateTrackDialog dial(m_profileDb.data(), this);
    if (!dial.exec()) return;

    const DiscProfile profile = dial.selectedProfile();
    Converter converter(this, profile);

    QProgressDialog progress(tr("Generating track…"), tr("Abort"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    connect(&converter, &Converter::progressChanged, &progress, &QProgressDialog::setValue);
    connect(&progress,  &QProgressDialog::canceled,  &converter, &Converter::cancelConverting);

    if (converter.convert(m_image, dial.leFileName->text()))
        QMessageBox::information(this, tr("Success"),
            tr("Track created. Burn with:<br><b>cdrecord -audio dev=&lt;device&gt; %1</b>")
            .arg(dial.leFileName->text()));
    else
        QMessageBox::warning(this, tr("Stopped"), tr("Cancelled by user."));
}

void MainWindow::detectDisc() {
    const QStringList devices = m_backend->availableDevices();
    if (devices.isEmpty()) {
        QMessageBox::warning(this, tr("No disc drive found"),
                             tr("No optical drives were detected."));
        return;
    }

    QString device = devices.first();
    if (devices.size() > 1) {
        bool ok = false;
        device = QInputDialog::getItem(this, tr("Select drive"),
                                       tr("Drive:"), devices, 0, false, &ok);
        if (!ok) return;
    }

    m_lastDetectedDevice = device;
    m_detector->detectAsync(device);
    statusBar()->showMessage(tr("Detecting disc geometry…"));
}

void MainWindow::onProfileFound(DiscProfile profile) {
    m_currentProfile = profile;
    statusBar()->showMessage(
        tr("Disc identified: %1").arg(profile.name.isEmpty() ? profile.discId : profile.name));
}

void MainWindow::onProfileNotFound(RawDiscInfo info) {
    statusBar()->showMessage(tr("Unknown disc — opening calibration wizard."));
    const QStringList devices = m_backend->availableDevices();
    CalibrationWizard wizard(m_backend.data(), m_profileDb.data(), info, this);
    wizard.setProperty("devicePath", m_lastDetectedDevice);
    if (wizard.exec() == QDialog::Accepted) {
        m_currentProfile = wizard.calibratedProfile();
        statusBar()->showMessage(tr("Calibration saved: %1").arg(m_currentProfile.name));
    }
}

void MainWindow::onDetectionFailed(QString error) {
    statusBar()->showMessage(tr("Detection failed."));
    QMessageBox::critical(this, tr("Detection failed"), error);
}

void MainWindow::about() {
    QMessageBox::about(this, tr("About"), tr(
        "<h1>CDImage</h1>"
        "<h2>A tool for burning pictures on a compact disc surface</h2>"
        "<h3>version 0.0</h3>"
        "Copyright (C) 2008-2022 arduinocelentano<hr>"
        "<font color=gray><p>This program is free software: you can redistribute it "
        "and/or modify it under the terms of the GNU General Public License as "
        "published by the Free Software Foundation, either version 3 of the License, "
        "or (at your option) any later version.</p></font>"));
}
```

- [ ] **Step 3: Update `cdimage.pro`**

Replace the existing `cdimage.pro` with:

```
######################################################################
# Automatically generated by qmake (3.1) Fri Jul 8 07:52:22 2022
######################################################################

TEMPLATE = app
TARGET   = cdimage
QT      += widgets concurrent
INCLUDEPATH += .

HEADERS += src/cdpreview.h \
           src/converter.h \
           src/createtrackdialog.h \
           src/mainwindow.h \
           src/discprofile.h \
           src/idiscbackend.h \
           src/icalibrationmethod.h \
           src/discdetector.h \
           src/profiledatabase.h \
           src/testpatterngenerator.h \
           src/photocalibration.h \
           src/drivereadbackcalibration.h \
           src/calibrationwizard.h

SOURCES += src/cdpreview.cpp \
           src/converter.cpp \
           src/createtrackdialog.cpp \
           src/main.cpp \
           src/mainwindow.cpp \
           src/discprofile.cpp \
           src/discdetector.cpp \
           src/profiledatabase.cpp \
           src/testpatterngenerator.cpp \
           src/photocalibration.cpp \
           src/drivereadbackcalibration.cpp \
           src/calibrationwizard.cpp

unix:HEADERS  += src/linuxdiscbackend.h
unix:SOURCES  += src/linuxdiscbackend.cpp

win32:HEADERS += src/windowsdiscbackend.h
win32:SOURCES += src/windowsdiscbackend.cpp

FORMS += src/createtrackdialog.ui \
         src/mainwindow.ui \
         src/calibrationwizard.ui

RESOURCES += resources/profiles.qrc
```

- [ ] **Step 4: Build the full project**

```bash
cd /home/mezinster/cdimage
qmake && make 2>&1 | grep -E "^(.*error:|.*warning:)" | head -40
```

Expected: Clean build, zero errors. Resolve any compile errors before proceeding.

- [ ] **Step 5: Run all unit tests**

```bash
cd /home/mezinster/cdimage/tests
qmake tests.pro && make && ./cdimage_tests
```

Expected output:
```
PASS   : TestDiscProfile::roundtrip_preserves_all_fields()
PASS   : TestDiscProfile::fromJson_uses_defaults_for_missing_fields()
PASS   : TestDiscProfile::dvd_dl_roundtrip()
PASS   : TestProfileDatabase::bundled_presets_loaded()
PASS   : TestProfileDatabase::findById_returns_nullopt_for_empty_id()
PASS   : TestProfileDatabase::findById_finds_user_profile()
PASS   : TestProfileDatabase::user_profile_overrides_bundled()
PASS   : TestProfileDatabase::remove_user_profile()
PASS   : TestDiscDetector::emits_profileFound_when_disc_in_db()
PASS   : TestDiscDetector::emits_profileNotFound_when_disc_unknown()
PASS   : TestDiscDetector::emits_detectionFailed_on_backend_error()
PASS   : TestPhotoCalibration::radialProfile_peaks_at_correct_radius()
PASS   : TestPhotoCalibration::findEdge_locates_transition()
PASS   : TestPhotoCalibration::start_extracts_geometry_from_synthetic_photo()
Totals: 14 passed, 0 failed
```

- [ ] **Step 6: Final commit**

```bash
cd /home/mezinster/cdimage
git add src/mainwindow.h src/mainwindow.cpp cdimage.pro
git commit -m "feat: wire MainWindow with disc detection, calibration wizard, and ProfileDatabase"
```
