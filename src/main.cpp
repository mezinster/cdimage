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

//main

#include "mainwindow.h"
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

static QFile  g_logFile;
static QMutex g_logMutex;

static void cdimageMessageHandler(QtMsgType type,
                                  const QMessageLogContext& /*ctx*/,
                                  const QString& msg) {
	QMutexLocker lock(&g_logMutex);
	if (!g_logFile.isOpen()) return;
	const char* level = "?";
	switch (type) {
	case QtDebugMsg:    level = "DEBUG"; break;
	case QtInfoMsg:     level = "INFO";  break;
	case QtWarningMsg:  level = "WARN";  break;
	case QtCriticalMsg: level = "CRIT";  break;
	case QtFatalMsg:    level = "FATAL"; break;
	}
	QTextStream out(&g_logFile);
	out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
	    << " [" << level << "] " << msg << '\n';
	out.flush();
}

#ifdef Q_OS_WIN
static LONG WINAPI cdimageUnhandledFilter(EXCEPTION_POINTERS* ep) {
	QMutexLocker lock(&g_logMutex);
	if (g_logFile.isOpen()) {
		QTextStream out(&g_logFile);
		out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
		    << " [CRASH] code=0x"
		    << QString::number(ep->ExceptionRecord->ExceptionCode, 16).toUpper()
		    << " addr=0x"
		    << QString::number(reinterpret_cast<quintptr>(
		           ep->ExceptionRecord->ExceptionAddress), 16).toUpper()
		    << '\n';
		out.flush();
		g_logFile.close();
	}
	// Let WER take over so a minidump still gets written.
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main ( int argc, char** argv )
{
	QApplication app ( argc, argv );
	QCoreApplication::setOrganizationName("CDImage");
	QCoreApplication::setOrganizationDomain("cdimage.local");
	QCoreApplication::setApplicationName("CDImage");

	// Persistent log so silent crashes leave a trail next to AppData.
	const QString logDir = QStandardPaths::writableLocation(
	                           QStandardPaths::AppDataLocation);
	QDir().mkpath(logDir);
	g_logFile.setFileName(logDir + "/cdimage.log");
	g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
	qInstallMessageHandler(cdimageMessageHandler);
	qInfo() << "==================================================";
	qInfo() << "cdimage starting; log:" << g_logFile.fileName();
#ifdef Q_OS_WIN
	SetUnhandledExceptionFilter(cdimageUnhandledFilter);
#endif

	MainWindow win;
	win.show();
	const int code = app.exec();
	qInfo() << "cdimage exiting with code" << code;
	return code;
}
