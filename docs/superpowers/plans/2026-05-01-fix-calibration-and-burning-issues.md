# Fix Calibration Save, Burn Detection, Profile Selection, and Track Format Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix four user-visible bugs that prevent end-to-end use of CDImage on Windows + Nero: (1) calibration profiles can't be saved, (2) test pattern burn silently fails when `cdrecord` is missing, (3) saved/current profile not selectable in Create-Track dialog, (4) generated track lacks a header so Nero/ImgBurn rejects it.

**Architecture:** Five small, independent slices. Phase 1 fixes the foundation (Qt app identity + persist error reporting). Phase 2 fixes the burn-success false positive at the backend layer. Phase 3 wires those into the wizard so save actually works and the user gets feedback. Phase 4 cleans up the Create-Track dialog so user profiles are visible and pre-selected. Phase 5 wraps the raw audio data in a WAV header so any audio-CD burner accepts it.

**Tech Stack:** Qt 6 (widgets, concurrent), C++17, qmake build, QTest unit tests.

---

## Background — What Each Bug Actually Is

Read this section before starting. It tells you *why* each step exists.

### Bug 1 — Profile name input but save does nothing

`src/calibrationwizard.cpp:245-248` registers a save lambda on `QWizard::accepted` using `Qt::UniqueConnection`. Two real problems:

1. `QApplication` has no organisation/application name set in `src/main.cpp`. `QStandardPaths::writableLocation(AppDataLocation)` therefore returns a path keyed off the binary name, which is non-obvious and on some platforms not writable from the binary's working directory. On Windows the directory may be created somewhere unexpected and the user sees no profile in the dropdown later.
2. `ProfileDatabase::persist()` (`src/profiledatabase.cpp:65-73`) silently `return`s when `f.open(WriteOnly)` fails. No signal, no log, no dialog — the user sees no error and assumes save worked.
3. `Qt::UniqueConnection` does not deduplicate lambda connections (Qt only deduplicates by named slot). Going Back→Next on the result page stacks duplicate save handlers.
4. When ATIP read fails, `disc.discId` is empty, so `findById` (which short-circuits on empty) never matches and the profile won't auto-load next time even if persisted.

### Bug 2 — Test pattern never written to disc

`LinuxDiscBackend::burnTestPattern` (`src/linuxdiscbackend.cpp:102-110`) and `WindowsDiscBackend::burnTestPattern` (`src/windowsdiscbackend.cpp:108-118`) both do:

```cpp
QProcess proc;
proc.start("cdrecord", {...});
proc.waitForFinished(300000);
return proc.exitCode() == 0;
```

If `cdrecord` is not installed (overwhelmingly likely on Windows), `proc.start()` fails, `waitForFinished` returns immediately, and `proc.exitCode()` returns 0 by default. The function reports SUCCESS while having written nothing. The wizard then advances and the calibration math runs against random pixels in the user's photo (or unmoved drive timing data).

### Bug 3 — Local-library profile not selectable in Create Track

`CreateTrackDialog` (`src/createtrackdialog.cpp`) populates `cbPresets` from `m_db->allProfiles()`. Three issues:

1. If Bug 1 prevented persist, no user profiles exist and only bundled ones appear.
2. `m_currentProfile` (set by `MainWindow::onProfileFound`/wizard accept) is never passed to the dialog, so the just-calibrated profile isn't pre-selected.
3. `loadPreset` calls `QString::number(p.tr0, 'g', QLocale::FloatingPointShortest)` — the third arg is an `int` precision but `FloatingPointShortest` is the enum value -128, which is silently treated as default precision (6 digits). This loses precision relative to the wizard's `'g', 10`, causing the values shown in the dialog to differ from the calibrated values.

### Bug 4 — Nero rejects the generated track

`Converter::convert` (`src/converter.cpp:65-134`) writes ~800 MB of raw audio CD sector bytes (2352 bytes × N sectors). That is exactly the format `cdrecord -audio` ingests, but Nero/ImgBurn/Windows Media Player all require a proper PCM WAV file. The byte content is already correct (16-bit signed LE stereo @ 44.1 kHz interleaved); it just needs a 44-byte RIFF/WAVE header.

---

## File Structure

| File | Responsibility | Action |
|------|---------------|--------|
| `src/main.cpp` | App entry point | Modify: add organisation/application name |
| `src/profiledatabase.h/.cpp` | Profile JSON store | Modify: bool return from persist + saveUserProfile, error signal, separate user/bundled accessors |
| `src/idiscbackend.h` | Backend interface | Modify: change `burnTestPattern` return to a `BurnResult` struct |
| `src/linuxdiscbackend.h/.cpp` | Linux burn impl | Modify: detect process-start failure, capture stderr |
| `src/windowsdiscbackend.h/.cpp` | Windows burn impl | Modify: same as Linux |
| `src/calibrationwizard.h/.cpp` | Wizard pages | Modify: explicit save method, success/failure dialog, generate fallback discId, surface burn errors |
| `src/createtrackdialog.h/.cpp` | Track dialog | Modify: accept current profile, pre-select, fix precision, group user vs bundled |
| `src/createtrackdialog.ui` | Dialog UI | (no change needed) |
| `src/converter.h/.cpp` | Audio track writer | Modify: write WAV header, update size fields after data |
| `src/mainwindow.cpp` | Main window glue | Modify: pass `m_currentProfile` into dialog, update success message |
| `tests/test_profiledatabase.cpp` | Profile DB tests | Modify: cover new APIs |
| `tests/mockdiscbackend.h` | Test mock | Modify: match new `BurnResult` return type |
| `tests/test_burnresult.cpp` | New test file | Create: verify backend returns false when cdrecord missing |
| `tests/test_burnresult.h` | New test header | Create |
| `tests/test_converter_wav.cpp` | New test file | Create: verify WAV header bytes |
| `tests/test_converter_wav.h` | New test header | Create |
| `tests/main.cpp` | Test runner | Modify: register new test classes |
| `tests/tests.pro` | Test build | Modify: add new sources |

---

## Phase 1 — Foundation: App identity + persist error reporting

### Task 1: Set Qt application identity

**Files:**
- Modify: `src/main.cpp:20-26`

- [ ] **Step 1.1: Edit main.cpp**

Replace the body of `main` so the app has a stable identity. This makes `QStandardPaths::AppDataLocation` resolve to a predictable, user-writable path on every platform.

