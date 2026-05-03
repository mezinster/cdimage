#include "calibrationwizard.h"
#include "photocalibration.h"
#include "drivereadbackcalibration.h"
#include "testpatterngenerator.h"
#include "capacitydialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QPushButton>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QtConcurrent/QtConcurrent>

// Returns audio bytes (sector-aligned) the caller should pass to Converter.
// Asks the backend first; if that returns 0 (unknown), prompts the user via
// CapacityDialog. Returns 0 if the user cancelled.
static qint64 resolveAudioCapacityBytes(IDiscBackend* backend,
                                         const QString& devicePath,
                                         QWidget* dialogParent) {
    DiscCapacity cap = backend->queryCapacity(devicePath);
    if (cap.freeBytes > 0) {
        // Round down to a sector boundary defensively.
        return (cap.freeBytes / 2352) * 2352;
    }
    CapacityDialog dlg(dialogParent);
    if (dlg.exec() != QDialog::Accepted) return 0;
    return dlg.selectedBytes();
}

static const QMap<MediaType, QString> kMediaNames = {
    {MediaType::CD_R,   "CD-R"},   {MediaType::CD_RW,  "CD-RW"},
    {MediaType::DVD_R,  "DVD-R"},  {MediaType::DVD_RW, "DVD-RW"},
    {MediaType::DVD_DL, "DVD+R DL"}
};

// ── CalibrationWizard ─────────────────────────────────────────────────────────

CalibrationWizard::CalibrationWizard(IDiscBackend* backend, ProfileDatabase* db,
                                     const RawDiscInfo& disc, QWidget* parent)
    : QWizard(parent), m_backend(backend), m_db(db), m_disc(disc)
{
    setWindowTitle(tr("Disc Calibration Wizard"));
    setPage(Page_Welcome,      new WelcomePage(disc, this));
    setPage(Page_BurnPattern,  new BurnPatternPage(backend, disc, this));
    setPage(Page_MethodSelect, new MethodSelectPage(this));
    setPage(Page_Photo,        new PhotoPage(this));
    setPage(Page_Readback,     new ReadbackPage(this));
    setPage(Page_Analysis,     new AnalysisPage(backend, disc, this));
    setPage(Page_Result,       new ResultPage(db, this));
}

// ── WelcomePage ───────────────────────────────────────────────────────────────

WelcomePage::WelcomePage(const RawDiscInfo& disc, QWidget* parent)
    : QWizardPage(parent), m_disc(disc)
{
    m_lbl = new QLabel(this);
    m_lbl->setWordWrap(true);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_lbl);
    retranslateUi();
}

void WelcomePage::retranslateUi() {
    setTitle(tr("Disc Calibration"));
    m_lbl->setText(tr(
        "<p>Detected disc: <b>%1</b> (ID: %2)</p>"
        "<p>This wizard burns a test pattern onto the disc, then measures the disc "
        "geometry either from a photograph or by re-reading the disc with the drive.</p>")
        .arg(kMediaNames.value(m_disc.mediaType, tr("Unknown")))
        .arg(m_disc.discId.isEmpty() ? tr("unknown") : m_disc.discId));
}

void WelcomePage::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange) retranslateUi();
    QWizardPage::changeEvent(e);
}

// ── BurnPatternPage ───────────────────────────────────────────────────────────

BurnPatternPage::BurnPatternPage(IDiscBackend* backend, const RawDiscInfo& disc,
                                  QWidget* parent)
    : QWizardPage(parent), m_backend(backend), m_disc(disc)
{
    m_status   = new QLabel(this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);

    m_lePath = new QLineEdit(this);
    m_lePath->setText(QDir::tempPath() + "/cdimage_testpattern.wav");
    m_browseBtn = new QPushButton(this);
    connect(m_browseBtn, &QPushButton::clicked, this, &BurnPatternPage::browseWavPath);

    m_burnBtn = new QPushButton(this);
    connect(m_burnBtn, &QPushButton::clicked, this, &BurnPatternPage::doBurn);

    m_pathLabel = new QLabel(this);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_pathLabel);
    pathRow->addWidget(m_lePath);
    pathRow->addWidget(m_browseBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addLayout(pathRow);
    layout->addWidget(m_progress);
    layout->addWidget(m_burnBtn);

    retranslateUi();
}

