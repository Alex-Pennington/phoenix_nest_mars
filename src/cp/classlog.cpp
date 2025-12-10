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

#include "classlog.h"

void ClassLog::purgeLogFiles()
{
    //purge log files after 7 days
    const QString logDirPath = QApplication::applicationDirPath() + "/LOGFILES";
    QDir logDir(logDirPath);

    if (!logDir.exists()) {
        return;  // Exit early if the directory doesn't exist
    }

    logDir.setFilter(QDir::Files);
    logDir.setNameFilters(QStringList() << "*.log" << "*.LOG");

    const QDateTime sevenDaysAgo = QDateTime::currentDateTimeUtc().addDays(-7);
    const QList<QFileInfo> fileInfoList = logDir.entryInfoList();

    for (const QFileInfo &fileInfo : fileInfoList) {
        bool fileIsToOld = fileInfo.birthTime() < sevenDaysAgo;
        if (fileIsToOld) {
            QFile readFile(fileInfo.absoluteFilePath());

            if (readFile.remove()) {
                this->write("Purging log file: " + fileInfo.fileName(), false);
            } else {
                this->write("ERROR: purging log file: " + fileInfo.fileName(), false);
            }
        }//if file is too old
    }//for each file

//    if(logDir.count() > 0){
//        for(int i = 0; i < logDir.entryInfoList().size();i++)//for each file in the directory
//        {
//            QFile readFile(logDir.entryInfoList().at(i).absoluteFilePath());
//            QFileInfo fileInfo(readFile);
//            if(fileInfo.birthTime() < QDateTime::currentDateTimeUtc().addDays(-7)){
//                if(readFile.remove()){
//                    this->write("Purging log file: " + fileInfo.fileName(),false);
//                }
//                else{
//                    this->write("ERROR: purging log file: " + fileInfo.fileName(),false);
//                }
//            }//if file exists
//            else {
//                continue;
//            }
//        }//for each file in folder
//    }//if there are files in folder
}//purgeLogFiles

void ClassLog::deleteFile(QFileInfo fileInfo)
{
    QFile readFile(fileInfo.absoluteFilePath());
    if (readFile.remove())
        this->write("Purging log file: " + fileInfo.fileName(), false);
    else
        this->write("ERROR: purging log file: " + fileInfo.fileName(), false);
}//deleteFile

ClassLog::ClassLog(QObject *parent) : QObject(parent)
{
    if(!logDir.exists()){
        logDir.mkdir(QString(QApplication::applicationDirPath()+"/LOGFILES/"));
    }
    this->purgeLogFiles();
    currentLogFile.setFileName(logFileName);
}//ClassLog

void ClassLog::write(QString logData, bool isDebug)
{
    //write to disk
    if(this->currentLogFile.open(QIODevice::ReadWrite|QIODevice::Append|QIODevice::Text)){
        QTextStream out(&this->currentLogFile);
        out << QDateTime::currentDateTimeUtc().toString("yyyy-MMM-dd@hh:mm:ss.zzz - ") << logData << Qt::endl;
        currentLogFile.flush();
        currentLogFile.close();
    }
    else {
        //update UI log
        emit signalUpdateUILog(QDateTime::currentDateTimeUtc().toString("yyyy-MMM-dd@hh:mm:ss.zzz - ")+
                                   "Error writing log file to disk! - "
                                   +this->currentLogFile.errorString()+" - "+
                                   logFileName, false);
    }
    //update UI log
    emit signalUpdateUILog(QDateTime::currentDateTimeUtc().toString("yyyy-MMM-dd@hh:mm:ss.zzz - ")+logData, isDebug);
}//write
