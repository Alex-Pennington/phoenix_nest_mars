/* * Copyright © 2016 - 2024 Timothy Millea dba Green Radio Software Systems (GRSS)
*
* Special use rights granted to US Army NETCOM, HQ Army MARS, Contractors or
* person(s) supporting HQ Army MARS missions to use/modify/compile this
* source code.
*
* This source code is not to be released or used outside of US Army MARS.
*
* In no case may any person or organization, license, sell, rent, lease,
* assign, distribute, transmit, host, outsource, disclose or otherwise
* commercially exploit this source code or compiled executables, object files,
* libraries, or share this source code with any 3rd party unless specifically
* licensed to do so by GRSS.
*
* */




#ifndef CIPHERLIBRARYCLASS_H
#define CIPHERLIBRARYCLASS_H

#include "cipherlibraryclass_global.h"
#include "qobject.h"

//#include "globals.h"

//#include "classsignmessage.h"

//if we're using Qt
#if defined(QT_CORE_LIB)
#include <QStringList>
#endif

    /*Cipher initialization returns:
     *  1 - init OK or already initialized
     *  0 - init failed - unspecified error
     * -1 - libeay32.dll not found
     * -2 - ../Cipher/MSCCipherKeys.db not found
     * -3 - key database hash check failed
     * -4 - no keys loaded in database
     * -5 - failed self test
     * -6 - Internet access found
     * -7 - 2-part cipher library failed to load
     * -8 - 2-part keys not found
     * -9 - 2-part key folders not found
     * -10 - 2-part cipher failed self-test
     * -11 - OnlineDLLProj.dll not found (Cipher folder)
     * -12 - cc32220mt.dll not found (application folder)
     * */

class MSCCIPHERLIBRARYCLASSSHARED_EXPORT MSCCipherLibraryClass : public QObject
{
    Q_OBJECT

    QByteArray printables(QByteArray dataByteArray);
    QByteArray newPrintables(QByteArray dataByteArray);

public:
    MSCCipherLibraryClass();

#if !defined Q_OS_WIN  || defined USE_MSCCIPHER
    //CIPHER MODULE SELF SEST
    int SelfTest();
#endif
    /*******************************************************
     * C++ functions
     * *****************************************************/
//    int CPP_Init(std::string baseDirectory);
//    std::vector<std::string> CPP_GetKeys();
//    std::string CPP_ProcessData(std::string plaintext,
//                                std::string keyName,
//                                std::string sourceStation,
//                                std::string destinationStation,
//                                int encryptData,
//                                int compressData,
//                                int extendedChecksumHeader);
//    std::vector<std::string> CPP_Decrypt(std::string cipherText);
//    std::vector<std::string> CPP_AuthMessage(std::string messgeText);

    /*******************************************************
     * Qt functions - remove if not using Qt
     * *****************************************************/
//    int QT_Zeroize();

     int QT_Init(QString baseDirectory);
    int QT_DebugMode(int Mode);
    QStringList QT_GetKeys();//returns QStringList of keynames
    QByteArray QT_ProcessData(QByteArray plaintext,
                              QString keyname,
                              QString sourceStation,
                              QString destinationStation,
                              bool encryptData,
                              bool compressData,
                              bool extendedChecksumHeader);
    //decrypts QByteArray ciphertext using all available keys or until a successful decrypt occurs
    QStringList QT_Decrypt(QByteArray ciphertext);
    /*
    0 - Decrypted plaintext or ERROR
    1 - CRC - PASS/FAIL/UNKN/ERRO
    2 - Source Station
    3 - Destination Station
    4 - Byte Count
    5 - COMPRESSED if compressed empty if not
    6 - Encryption Key used to decrypt
    7 - Was Authenticated - 1 for Yes, 0 for No
    8 - Authentication Passed - 1 for Yes, 0 for No
    9 - Authentication Agency
    */
    QStringList QT_AuthMessage(QString messageText);

signals:
    void signalTest(int);

};
#endif // CIPHERLIBRARYCLASS_H