void BurnPatternPage::retranslateUi() {
    setTitle(tr("Burn Test Pattern"));
    m_status->setText(tr("Click 'Burn' to write the test pattern to the disc."));
    m_pathLabel->setText(tr("Test WAV:"));
    m_browseBtn->setText(tr("Browse…"));
    m_burnBtn->setText(tr("Burn Test Pattern"));
}

void BurnPatternPage::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange) retranslateUi();
    QWizardPage::changeEvent(e);
}

void BurnPatternPage::browseWavPath() {
    const QString f = QFileDialog::getSaveFileName(
        this, tr("Save test pattern as"), m_lePath->text(),
        tr("WAV audio (*.wav);;All files (*)"));
    if (!f.isEmpty()) m_lePath->setText(f);
}

void BurnPatternPage::initializePage() { m_done = false; emit completeChanged(); }
bool BurnPatternPage::isComplete() const { return m_done; }

void BurnPatternPage::doBurn() {
    if (m_watcher) return;  // already running

    const QString device = wizard()->property("devicePath").toString();
    QString outPath = m_lePath->text().trimmed();
    if (outPath.isEmpty())
        outPath = QDir::tempPath() + "/cdimage_testpattern.wav";

    // Resolve disc capacity BEFORE the worker spins up — the dialog (if
    // shown) needs the GUI thread.
    const qint64 capacityBytes = resolveAudioCapacityBytes(m_backend, device, this);
    if (capacityBytes <= 0) {
        m_status->setText(tr("Cancelled."));
        return;
    }
    qInfo() << "BurnPatternPage::doBurn dispatch device=" << device
            << "wav=" << outPath << "capacityBytes=" << capacityBytes;

    m_burnBtn->setEnabled(false);
    m_progress->setVisible(true);
    m_status->setText(tr("Generating test track + burning (this may take several minutes)…"));

    IDiscBackend* backend = m_backend;

    // Heavy work runs on a thread-pool worker so the GUI thread stays
    // responsive (no DWM "Not Responding" hang) and any C-runtime fault
    // during WAV generation propagates as an SEH on the worker rather than
    // killing the GUI thread inline.
    QFuture<BurnResult> fut = QtConcurrent::run([backend, device, outPath, capacityBytes]() {
        BurnResult r;
        DiscProfile defaultProfile;
        qInfo() << "Worker: generating test pattern WAV bytes=" << capacityBytes;
        if (TestPatternGenerator::generateRingsTrack(defaultProfile, outPath, capacityBytes).isEmpty()) {
            r.errorMessage = QStringLiteral("Failed to generate test track WAV.");
            qWarning() << "Worker: WAV generation failed";
            return r;
        }
        qInfo() << "Worker: WAV generated; invoking burnTestPattern";
        r = backend->burnTestPattern(device, outPath);
        qInfo() << "Worker: burnTestPattern returned started="
                << r.started << "finished=" << r.finished
                << "exitCode=" << r.exitCode << "msg=" << r.errorMessage;
        return r;
    });

    m_watcher = new QFutureWatcher<BurnResult>(this);
    connect(m_watcher, &QFutureWatcherBase::finished,
            this, &BurnPatternPage::onBurnFinished);
    m_watcher->setFuture(fut);
}

void BurnPatternPage::onBurnFinished() {
    const BurnResult br = m_watcher->result();
    m_watcher->deleteLater();
    m_watcher = nullptr;

    m_progress->setVisible(false);
    m_burnBtn->setEnabled(true);

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
}

// ── MethodSelectPage ──────────────────────────────────────────────────────────

