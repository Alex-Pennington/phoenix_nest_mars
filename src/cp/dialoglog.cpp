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

#include "dialoglog.h"
#include "ui_dialoglog.h"

const QString LOG_FOLDER = "/LOGFILES";
const QString LOG_EXTENSION = ".log";

void DialogLog::initializeLogFiles()
{
    openAndInitializeLogFile(appLogFile, "Application");
    openAndInitializeLogFile(debugLogFile, "Debug");
    openAndInitializeLogFile(modemLogFile, "Modem");
    openAndInitializeLogFile(tcpLogFile, "TCP");
    openAndInitializeLogFile(udpLogFile, "UDP");
}//initializeLogFiles

void DialogLog::openAndInitializeLogFile(QFile& logFile, const QString& logType)
{
    if (!logFile.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&logFile);
    out << "**************************************************************************************************\n"
        << QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ")
        << logType + " started...\n";

    logFile.flush();
    logFile.close();
    slotUpdateBrowser("Created log file: " + logFile.fileName());
}//openAndInitializeLogFile

void DialogLog::createLogFolder()
{
    QDir logDir(QApplication::applicationDirPath() + LOG_FOLDER);
    if (!logDir.exists()) {
        if (!logDir.mkpath(QApplication::applicationDirPath() + LOG_FOLDER)) {
            QString errorMessage = "ERROR: Log file path error - " + logDir.currentPath();
            qDebug() << errorMessage;
            slotUpdateBrowser(errorMessage);
        } else {
            slotUpdateBrowser("Created LOGFILES folder... OK");
        }
    } else {
        slotUpdateBrowser("Found existing LOGFILES folder... OK");
    }
}//createLogFolder

void DialogLog::setLogFileNames()
{
    QString baseLogPath = QApplication::applicationDirPath() + LOG_FOLDER;
    appLogFile.setFileName(baseLogPath + "/logApplication" + LOG_EXTENSION);
    debugLogFile.setFileName(baseLogPath + "/logDebug" + LOG_EXTENSION);
    modemLogFile.setFileName(baseLogPath + "/logModem" + LOG_EXTENSION);
    tcpLogFile.setFileName(baseLogPath + "/logTCP" + LOG_EXTENSION);
    udpLogFile.setFileName(baseLogPath + "/logUDP" + LOG_EXTENSION);
}//setLogFileNames

void DialogLog::appendLogFilesToList()
{
    logFileList.append(&appLogFile);
    logFileList.append(&debugLogFile);
    logFileList.append(&modemLogFile);
    logFileList.append(&tcpLogFile);
    logFileList.append(&udpLogFile);
}//appendLogFilesToList

void DialogLog::updateUI()
{
    this->setWindowTitle("Log - "+globalConfigSettings.generalSettings.positionIdentifier);
    ui->checkBoxDisplayDebugInfo->setChecked(globalConfigSettings.generalSettings.showDebugInfo);
    this->move(globalConfigSettings.log_Dialog.pos);
    this->resize(globalConfigSettings.log_Dialog.size);
}//updateUI

DialogLog::DialogLog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogLog)
{
    ui->setupUi(this);

    createLogFolder();
    setLogFileNames();
    appendLogFilesToList();
    purgeOldLogs();
    rotateLogs();
    initializeLogFiles();

    slotUpdateBrowser("Function Name: " + tr(Q_FUNC_INFO));
    updateUI();
}//DialogLog

DialogLog::~DialogLog()
{
    delete ui;
}//DialogLog

void DialogLog::moveEvent(QMoveEvent *event)
{
    globalConfigSettings.log_Dialog.pos = this->pos();
    event->accept();
}//moveEvent

void DialogLog::resizeEvent(QResizeEvent *event)
{
    globalConfigSettings.log_Dialog.size = this->size();
    event->accept();
}//resizeEvent

