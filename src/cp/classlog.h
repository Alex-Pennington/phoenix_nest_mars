/****************************************************************************
**
** Copyright (C) 2016 - 2024 Timothy Millea <timothy.j.millea@gmail.com>
**
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************************/

#ifndef CLASSLOG_H
#define CLASSLOG_H

#include <QObject>
#include <QFile>
#include <QDir>
#include <QSize>
#include <QPoint>
#include <QMap>
#include <QApplication>
#include <QDateTime>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QTextStream>

class ClassLog : public QObject
{
    Q_OBJECT
    void purgeLogFiles();
    QDir logDir = QDir(QString(QApplication::applicationDirPath()+"/LOGFILES/"));
    QString logFileName = QString(logDir.absolutePath()+"/"+QApplication::applicationName()+
                                  QDateTime::currentDateTimeUtc().toString("_yyyy-MM-dd.log"));

    QFile currentLogFile;

public:
    explicit ClassLog(QObject *parent = nullptr);

signals:
    void signalUpdateUILog(QString logData, bool isDebug);

public slots:
    void write(QString logData, bool isDebug);
private:
    void deleteFile(QFileInfo fileInfo);
};

#endif // CLASSLOG_H