MethodSelectPage::MethodSelectPage(QWidget* parent) : QWizardPage(parent) {
    m_rbPhoto    = new QRadioButton(this);
    m_rbReadback = new QRadioButton(this);
    m_rbPhoto->setChecked(true);
    registerField("usePhoto", m_rbPhoto);

    m_lblPhotoHint    = new QLabel(this);
    m_lblReadbackHint = new QLabel(this);
    m_lblPhotoHint->setWordWrap(true);
    m_lblReadbackHint->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_rbPhoto);
    layout->addWidget(m_lblPhotoHint);
    layout->addSpacing(8);
    layout->addWidget(m_rbReadback);
    layout->addWidget(m_lblReadbackHint);

    retranslateUi();
}

void MethodSelectPage::retranslateUi() {
    setTitle(tr("Choose Measurement Method"));
    m_rbPhoto->setText(tr("Photograph the disc"));
    m_rbReadback->setText(tr("Let the drive re-read the disc"));
    m_lblPhotoHint->setText(tr("  Take a photo of the burned disc and upload it. "
                               "Works without a functional drive reader."));
    m_lblReadbackHint->setText(tr("  The drive seeks to known sectors and measures "
                                  "timing. Fully automated."));
}

void MethodSelectPage::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange) retranslateUi();
    QWizardPage::changeEvent(e);
}

int MethodSelectPage::nextId() const {
    return field("usePhoto").toBool() ? Page_Photo : Page_Readback;
}

// ── PhotoPage ─────────────────────────────────────────────────────────────────

PhotoPage::PhotoPage(QWidget* parent) : QWizardPage(parent) {
    m_lblIntro = new QLabel(this);
    m_lblIntro->setWordWrap(true);
    m_path = new QLineEdit(this);
    registerField("photoPath", m_path);
    connect(m_path, &QLineEdit::textChanged, this, &PhotoPage::completeChanged);
    m_browseBtn = new QPushButton(this);
    connect(m_browseBtn, &QPushButton::clicked, this, [this]{
        const QString f = QFileDialog::getOpenFileName(
            this, tr("Select disc photo"), {},
            tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!f.isEmpty()) m_path->setText(f);
    });
    auto* row = new QHBoxLayout;
    row->addWidget(m_path);
    row->addWidget(m_browseBtn);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_lblIntro);
    layout->addLayout(row);

    retranslateUi();
}

void PhotoPage::retranslateUi() {
    setTitle(tr("Upload Disc Photo"));
    m_lblIntro->setText(tr("Place the burned disc on a dark background under even "
                           "lighting. Take a photo showing the full disc, then "
                           "select it below:"));
    m_browseBtn->setText(tr("Browse…"));
}

void PhotoPage::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange) retranslateUi();
    QWizardPage::changeEvent(e);
}

bool PhotoPage::isComplete() const { return !m_path->text().isEmpty(); }

// ── ReadbackPage ──────────────────────────────────────────────────────────────

ReadbackPage::ReadbackPage(QWidget* parent) : QWizardPage(parent) {
    m_lbl = new QLabel(this);
    m_lbl->setWordWrap(true);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_lbl);
    retranslateUi();
}

void ReadbackPage::retranslateUi() {
    setTitle(tr("Drive Read-Back"));
    m_lbl->setText(tr("Re-insert the burned disc. The drive will seek "
                      "to known sectors to measure timing automatically "
                      "on the next page."));
}

void ReadbackPage::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange) retranslateUi();
    QWizardPage::changeEvent(e);
}

void ReadbackPage::initializePage() { emit completeChanged(); }

// ── AnalysisPage ──────────────────────────────────────────────────────────────

AnalysisPage::AnalysisPage(IDiscBackend* backend, const RawDiscInfo& disc,
                            QWidget* parent)
    : QWizardPage(parent), m_backend(backend), m_disc(disc)
{
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_status = new QLabel(this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_progress);
    retranslateUi();
}