```cpp
int main ( int argc, char** argv )
{
    QApplication app ( argc, argv );
    QCoreApplication::setOrganizationName("CDImage");
    QCoreApplication::setOrganizationDomain("cdimage.local");
    QCoreApplication::setApplicationName("CDImage");
    MainWindow win;
    win.show();
    return app.exec();
}
```

- [ ] **Step 1.2: Build and verify it still launches**

Run: `qmake && make`
Expected: clean build, no warnings about main.cpp.

- [ ] **Step 1.3: Commit**

```bash
git add src/main.cpp
git commit -m "fix: set Qt application identity so AppDataLocation resolves correctly"
```

---

### Task 2: Make ProfileDatabase report save failures

**Files:**
- Modify: `src/profiledatabase.h`
- Modify: `src/profiledatabase.cpp`
- Modify: `tests/test_profiledatabase.h`
- Modify: `tests/test_profiledatabase.cpp`

- [ ] **Step 2.1: Write the failing test for bool return + signal**

Edit `tests/test_profiledatabase.h` — add two new private slots:

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
    void save_returns_true_on_success();
    void save_returns_false_when_path_unwritable();
    void user_and_bundled_profiles_are_separable();
};
#endif
```

Edit `tests/test_profiledatabase.cpp` — append three tests at the end of the file:

```cpp
#include <QSignalSpy>

void TestProfileDatabase::save_returns_true_on_success() {
    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    DiscProfile p; p.discId = "ok"; p.name = "OK";
    QVERIFY(db.saveUserProfile(p));
}

void TestProfileDatabase::save_returns_false_when_path_unwritable() {
    // Path inside a non-existent, non-creatable parent (root-owned on Linux).
    ProfileDatabase db("/proc/cdimage_should_not_exist/profiles.json");
    QSignalSpy spy(&db, &ProfileDatabase::saveFailed);
    DiscProfile p; p.discId = "x"; p.name = "X";
    QVERIFY(!db.saveUserProfile(p));
    QCOMPARE(spy.count(), 1);
}

void TestProfileDatabase::user_and_bundled_profiles_are_separable() {
    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    const int bundledCount = db.bundledProfiles().size();
    QVERIFY(bundledCount >= 4);
    QVERIFY(db.userProfiles().isEmpty());

    DiscProfile p; p.discId = "u1"; p.name = "User1";
    db.saveUserProfile(p);
    QCOMPARE(db.userProfiles().size(), 1);
    QCOMPARE(db.bundledProfiles().size(), bundledCount);
}
```

- [ ] **Step 2.2: Run the tests to verify they fail**

Run: `cd tests && qmake && make && ./cdimage_tests -v2`
Expected: FAIL — `saveFailed` signal does not exist, `bundledProfiles`/`userProfiles` do not exist, `saveUserProfile` returns void not bool.

- [ ] **Step 2.3: Update ProfileDatabase header**

Replace `src/profiledatabase.h` entirely with:

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
    explicit ProfileDatabase(const QString& userProfilePath = QString(),
                             QObject* parent = nullptr);

    std::optional<DiscProfile> findById(const QString& discId) const;
    QList<DiscProfile>         allProfiles() const;
    QList<DiscProfile>         userProfiles() const    { return m_user; }
    QList<DiscProfile>         bundledProfiles() const { return m_bundled; }
    QString                    userProfilePath() const { return m_userPath; }

    bool                       saveUserProfile(const DiscProfile&);
    bool                       removeUserProfile(const QString& discId);

signals:
    void saveFailed(QString errorMessage);

private:
    void loadBundled();
    void loadUser();
    bool persist();

    QList<DiscProfile> m_bundled;
    QList<DiscProfile> m_user;
    QString            m_userPath;
};

#endif
```

- [ ] **Step 2.4: Update ProfileDatabase implementation**

Replace `src/profiledatabase.cpp` entirely with:

```cpp
#include "profiledatabase.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

bool ProfileDatabase::saveUserProfile(const DiscProfile& profile) {
    m_user.removeIf([&](const DiscProfile& p){ return p.discId == profile.discId; });
    m_user.append(profile);
    return persist();
}

bool ProfileDatabase::removeUserProfile(const QString& discId) {
    m_user.removeIf([&](const DiscProfile& p){ return p.discId == discId; });
    return persist();
}

bool ProfileDatabase::persist() {
    const QString dir = QFileInfo(m_userPath).absolutePath();
    if (!QDir().mkpath(dir)) {
        emit saveFailed(QStringLiteral("Cannot create directory: %1").arg(dir));
        return false;
    }
    QFile f(m_userPath);
    if (!f.open(QIODevice::WriteOnly)) {
        emit saveFailed(QStringLiteral("Cannot open %1 for writing: %2")
                        .arg(m_userPath, f.errorString()));
        return false;
    }
    QJsonArray arr;
    for (const auto& p : m_user)
        arr.append(toJson(p));
    const QByteArray bytes = QJsonDocument(arr).toJson();
    if (f.write(bytes) != bytes.size()) {
        emit saveFailed(QStringLiteral("Short write to %1: %2")
                        .arg(m_userPath, f.errorString()));
        return false;
    }
    return true;
}
```

- [ ] **Step 2.5: Run the tests to verify they pass**

Run: `cd tests && make && ./cdimage_tests -v2`
Expected: PASS for all `TestProfileDatabase` slots. (`/proc/...` test relies on Linux semantics — on a non-Linux runner, change the path to `/this/path/cannot/exist/profiles.json` which still fails on mkpath if any segment is unwritable.)

- [ ] **Step 2.6: Commit**

```bash
git add src/profiledatabase.h src/profiledatabase.cpp tests/test_profiledatabase.h tests/test_profiledatabase.cpp
git commit -m "feat(profiledb): bool return + saveFailed signal + user/bundled accessors"
```

---

## Phase 2 — Burn backend: detect process-start failure

### Task 3: Introduce BurnResult and refactor backends

**Files:**
- Modify: `src/idiscbackend.h`
- Modify: `src/linuxdiscbackend.h`
- Modify: `src/linuxdiscbackend.cpp`
- Modify: `src/windowsdiscbackend.h`
- Modify: `src/windowsdiscbackend.cpp`
- Modify: `tests/mockdiscbackend.h`

- [ ] **Step 3.1: Define BurnResult in idiscbackend.h**

Replace the contents of `src/idiscbackend.h` with:

