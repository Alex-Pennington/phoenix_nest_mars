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

#include "dialogtcpip.h"
#include "ui_dialogtcpip.h"

void DialogTCPIP::moveEvent(QMoveEvent *event)
{
    globalConfigSettings.tcp_Dialog.pos = this->pos();
    event->accept();
}

void DialogTCPIP::resizeEvent(QResizeEvent *event)
{
    globalConfigSettings.tcp_Dialog.size = this->size();
    event->accept();
}

DialogTCPIP::DialogTCPIP(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogTCPIP)
{
    ui->setupUi(this);
    this->setWindowTitle("TCP - "+globalConfigSettings.generalSettings.positionIdentifier);
    ui->checkBoxHideHandshakePackets->setChecked(globalConfigSettings.generalSettings.hideHandShakePackets);
    this->move(globalConfigSettings.tcp_Dialog.pos);
    this->resize(globalConfigSettings.tcp_Dialog.size);
}//DialogTCPIP

DialogTCPIP::~DialogTCPIP()
{
    delete ui;
}

void DialogTCPIP::slotUpdateBrowser(QByteArray data)
{
    ui->plainTextEditReceivedTCPIPData->appendPlainText(printables(data));
    emit signalWriteTCPLogFile(data);
}//slotUpdateBrowser

//QByteArray DialogTCPIP::printables(QByteArray byteArray)
//{
//    for(int i=0;i<byteArray.size();i++){
//        if(byteArray.at(i) == '\r' ||
//                byteArray.at(i) == '\n' )
//            continue;
//        else if(byteArray.at(i) < 32 || byteArray.at(i) > 126)
//            byteArray[i] = '?';
//    }//for each char
//    return byteArray;
//}//printables

void DialogTCPIP::on_checkBoxHideHandshakePackets_clicked()
{
    globalConfigSettings.generalSettings.hideHandShakePackets = ui->checkBoxHideHandshakePackets->isChecked();
    emit signalUpdateMainWindow();
}//on_checkBoxHideHandshakePackets_clicked