void AnalysisPage::retranslateUi() {
    setTitle(tr("Analysing…"));
}

void AnalysisPage::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange) retranslateUi();
    QWizardPage::changeEvent(e);
}

void AnalysisPage::initializePage() {
    m_done = false;
    m_progress->setValue(0);
    m_status->setText(tr("Running calibration…"));

    const bool usePhoto = field("usePhoto").toBool();
    ICalibrationMethod* method = nullptr;

    if (usePhoto) {
        method = new PhotoCalibration(field("photoPath").toString(),
                                       PhotoCalibration::PatternType::Rings, this);
    } else {
        const QString dev = wizard()->property("devicePath").toString();
        method = new DriveReadbackCalibration(m_backend, dev, this);
    }

    connect(method, &ICalibrationMethod::progressChanged,
            m_progress, &QProgressBar::setValue);
    connect(method, &ICalibrationMethod::finished,
            this, [this](DiscProfile p) {
                static_cast<CalibrationWizard*>(wizard())->setResult(p);
                m_status->setText(tr("Calibration complete."));
                m_done = true;
                emit completeChanged();
                wizard()->next();
            });
    connect(method, &ICalibrationMethod::failed,
            this, [this](const QString& err) {
                m_status->setText(tr("Failed: %1").arg(err));
                QMessageBox::critical(wizard(), tr("Calibration Failed"), err);
            });

    method->start(m_disc);
}

// ── ResultPage ────────────────────────────────────────────────────────────────

ResultPage::ResultPage(ProfileDatabase* db, QWidget* parent)
    : QWizardPage(parent), m_db(db)
{
    m_leName       = new QLineEdit(this);
    m_lblTr0       = new QLabel(this);
    m_lblDtr       = new QLabel(this);
    m_lblR0        = new QLabel(this);
    m_lblMediaType = new QLabel(this);
    registerField("profileName*", m_leName);

    m_form = new QFormLayout(this);
    m_form->addRow(QString(), m_leName);        // labels filled in by retranslateUi
    m_form->addRow(QString(), m_lblTr0);
    m_form->addRow(QString(), m_lblDtr);
    m_form->addRow(QString(), m_lblR0);
    m_form->addRow(QString(), m_lblMediaType);

    retranslateUi();
}

void ResultPage::retranslateUi() {
    setTitle(tr("Calibration Complete"));
    setSubTitle(tr("Click Finish to save this profile to your local library."));
    if (auto* l = qobject_cast<QLabel*>(m_form->labelForField(m_leName)))       l->setText(tr("Profile name:"));
    if (auto* l = qobject_cast<QLabel*>(m_form->labelForField(m_lblTr0)))       l->setText(tr("tr0:"));
    if (auto* l = qobject_cast<QLabel*>(m_form->labelForField(m_lblDtr)))       l->setText(tr("dtr:"));
    if (auto* l = qobject_cast<QLabel*>(m_form->labelForField(m_lblR0)))        l->setText(tr("r0 (mm):"));
    if (auto* l = qobject_cast<QLabel*>(m_form->labelForField(m_lblMediaType))) l->setText(tr("Media type:"));
    // Refresh "Unknown" media-type label if currently displayed.
    if (m_lblMediaType->text() == QStringLiteral("Unknown")
        || m_lblMediaType->text().isEmpty()) {
        // initializePage() will repopulate from the wizard result; no-op here.
    }
}

void ResultPage::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange) retranslateUi();
    QWizardPage::changeEvent(e);
}

void ResultPage::initializePage() {
    const DiscProfile& p = static_cast<CalibrationWizard*>(wizard())->calibratedProfile();
    m_lblTr0->setText(QString::number(p.tr0, 'g', 10));
    m_lblDtr->setText(QString::number(p.dtr, 'g', 10));
    m_lblR0->setText(QString::number(p.r0,  'f', 2));
    m_lblMediaType->setText(kMediaNames.value(p.mediaType, tr("Unknown")));
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
