/****************************************************************************
**
** Copyright (C) 2016 - 2019 Timothy Millea <timothy.j.millea@gmail.com>
** Released under GPL License
** See gpl-3.0.txt included with the source code.
**
****************************************************************************/

#ifndef CLASSXML_H
#define CLASSXML_H

//#include "globals.h"

#include <QObject>
#include <QXmlStreamReader>


class ClassXML : public QObject
{
    Q_OBJECT
    bool parseXMLDocument(QByteArray xmlDocument);
    QByteArray printables(QByteArray byteArray);
    QString calculatePriority(QByteArray dataByteArray);

    bool parsingError=false;

    //XML variables
    bool wasEncrypted=false;
    bool wasCompressed=false;

    QStringList keyList,
    callsignList,
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
    wasAuthenticated,
    authPassed,
    authAgency,
    receivedMessage;

public:
    explicit ClassXML(QByteArray xmlDocument, QObject *parent = nullptr);
    explicit ClassXML(QObject *parent = nullptr);
    QByteArray wrap(QString type, QByteArray message, bool isHandshake, QString positionID, QString ALEStation,
                    QString compress, QString encrypt, QString encryptionKey, QString sourceStation,
                    QString destinationStation, QString modemSpeed, QString modemInterleave);

    //class getter functions
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
    QString getPositionID() const;
    bool getWasEncrypted() const;
    bool getWasCompressed() const;
    QString getCommand() const;
    QStringList getALEAddressList() const;
    QString getReceivedMessage() const;
    QStringList getReceivedMessageList() const;
    int getReceivedMessageListCount() const;
    bool getParsingError() const;
    QStringList getCallsignList() const;
    QString getWasAuthenticated() const;
    QString getAuthPassed() const;
    QString getAuthAgency() const;

signals:

public slots:
};

#endif // CLASSXML_H
