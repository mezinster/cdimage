// A tool for burning visible pictures on a compact disc surfase
// Copyright (C) 2008-2022 arduinocelentano

//This program is free software: you can redistribute it and/or modify it under the terms
//of the GNU General Public License as published by the Free Software Foundation,
//either version 3 of the License, or (at your option) any later version.

//This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
//without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//See the GNU General Public License for more details.

//You should have received a copy of the GNU General Public License along with this program.
//If not, see <https://www.gnu.org/licenses/>.

//Converter does the actual job to generate Audio CD track from the image.

//The implementation of this converter if mostly based on an opensource
//project created by a person with username [undefer]. Unfortunately I can't
//find her or his github or whatever repository.
//I acknowledge and am grateful to this developer for their contributions.

#include <math.h>

#include "converter.h"
#include <QtEndian>
#include <QDebug>

Converter::Converter(QObject *parent)
 : QObject(parent)
{
#define D 4
	m_tr0=22951.52052;
	m_dtr=1.3865961805;
	m_r0=24.5;
	nh=28*D-1;
	pinf=0;
	c=0;
#undef D
}

Converter::Converter(QObject *parent, double tr0, double dtr, double r0)
	:QObject(parent), m_tr0(tr0), m_dtr(dtr), m_r0(r0)
{
#define D 4
	nh=28*D-1;
	pinf=0;
	c=0;
#undef D
}

Converter::Converter(QObject *parent, const DiscProfile& profile)
	:QObject(parent), m_tr0(profile.tr0), m_dtr(profile.dtr), m_r0(profile.r0),
	 m_mixColors(profile.mixColors)
{
#define D 4
	nh=28*D-1;
	pinf=0;
	c=0;
#undef D
}

Converter::~Converter()
{
}


bool Converter::convert(const QImage img, const QString& filename, qint64 maxAudioBytes)
{
	qInfo() << "Converter::convert begin file=" << filename
	        << "img=" << img.width() << "x" << img.height()
	        << "tr0=" << m_tr0 << "dtr=" << m_dtr << "r0=" << m_r0
	        << "maxAudioBytes=" << maxAudioBytes;
	const char pallete[]={'\x10','\x21','\x28','\xAA'};
	double tr = m_tr0;
	double r = m_r0;
	double dr = m_dtr * m_r0 / m_tr0;
	const qint64 all = maxAudioBytes;
    m_canceled = false;
	double c=0;
	int itr;
	double ri;
	double ir = 1500;
	double alpha, xi, yi;
	double rcd=57.5;
	double cx = 1500;
	double cy = 1500;
	int zs=0;
	int zf=0;
	int ic=0;
	unsigned char color = 0, cl = 0, c1 = 0 , c2 = 0 ;
	const int imgW = img.width();
	const int imgH = img.height();
	QFile imageFile(filename);
	imageFile.remove();
	if (!imageFile.open(QIODevice::WriteOnly)) {
		qWarning() << "Converter::convert: cannot open" << filename;
		return false;
	}
	m_audioBytesWritten = 0;
	writeWavHeader(&imageFile, 0);  // placeholder; patched at end
	while (c<(all-tr))
	{
        if (m_canceled){
            imageFile.remove();
            return false;
        }
        emit progressChanged(100*c/all);
		itr = int(tr);
		ri=ir*r/rcd;
		for (long long i=0; i<itr; i++)
		{
			alpha=2*M_PI*i/itr;
			xi=cx+ri*cos(alpha);
			yi=cy+ri*sin(alpha);
			// Once r exceeds rcd the spiral walks outside the source image. Pre-
			// existing code called QImage::pixel() unconditionally, which is UB
			// past the image bounds and on Windows lands as an access violation
			// inside ucrtbase.dll's heap arithmetic. Treat out-of-bounds as black.
			const int xPix = static_cast<int>(xi);
			const int yPix = static_cast<int>(yi);
			if (xPix >= 0 && xPix < imgW && yPix >= 0 && yPix < imgH)
				color = qGray(img.pixel(xPix, yPix));
			else
				color = 0;
			c1=color/85;
			c2=c1+1;
			//ad (pallete[cl], imagefile);
			//imageFile.write(&pallete[cl],1);
			
			if(m_mixColors)
				cl = ( ((rand()*85/RAND_MAX)<(color%85)) || ((color%85)==84))? c2: c1;
			else
				cl=( ( (color%85)>(zs*5+zf) ) || ((color%85)==84))?c2:c1;
			ad(pallete[cl],&imageFile);
			if ((++zf)>=5)
				zf=0;
		}
		c+=tr;
		ic+=itr;
		while ( ((int) c)>ic )
		{
			//ad (pallete[color/51], imagefile);
			//imageFile.write(&pallete[cl],1);
			ad(pallete[cl],&imageFile);
			ic++;
			if ((++zf)>=4) zf=0;
		}

		tr+=m_dtr;
		r+=dr;
		
		if ((++zs)>=17) zs=0;
	}
	// Flush + pad the partial sector to a 2352-byte CD audio sector boundary.
	// IMAPI 2's IDiscFormat2TrackAtOnce::AddAudioTrack rejects audio streams
	// whose length isn't a multiple of one audio sector, returning
	// 0xC0AA050D (IMAPI2_E_AUDIO_TRACK_BROKEN_TRANSFER). The original
	// cdrtools-based path tolerated unaligned data because cdrecord pads
	// internally; native IMAPI does not. Pad with silence (zeros) — that
	// region is past the disc data area anyway, so the audio waveform
	// difference is invisible on the surface.
	// Note: local `c` is the spiral accumulator (double); `this->c` is the
	// buffer fill count (int class member).
	if (this->c > 0) {
		imageFile.write(buffer, this->c);
		const int padBytes = 2352 - this->c;
		static const char zeros[2352] = {0};
		imageFile.write(zeros, padBytes);
		m_audioBytesWritten += this->c + padBytes;
		this->c = 0;
	}
	// Patch RIFF size and data size now that we know the audio length.
	const quint32 dataBytes = static_cast<quint32>(m_audioBytesWritten);
	const quint32 riffSize  = dataBytes + 36;  // 44-byte header minus the 8 bytes ('RIFF' + size)
	imageFile.seek(4);
	quint32 le = qToLittleEndian(riffSize);
	imageFile.write(reinterpret_cast<const char*>(&le), 4);
	imageFile.seek(40);
	le = qToLittleEndian(dataBytes);
	imageFile.write(reinterpret_cast<const char*>(&le), 4);
	imageFile.close();
	qInfo() << "Converter::convert end audioBytes=" << m_audioBytesWritten;
    return true;
}

