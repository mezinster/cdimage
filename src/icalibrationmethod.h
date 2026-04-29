#ifndef ICALIBRATIONMETHOD_H
#define ICALIBRATIONMETHOD_H

#include "discprofile.h"
#include "idiscbackend.h"
#include <QObject>

class ICalibrationMethod : public QObject {
    Q_OBJECT
public:
    explicit ICalibrationMethod(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ICalibrationMethod() = default;
    virtual void start(const RawDiscInfo& disc) = 0;

signals:
    void progressChanged(int pct);
    void finished(DiscProfile result);
    void failed(QString error);
};

#endif
