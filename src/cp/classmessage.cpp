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
#include "classmessage.h"

int ClassMessage::getMessageSerialNumber() const
{
    return messageSerialNumber;
}

QString ClassMessage::getMessageDateTimeFromXMLDocument(QString message)
{
    if(message.size() > 0)
    {
        QStringList messageByteArrayList = message.split('\n');
        foreach (QString tempByteArray, messageByteArrayList) {
            if (tempByteArray.startsWith("R ") ||
                tempByteArray.startsWith("P ") ||
                tempByteArray.startsWith("O ") ||
                tempByteArray.startsWith("Z "))
            {
                return calcDateTime(tempByteArray);
            }//else if RR/PP/OO/ZZ
        }
        return "";
    }
    else {
        return "";
    }
}

QString ClassMessage::calcDateTime(QString dateString)
{
    if(!dateString.isEmpty())
    {
        QStringList tempStringList = dateString.split(" ",Qt::SkipEmptyParts);
        if(tempStringList.count() < 4)
            return "";
        QString year = tempStringList.at(3);
        year.remove("\r");
        year.remove("\n");
        if(year.size() == 2)
            year.prepend(QDateTime::currentDateTimeUtc().toString("yyyy").left(2));
        QString month = tempStringList.at(2);
        if(month.size() > 3)
            month.resize(3);
        QStringList dateTime = tempStringList.at(1).split("Z",Qt::SkipEmptyParts);
        QString day = dateTime.at(0).left(2);
        QString time = dateTime.at(0).right(4);
        QTime formattedTime(time.left(2).toInt(), time.right(2).toInt());
        QDate formattedDate = QDate::fromString(year+month+day,"yyyyMMMdd");
        QDateTime formattedDateTime(formattedDate, formattedTime);

        if(!formattedTime.isValid())
        {
            emit signalUpdateApplicationLog("ERROR: Time format error!");
            emit signalUpdateApplicationLog("Time: " + time);
        }

        if(!formattedDate.isValid())
        {
            emit signalUpdateApplicationLog("ERROR: Date format error!");
            emit signalUpdateApplicationLog("Year: " + year);
            emit signalUpdateApplicationLog("Month: " + month);
            emit signalUpdateApplicationLog("Day: " + day);
        }
        return formattedDateTime.toString();
    }

    return "";
}//calcDateTime

QString ClassMessage::getReceivedMessage() const
{
    return receivedMessage;
}

QString ClassMessage::getChecksum() const
{
    return checksum;
}

QString ClassMessage::getModemWaveform() const
{
    return modemWaveform;
}

QString ClassMessage::getModemInterleave() const
{
    return modemInterleave;
}

QString ClassMessage::getModemDataRate() const
{
    return modemDataRate;
}

QString ClassMessage::getRadioTXMode() const
{
    return radioTXMode;
}

QString ClassMessage::getRadioOPMode() const
{
    return radioOPMode;
}

QString ClassMessage::getRadioChannel() const
{
    return radioChannel;
}

QString ClassMessage::getRadioFrequency() const
{
    return radioFrequency;
}

QString ClassMessage::getListenTCPPort() const
{
    return listenTCPPort;
}

QString ClassMessage::getListenIPAddress() const
{
    return listenIPAddress;
}

QString ClassMessage::getAntennaBeamHeading() const
{
    return antennaBeamHeading;
}

QString ClassMessage::getDestinationStation() const
{
    return destinationStation;
}

QString ClassMessage::getSourceStation() const
{
    return sourceStation;
}

QString ClassMessage::getEncryptionKey() const
{
    return encryptionKey;
}

QString ClassMessage::getEncrypt() const
{
    return encrypt;
}

QString ClassMessage::getCompress() const
{
    return compress;
}

QString ClassMessage::getPriority() const
{
    return priority;
}

QString ClassMessage::getDestinationAddress() const
{
    return destinationAddress;
}

QString ClassMessage::getALEAddress() const
{
    return ALEAddress;
}

QString ClassMessage::getCommand() const
{
    return command;
}

QString ClassMessage::getPositionID() const
{
    return positionID;
}

QStringList ClassMessage::getReceivedMessageList() const
{
    return receivedMessageList;
}

QStringList ClassMessage::getALEAddressList() const
{
    return ALEAddressList;
}

QStringList ClassMessage::getKeyList() const
{
    return keyList;
}

bool ClassMessage::getWasCompressed() const
{
    return wasCompressed;
}

int ClassMessage::getReceivedMessageCount() const
{
    return receivedMessageCount;
}

bool ClassMessage::getWasEncrypted() const
{
    return wasEncrypted;
}

int ClassMessage::getMessagePriority()
{
    return this->messagePriority;
}

QDateTime ClassMessage::getMessageDateTime()
{
    return this->messageDateTime;
}

QByteArray ClassMessage::getXMLDocument()
{
    return this->xmlDocument;
}

QByteArrayList ClassMessage::getMessageText()
{
    return this->messageList;
}

ClassMessage::ClassMessage(QByteArray xmlDocument, QObject *parent) : QObject(parent)
{
    ClassXML xmlClass(xmlDocument);
    this->xmlDocument = xmlDocument;

    this->messageSerialNumber = globalConfigSettings.generalSettings.messageSerialNumber;
    globalConfigSettings.generalSettings.messageSerialNumber++;
    if(globalConfigSettings.generalSettings.messageSerialNumber > 99)
        globalConfigSettings.generalSettings.messageSerialNumber = 0;

    this->wasEncrypted=xmlClass.getWasEncrypted();
    this->wasCompressed=xmlClass.getWasCompressed();
    this->keyList=xmlClass.getKeyList();
    this->ALEAddressList=xmlClass.getALEAddressList();
    this->receivedMessageList=xmlClass.getReceivedMessageList();
    this->positionID=xmlClass.getPositionID();
    this->command=xmlClass.getCommand();
    this->ALEAddress=xmlClass.getALEAddres();
    this->destinationAddress=xmlClass.getDestinationStation();
    this->priority=xmlClass.getPriority();
    this->compress=xmlClass.getCompress();
    this->encrypt=xmlClass.getEncrypt();
    this->encryptionKey=xmlClass.getEncryptionKey();
    this->sourceStation=xmlClass.getSourceStation();
    this->destinationStation=xmlClass.getDestinationStation();
    this->antennaBeamHeading=xmlClass.getAntennaBeamHeading();
    this->listenIPAddress=xmlClass.getListenIPAddress();
    this->listenTCPPort=xmlClass.getListenTCPPort();
    this->radioFrequency=xmlClass.getRadioFrequency();
    this->radioChannel=xmlClass.getRadioChannel();
    this->radioOPMode=xmlClass.getRadioOPMode();
    this->radioTXMode=xmlClass.getRadioTXMode();
    this->modemDataRate=xmlClass.getModemDataRate();
    this->modemInterleave=xmlClass.getModemInterleave();
    this->modemWaveform=xmlClass.getModemWaveform();
    this->checksum=xmlClass.getChecksum();
    this->receivedMessage=xmlClass.getReceivedMessage();
    this->receivedMessageCount=xmlClass.getReceivedMessageListCount();
    this->messageList = xmlClass.getMessageText();
    this->messagePriority = priority.toInt();

    this->messageDateTimeString = this->getMessageDateTimeFromXMLDocument(this->receivedMessage);
    if(!this->messageDateTimeString.isEmpty()){
        if(QDateTime::fromString(this->messageDateTimeString).isValid()){
            this->messageDateTime = QDateTime::fromString(this->messageDateTimeString);
        }
    }
}