```cpp
#ifndef IDISCBACKEND_H
#define IDISCBACKEND_H

#include "discprofile.h"
#include <QMetaType>
#include <QStringList>
#include <QVector>
#include <stdexcept>

struct RawDiscInfo {
    QString   discId;
    MediaType mediaType = MediaType::CD_RW;
};

struct BurnResult {
    bool    started   = false;   // process actually launched
    bool    finished  = false;   // process exited normally (not killed/timeout)
    int     exitCode  = -1;      // exit code when finished == true
    QString errorMessage;        // human-readable, populated on any failure
    QString stderrText;          // captured stderr for diagnostics

    bool succeeded() const { return started && finished && exitCode == 0; }
};

class IDiscBackend {
public:
    virtual ~IDiscBackend() = default;

    virtual QStringList     availableDevices()                                        = 0;
    virtual RawDiscInfo     queryDisc(const QString& devicePath)                      = 0;
    virtual BurnResult      burnTestPattern(const QString& devicePath,
                                            const QString& trackFile)                 = 0;
    virtual QVector<qint64> measureSeekTimes(const QString& devicePath,
                                             const QVector<qint64>& sectors)          = 0;
};

Q_DECLARE_METATYPE(RawDiscInfo)
Q_DECLARE_METATYPE(DiscProfile)

IDiscBackend* createDiscBackend();

#endif
```

- [ ] **Step 3.2: Update Linux backend signature**

Edit `src/linuxdiscbackend.h` — change the `burnTestPattern` return type to `BurnResult`:

```cpp
BurnResult burnTestPattern(const QString& devicePath,
                           const QString& trackFile) override;
```

- [ ] **Step 3.3: Implement Linux backend with proper error checks**

Replace `LinuxDiscBackend::burnTestPattern` in `src/linuxdiscbackend.cpp` with:

```cpp
BurnResult LinuxDiscBackend::burnTestPattern(const QString& devicePath,
                                              const QString& trackFile) {
    BurnResult r;
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("cdrecord", {"-audio",
                            QString("dev=%1").arg(devicePath),
                            trackFile});
    if (!proc.waitForStarted(5000)) {
        r.errorMessage = QStringLiteral(
            "Failed to start cdrecord: %1. Is it installed and in PATH?")
            .arg(proc.errorString());
        return r;
    }
    r.started = true;
    if (!proc.waitForFinished(600000)) {
        proc.kill();
        proc.waitForFinished(2000);
        r.stderrText   = QString::fromLocal8Bit(proc.readAll());
        r.errorMessage = QStringLiteral("cdrecord did not finish within 10 minutes.");
        return r;
    }
    r.stderrText = QString::fromLocal8Bit(proc.readAll());
    if (proc.exitStatus() != QProcess::NormalExit) {
        r.errorMessage = QStringLiteral("cdrecord crashed.");
        return r;
    }
    r.finished = true;
    r.exitCode = proc.exitCode();
    if (r.exitCode != 0)
        r.errorMessage = QStringLiteral("cdrecord exited with code %1").arg(r.exitCode);
    return r;
}
```

- [ ] **Step 3.4: Update Windows backend signature**

Edit `src/windowsdiscbackend.h` — same change to `burnTestPattern` return type.

- [ ] **Step 3.5: Implement Windows backend with proper error checks**

Replace `WindowsDiscBackend::burnTestPattern` in `src/windowsdiscbackend.cpp` with:

```cpp
BurnResult WindowsDiscBackend::burnTestPattern(const QString& devicePath,
                                                const QString& trackFile) {
    BurnResult r;
    if (devicePath.size() < 5) {
        r.errorMessage = QStringLiteral("Bad device path: %1").arg(devicePath);
        return r;
    }
    const QChar driveLetter = devicePath.at(4);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("cdrecord", {"-audio",
                            QString("dev=%1:").arg(driveLetter),
                            trackFile});
    if (!proc.waitForStarted(5000)) {
        r.errorMessage = QStringLiteral(
            "Failed to start cdrecord: %1. cdrecord (cdrtools) is not installed "
            "by default on Windows. Install it (e.g. via Cygwin/Cdrtools) and "
            "add it to PATH, or burn the generated WAV with Nero/ImgBurn manually.")
            .arg(proc.errorString());
        return r;
    }
    r.started = true;
    if (!proc.waitForFinished(600000)) {
        proc.kill();
        proc.waitForFinished(2000);
        r.stderrText   = QString::fromLocal8Bit(proc.readAll());
        r.errorMessage = QStringLiteral("cdrecord did not finish within 10 minutes.");
        return r;
    }
    r.stderrText = QString::fromLocal8Bit(proc.readAll());
    if (proc.exitStatus() != QProcess::NormalExit) {
        r.errorMessage = QStringLiteral("cdrecord crashed.");
        return r;
    }
    r.finished = true;
    r.exitCode = proc.exitCode();
    if (r.exitCode != 0)
        r.errorMessage = QStringLiteral("cdrecord exited with code %1").arg(r.exitCode);
    return r;
}
```

- [ ] **Step 3.6: Update mock backend to match new signature**

Replace `tests/mockdiscbackend.h` with:

```cpp
#ifndef MOCKDISCBACKEND_H
#define MOCKDISCBACKEND_H

#include "../src/idiscbackend.h"

class MockDiscBackend : public IDiscBackend {
public:
    RawDiscInfo     m_discInfo;
    QVector<qint64> m_seekTimes;
    bool            m_queryFails = false;
    BurnResult      m_burnResult{true, true, 0, {}, {}};

    QStringList availableDevices() override { return {"/dev/mock"}; }

    RawDiscInfo queryDisc(const QString&) override {
        if (m_queryFails) throw std::runtime_error("mock error");
        return m_discInfo;
    }

    BurnResult burnTestPattern(const QString&, const QString&) override {
        return m_burnResult;
    }

    QVector<qint64> measureSeekTimes(const QString&,
                                     const QVector<qint64>& sectors) override {
        return m_seekTimes.isEmpty()
               ? QVector<qint64>(sectors.size(), 100LL)
               : m_seekTimes;
    }
};

#endif
```

- [ ] **Step 3.7: Build and run existing tests**

Run: `cd tests && qmake && make && ./cdimage_tests -v2`
Expected: PASS (existing tests still compile against the new signature; behaviour unchanged for callers that only checked truthiness).

- [ ] **Step 3.8: Commit**

