#include "calibrationwizard.h"
#include "photocalibration.h"
#include "drivereadbackcalibration.h"
#include "testpatterngenerator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QPushButton>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QtConcurrent/QtConcurrent>

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
    : QWizardPage(parent)
{
    setTitle(tr("Disc Calibration"));
    auto* lbl = new QLabel(tr(
        "<p>Detected disc: <b>%1</b> (ID: %2)</p>"
        "<p>This wizard burns a test pattern onto the disc, then measures the disc "
        "geometry either from a photograph or by re-reading the disc with the drive.</p>")
        .arg(kMediaNames.value(disc.mediaType, "Unknown"))
        .arg(disc.discId.isEmpty() ? tr("unknown") : disc.discId), this);
    lbl->setWordWrap(true);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(lbl);
}

// ── BurnPatternPage ───────────────────────────────────────────────────────────

BurnPatternPage::BurnPatternPage(IDiscBackend* backend, const RawDiscInfo& disc,
                                  QWidget* parent)
    : QWizardPage(parent), m_backend(backend), m_disc(disc)
{
    setTitle(tr("Burn Test Pattern"));
    m_status   = new QLabel(tr("Click 'Burn' to write the test pattern to the disc."), this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);
    m_burnBtn = new QPushButton(tr("Burn Test Pattern"), this);
    connect(m_burnBtn, &QPushButton::clicked, this, &BurnPatternPage::doBurn);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_progress);
    layout->addWidget(m_burnBtn);
}

void BurnPatternPage::initializePage() { m_done = false; emit completeChanged(); }
bool BurnPatternPage::isComplete() const { return m_done; }

void BurnPatternPage::doBurn() {
    if (m_watcher) return;  // already running

    m_burnBtn->setEnabled(false);
    m_progress->setVisible(true);
    m_status->setText(tr("Generating test track + burning (this may take several minutes)…"));

    const QString device = wizard()->property("devicePath").toString();
    const QString outPath = QDir::tempPath() + "/cdimage_testpattern.wav";
    IDiscBackend* backend = m_backend;
    qInfo() << "BurnPatternPage::doBurn dispatch device=" << device << "wav=" << outPath;

    // Heavy work runs on a thread-pool worker so the GUI thread stays
    // responsive (no DWM "Not Responding" hang) and any C-runtime fault
    // during WAV generation propagates as an SEH on the worker rather than
    // killing the GUI thread inline.
    QFuture<BurnResult> fut = QtConcurrent::run([backend, device, outPath]() {
        BurnResult r;
        DiscProfile defaultProfile;
        qInfo() << "Worker: generating test pattern WAV";
        if (TestPatternGenerator::generateRingsTrack(defaultProfile, outPath).isEmpty()) {
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
    setTitle(tr("Choose Measurement Method"));
    m_rbPhoto    = new QRadioButton(tr("Photograph the disc"), this);
    m_rbReadback = new QRadioButton(tr("Let the drive re-read the disc"), this);
    m_rbPhoto->setChecked(true);
    registerField("usePhoto", m_rbPhoto);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_rbPhoto);
    layout->addWidget(new QLabel(tr("  Take a photo of the burned disc and upload it. "
                                    "Works without a functional drive reader."), this));
    layout->addSpacing(8);
    layout->addWidget(m_rbReadback);
    layout->addWidget(new QLabel(tr("  The drive seeks to known sectors and measures "
                                    "timing. Fully automated."), this));
}

int MethodSelectPage::nextId() const {
    return field("usePhoto").toBool() ? Page_Photo : Page_Readback;
}

// ── PhotoPage ─────────────────────────────────────────────────────────────────

PhotoPage::PhotoPage(QWidget* parent) : QWizardPage(parent) {
    setTitle(tr("Upload Disc Photo"));
    auto* lbl = new QLabel(tr("Place the burned disc on a dark background under even "
                               "lighting. Take a photo showing the full disc, then "
                               "select it below:"), this);
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
    row->addWidget(m_path);
    row->addWidget(btn);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(lbl);
    layout->addLayout(row);
}

bool PhotoPage::isComplete() const { return !m_path->text().isEmpty(); }

// ── ReadbackPage ──────────────────────────────────────────────────────────────

ReadbackPage::ReadbackPage(QWidget* parent) : QWizardPage(parent) {
    setTitle(tr("Drive Read-Back"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Re-insert the burned disc. The drive will seek "
                                    "to known sectors to measure timing automatically "
                                    "on the next page."), this));
}

void ReadbackPage::initializePage() { emit completeChanged(); }

// ── AnalysisPage ──────────────────────────────────────────────────────────────

AnalysisPage::AnalysisPage(IDiscBackend* backend, const RawDiscInfo& disc,
                            QWidget* parent)
    : QWizardPage(parent), m_backend(backend), m_disc(disc)
{
    setTitle(tr("Analysing…"));
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_status = new QLabel(this);
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
