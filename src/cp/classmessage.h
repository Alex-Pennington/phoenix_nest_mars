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

#ifndef CLASSMESSAGE_H
#define CLASSMESSAGE_H

#include "globals.h"
#include "classxml.h"

class ClassMessage : public QObject
{
    Q_OBJECT
    int messageSerialNumber;
    QByteArray xmlDocument;
    QByteArrayList messageList;
    QString getMessageDateTimeFromXMLDocument(QString message);
    int messagePriority = -1;
    QDateTime messageDateTime;
    QString messageDateTimeString;

    QString calcDateTime(QString dateString);

    bool wasEncrypted=false;
    bool wasCompressed=false;

    int receivedMessageCount;

    QStringList keyList,
    ALEAddressList,
    receivedMessageList;

    QString positionID,
    command,
    ALEAddress,
    destinationAddress,
    priority,
    compress,
    encrypt,
    encryptionKey,
    sourceStation,
    destinationStation,
    antennaBeamHeading,
    listenIPAddress,
    listenTCPPort,
    radioFrequency,
    radioChannel,
    radioOPMode,
    radioTXMode,
    modemDataRate,
    modemInterleave,
    modemWaveform,
    checksum,
    receivedMessage;
    
public:
    explicit ClassMessage(QByteArray xmlDocument, QObject *parent = nullptr);
    int getMessagePriority();
    QDateTime getMessageDateTime();
    QByteArray getXMLDocument();
    QByteArrayList getMessageText();

    bool getWasEncrypted() const;
    bool getWasCompressed() const;

    int getReceivedMessageCount() const;

    QStringList getKeyList() const;
    QStringList getALEAddressList() const;
    QStringList getReceivedMessageList() const;

    QString getPositionID() const;
    QString getCommand() const;
    QString getALEAddress() const;
    QString getDestinationAddress() const;
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
    QString getReceivedMessage() const;

    int getMessageSerialNumber() const;

signals:
    void signalUpdateApplicationLog(QString data);
    void signalUpdateDebugLog(QString data);

public slots:
};

#endif // CLASSMESSAGE_H