```bash
git add src/idiscbackend.h src/linuxdiscbackend.h src/linuxdiscbackend.cpp src/windowsdiscbackend.h src/windowsdiscbackend.cpp tests/mockdiscbackend.h
git commit -m "fix(burn): detect process-start failure with BurnResult struct"
```

---

### Task 4: Smoke test that backend reports failure for missing binary

**Files:**
- Create: `tests/test_burnresult.h`
- Create: `tests/test_burnresult.cpp`
- Modify: `tests/main.cpp`
- Modify: `tests/tests.pro`

This test creates a real backend (Linux on Linux CI, Windows otherwise), points `burnTestPattern` at a non-existent device, and verifies the function reports `started=false` plus a non-empty `errorMessage`. It deliberately does NOT need `cdrecord` to be present — that is the whole point.

- [ ] **Step 4.1: Create the test header**

Create `tests/test_burnresult.h`:

```cpp
#ifndef TEST_BURNRESULT_H
#define TEST_BURNRESULT_H
#include <QObject>
class TestBurnResult : public QObject {
    Q_OBJECT
private slots:
    void burn_with_missing_cdrecord_returns_failure();
};
#endif
```

- [ ] **Step 4.2: Create the test implementation**

Create `tests/test_burnresult.cpp`:

```cpp
#include "test_burnresult.h"
#include "../src/idiscbackend.h"
#include <QTest>
#include <QStandardPaths>
#include <QProcess>

void TestBurnResult::burn_with_missing_cdrecord_returns_failure() {
    // If cdrecord IS installed on this machine, skip — the test only validates
    // the missing-binary code path.
    if (!QStandardPaths::findExecutable("cdrecord").isEmpty())
        QSKIP("cdrecord is installed on this host; cannot test missing-binary path");

    QScopedPointer<IDiscBackend> backend(createDiscBackend());
    BurnResult r = backend->burnTestPattern("/dev/null", "/tmp/cdimage_nonexistent.cdr");
    QVERIFY(!r.succeeded());
    QVERIFY(!r.started);
    QVERIFY(!r.errorMessage.isEmpty());
}
```

- [ ] **Step 4.3: Wire the test into the runner**

Edit `tests/main.cpp` — add the include and registration:

```cpp
#include <QCoreApplication>
#include <QTest>
#include "test_discprofile.h"
#include "test_profiledatabase.h"
#include "test_discdetector.h"
#include "test_photocalibration.h"
#include "test_burnresult.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("cdimage_tests");
    int status = 0;
    auto run = [&](QObject* t){ status |= QTest::qExec(t, argc, argv); delete t; };
    run(new TestDiscProfile);
    run(new TestProfileDatabase);
    run(new TestDiscDetector);
    run(new TestPhotoCalibration);
    run(new TestBurnResult);
    return status;
}
```

- [ ] **Step 4.4: Wire the test into the build**

Edit `tests/tests.pro` — add the new files to `HEADERS`, `SOURCES`, and add the platform backend so `createDiscBackend()` resolves:

```pro
TEMPLATE = app
TARGET   = cdimage_tests
CONFIG  += testcase
QT      += testlib widgets concurrent
INCLUDEPATH += ..

HEADERS += test_discprofile.h \
           test_profiledatabase.h \
           test_discdetector.h \
           test_photocalibration.h \
           test_burnresult.h \
           mockdiscbackend.h

SOURCES += main.cpp \
           test_discprofile.cpp \
           test_profiledatabase.cpp \
           test_discdetector.cpp \
           test_photocalibration.cpp \
           test_burnresult.cpp \
           ../src/discprofile.cpp \
           ../src/profiledatabase.cpp \
           ../src/discdetector.cpp \
           ../src/photocalibration.cpp \
           ../src/testpatterngenerator.cpp \
           ../src/converter.cpp

unix:SOURCES  += ../src/linuxdiscbackend.cpp
unix:HEADERS  += ../src/linuxdiscbackend.h
win32:SOURCES += ../src/windowsdiscbackend.cpp
win32:HEADERS += ../src/windowsdiscbackend.h

RESOURCES += ../resources/profiles.qrc
```

- [ ] **Step 4.5: Build and run**

Run: `cd tests && qmake && make && ./cdimage_tests -v2`
Expected: PASS — `burn_with_missing_cdrecord_returns_failure` either passes (cdrecord absent, failure correctly detected) or `QSKIP` (cdrecord present).

- [ ] **Step 4.6: Commit**

```bash
git add tests/test_burnresult.h tests/test_burnresult.cpp tests/main.cpp tests/tests.pro
git commit -m "test(burn): verify backend reports failure when cdrecord is missing"
```

---

## Phase 3 — Wizard: explicit save with feedback + surface burn errors

### Task 5: Surface burn failures in the wizard

**Files:**
- Modify: `src/calibrationwizard.cpp` (`BurnPatternPage::doBurn`, lines ~76-103)

- [ ] **Step 5.1: Replace doBurn to use BurnResult**

In `src/calibrationwizard.cpp`, replace the entire body of `BurnPatternPage::doBurn` with:

```cpp
void BurnPatternPage::doBurn() {
    m_progress->setVisible(true);
    m_status->setText(tr("Generating test track (this may take several minutes)…"));
    qApp->processEvents();

    const QString device = wizard()->property("devicePath").toString();
    const QString outPath = QDir::tempPath() + "/cdimage_testpattern.wav";

    DiscProfile defaultProfile;
    if (TestPatternGenerator::generateRingsTrack(defaultProfile, outPath).isEmpty()) {
        m_status->setText(tr("Failed to generate test track."));
        m_progress->setVisible(false);
        return;
    }

    m_status->setText(tr("Burning test pattern to disc…"));
    qApp->processEvents();

    const BurnResult br = m_backend->burnTestPattern(device, outPath);
    if (br.succeeded()) {
        m_status->setText(tr("Test pattern burned. Remove the disc and proceed."));
        m_done = true;
        emit completeChanged();
    } else {
        m_status->setText(tr("Burn failed: %1").arg(br.errorMessage));
        QString detail = br.errorMessage;
        if (!br.stderrText.isEmpty())
            detail += "\n\nProcess output:\n" + br.stderrText.left(2000);
        QMessageBox::critical(this, tr("Burn Failed"), detail);
    }
    m_progress->setVisible(false);
}
```

