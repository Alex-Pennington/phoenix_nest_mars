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
#ifndef DIALOGLOG_H
#define DIALOGLOG_H

#include <globals.h>

#define DAYS_TO_PURGE 7

namespace Ui {
class DialogLog;
}

class DialogLog : public QDialog
{
    Q_OBJECT

public:
    explicit DialogLog(QWidget *parent = nullptr);
    ~DialogLog();

    QList <QFile *> logFileList;

protected:
    virtual void moveEvent(QMoveEvent *event);
    virtual void resizeEvent(QResizeEvent *event);

public slots:
    void slotUpdateBrowser(QString dataByte);
    void slotUpdateDebug(QString dataByte);
    void slotWriteModemLogFile(QString data);
    //    void slotWriteRadioLogFile(QString data);
    void slotWriteTCPLogFile(QString data);
    void slotWriteUDPLogFile(QByteArray data);
    void slotUpdateCriticalNotifications(QString data);

private slots:
    void on_checkBoxDisplayDebugInfo_clicked();
    void on_lineEditMaxLogSize_textChanged(const QString &arg1);

private:
    Ui::DialogLog *ui;

    Settings localSettings;

    QFile appLogFile,
        debugLogFile,
        modemLogFile,
        tcpLogFile,
        udpLogFile;

    void writeApplicationLogFile(const QString& data);
    void writeDebugLogFile(QString data);
    void purgeOldLogs();
    void rotateLogs();
    void updateLogFiles(const QString &message);
//    void createLogFiles(const QStringList &logFileNames);
//    void initializeLogFiles(const QString &logDirPath, QVector<QFile *> &logFileList);
//    void createLogDirectory(const QString &path);
    void initializeLogFiles();
    void openAndInitializeLogFile(QFile &logFile, const QString &logType);
    void createLogFolder();
    void setLogFileNames();
    void appendLogFilesToList();
    void updateUI();
    void writeLogFile(QFile &logFile, const QString &data);
    void rotateLogWhenNameCollision(QString newLogFileName, QString timestamp, QString logFileName);
    void purgeLogFile(const QFileInfo &fileInfo);
};

#endif // DIALOGLOG_H
