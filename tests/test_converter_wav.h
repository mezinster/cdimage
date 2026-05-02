#ifndef TEST_CONVERTER_WAV_H
#define TEST_CONVERTER_WAV_H
#include <QObject>
class TestConverterWav : public QObject {
    Q_OBJECT
private slots:
    void output_starts_with_riff_wave_fmt_header();
    void header_declares_44100hz_stereo_16bit();
    void data_size_matches_actual_audio_bytes();
};
#endif
