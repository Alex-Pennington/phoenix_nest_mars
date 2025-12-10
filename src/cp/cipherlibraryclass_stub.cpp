/* Stub implementation of MSCCipherLibraryClass
 * Provides plaintext-only operation for GPL build
 * Encryption/decryption operations pass through unchanged
 */

#include "cipherlibraryclass.h"
#include <QDebug>

MSCCipherLibraryClass::MSCCipherLibraryClass() {
    qDebug() << "MSCCipherLibraryClass: Stub implementation (plaintext only)";
}

int MSCCipherLibraryClass::QT_Init(QString baseDirectory) {
    Q_UNUSED(baseDirectory)
    qDebug() << "MSCCipherLibraryClass::QT_Init - Stub (no encryption available)";
    return 1; // Return success
}

int MSCCipherLibraryClass::QT_DebugMode(int Mode) {
    Q_UNUSED(Mode)
    return 0;
}

QStringList MSCCipherLibraryClass::QT_GetKeys() {
    // Return empty list - no encryption keys available
    return QStringList();
}

QByteArray MSCCipherLibraryClass::QT_ProcessData(QByteArray plaintext,
                                                  QString keyname,
                                                  QString sourceStation,
                                                  QString destinationStation,
                                                  bool encryptData,
                                                  bool compressData,
                                                  bool extendedChecksumHeader) {
    Q_UNUSED(keyname)
    Q_UNUSED(sourceStation)
    Q_UNUSED(destinationStation)
    Q_UNUSED(encryptData)
    Q_UNUSED(compressData)
    Q_UNUSED(extendedChecksumHeader)
    
    // Pass through plaintext unchanged
    qDebug() << "MSCCipherLibraryClass::QT_ProcessData - Passthrough (no encryption)";
    return plaintext;
}

QStringList MSCCipherLibraryClass::QT_Decrypt(QByteArray ciphertext) {
    QStringList result;
    // Return data as-is (assuming plaintext)
    result << QString::fromUtf8(ciphertext);  // 0 - plaintext
    result << "UNKN";                          // 1 - CRC status
    result << "";                              // 2 - Source Station
    result << "";                              // 3 - Destination Station
    result << QString::number(ciphertext.size()); // 4 - Byte Count
    result << "";                              // 5 - Compressed flag
    result << "";                              // 6 - Encryption Key
    result << "0";                             // 7 - Was Authenticated
    result << "0";                             // 8 - Auth Passed
    result << "";                              // 9 - Auth Agency
    return result;
}

QStringList MSCCipherLibraryClass::QT_AuthMessage(QString messageText) {
    Q_UNUSED(messageText)
    QStringList result;
    result << "0";  // Not authenticated (stub)
    result << "0";  // Auth not passed
    result << "";   // No agency
    return result;
}

QByteArray MSCCipherLibraryClass::printables(QByteArray dataByteArray) {
    return dataByteArray;
}

QByteArray MSCCipherLibraryClass::newPrintables(QByteArray dataByteArray) {
    return dataByteArray;
}

#if !defined Q_OS_WIN || defined USE_MSCCIPHER
int MSCCipherLibraryClass::SelfTest() {
    return 1; // Pass
}
#endif
