#include "test_converter_wav.h"
#include "../src/converter.h"
#include "../src/discprofile.h"
#include <QTest>
#include <QTemporaryFile>
#include <QImage>
#include <QFile>
#include <QtEndian>

static QImage makeBlack3000() {
    QImage img(3000, 3000, QImage::Format_RGB32);
    img.fill(Qt::black);
    return img;
}

static QByteArray readBytes(const QString& path, qint64 n) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.read(n);
}

void TestConverterWav::output_starts_with_riff_wave_fmt_header() {
    if (qEnvironmentVariableIsSet("CDIMAGE_SKIP_LONG"))
        QSKIP("long-running converter test skipped");

    QTemporaryFile tmp("cdimage_wav_XXXXXX.wav");
    tmp.open();
    const QString path = tmp.fileName();
    tmp.close();

    DiscProfile profile;
    Converter conv(nullptr, profile);
    QVERIFY(conv.convert(makeBlack3000(), path));

    const QByteArray head = readBytes(path, 44);
    QCOMPARE(head.left(4),       QByteArray("RIFF"));
    QCOMPARE(head.mid(8, 4),     QByteArray("WAVE"));
    QCOMPARE(head.mid(12, 4),    QByteArray("fmt "));
    QCOMPARE(head.mid(36, 4),    QByteArray("data"));
}

void TestConverterWav::header_declares_44100hz_stereo_16bit() {
    if (qEnvironmentVariableIsSet("CDIMAGE_SKIP_LONG"))
        QSKIP("long-running converter test skipped");

    QTemporaryFile tmp("cdimage_wav_XXXXXX.wav");
    tmp.open();
    const QString path = tmp.fileName();
    tmp.close();

    DiscProfile profile;
    Converter conv(nullptr, profile);
    QVERIFY(conv.convert(makeBlack3000(), path));

    const QByteArray head = readBytes(path, 44);
    auto u16 = [&](int off){ return qFromLittleEndian<quint16>(head.constData() + off); };
    auto u32 = [&](int off){ return qFromLittleEndian<quint32>(head.constData() + off); };

    QCOMPARE(u16(20), quint16(1));         // PCM
    QCOMPARE(u16(22), quint16(2));         // channels
    QCOMPARE(u32(24), quint32(44100));     // sample rate
    QCOMPARE(u32(28), quint32(44100 * 4)); // byte rate
    QCOMPARE(u16(32), quint16(4));         // block align
    QCOMPARE(u16(34), quint16(16));        // bits per sample
}

void TestConverterWav::data_size_matches_actual_audio_bytes() {
    if (qEnvironmentVariableIsSet("CDIMAGE_SKIP_LONG"))
        QSKIP("long-running converter test skipped");

    QTemporaryFile tmp("cdimage_wav_XXXXXX.wav");
    tmp.open();
    const QString path = tmp.fileName();
    tmp.close();

    DiscProfile profile;
    Converter conv(nullptr, profile);
    QVERIFY(conv.convert(makeBlack3000(), path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const qint64 fileSize = f.size();

    f.seek(4);
    quint32 riffSize = 0;
    f.read(reinterpret_cast<char*>(&riffSize), 4);
    riffSize = qFromLittleEndian(riffSize);
    QCOMPARE(qint64(riffSize), fileSize - 8);

    f.seek(40);
    quint32 dataSize = 0;
    f.read(reinterpret_cast<char*>(&dataSize), 4);
    dataSize = qFromLittleEndian(dataSize);
    QCOMPARE(qint64(dataSize), fileSize - 44);
}
