#include "createtrackdialog.h"
#include <QFileDialog>

CreateTrackDialog::CreateTrackDialog(ProfileDatabase* db, QWidget* parent)
    : QDialog(parent), m_db(db)
{
    setupUi(this);
    for (const DiscProfile& p : m_db->allProfiles())
        cbPresets->addItem(p.name);
    if (cbPresets->count() > 0) loadPreset(0);
    connect(tbBrowse, &QAbstractButton::clicked,
            this, &CreateTrackDialog::selectFile);
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
    const QList<DiscProfile> profiles = m_db->allProfiles();
    const int idx = cbPresets->currentIndex();
    DiscProfile p = (idx >= 0 && idx < profiles.size()) ? profiles.at(idx) : DiscProfile{};
    // Allow manual overrides of geometry fields
    p.tr0       = leTr0->text().toDouble();
    p.dtr       = leDtr->text().toDouble();
    p.r0        = leR0->text().toDouble();
    p.mixColors = cbMixColors->isChecked();
    return p;
}
