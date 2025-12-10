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

#include "dialogale.h"
#include "ui_dialogale.h"

const QString LOG_FOLDER = "/LOGFILES";
const QString LOG_EXTENSION = ".log";

DialogAle::DialogAle(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAle)
{
    ui->setupUi(this);
    this->setWindowTitle("ALE - "+globalConfigSettings.generalSettings.positionIdentifier);

    rotateLog();

    QString baseLogPath = QApplication::applicationDirPath() + LOG_FOLDER;
    aleLogFile.setFileName(baseLogPath + "/logApplication" + LOG_EXTENSION);

    if (!aleLogFile.open(QIODevice::Append | QIODevice::Text )){
        qDebug() << aleLogFile.errorString();
        return;
    }

    QTextStream out(&aleLogFile);
    out << "**************************************************************************************************\n"
        << QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ")
        << "Application started...\n";
    aleLogFile.flush();
    aleLogFile.close();
    
    this->move(globalConfigSettings.ale_Dialog.pos);
    this->resize(globalConfigSettings.ale_Dialog.size);
    ui->splitter->restoreState(globalConfigSettings.ale_Dialog.splitterState);
    ui->splitter_2->restoreState(globalConfigSettings.ale_Dialog.splitterState2);

}

DialogAle::~DialogAle()
{
    delete ui;
}

void DialogAle::moveEvent(QMoveEvent *event)
{
    globalConfigSettings.ale_Dialog.pos = this->pos();
    event->accept();
}

void DialogAle::resizeEvent(QResizeEvent *event)
{
    globalConfigSettings.ale_Dialog.size = this->size();
    event->accept();
}

void DialogAle::slotUpdateBrowser(QByteArray dataByte)
{
    ui->plainTextEditAleCommands->appendPlainText(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ")+dataByte);
    writeALELogFile(dataByte);
}

void DialogAle::slotUpdateAMDBrowser(QByteArray dataByte)
{
    ui->textBrowserAMDMessages->append(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss - ")+dataByte);
}

void DialogAle::slotUpdateHeardListBrowser(QStringList ALEHeardList)
{
    ui->textBrowserALEHeardList->clear();
    foreach (QString ALEAddress, ALEHeardList) {
        if(!this->ALEHeardList.contains(ALEAddress))
            this->ALEHeardList.append(ALEAddress);
        ui->textBrowserALEHeardList->append(ALEAddress);
    }
    ui->comboBoxSelectALEAddress->clear();
    ui->comboBoxSelectALEAddress->addItems(this->ALEHeardList);
}

void DialogAle::slotUpdateALEAddressSelect(QStringList knowAddressList)
{
    ui->comboBoxSelectALEAddress->clear();
    ui->comboBoxSelectALEAddress->addItems(knowAddressList);
    globalConfigSettings.aleSettings.knownALEAddressList = knowAddressList;
}

void DialogAle::writeALELogFile(QString data)
{
    if (!aleLogFile.open(QIODevice::Append | QIODevice::Text ))
        return;

    QTextStream out(&aleLogFile);
    data.replace("\r","");
    data.replace("\n","");
    out << QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ") << data << "\n";
    aleLogFile.flush();
    aleLogFile.close();
}//writeALELogFile

void DialogAle::rotateLog()
{
    QFileInfo logFileInfo(aleLogFile);
    QDateTime logFileCreated = logFileInfo.lastModified();

    if(!aleLogFile.exists())
        return;

    if (!aleLogFile.open(QIODevice::Append | QIODevice::Text ))
        return;

    QTextStream out(&aleLogFile);
    out << QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ") << "Log file rotated." << "\n";
    aleLogFile.flush();
    aleLogFile.close();

    if(aleLogFile.copy(QString(aleLogFile.fileName()+
                                logFileCreated.toString("ddMMMyyyyTHHmmsszzz")+".log"))){
        aleLogFile.remove();
    }//if copy successful
    else {
        QFile existingLogFile(QString(aleLogFile.fileName()+
                                      logFileCreated.toString("ddMMMyyyyTHHmmsszzz")+".log"));
        if(existingLogFile.exists()){
            if(existingLogFile.copy(QString(aleLogFile.fileName()+
                                             logFileCreated.toString("ddMMMyyyyTHHmmsszzz")+"a.log"))){
                existingLogFile.remove();
            }
        }
    }
}

void DialogAle::on_pushButtonSendAMD_clicked()
{
    emit signalSendALECommand("CMD AMD "+ui->comboBoxSelectALEAddress->currentText().toLatin1()
                              +" \""+ui->lineEditAMDMessage->text().toLatin1()+" \"");
}

void DialogAle::on_splitter_splitterMoved(int pos, int index)
{
    Q_UNUSED(pos)
    Q_UNUSED(index)
    if(g_ApplicationStart)
        return;

    globalConfigSettings.ale_Dialog.splitterState = ui->splitter->saveState();
}


void DialogAle::on_splitter_2_splitterMoved(int pos, int index)
{
    Q_UNUSED(pos)
    Q_UNUSED(index)
    if(g_ApplicationStart)
        return;

    globalConfigSettings.ale_Dialog.splitterState2 = ui->splitter_2->saveState();
}

