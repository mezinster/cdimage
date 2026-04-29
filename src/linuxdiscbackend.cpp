#include "linuxdiscbackend.h"

#include <QDir>
#include <QProcess>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include <scsi/sg.h>
#include <time.h>
#include <cstring>
#include <stdexcept>

QStringList LinuxDiscBackend::availableDevices() {
    QStringList devices;
    const QDir dev("/dev");
    for (const QString& e : dev.entryList({"sr*", "scd*"}, QDir::System))
        devices << "/dev/" + e;
    return devices;
}

bool LinuxDiscBackend::sendCommand(int fd, unsigned char* cdb, int cdbLen,
                                   unsigned char* buf, int bufLen) {
    unsigned char sense[32] = {};
    sg_io_hdr_t io;
    std::memset(&io, 0, sizeof(io));
    io.interface_id    = 'S';
    io.dxfer_direction = SG_DXFER_FROM_DEV;
    io.cmdp            = cdb;
    io.cmd_len         = static_cast<unsigned char>(cdbLen);
    io.dxferp          = buf;
    io.dxfer_len       = static_cast<unsigned int>(bufLen);
    io.sbp             = sense;
    io.mx_sb_len       = sizeof(sense);
    io.timeout         = 5000;
    return ioctl(fd, SG_IO, &io) == 0 && io.status == 0;
}

RawDiscInfo LinuxDiscBackend::readDiscInfo(int fd) {
    // MMC-5: READ DISC INFORMATION (Standard, Data Type 000b)
    unsigned char cdb[10] = {0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 34, 0x00};
    unsigned char buf[34] = {};
    if (!sendCommand(fd, cdb, sizeof(cdb), buf, sizeof(buf)))
        throw std::runtime_error("READ DISC INFORMATION failed");
    RawDiscInfo info;
    info.mediaType = mediaTypeFromDiscTypeByte(buf[8]);
    return info;
}

RawDiscInfo LinuxDiscBackend::readAtip(int fd) {
    // MMC-5: READ TOC/PMA/ATIP, Format=4 (ATIP), MSF=1
    // Bytes [4..6] of the response contain the ATIP lead-in time
    // (MM:SS:FF) which encodes the disc manufacturer code per Orange Book.
    unsigned char cdb[10] = {0x43, 0x02, 0x04, 0, 0, 0, 0, 0, 28, 0};
    unsigned char buf[28] = {};
    if (!sendCommand(fd, cdb, sizeof(cdb), buf, sizeof(buf)))
        throw std::runtime_error("READ ATIP failed");
    const quint8 mm = buf[4] & 0x7F;
    const quint8 ss = buf[5];
    const quint8 ff = buf[6];
    RawDiscInfo info;
    info.discId = QString("%1m%2s%3f")
                  .arg(mm, 2, 10, QChar('0'))
                  .arg(ss, 2, 10, QChar('0'))
                  .arg(ff, 2, 10, QChar('0'));
    return info;
}

MediaType LinuxDiscBackend::mediaTypeFromDiscTypeByte(quint8 b) {
    // MMC-5 Table 404: Disc Type field codes
    switch (b) {
        case 0x00: return MediaType::CD_R;
        case 0x20: return MediaType::CD_R;
        case 0x21: return MediaType::CD_RW;
        case 0x12: return MediaType::DVD_R;
        case 0x13: return MediaType::DVD_RW;
        case 0x1A: return MediaType::DVD_RW;  // DVD+RW
        case 0x1B: return MediaType::DVD_R;   // DVD+R
        case 0x2B: return MediaType::DVD_DL;  // DVD+R DL
        default:   return MediaType::CD_R;
    }
}

RawDiscInfo LinuxDiscBackend::queryDisc(const QString& devicePath) {
    int fd = open(devicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        throw std::runtime_error("Cannot open device: " + devicePath.toStdString());

    RawDiscInfo info;
    try { info = readDiscInfo(fd); } catch (...) {}
    if (info.mediaType == MediaType::CD_R || info.mediaType == MediaType::CD_RW) {
        try {
            RawDiscInfo atip = readAtip(fd);
            info.discId = atip.discId;
        } catch (...) {}
    }
    close(fd);
    return info;
}

bool LinuxDiscBackend::burnTestPattern(const QString& devicePath,
                                       const QString& trackFile) {
    QProcess proc;
    proc.start("cdrecord", {"-audio",
                            QString("dev=%1").arg(devicePath),
                            trackFile});
    proc.waitForFinished(300000);
    return proc.exitCode() == 0;
}

QVector<qint64> LinuxDiscBackend::measureSeekTimes(const QString& devicePath,
                                                    const QVector<qint64>& sectors) {
    int fd = open(devicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        throw std::runtime_error("Cannot open device: " + devicePath.toStdString());

    QVector<qint64> times;
    times.reserve(sectors.size());
    for (qint64 sector : sectors) {
        const qint64 lba = sector + 150;
        struct cdrom_msf msf;
        msf.cdmsf_min0   = static_cast<quint8>(lba / 4500);
        msf.cdmsf_sec0   = static_cast<quint8>((lba % 4500) / 75);
        msf.cdmsf_frame0 = static_cast<quint8>(lba % 75);

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        ioctl(fd, CDROMSEEK, &msf);
        clock_gettime(CLOCK_MONOTONIC, &t1);

        times.append((t1.tv_sec - t0.tv_sec) * 1000000LL
                   + (t1.tv_nsec - t0.tv_nsec) / 1000LL);
    }
    close(fd);
    return times;
}

IDiscBackend* createDiscBackend() { return new LinuxDiscBackend(); }
