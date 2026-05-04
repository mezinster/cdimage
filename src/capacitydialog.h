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

    // Safety margin subtracted from any nominal capacity before handing the
    // value to the burner. IMAPI's get_FreeSectorsOnMedia and the user-picked
    // 74/80/90-minute presets both report the disc's *spec* capacity, but
    // drive firmware reserves additional sectors for lead-out and gaps. A
    // 2-second cushion (150 sectors) avoids "fits the spec but not the
    // drive" overburn failures on strict recorders. See discussion in
    // resolveAudioCapacityBytes (calibrationwizard.cpp).
    static constexpr qint64 kSafetyMarginSectors = 150;

    // Common capacities, in bytes, after subtracting kSafetyMarginSectors.
    // Public so other code (e.g. defaults) can reference the same constants.
    static constexpr qint64 kBytes74min = (qint64(333000) - kSafetyMarginSectors) * 2352;  // 782,863,200
    static constexpr qint64 kBytes80min = (qint64(360000) - kSafetyMarginSectors) * 2352;  // 847,087,200
    static constexpr qint64 kBytes90min = (qint64(405000) - kSafetyMarginSectors) * 2352;  // 952,207,200

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
