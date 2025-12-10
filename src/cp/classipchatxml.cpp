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
#include "classipchatxml.h"

ClassIPChatXML::ClassIPChatXML()
{

}

void ClassIPChatXML::removeNACKMessage(MessageStructure *message)
{
    if(message->messageText.endsWith("<br><br>There was no response from the intended recipient. "
                                        "<br>Please make sure the address you are sending to exists and that it is running RF IP Chat."))
        message->messageText.remove(message->messageText.indexOf("<br><br>There was no response from the intended recipient. "
                                                                     "<br>Please make sure the address you are sending to exists and that it is running RF IP Chat."),
                                      QByteArray("<br><br>There was no response from the intended recipient. "
                                                 "<br>Please make sure the address you are sending to exists and that it is running RF IP Chat.").size());
}

QByteArray ClassIPChatXML::wrap(const QString type, MessageStructure *message,
                                const uint packetNumber, const uint totalPackets,
                                const QString fileName)
{
    QByteArray returnArray;

    if(type == "ack"){
        QByteArray fromStation = message->messageID;

        if(message->messageText.startsWith("image|")){
            returnArray.append("ack");
            returnArray.append('|');
            returnArray.append(message->messageID);
            return returnArray;
        }
        else {
            fromStation = fromStation.remove(fromStation.size()-11, 11);
        }
        if(fromStation != message->fromStation){
            returnArray.append("ack");
            returnArray.append('|');
            returnArray.append(message->messageID);
            returnArray.append('|');
            returnArray.append(message->fromStation);
        }
        else {
            returnArray.append("ack");
            returnArray.append('|');
            returnArray.append(message->messageID);
        }
    }//if type == "ack"
    else if(type == "canx"){
        returnArray.append("canx");
        returnArray.append('|');
        returnArray.append(message->messageID);
    }
    else if(type == "nack"){
        returnArray.append("nack");
        returnArray.append('|');
        returnArray.append(message->messageID);
    }
    else if (type == "chat") {
        removeNACKMessage(message);
        returnArray.append("chat");
        returnArray.append('|');
        returnArray.append(message->messageID);
        returnArray.append('|');
        returnArray.append(message->messageText);
    }
    else if (type == "image") {
        returnArray.append("image");
        returnArray.append('|');
        returnArray.append(message->messageID);
        returnArray.append('|');
        returnArray.append(fileName.toLocal8Bit());
        returnArray.append('|');
        returnArray.append(QString::number(packetNumber).toLocal8Bit());
        returnArray.append('|');
        returnArray.append(QString::number(totalPackets).toLocal8Bit());
        returnArray.append('|');
        returnArray.append(message->messageText);
    }
    else if (type == "file") {
        returnArray.append("file");
        returnArray.append('|');
        returnArray.append(message->messageID);
        returnArray.append('|');
        returnArray.append(fileName.toLocal8Bit());
        returnArray.append('|');
        returnArray.append(QString::number(packetNumber).toLocal8Bit());
        returnArray.append('|');
        returnArray.append(QString::number(totalPackets).toLocal8Bit());
        returnArray.append('|');
        returnArray.append(message->messageText);
    }
    return returnArray;
}

MessageStructure *ClassIPChatXML::unwrap(QByteArray data)
{
    if(!data.contains('|'))
        return nullptr;

    QByteArrayList messageList = data.split('|');
    messageList.removeAll(" ");
    MessageStructure *message = nullptr;
    if(messageList.size() > 2){
        message = new MessageStructure;
        message->messageID = messageList.at(1);
        QByteArray fromStation = messageList.at(1);
        fromStation = fromStation.remove(fromStation.size()-11, 11);
        message->fromStation = fromStation;
        QByteArray destinationStation = QString(messageList.at(2)).split(" DE ").at(0).toLocal8Bit();
        message->destinationStation = destinationStation;
        messageList.removeFirst();
        messageList.removeFirst();
        message->messageText = messageList.join('|');
        message->isOutgoing = false;
        message->msgStatus = STATUS_NACK;
    }
    else {
        return nullptr;
    }

    QDateTime timeStamp = QDateTime::currentDateTimeUtc();
    message->timeStamp = timeStamp;

    return message;
}
