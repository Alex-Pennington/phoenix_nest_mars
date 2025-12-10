#ifndef CLASSCHECKSUM_H
#define CLASSCHECKSUM_H

#include "globals.h"

#include <QObject>

class ClassChecksum : public QObject
{
    Q_OBJECT
    unsigned int calcChecksum(QByteArray payload, int payloadSize);
    int payloadByteCount = 0, predictedByteCountSize = 0;
    Bool checksumBool = UNKN;

    QByteArrayList byteArrayList;
    QByteArray extPayload,
    checkSum,
    extChecksum,
    checkSumCheck,
    extChecksumCheck,
    byteCount,
    protocolType,
    sourceAddress,
    destinationAddress;

public:
    explicit ClassChecksum(QObject *parent = nullptr);

    QByteArray appendTCHeader(QByteArray messageData, QByteArray sourceStation,
                              QByteArray destinationStation, QByteArray instruction,
                              QByteArray TXInstruction,
                              int messageSerialNumber);
    bool detectTCHeader(QByteArray receivedMessage);
    QByteArray stripTCHeader(QByteArray byteArrayData);
    QByteArray getMissingBlocks(QByteArray receivedMessage);
    //getter functions
    Bool getChecksumBool() const;
    int getPredictedByteCountSize() const;
    QByteArray getByteCount() const;
    int getPayloadByteCount() const;
    QByteArray getSourceAddress() const;
    QByteArray getDestinationAddress() const;


signals:
    void signalUpdateDebugLog(QString data);

public slots:
};

#endif // CLASSCHECKSUM_H