Note the `.cdr` → `.wav` extension change — Phase 5 makes the converter emit WAV. Until Phase 5 lands, the file is still raw bytes but the extension is harmless: cdrecord ignores extension and reads the bytes; once Phase 5 lands the file will be a real WAV.

- [ ] **Step 5.2: Build to confirm no signature mismatch**

Run: `qmake && make`
Expected: clean build.

- [ ] **Step 5.3: Commit**

```bash
git add src/calibrationwizard.cpp
git commit -m "fix(wizard): surface real burn failures via BurnResult instead of silent success"
```

---

### Task 6: Explicit save with success/failure feedback + fallback discId

**Files:**
- Modify: `src/calibrationwizard.h`
- Modify: `src/calibrationwizard.cpp`

The current `ResultPage` connects a save lambda to `accepted()` inside `initializePage()`. Two problems: lambdas + `UniqueConnection` don't deduplicate, and the user gets no feedback. Replace with a named `saveProfile()` slot connected once in the constructor, and show a result dialog.

- [ ] **Step 6.1: Update ResultPage header**

In `src/calibrationwizard.h`, replace the `ResultPage` class declaration with:

```cpp
class ResultPage : public QWizardPage {
    Q_OBJECT
public:
    ResultPage(ProfileDatabase* db, QWidget* parent = nullptr);
    void initializePage() override;
    bool validatePage() override;
private:
    ProfileDatabase* m_db;
    QLabel*          m_lblTr0;
    QLabel*          m_lblDtr;
    QLabel*          m_lblR0;
    QLabel*          m_lblMediaType;
    QLineEdit*       m_leName;
};
```

(Two changes: added `Q_OBJECT` and `validatePage()`. The lambda field for save is gone — save now happens in `validatePage`, which Qt calls when the user clicks Finish and which can refuse to close the wizard if save fails.)

- [ ] **Step 6.2: Update ResultPage implementation**

In `src/calibrationwizard.cpp`, replace `ResultPage::ResultPage` and `ResultPage::initializePage` with:

```cpp
ResultPage::ResultPage(ProfileDatabase* db, QWidget* parent)
    : QWizardPage(parent), m_db(db)
{
    setTitle(tr("Calibration Complete"));
    setSubTitle(tr("Click Finish to save this profile to your local library."));
    m_leName       = new QLineEdit(this);
    m_lblTr0       = new QLabel(this);
    m_lblDtr       = new QLabel(this);
    m_lblR0        = new QLabel(this);
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
    const DiscProfile& p = static_cast<CalibrationWizard*>(wizard())->calibratedProfile();
    m_lblTr0->setText(QString::number(p.tr0, 'g', 10));
    m_lblDtr->setText(QString::number(p.dtr, 'g', 10));
    m_lblR0->setText(QString::number(p.r0,  'f', 2));
    m_lblMediaType->setText(kMediaNames.value(p.mediaType, "Unknown"));
}

bool ResultPage::validatePage() {
    DiscProfile p = static_cast<CalibrationWizard*>(wizard())->calibratedProfile();
    p.name = m_leName->text().trimmed();
    if (p.name.isEmpty()) {
        QMessageBox::warning(this, tr("Profile name required"),
            tr("Please enter a name for this profile."));
        return false;
    }
    // If ATIP didn't yield a discId, mint a stable synthetic one based on the
    // profile name + media type so findById has something to match next time.
    if (p.discId.isEmpty())
        p.discId = QStringLiteral("user:%1:%2")
                   .arg(static_cast<int>(p.mediaType))
                   .arg(p.name);

    QString err;
    QObject::connect(m_db, &ProfileDatabase::saveFailed,
                     this, [&err](const QString& m){ err = m; },
                     Qt::DirectConnection);
    const bool ok = m_db->saveUserProfile(p);
    QObject::disconnect(m_db, &ProfileDatabase::saveFailed, this, nullptr);

    if (!ok) {
        QMessageBox::critical(this, tr("Save Failed"),
            tr("Could not save profile.\n\n%1\n\nLocation: %2")
                .arg(err.isEmpty() ? tr("Unknown error") : err,
                     m_db->userProfilePath()));
        return false;  // keep wizard open so user can retry / copy the error
    }
    static_cast<CalibrationWizard*>(wizard())->setResult(p);  // sync back the discId
    QMessageBox::information(this, tr("Profile Saved"),
        tr("Saved to %1").arg(m_db->userProfilePath()));
    return true;
}
```

(Note: `validatePage()` is the canonical Qt way to gate Finish/Next. Returning false keeps the wizard open. This replaces the fragile lambda-on-`accepted` pattern entirely.)

- [ ] **Step 6.3: Add the missing include for QObject lambda connect**

Verify `src/calibrationwizard.cpp` has `#include <QMessageBox>` near the top — it already does (line 11). No new includes needed.

- [ ] **Step 6.4: Build**

Run: `qmake && make`
Expected: clean build. (Note: `Q_OBJECT` was added to `ResultPage` in Step 6.1; qmake/moc will regenerate.)

- [ ] **Step 6.5: Manual smoke test**

Launch the app, run `Edit → Detect disc geometry`, complete the wizard with a deliberately empty name → confirm warning dialog appears and Finish is blocked. Re-enter a name → confirm "Profile Saved" dialog with a real path appears. Then re-launch the app and verify the saved profile is loaded.

- [ ] **Step 6.6: Commit**

```bash
git add src/calibrationwizard.h src/calibrationwizard.cpp
git commit -m "fix(wizard): explicit save in validatePage with feedback and fallback discId"
```

---

## Phase 4 — Create-Track dialog: pre-select current profile, fix precision, group by source

### Task 7: Pass current profile into Create-Track dialog and fix precision

**Files:**
- Modify: `src/createtrackdialog.h`
- Modify: `src/createtrackdialog.cpp`
- Modify: `src/mainwindow.cpp:63-82`

- [ ] **Step 7.1: Update dialog header to accept a current profile**

Replace `src/createtrackdialog.h` with:

```cpp
#ifndef CREATETRACKDIALOG_H
#define CREATETRACKDIALOG_H

#include "ui_createtrackdialog.h"
#include "discprofile.h"
#include "profiledatabase.h"

class CreateTrackDialog: public QDialog, public Ui::CreateTrackDialog {
Q_OBJECT
public:
    explicit CreateTrackDialog(ProfileDatabase* db,
                               const DiscProfile& currentProfile = {},
                               QWidget* parent = nullptr);
    DiscProfile selectedProfile() const;

public slots:
    void selectFile();
    void loadPreset(int index);

private:
    void populatePresets();
    int  indexForProfile(const DiscProfile& p) const;

    ProfileDatabase*    m_db;
    QList<DiscProfile>  m_orderedProfiles;  // mirrors combo order: user first, then bundled
    DiscProfile         m_currentProfile;
};

#endif
```

