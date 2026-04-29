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
    auto* btn = new QPushButton(tr("Burn Test Pattern"), this);
    connect(btn, &QPushButton::clicked, this, &BurnPatternPage::doBurn);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_progress);
    layout->addWidget(btn);
}

void BurnPatternPage::initializePage() { m_done = false; emit completeChanged(); }
bool BurnPatternPage::isComplete() const { return m_done; }

void BurnPatternPage::doBurn() {
    m_progress->setVisible(true);
    m_status->setText(tr("Generating test track (this may take several minutes)…"));
    qApp->processEvents();

    const QString device = wizard()->property("devicePath").toString();
    const QString outPath = QDir::tempPath() + "/cdimage_testpattern.cdr";

    DiscProfile defaultProfile;
    if (TestPatternGenerator::generateRingsTrack(defaultProfile, outPath).isEmpty()) {
        m_status->setText(tr("Failed to generate test track."));
        m_progress->setVisible(false);
        return;
    }

    m_status->setText(tr("Burning test pattern to disc…"));
    qApp->processEvents();

    if (m_backend->burnTestPattern(device, outPath)) {
        m_status->setText(tr("Test pattern burned. Remove the disc and proceed."));
        m_done = true;
        emit completeChanged();
    } else {
        m_status->setText(tr("Burn failed. Check that cdrecord is installed and "
                             "the device path is correct."));
    }
    m_progress->setVisible(false);
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

    connect(wizard(), &QWizard::accepted, this, [this, p = DiscProfile(p)]() mutable {
        p.name = m_leName->text();
        m_db->saveUserProfile(p);
    }, Qt::UniqueConnection);
}
