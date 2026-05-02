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
