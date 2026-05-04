#include "capacitydialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QEvent>

CapacityDialog::CapacityDialog(QWidget* parent) : QDialog(parent) {
    setModal(true);

    m_lblIntro = new QLabel(this);
    m_lblIntro->setWordWrap(true);

    m_rb74 = new QRadioButton(this);
    m_rb80 = new QRadioButton(this);
    m_rb90 = new QRadioButton(this);
    m_rbCustom = new QRadioButton(this);
    m_rb80->setChecked(true);  // safest default

    m_customMin = new QSpinBox(this);
    m_customMin->setRange(20, 99);
    m_customMin->setValue(80);
    m_customMin->setEnabled(false);
    connect(m_rbCustom, &QRadioButton::toggled, m_customMin, &QSpinBox::setEnabled);

    auto* group = new QButtonGroup(this);
    group->addButton(m_rb74);
    group->addButton(m_rb80);
    group->addButton(m_rb90);
    group->addButton(m_rbCustom);

    auto* customRow = new QHBoxLayout;
    customRow->addWidget(m_rbCustom);
    customRow->addWidget(m_customMin);
    customRow->addStretch();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_lblIntro);
    layout->addWidget(m_rb74);
    layout->addWidget(m_rb80);
    layout->addWidget(m_rb90);
    layout->addLayout(customRow);
    layout->addWidget(buttons);

    retranslateUi();
}

void CapacityDialog::retranslateUi() {
    setWindowTitle(tr("Select CD-R Capacity"));
    m_lblIntro->setText(tr(
        "<p>Could not detect the inserted disc's capacity automatically.</p>"
        "<p>Select the size of your CD-R so the audio track can be sized to fit:</p>"));
    m_rb74->setText(tr("74 minutes (650 MB) — older media"));
    m_rb80->setText(tr("80 minutes (700 MB) — most common"));
    m_rb90->setText(tr("90 minutes (800 MB) — overburn / rare"));
    m_rbCustom->setText(tr("Custom:"));
    m_customMin->setSuffix(tr(" min"));
}

void CapacityDialog::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange) retranslateUi();
    QDialog::changeEvent(e);
}

qint64 CapacityDialog::selectedBytes() const {
    if (m_rb74->isChecked()) return kBytes74min;
    if (m_rb80->isChecked()) return kBytes80min;
    if (m_rb90->isChecked()) return kBytes90min;
    // Custom: minutes -> sectors -> bytes. Sectors per minute = 60*75 = 4500.
    // Apply the same safety margin as the presets so a custom 80-min entry
    // matches the 80-min radio button.
    const qint64 sectors = qint64(m_customMin->value()) * 4500 - kSafetyMarginSectors;
    return std::max<qint64>(0, sectors) * 2352;
}
