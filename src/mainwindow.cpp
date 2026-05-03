// A tool for burning visible pictures on a compact disc surfase
// Copyright (C) 2008-2022 arduinocelentano

//This program is free software: you can redistribute it and/or modify it under the terms
//of the GNU General Public License as published by the Free Software Foundation,
//either version 3 of the License, or (at your option) any later version.

//This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
//without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//See the GNU General Public License for more details.

//You should have received a copy of the GNU General Public License along with this program.
//If not, see <https://www.gnu.org/licenses/>.

//main window class

#include <QFileDialog>
#include <QProgressDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QEvent>

#include "mainwindow.h"
#include "converter.h"
#include "createtrackdialog.h"
#include "calibrationwizard.h"
#include "capacitydialog.h"
#include "i18n.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi(this);
    setCentralWidget(&centralView);
    m_path = QDir::currentPath();

    m_backend.reset(createDiscBackend());
    m_profileDb.reset(new ProfileDatabase(QString(), this));
    m_detector.reset(new DiscDetector(m_backend.data(), m_profileDb.data(), this));

    connect(actionLoad_image,   &QAction::triggered, this, &MainWindow::loadImage);
    connect(actionCreate_track, &QAction::triggered, this, &MainWindow::createTrack);
    connect(actionAbout,        &QAction::triggered, this, &MainWindow::about);

    m_actionDetect = new QAction(this);
    menuEdit->addAction(m_actionDetect);
    connect(m_actionDetect, &QAction::triggered, this, &MainWindow::detectDisc);

    buildLanguageMenu();
    retranslateDynamic();

    connect(m_detector.data(), &DiscDetector::profileFound,
            this, &MainWindow::onProfileFound);
    connect(m_detector.data(), &DiscDetector::profileNotFound,
            this, &MainWindow::onProfileNotFound);
    connect(m_detector.data(), &DiscDetector::detectionFailed,
            this, &MainWindow::onDetectionFailed);
}

void MainWindow::buildLanguageMenu() {
    m_menuLanguage = menubar->addMenu(QString());   // title set in retranslateDynamic
    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);

    const QString active = I18n::currentLocale();
    for (const QString& code : I18n::availableLocales()) {
        QAction* a = m_menuLanguage->addAction(I18n::displayName(code));
        a->setCheckable(true);
        a->setData(code);
        if (code == active) a->setChecked(true);
        m_languageGroup->addAction(a);
        connect(a, &QAction::triggered, this, [code]() {
            I18n::switchTo(code);
        });
    }
}

void MainWindow::retranslateDynamic() {
    if (m_actionDetect) m_actionDetect->setText(tr("Detect disc geometry"));
    if (m_menuLanguage) m_menuLanguage->setTitle(tr("&Language"));
}

void MainWindow::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange) {
        retranslateUi(this);          // .ui-derived strings
        retranslateDynamic();         // hand-built strings
    }
    QMainWindow::changeEvent(e);
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
    CreateTrackDialog dial(m_profileDb.data(), m_currentProfile, this);
    if (!dial.exec()) return;

    const DiscProfile profile = dial.selectedProfile();

    // Image-track creation doesn't know which drive will burn, so always
    // ask the user for capacity (no auto-detect path here). The resulting
    // WAV is sized to fit the chosen disc.
    CapacityDialog capDlg(this);
    if (capDlg.exec() != QDialog::Accepted) return;
    const qint64 capacityBytes = capDlg.selectedBytes();

    Converter converter(this, profile);

    QProgressDialog progress(tr("Generating track…"), tr("Abort"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    connect(&converter, &Converter::progressChanged, &progress, &QProgressDialog::setValue);
    connect(&progress,  &QProgressDialog::canceled,  &converter, &Converter::cancelConverting);

    if (converter.convert(m_image, dial.leFileName->text(), capacityBytes))
        QMessageBox::information(this, tr("Success"),
            tr("Track created as a standard audio WAV file:<br><b>%1</b><br><br>"
               "Burn it as an <b>Audio CD</b> with any burning software "
               "(Nero, ImgBurn, Windows Media Player, or "
               "<code>cdrecord -audio dev=&lt;device&gt; %1</code>).")
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
