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

#include "dialogudp.h"
#include "ui_dialogudp.h"

void DialogUDP::moveEvent(QMoveEvent *event)
{
    globalConfigSettings.udp_Dialog.pos = this->pos();
    event->accept();
}

void DialogUDP::resizeEvent(QResizeEvent *event)
{
    globalConfigSettings.udp_Dialog.size = this->size();
    event->accept();
}

DialogUDP::DialogUDP(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogUDP)
{
    ui->setupUi(this);
    this->setWindowTitle("UDP - "+globalConfigSettings.generalSettings.positionIdentifier);
    
    this->move(globalConfigSettings.udp_Dialog.pos);
    this->resize(globalConfigSettings.udp_Dialog.size);
}//DialogTCPIP

DialogUDP::~DialogUDP()
{
//    delete ui;
}

void DialogUDP::slotUpdateBrowser(QByteArray data)
{
    ui->plainTextEditReceivedUDPData->appendPlainText(printables(data));
    emit signalWriteUDPLogFile(data);
}//slotUpdateBrowser

//QByteArray DialogUDP::printables(QByteArray byteArray)
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
