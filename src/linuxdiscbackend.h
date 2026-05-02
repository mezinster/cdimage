#ifndef LINUXDISCBACKEND_H
#define LINUXDISCBACKEND_H

#include "idiscbackend.h"

class LinuxDiscBackend : public IDiscBackend {
public:
    QStringList     availableDevices()                                        override;
    RawDiscInfo     queryDisc(const QString& devicePath)                      override;
    BurnResult      burnTestPattern(const QString& devicePath,
                                    const QString& trackFile)                 override;
    QVector<qint64> measureSeekTimes(const QString& devicePath,
                                     const QVector<qint64>& sectors)          override;
private:
    RawDiscInfo readDiscInfo(int fd);
    RawDiscInfo readAtip(int fd);
    MediaType   mediaTypeFromDiscTypeByte(quint8 b);
    bool        sendCommand(int fd, unsigned char* cdb, int cdbLen,
                            unsigned char* buf, int bufLen);
};

#endif