void DialogLog::slotUpdateBrowser(QString dataByte)
{
    ui->plainTextEditLog->appendPlainText(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ")+dataByte);
    writeApplicationLogFile(dataByte);
}//slotUpdateBrowser

void DialogLog::slotUpdateDebug(QString dataByte)
{
    if(globalConfigSettings.generalSettings.showDebugInfo){
        ui->plainTextEditLog->appendPlainText(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ")+dataByte);
    }//if display debug info
    writeDebugLogFile(dataByte);
}//slotUpdateDebug

void DialogLog::writeLogFile(QFile& logFile, const QString& data)
{
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        QString cleanedData = data;
        cleanedData.replace("\r", "");
        cleanedData.replace("\n", "");
        out << QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ") << cleanedData << "\n";
        logFile.close();
    }//if log file open
}//writeLogFile

void DialogLog::writeApplicationLogFile(const QString& data)
{
    if(appLogFile.exists())
        writeLogFile(appLogFile, data);
}//writeApplicationLogFile

void DialogLog::writeDebugLogFile(QString data)
{
    if(debugLogFile.exists())
        writeLogFile(debugLogFile, data);
}//writeDebugLogFile

void DialogLog::slotWriteModemLogFile(QString data)
{
    if(modemLogFile.exists())
        writeLogFile(modemLogFile, data);
}//slotWriteModemLogFile

void DialogLog::slotWriteTCPLogFile(QString data)
{
    if(tcpLogFile.exists())
        writeLogFile(tcpLogFile, data);
}//slotWriteTCPLogFile

void DialogLog::slotWriteUDPLogFile(QByteArray data)
{
    if(udpLogFile.exists())
        writeLogFile(udpLogFile, data);
}//slotWriteUDPLogFile

void DialogLog::slotUpdateCriticalNotifications(QString data)
{
    ui->plainTextEditCriticalNotifications->appendPlainText(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ")+data);
    this->show();
    this->raise();
    ui->tabWidget->setCurrentIndex(1);
    writeApplicationLogFile(data);
}//slotUpdateCriticalNotifications

void DialogLog::purgeOldLogs()
{
    const QString logDirPath = QApplication::applicationDirPath() + LOG_EXTENSION;
    QDir logDir(logDirPath);

    if (!logDir.exists())
        return;

    logDir.setFilter(QDir::Files);
    logDir.setNameFilters(QStringList() << "*.log" << "*.LOG");

    const QDateTime expireDateTime = QDateTime::currentDateTimeUtc().addDays(- DAYS_TO_PURGE);
    const QList<QFileInfo> fileInfoList = logDir.entryInfoList();

    for (const QFileInfo &fileInfo : fileInfoList)
        if (fileInfo.birthTime().toTimeSpec(Qt::UTC) < expireDateTime)
            purgeLogFile(fileInfo);
}//purgeOldLogs

void DialogLog::purgeLogFile(const QFileInfo &fileInfo)
{
    QFile readFile(fileInfo.absoluteFilePath());
    if (readFile.remove())
        updateLogFiles("Purging log file: " + fileInfo.fileName());
    else
        updateLogFiles("ERROR: Purging log file: " + readFile.fileName());
}//purgeLogFile

void DialogLog::rotateLogWhenNameCollision(QString newLogFileName, QString timestamp, QString logFileName)
{
    QFile existingLogFile(newLogFileName);
    if (existingLogFile.exists()) {
        updateLogFiles("ERROR: Log file exists: " + existingLogFile.fileName());
        QString alternateFileName = logFileName + timestamp + "a.log";
        if (existingLogFile.copy(alternateFileName))
            existingLogFile.remove();
    }//if file exists
}//rotateLogWhenNameCollision

void DialogLog::rotateLogs()
{
    foreach (QFile *logFile, logFileList) {
        if (!logFile->exists())
            continue;

        QString logFileName = logFile->fileName();
        QFileInfo logFileInfo(*logFile);
        QDateTime logFileCreated = logFileInfo.lastModified();
        QString timestamp = logFileCreated.toString("ddMMMyyyyTHHmmsszzz");
        QString newLogFileName = logFileName + timestamp + LOG_EXTENSION;

        // Log file rotation message
        writeLogFile(*logFile, "Log file rotated.");

        // Attempt to copy old log to new log
        if (logFile->copy(newLogFileName)) {
            updateLogFiles("Rotated log file: " + logFileName);
            logFile->remove();
        }//if log file copy OK
        else {
            QString errorLogMessage = "ERROR: Rotating log file: " + logFileName + " - " + logFile->errorString();
            updateLogFiles(errorLogMessage);
            rotateLogWhenNameCollision(newLogFileName, timestamp, logFileName);
        }//else unable to rotate log
    }//foreach log file
}//rotateLogs

void DialogLog::updateLogFiles(const QString &message) {
    slotUpdateBrowser(message);
    writeApplicationLogFile(message);
}//handleLogRotation

void DialogLog::on_checkBoxDisplayDebugInfo_clicked()
{
    globalConfigSettings.generalSettings.showDebugInfo = ui->checkBoxDisplayDebugInfo->isChecked();
}//on_checkBoxDisplayDebugInfo_clicked

void DialogLog::on_lineEditMaxLogSize_textChanged(const QString &arg1)
{
    ui->plainTextEditLog->setMaximumBlockCount(arg1.toInt());
}//on_lineEditMaxLogSize_textChanged
