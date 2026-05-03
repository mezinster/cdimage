#ifndef CAPACITYDIALOG_H
#define CAPACITYDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>

// Modal prompt for the user to pick the inserted CD-R's capacity when the
// backend can't determine it automatically (Linux always; Windows when
// IMAPI returns 0 free sectors). The result is in bytes, already aligned
// to 2352-byte CD audio sectors.
class CapacityDialog : public QDialog {
    Q_OBJECT
public:
    explicit CapacityDialog(QWidget* parent = nullptr);
    qint64 selectedBytes() const;

    // Common capacities, in bytes. Public so other code (e.g. defaults) can
    // reference the same constants.
    static constexpr qint64 kBytes74min = qint64(333000) * 2352;  // 783,216,000
    static constexpr qint64 kBytes80min = qint64(360000) * 2352;  // 847,440,000
    static constexpr qint64 kBytes90min = qint64(405000) * 2352;  // 952,560,000

protected:
    void changeEvent(QEvent* e) override;

private:
    void retranslateUi();

    QLabel*       m_lblIntro;
    QRadioButton* m_rb74;
    QRadioButton* m_rb80;
    QRadioButton* m_rb90;
    QRadioButton* m_rbCustom;
    QSpinBox*     m_customMin;  // user enters minutes; we convert to bytes
};

#endif
