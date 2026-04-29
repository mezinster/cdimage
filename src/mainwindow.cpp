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
    m_profileDb.reset(new ProfileDatabase(QString(), this));
    m_detector.reset(new DiscDetector(m_backend.data(), m_profileDb.data(), this));

    connect(actionLoad_image,   &QAction::triggered, this, &MainWindow::loadImage);
    connect(actionCreate_track, &QAction::triggered, this, &MainWindow::createTrack);
    connect(actionAbout,        &QAction::triggered, this, &MainWindow::about);

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