- [ ] **Step 7.2: Update dialog implementation**

Replace `src/createtrackdialog.cpp` with:

```cpp
#include "createtrackdialog.h"
#include <QFileDialog>

CreateTrackDialog::CreateTrackDialog(ProfileDatabase* db,
                                      const DiscProfile& currentProfile,
                                      QWidget* parent)
    : QDialog(parent), m_db(db), m_currentProfile(currentProfile)
{
    setupUi(this);
    populatePresets();

    const int idx = indexForProfile(m_currentProfile);
    if (idx >= 0) {
        cbPresets->setCurrentIndex(idx);
        loadPreset(idx);
    } else if (cbPresets->count() > 0) {
        loadPreset(0);
    }

    connect(tbBrowse, &QAbstractButton::clicked,
            this, &CreateTrackDialog::selectFile);
    connect(cbPresets, qOverload<int>(&QComboBox::activated),
            this, &CreateTrackDialog::loadPreset);
}

void CreateTrackDialog::populatePresets() {
    cbPresets->clear();
    m_orderedProfiles.clear();

    const QList<DiscProfile> userP    = m_db->userProfiles();
    const QList<DiscProfile> bundledP = m_db->bundledProfiles();

    for (const DiscProfile& p : userP) {
        cbPresets->addItem(QStringLiteral("[Local] %1").arg(p.name));
        m_orderedProfiles.append(p);
    }
    for (const DiscProfile& p : bundledP) {
        cbPresets->addItem(QStringLiteral("[Bundled] %1").arg(p.name));
        m_orderedProfiles.append(p);
    }
}

int CreateTrackDialog::indexForProfile(const DiscProfile& p) const {
    if (p.discId.isEmpty() && p.name.isEmpty()) return -1;
    for (int i = 0; i < m_orderedProfiles.size(); ++i) {
        const DiscProfile& q = m_orderedProfiles.at(i);
        if (!p.discId.isEmpty() && q.discId == p.discId) return i;
        if (!p.name.isEmpty()   && q.name   == p.name  ) return i;
    }
    return -1;
}

void CreateTrackDialog::selectFile() {
    const QString f = QFileDialog::getSaveFileName(
        this, tr("Save track as"), leFileName->text(),
        tr("WAV audio (*.wav);;All files (*)"));
    if (!f.isNull()) leFileName->setText(f);
}

void CreateTrackDialog::loadPreset(int index) {
    if (index < 0 || index >= m_orderedProfiles.size()) return;
    const DiscProfile& p = m_orderedProfiles.at(index);
    leTr0->setText(QString::number(p.tr0, 'g', 10));
    leDtr->setText(QString::number(p.dtr, 'g', 10));
    leR0->setText(QString::number(p.r0,  'f', 2));
}

DiscProfile CreateTrackDialog::selectedProfile() const {
    const int idx = cbPresets->currentIndex();
    DiscProfile p = (idx >= 0 && idx < m_orderedProfiles.size())
                      ? m_orderedProfiles.at(idx)
                      : DiscProfile{};
    p.tr0       = leTr0->text().toDouble();
    p.dtr       = leDtr->text().toDouble();
    p.r0        = leR0->text().toDouble();
    p.mixColors = cbMixColors->isChecked();
    return p;
}
```

(Three fixes here: precision is now a real `int` 10 matching the wizard; profiles are tagged `[Local]` vs `[Bundled]` so the user can tell them apart; current profile pre-selects.)

- [ ] **Step 7.3: Update MainWindow::createTrack to pass current profile and default WAV path**

In `src/mainwindow.cpp`, replace the body of `MainWindow::createTrack` with:

```cpp
void MainWindow::createTrack() {
    m_image = centralView.getImage();
    CreateTrackDialog dial(m_profileDb.data(), m_currentProfile, this);
    if (!dial.exec()) return;

    const DiscProfile profile = dial.selectedProfile();
    Converter converter(this, profile);

    QProgressDialog progress(tr("Generating track…"), tr("Abort"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    connect(&converter, &Converter::progressChanged, &progress, &QProgressDialog::setValue);
    connect(&progress,  &QProgressDialog::canceled,  &converter, &Converter::cancelConverting);

    if (converter.convert(m_image, dial.leFileName->text()))
        QMessageBox::information(this, tr("Success"),
            tr("Track created as a standard audio WAV file:<br><b>%1</b><br><br>"
               "Burn it as an <b>Audio CD</b> with any burning software "
               "(Nero, ImgBurn, Windows Media Player, or "
               "<code>cdrecord -audio dev=&lt;device&gt; %1</code>).")
            .arg(dial.leFileName->text()));
    else
        QMessageBox::warning(this, tr("Stopped"), tr("Cancelled by user."));
}
```

- [ ] **Step 7.4: Build**

Run: `qmake && make`
Expected: clean build.

- [ ] **Step 7.5: Manual smoke test**

Launch the app. After Detect/Calibration, open Create-Track — confirm the just-calibrated profile is pre-selected and labelled `[Local]`. Confirm bundled entries are labelled `[Bundled]`.

- [ ] **Step 7.6: Commit**

```bash
git add src/createtrackdialog.h src/createtrackdialog.cpp src/mainwindow.cpp
git commit -m "fix(track-dialog): pre-select current profile, label local vs bundled, fix precision"
```

---

## Phase 5 — Converter: emit a real WAV file

### Task 8: TDD — verify Converter writes a WAV header

**Files:**
- Create: `tests/test_converter_wav.h`
- Create: `tests/test_converter_wav.cpp`
- Modify: `tests/main.cpp`
- Modify: `tests/tests.pro`

- [ ] **Step 8.1: Create the failing test header**

Create `tests/test_converter_wav.h`:

```cpp
#ifndef TEST_CONVERTER_WAV_H
#define TEST_CONVERTER_WAV_H
#include <QObject>
class TestConverterWav : public QObject {
    Q_OBJECT
private slots:
    void output_starts_with_riff_wave_fmt_header();
    void header_declares_44100hz_stereo_16bit();
    void data_size_matches_actual_audio_bytes();
};
#endif
```