void Converter::ad (char b, QFile *file)
{
	intseq[n2m(delays[pinf++])]=b;
	if (pinf>=24)
	{
		pinf=0;
		nh++;
		if (nh>=28*4) nh=0;
		for (int i=0; i<24; i++)
		{
			bw(intseq[n2m(i)], file);
		}
	}
}

inline int Converter::n2m(int n)
{
	if ((nh*24+n)>=(28*4*24))
		return nh*24+n-28*4*24;
	else if ((nh*24+n)<0)
		return nh*24+n+28*4*24;
	else return nh*24+n;
}

void Converter::bw (char b, QFile *file)
{
	buffer[c++]=b;
	if (c>=2352)
	{
		if (file->write(buffer, 2352)==-1)
		{
			qCritical ("Converter: Cannot write data to file");
			return;
		}
		m_audioBytesWritten += 2352;
		c=0;
	}
}

void Converter::cancelConverting()
{
    m_canceled = true;
}

void Converter::writeWavHeader(QFile* file, quint32 dataBytes)
{
    const quint32 sampleRate  = 44100;
    const quint16 channels    = 2;
    const quint16 bitsSample  = 16;
    const quint32 byteRate    = sampleRate * channels * (bitsSample / 8);
    const quint16 blockAlign  = channels * (bitsSample / 8);
    const quint32 fmtChunkLen = 16;
    const quint32 riffSize    = dataBytes + 36;

    auto put32 = [&](quint32 v) {
        v = qToLittleEndian(v);
        file->write(reinterpret_cast<const char*>(&v), 4);
    };
    auto put16 = [&](quint16 v) {
        v = qToLittleEndian(v);
        file->write(reinterpret_cast<const char*>(&v), 2);
    };

    file->write("RIFF", 4);
    put32(riffSize);
    file->write("WAVE", 4);
    file->write("fmt ", 4);
    put32(fmtChunkLen);
    put16(1);             // PCM
    put16(channels);
    put32(sampleRate);
    put32(byteRate);
    put16(blockAlign);
    put16(bitsSample);
    file->write("data", 4);
    put32(dataBytes);
}
