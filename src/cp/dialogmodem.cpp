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
#include "dialogmodem.h"
#include "ui_dialogmodem.h"

void DialogModem::moveEvent(QMoveEvent *event)
{
    globalConfigSettings.modem_Dialog.pos = this->pos();
    event->accept();
}//moveEvent

void DialogModem::resizeEvent(QResizeEvent *event)
{
    globalConfigSettings.modem_Dialog.size = this->size();
    event->accept();
}//resizeEvent

void DialogModem::setuDialogUI()
{
    ui->setupUi(this);
    
    this->setWindowTitle("Modem - "+globalConfigSettings.generalSettings.positionIdentifier);
    this->move(globalConfigSettings.modem_Dialog.pos);
    this->resize(globalConfigSettings.modem_Dialog.size);
    ui->splitter->restoreState(globalConfigSettings.modem_Dialog.splitterState);
}//setupUI

DialogModem::DialogModem(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogModem)
{
    setuDialogUI();
}//DialogModem

DialogModem::~DialogModem()
{
    delete ui;
}//DialogModem

//QByteArray DialogModem::printables(QByteArray byteArray)
//{
//    for (int i = 0; i < byteArray.size(); i++) {
//        char currentByte = byteArray.at(i);

//        if (currentByte == '\r' ||
//            currentByte == '\n' ||
//            currentByte == '\t')
//            continue;

//        else if (currentByte < 32 || currentByte > 126)
//            byteArray[i] = '.';

//    }//foreach char in byteArray
//    return byteArray;
//}//printables

QString DialogModem::humanReadableHex(QByteArray hexCode)
{
    QString returnString;
    int chCounter = 0;
    int lineCounter = 0;

    for (int x = 0; x < hexCode.size(); x++) {
        chCounter++;
        lineCounter++;

        QString ch(hexCode.at(x));
        QByteArray chArray = ch.toLatin1();

        returnString.append(chArray);

        if (chCounter == 2) {
            returnString.append(" ");
            chCounter = 0;
        }

        if (lineCounter == 32) {
            returnString.append('\n');
            lineCounter = 0;
        }
    }//for x < hexCode.size

    return returnString.toUpper();
}//humanReadable

void DialogModem::displayHexCode(const QByteArray & header, const QByteArray &dataByte)
{
    ui->plainTextEditModemReceivedDataHex->appendPlainText(header+humanReadableHex(dataByte.toHex()));
    emit signalWriteModemLogFile(dataByte.toHex());
}//displayHexCode

void DialogModem::displayASCIIText(const QByteArray &dataByte)
{
    ui->plainTextEditModemReceivedDataASCII->appendPlainText(printables(dataByte));
    emit signalWriteModemLogFile(printables(dataByte));
}//displayASCIIText

void DialogModem::displayReceivedData(const QByteArray &dataByte, bool hexCode)
{
    const QByteArray timestamp = QDateTime::currentDateTimeUtc().toString(TIMESTAMP_FORMAT).toLocal8Bit();
    const QByteArray header = "\nRECEIVED " + timestamp + "\n";

    hexCode ? displayHexCode(header, dataByte) : displayASCIIText(header+dataByte);

    emit signalWriteModemLogFile(header);
}//displayReceivedData

void DialogModem::displaySentData(const QByteArray &dataByte, bool hexCode)
{
    const QByteArray timestamp = QDateTime::currentDateTimeUtc().toString(TIMESTAMP_FORMAT).toLocal8Bit();
    const QByteArray header = "\nSENT " + timestamp + "\n";

    hexCode ? displayHexCode(header, dataByte) : displayASCIIText(header+dataByte);

    emit signalWriteModemLogFile(header);
}//displaySentData

void DialogModem::slotUpdateBrowser(QByteArray dataByte, bool received, bool hexCode)
{
    received ? displayReceivedData(dataByte, hexCode) : displaySentData(dataByte, hexCode);
}//slotUpdateBrowser

void DialogModem::slotUpdateModemStatus(QByteArray statusByteArray)
{
    QString timeStamp = QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz: ");
    ui->plainTextEditModemStatus->appendPlainText(timeStamp+statusByteArray);
}//slotUpdateModemStatus

void DialogModem::on_lineEditOrderwireData_returnPressed()
{
    QByteArray orderWireMessage = ui->lineEditOrderwireData->text().toLatin1();
    ui->lineEditOrderwireData->clear();

    const QByteArray timestamp = QDateTime::currentDateTimeUtc().toString(TIMESTAMP_FORMAT).toLocal8Bit();
    const QByteArray header = "\nSENT " + timestamp + "\n";

    displayASCIIText(header+orderWireMessage);

    emit signalSendDataToModem(orderWireMessage);
}//on_lineEditOrderwireData_returnPressed

void DialogModem::on_lineEditRawData_returnPressed()
{
    QByteArray rawDataMessage = ui->lineEditRawData->text().toLatin1();
    if(ui->radioButtonHexCommand->isChecked())
        rawDataMessage = QByteArray::fromHex(rawDataMessage);

    const QByteArray timestamp = QDateTime::currentDateTimeUtc().toString(TIMESTAMP_FORMAT).toLocal8Bit();
    const QByteArray header = "\nSENT " + timestamp + "\n";

    bool hexCode = ui->radioButtonHexCommand->isChecked();
    hexCode ? displayHexCode(header, rawDataMessage) : displayASCIIText(header+rawDataMessage);

    emit signalSendDataToModem(rawDataMessage);
}//on_lineEditRawData_returnPressed

void DialogModem::on_splitter_splitterMoved(int pos, int index)
{
    Q_UNUSED(pos)
    Q_UNUSED(index)
    
    globalConfigSettings.modem_Dialog.splitterState = ui->splitter->saveState();
}//on_splitter_splitterMoved