- [ ] **Step 8.2: Create the failing test implementation**

Create `tests/test_converter_wav.cpp`:

```cpp
#include "test_converter_wav.h"
#include "../src/converter.h"
#include "../src/discprofile.h"
#include <QTest>
#include <QTemporaryFile>
#include <QImage>
#include <QFile>
#include <QtEndian>

// Build a tiny 3000x3000 black image. Converter will produce ~800 MB which is
// too big for CI; the convert() loop is bounded by `all=800*1024*1024`. To keep
// the test fast, we use a converter that stops after ~1 MB by overriding
// behaviour. Until refactor, accept the long runtime — but mark the test as
// long-running so it can be skipped under -fast.

static QImage makeBlack3000() {
    QImage img(3000, 3000, QImage::Format_RGB32);
    img.fill(Qt::black);
    return img;
}

static QByteArray readBytes(const QString& path, qint64 n) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.read(n);
}

void TestConverterWav::output_starts_with_riff_wave_fmt_header() {
    if (qEnvironmentVariableIsSet("CDIMAGE_SKIP_LONG"))
        QSKIP("long-running converter test skipped");

    QTemporaryFile tmp("cdimage_wav_XXXXXX.wav");
    tmp.open();
    const QString path = tmp.fileName();
    tmp.close();

    DiscProfile profile;
    Converter conv(nullptr, profile);
    QVERIFY(conv.convert(makeBlack3000(), path));

    const QByteArray head = readBytes(path, 44);
    QCOMPARE(head.left(4),       QByteArray("RIFF"));
    QCOMPARE(head.mid(8, 4),     QByteArray("WAVE"));
    QCOMPARE(head.mid(12, 4),    QByteArray("fmt "));
    QCOMPARE(head.mid(36, 4),    QByteArray("data"));
}

void TestConverterWav::header_declares_44100hz_stereo_16bit() {
    if (qEnvironmentVariableIsSet("CDIMAGE_SKIP_LONG"))
        QSKIP("long-running converter test skipped");

    QTemporaryFile tmp("cdimage_wav_XXXXXX.wav");
    tmp.open();
    const QString path = tmp.fileName();
    tmp.close();

    DiscProfile profile;
    Converter conv(nullptr, profile);
    QVERIFY(conv.convert(makeBlack3000(), path));

    const QByteArray head = readBytes(path, 44);
    auto u16 = [&](int off){ return qFromLittleEndian<quint16>(head.constData() + off); };
    auto u32 = [&](int off){ return qFromLittleEndian<quint32>(head.constData() + off); };

    QCOMPARE(u16(20), quint16(1));         // PCM
    QCOMPARE(u16(22), quint16(2));         // channels
    QCOMPARE(u32(24), quint32(44100));     // sample rate
    QCOMPARE(u32(28), quint32(44100 * 4)); // byte rate
    QCOMPARE(u16(32), quint16(4));         // block align
    QCOMPARE(u16(34), quint16(16));        // bits per sample
}

void TestConverterWav::data_size_matches_actual_audio_bytes() {
    if (qEnvironmentVariableIsSet("CDIMAGE_SKIP_LONG"))
        QSKIP("long-running converter test skipped");

    QTemporaryFile tmp("cdimage_wav_XXXXXX.wav");
    tmp.open();
    const QString path = tmp.fileName();
    tmp.close();

    DiscProfile profile;
    Converter conv(nullptr, profile);
    QVERIFY(conv.convert(makeBlack3000(), path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const qint64 fileSize = f.size();

    f.seek(4);
    quint32 riffSize = 0;
    f.read(reinterpret_cast<char*>(&riffSize), 4);
    riffSize = qFromLittleEndian(riffSize);
    QCOMPARE(qint64(riffSize), fileSize - 8);

    f.seek(40);
    quint32 dataSize = 0;
    f.read(reinterpret_cast<char*>(&dataSize), 4);
    dataSize = qFromLittleEndian(dataSize);
    QCOMPARE(qint64(dataSize), fileSize - 44);
}
```

- [ ] **Step 8.3: Wire into the runner and build**

Edit `tests/main.cpp`:

```cpp
#include <QCoreApplication>
#include <QTest>
#include "test_discprofile.h"
#include "test_profiledatabase.h"
#include "test_discdetector.h"
#include "test_photocalibration.h"
#include "test_burnresult.h"
#include "test_converter_wav.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("cdimage_tests");
    int status = 0;
    auto run = [&](QObject* t){ status |= QTest::qExec(t, argc, argv); delete t; };
    run(new TestDiscProfile);
    run(new TestProfileDatabase);
    run(new TestDiscDetector);
    run(new TestPhotoCalibration);
    run(new TestBurnResult);
    run(new TestConverterWav);
    return status;
}
```

Edit `tests/tests.pro` — add `test_converter_wav.h` to `HEADERS` and `test_converter_wav.cpp` to `SOURCES`.

- [ ] **Step 8.4: Run the new tests to verify they fail**

Run: `cd tests && qmake && make && ./cdimage_tests TestConverterWav -v2`
Expected: FAIL — output starts with the raw palette byte, not "RIFF". (Note: each test takes minutes because Converter writes 800 MB. To keep dev iteration fast, set `CDIMAGE_SKIP_LONG=1` while iterating on unrelated phases; unset it for the final verification.)

- [ ] **Step 8.5: Commit the failing tests**

```bash
git add tests/test_converter_wav.h tests/test_converter_wav.cpp tests/main.cpp tests/tests.pro
git commit -m "test(converter): add failing tests for WAV header output"
```

---

### Task 9: Implement WAV header in Converter

**Files:**
- Modify: `src/converter.h`
- Modify: `src/converter.cpp`

The `convert()` method already writes the audio bytes via `bw()`. We need to: (a) write a 44-byte placeholder header before any audio bytes; (b) after the loop, seek back and patch the two size fields.

- [ ] **Step 9.1: Add a private helper declaration**

Edit `src/converter.h`. Inside the `Converter` class, in the `private:` section (around line 67), add:

```cpp
    void writeWavHeader(QFile* file, quint32 dataBytes);
    qint64 m_audioBytesWritten = 0;
```

(Keep all existing members. The new `m_audioBytesWritten` lets us patch the header without re-counting.)

- [ ] **Step 9.2: Implement WAV header logic in convert()**

