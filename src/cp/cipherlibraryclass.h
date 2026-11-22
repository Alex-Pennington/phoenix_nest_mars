/* Phoenix Nest MARS - cipher slot interface
 * Copyright (C) 2026 Phoenix Nest LLC
 * Licensed under the GNU General Public License v3 (see ClassSignMessageGPL/gpl-3.0.txt).
 *
 * The calls CP makes when it wants a message encrypted, decrypted or checked.
 * This build ships only the plain-text pass-through in cipherlibraryclass_stub.cpp.
 * A cipher authorized for MARS traffic plugs in behind these same calls.
 */

#ifndef CIPHERLIBRARYCLASS_H
#define CIPHERLIBRARYCLASS_H

#include "cipherlibraryclass_global.h"

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>

class MSCCIPHERLIBRARYCLASSSHARED_EXPORT MSCCipherLibraryClass : public QObject
{
    Q_OBJECT

    QByteArray printables(QByteArray dataByteArray);
    QByteArray newPrintables(QByteArray dataByteArray);

public:
    MSCCipherLibraryClass();

#if !defined Q_OS_WIN || defined USE_MSCCIPHER
    // Returns 1 when the cipher passes its own check.
    int SelfTest();
#endif

    // Loads the cipher from baseDirectory. Returns 1 on success, 0 or less on failure.
    int QT_Init(QString baseDirectory);

    // Turns debug output on (1) or off (0).
    int QT_DebugMode(int Mode);

    // Names of the keys the cipher has loaded.
    QStringList QT_GetKeys();

    // Prepares a message for sending: encrypts and/or compresses as asked.
    QByteArray QT_ProcessData(QByteArray plaintext,
                              QString keyname,
                              QString sourceStation,
                              QString destinationStation,
                              bool encryptData,
                              bool compressData,
                              bool extendedChecksumHeader);

    // Opens a received message. The list holds, in order: text, check result,
    // source station, destination station, byte count, compressed flag,
    // key used, authenticated (1/0), authentication passed (1/0), agency.
    QStringList QT_Decrypt(QByteArray ciphertext);

    // Checks a message's authentication. Returns authenticated, passed, agency.
    QStringList QT_AuthMessage(QString messageText);

signals:
    void signalTest(int);
};

#endif // CIPHERLIBRARYCLASS_H
