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

// Audio capacity of an inserted CD-R/RW. Bytes, not sectors. 0 means
// "unknown/unsupported" — caller should fall back to a user prompt
// rather than guess. CD audio sector size is 2352 bytes, so usable
// audio bytes for the standard sizes are:
//   74 min (333,000 sectors): 783,216,000 bytes
//   80 min (360,000 sectors): 847,440,000 bytes
//   90 min (405,000 sectors): 952,560,000 bytes (overburn)
struct DiscCapacity {
    qint64 freeBytes  = 0;  // bytes available for a new audio track
    qint64 totalBytes = 0;  // total addressable audio bytes on the medium
};

class IDiscBackend {
public:
    virtual ~IDiscBackend() = default;

    virtual QStringList     availableDevices()                                        = 0;
    virtual RawDiscInfo     queryDisc(const QString& devicePath)                      = 0;
    virtual DiscCapacity    queryCapacity(const QString& devicePath)                  = 0;
    virtual BurnResult      burnTestPattern(const QString& devicePath,
                                            const QString& trackFile)                 = 0;
    virtual QVector<qint64> measureSeekTimes(const QString& devicePath,
                                             const QVector<qint64>& sectors)          = 0;
};

Q_DECLARE_METATYPE(RawDiscInfo)
Q_DECLARE_METATYPE(DiscProfile)

IDiscBackend* createDiscBackend();

#endif