Edit `src/converter.cpp`. Replace the file-handling parts of `convert()` and `bw()` so the header is written first and updated last. Concretely:

Replace the file-open block in `convert()` (currently lines 85-87) with:

```cpp
    QFile imageFile(filename);
    imageFile.remove();
    if (!imageFile.open(QIODevice::WriteOnly)) return false;
    m_audioBytesWritten = 0;
    writeWavHeader(&imageFile, 0);  // placeholder; patched at end
```

Replace the loop's tail and close (currently lines 131-133) with:

```cpp
    }
    // Flush any partial sector held in `buffer` so all audio bytes are on disk.
    // The original convert loop only flushes whole 2352-byte sectors; the
    // remainder (if any) is conceptually padding to a sector boundary but for
    // a valid WAV we should declare exactly what we wrote — so flush it.
    if (c > 0) {
        imageFile.write(buffer, c);
        m_audioBytesWritten += c;
        c = 0;
    }
    // Patch RIFF size and data size now that we know the audio length.
    const quint32 dataBytes = static_cast<quint32>(m_audioBytesWritten);
    const quint32 riffSize  = dataBytes + 36;  // 44-byte header minus the 8 bytes ('RIFF' + size)
    imageFile.seek(4);
    quint32 le = qToLittleEndian(riffSize);
    imageFile.write(reinterpret_cast<const char*>(&le), 4);
    imageFile.seek(40);
    le = qToLittleEndian(dataBytes);
    imageFile.write(reinterpret_cast<const char*>(&le), 4);
    imageFile.close();
    return true;
}
```

(Note: this replaces the existing `imageFile.close(); return true;`.)

Then update `bw()` to count audio bytes. Replace the body (lines 160-172) with:

```cpp
void Converter::bw (char b, QFile *file)
{
    buffer[c++]=b;
    if (c>=2352)
    {
        if (file->write(buffer, 2352)==-1)
        {
            qCritical ("Converter: Cannot write data to file");
            return;
        }
        m_audioBytesWritten += 2352;
        c=0;
    }
}
```

Add the new helper at the end of `src/converter.cpp`:

```cpp
void Converter::writeWavHeader(QFile* file, quint32 dataBytes)
{
    const quint32 sampleRate  = 44100;
    const quint16 channels    = 2;
    const quint16 bitsSample  = 16;
    const quint32 byteRate    = sampleRate * channels * (bitsSample / 8);
    const quint16 blockAlign  = channels * (bitsSample / 8);
    const quint32 fmtChunkLen = 16;
    const quint32 riffSize    = dataBytes + 36;

    auto put32 = [&](quint32 v) {
        v = qToLittleEndian(v);
        file->write(reinterpret_cast<const char*>(&v), 4);
    };
    auto put16 = [&](quint16 v) {
        v = qToLittleEndian(v);
        file->write(reinterpret_cast<const char*>(&v), 2);
    };

    file->write("RIFF", 4);
    put32(riffSize);
    file->write("WAVE", 4);
    file->write("fmt ", 4);
    put32(fmtChunkLen);
    put16(1);             // PCM
    put16(channels);
    put32(sampleRate);
    put32(byteRate);
    put16(blockAlign);
    put16(bitsSample);
    file->write("data", 4);
    put32(dataBytes);
}
```

You also need an include — add at the top of `src/converter.cpp`:

```cpp
#include <QtEndian>
```

- [ ] **Step 9.3: Initialise m_audioBytesWritten in all three constructors**

This is already handled by the `= 0` default-member-initialiser in the header (Step 9.1). Confirm by checking that no constructor explicitly sets it to a different value. No change needed if you used the `= 0` initialiser.

- [ ] **Step 9.4: Build and run the WAV tests**

Run: `cd tests && qmake && make && unset CDIMAGE_SKIP_LONG && ./cdimage_tests TestConverterWav -v2`
Expected: PASS — all three header tests pass. Each test will take several minutes because the converter writes 800 MB.

- [ ] **Step 9.5: Run the full test suite to confirm no regressions**

Run: `./cdimage_tests -v2`
Expected: all tests pass.

- [ ] **Step 9.6: Manual smoke test (Nero compatibility)**

Launch the app, load any image, run Create Track → save as `track.wav` → confirm the file plays in any audio player (Windows Media Player, VLC) → confirm Nero Burning Rom recognises it as audio when adding to an Audio CD project.

- [ ] **Step 9.7: Commit**

```bash
git add src/converter.h src/converter.cpp
git commit -m "feat(converter): emit standard WAV header so any audio-CD burner accepts the track"
```

---

## Phase 6 — Final verification

### Task 10: End-to-end verification

- [ ] **Step 10.1: Run the full test suite one last time**

Run: `cd tests && ./cdimage_tests -v2`
Expected: every test passes (or `QSKIP`s for the missing-cdrecord case).

- [ ] **Step 10.2: Manual end-to-end smoke**

1. Launch the app.
2. `Edit → Detect disc geometry` with a known disc → wizard launches.
3. Click `Burn Test Pattern`. If `cdrecord` is missing, confirm a clear error dialog appears (not a silent advance).
4. With cdrecord present, complete the wizard, enter a profile name, click Finish → confirm "Profile Saved" dialog with file path.
5. Re-launch the app. `Edit → Create track` → confirm the saved profile appears in the dropdown labelled `[Local]` and is pre-selected.
6. Save the track as `out.wav` → confirm Nero recognises it for Audio CD burning.

- [ ] **Step 10.3: Final commit (if any uncommitted polish remains)**

```bash
git status
# If clean, nothing to do.
```

---

## Self-Review Notes

- Spec coverage: Bug 1 (Tasks 1, 2, 6); Bug 2 (Tasks 3, 4, 5); Bug 3 (Tasks 1, 2, 7); Bug 4 (Tasks 8, 9). All four reported bugs are addressed.
- Type consistency: `BurnResult` defined once in `idiscbackend.h`, used identically in both backends, the mock, and the wizard. `userProfiles()`/`bundledProfiles()` defined once on `ProfileDatabase`, used once in `CreateTrackDialog`.
- No placeholders: every code step contains the actual code; no TODO markers; every test has its assertions written out.
- Test framework matches existing conventions: QTest with `private slots:`, registered in `tests/main.cpp`, built via `tests/tests.pro`.
- Build commands match the project: `qmake && make` for app, `cd tests && qmake && make` for tests.
