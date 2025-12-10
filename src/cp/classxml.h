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

#ifndef CLASSXML_H
#define CLASSXML_H

#include "globals.h"

//#include <QObject>
//#include <QXmlStreamReader>

enum XML_PRIORITY{
    A_FLASH = 1,
    A_IMMEDIATE = 2,
    A_PRIORITY = 3,
    A_ROUTINE = 4,
};//used in message queue priority

class ClassXML : public QObject
{
    Q_OBJECT
//    QByteArray printables(QByteArray byteArray);

    bool parsingError=false;

    //XML variables
    bool wasEncrypted=false;
    bool wasCompressed=false;

    QStringList keyList,
        callsignlist,
        ALEAddressList,
        receivedMessageList;

    QByteArrayList messageText;

    QString protocol,
        positionID,
        command,
        //radio/station stuff
        ALEAddress,
        radioFrequency,
        radioChannel,
        radioOPMode,
        radioTXMode,
        antennaBeamHeading,
        //message stuff
        checksum,
        receivedMessage,
        priority,
        compress,
        encrypt,
        encryptionKey,
        sourceStation,
        destinationStation,
        //application stuff
        listenIPAddress,
        listenTCPPort,
        listenDMTAddress,
        listenDMTDataPort,
        listenDMTCmdPort,
        //modem stuff
        modemDataRate,
        modemInterleave,
        modemWaveform;

    bool parseXMLDocument(QByteArray xmlDocument);

public:
    explicit ClassXML(QByteArray xmlDocument, QObject *parent = Q_NULLPTR);
    explicit ClassXML(QObject *parent = Q_NULLPTR);

    QString getALEAddres() const;
    QString getPriority() const;
    QString getCompress() const;
    QString getEncrypt() const;
    QString getEncryptionKey() const;
    QString getSourceStation() const;
    QString getDestinationStation() const;
    QString getAntennaBeamHeading() const;
    QString getListenIPAddress() const;
    QString getListenTCPPort() const;
    QString getRadioFrequency() const;
    QString getRadioChannel() const;
    QString getRadioOPMode() const;
    QString getRadioTXMode() const;
    QString getModemDataRate() const;
    QString getModemInterleave() const;
    QString getModemWaveform() const;
    QString getChecksum() const;
    QStringList getKeyList() const;
    QStringList getALEAddressList() const;
    QStringList getCallSignList() const;
    QString getPositionID() const;
    bool getWasEncrypted() const;
    bool getWasCompressed() const;
    QString getCommand() const;
    QString getReceivedMessage() const;
    QStringList getReceivedMessageList() const;
    QByteArrayList getMessageText() const;
    int getReceivedMessageListCount() const;
    bool getParsingError() const;
    QString getProtocol() const;
    QString getListenDMTDataPort() const;
    QString getListenDMTCmdPort() const;
    QString getListenDMTAddress() const;

    QByteArray wrap(QString type, QByteArray message, bool isHandshake, QString positionID, QString ALEStation, QString compress, QString encrypt, QString encryptionKey, QString sourceStation, QString destinationStation, QString modemSpeed, QString modemInterleave, XML_PRIORITY AType);

signals:

public slots:
private:
    QString calculatePriority(QByteArray dataByteArray, XML_PRIORITY type);
};

#endif // CLASSXML_H
