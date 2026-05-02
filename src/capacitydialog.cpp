#include "capacitydialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QLabel>

CapacityDialog::CapacityDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Select CD-R Capacity"));
    setModal(true);

    auto* lbl = new QLabel(tr(
        "<p>Could not detect the inserted disc's capacity automatically.</p>"
        "<p>Select the size of your CD-R so the audio track can be sized to fit:</p>"), this);
    lbl->setWordWrap(true);

    m_rb74 = new QRadioButton(tr("74 minutes (650 MB) — older media"), this);
    m_rb80 = new QRadioButton(tr("80 minutes (700 MB) — most common"), this);
    m_rb90 = new QRadioButton(tr("90 minutes (800 MB) — overburn / rare"), this);
    m_rbCustom = new QRadioButton(tr("Custom:"), this);
    m_rb80->setChecked(true);  // safest default

    m_customMin = new QSpinBox(this);
    m_customMin->setRange(20, 99);
    m_customMin->setValue(80);
    m_customMin->setSuffix(tr(" min"));
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
    layout->addWidget(lbl);
    layout->addWidget(m_rb74);
    layout->addWidget(m_rb80);
    layout->addWidget(m_rb90);
    layout->addLayout(customRow);
    layout->addWidget(buttons);
}

qint64 CapacityDialog::selectedBytes() const {
    if (m_rb74->isChecked()) return kBytes74min;
    if (m_rb80->isChecked()) return kBytes80min;
    if (m_rb90->isChecked()) return kBytes90min;
    // Custom: minutes -> sectors -> bytes. Sectors per minute = 60*75 = 4500.
    const qint64 sectors = qint64(m_customMin->value()) * 4500;
    return sectors * 2352;
}
