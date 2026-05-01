#ifndef IDISCBACKEND_H
#define IDISCBACKEND_H

#include "discprofile.h"
#include <QMetaType>
#include <QStringList>
#include <QVector>
#include <stdexcept>

struct RawDiscInfo {
    QString   discId;
    MediaType mediaType = MediaType::CD_RW;
};

struct BurnResult {
    bool    started   = false;
    bool    finished  = false;
    int     exitCode  = -1;
    QString errorMessage;
    QString stderrText;

    bool succeeded() const { return started && finished && exitCode == 0; }
};

class IDiscBackend {
public:
    virtual ~IDiscBackend() = default;

    virtual QStringList     availableDevices()                                        = 0;
    virtual RawDiscInfo     queryDisc(const QString& devicePath)                      = 0;
    virtual BurnResult      burnTestPattern(const QString& devicePath,
                                            const QString& trackFile)                 = 0;
    virtual QVector<qint64> measureSeekTimes(const QString& devicePath,
                                             const QVector<qint64>& sectors)          = 0;
};

Q_DECLARE_METATYPE(RawDiscInfo)
Q_DECLARE_METATYPE(DiscProfile)

IDiscBackend* createDiscBackend();

#endif
