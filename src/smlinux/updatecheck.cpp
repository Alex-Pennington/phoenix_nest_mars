#include "main.h"
#include "updatecheck.h"
#include "classfile.h"
#include "debug.h"
#include <QDebug>
#include <QStringRef>
#include <QLabel>
#include <QSslSocket>

DownloaderMQ d;

Dynarray localVersions;

DownloaderMQ::DownloaderMQ(QObject *parent) :
    QObject(parent)
{
}

void DownloaderMQ::doDownload() {
    if (DebugFunctions > 3) {qDebug() << Q_FUNC_INFO;}

    //-----------------------------------------------------//

    QNetworkAccessManager *am = new QNetworkAccessManager(this);
    QString url;
    QString tmpSSLstatus = "";
    if (QSslSocket::supportsSsl()) {
        url = "https://www.KYHiTech.com/StationMapper/uploads/ServiceCodes.php";
        tmpSSLstatus = "True";
    } else {
        url = "http://www.KYHiTech.com/StationMapper/uploads/ServiceCodes.php";
        tmpSSLstatus = "False";
    }

    QNetworkRequest request0((QUrl(url)));
    QUrlQuery postData;
    postData.addQueryItem("param1", WorkingDB[SettingsVars.MyCallSign].CallSign);
    postData.addQueryItem("param2", QString::number(WorkingDB.size()));
    postData.addQueryItem("param3", tmpSSLstatus);
    postData.addQueryItem("param4", VERSION);
    postData.addQueryItem("param5", SettingsVars.ServiceCodes);
    QString concatenated0 = "StationMapper:2MLxB1KK38radR9j";
    QByteArray AuthData = concatenated0.toLocal8Bit().toBase64();
    QString headerData0 = "Basic " + AuthData;
    request0.setRawHeader( "Authorization", headerData0.toLocal8Bit() );
    request0.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    am->post(request0, postData.toString(QUrl::FullyEncoded).toUtf8());

    //-----------------------------------------------------//

    manager = new QNetworkAccessManager(this);

    connect(manager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(replyFinished(QNetworkReply*)));

    qDebug() << QSslSocket::supportsSsl() << QSslSocket::sslLibraryBuildVersionString() << QSslSocket::sslLibraryVersionString();

    //qDebug() << "Fetching Current Versions";
    QNetworkRequest request;
    if (QSslSocket::supportsSsl()) {
        request.setUrl( QUrl( "https://www.KyHiTech.com/StationMapper/Linux.txt" ) );
    } else {
        request.setUrl( QUrl( "http://www.KyHiTech.com/StationMapper/Linux.txt" ) );
    }

    QString concatenated = "StationMapper:2MLxB1KK38radR9j";
    QByteArray data = concatenated.toLocal8Bit().toBase64();
    QString headerData = "Basic " + data;
    request.setRawHeader( "Authorization", headerData.toLocal8Bit() );

    manager->get(request);
}

void DownloaderMQ::replyFinished (QNetworkReply *reply) {
    if (DebugFunctions > 3) {qDebug() << Q_FUNC_INFO;}
    bool newFile = false;
    if(reply->error())
    {
        qDebug() << "ERROR!";
        qDebug() << reply->errorString();
        return;
    } else {
        //qDebug() << reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    }
        QString tempVer = "";
        QString tempFilename = "";
        QStringList lstServiceCodes = SettingsVars.ServiceCodes.split(",");
        //qDebug() << "Parsing CurrentVersion.txt";
        QTextStream in(reply->readAll());                 // read to text stream
        bool newFileDL = false;
        while (!in.atEnd()){
            int matchFlag = 0;
            QString fileLine = in.readLine();
            QStringList lineToken = fileLine.split(",", QString::SkipEmptyParts);
            if (DebugFunctions > 5) { qDebug() << fileLine << lineToken; }
            if (lineToken.size() > 1) {
                tempVer = lineToken.at(0);
                tempFilename = lineToken.at(1);


                if (tempFilename.contains("SC_")) {
                    matchFlag = 2;
                    if ((lstServiceCodes.size() >= 1)) {
                        for(int x = 0; x < lstServiceCodes.size(); x++) {
                            if (tempFilename.contains(lstServiceCodes.at(x))) {
                                matchFlag = 1;
//                                qDebug() << tempFilename;
                            }
                        }
                    }
                }

                if (matchFlag < 2) {newFileDL = true;} else {newFileDL = false;}
                for (int i = 0; i <= localVersions.size(); i++) {
                    if ((localVersions[i].FileName == tempFilename.remove("\""))  & (matchFlag < 2)) {
//                        qDebug() << localVersions[i].Version.toFloat() << tempVer.remove("\"").toFloat();
//                        qDebug() << localVersions[i].FileName << tempFilename.remove("\"");
                        if (localVersions[i].Version.toFloat() < tempVer.remove("\"").toFloat()) {
                            //newer version found
//                            qDebug() << "New file " << localVersions[i].FileName;
                            newFile = true;
                        } else {
                            newFileDL = false;
                        }
                    }
                }
            }
        }
        if (newFile || newFileDL) {
            ptrWindow->updateA("Update Available");
        } else {
            //ptrWindow->updateA("No Updates Available");
        }
        reply->deleteLater();
}

void parseCurrentVersionTxt() {
    if (DebugFunctions > 3) {qDebug() << Q_FUNC_INFO;}
    QFile file(SettingsVars.WorkingDir + "CurrentVersion.txt");
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << file.errorString() << endl ;
        qDebug() << "C:\\MSC\\StationMapper\\CurrentVersion.txt";
        qDebug() << "Missing";
        return;
    } else {
        QTextStream in(&file);
        while (!in.atEnd()) {
            // read one line from textstream(separated by "\n")
            QString fileLine = in.readLine();
            // parse the read line into separate pieces(tokens) with "," as the delimiter
            QStringList lineToken = fileLine.split(",", QString::SkipEmptyParts);
            // load parsed data to model accordingly
            if(lineToken.size() >= 2 && lineToken.at(0) != "") {
                QString tmpFn = lineToken.at(1);
                QString tmpVer = lineToken.at(0);
                //qDebug().noquote() << tmpFn << tmpVer.remove("\"");
                localVersions.add(tmpFn.remove("\"") ,tmpVer.remove("\""));
            }
        }
        //qDebug() << "localFileCount = " << localFileCount;
        file.close();
    }
    for (int idx = 0; idx <= localVersions.size() ; idx++) {
        //qDebug() << localVersions[idx].FileName << localVersions[idx].Version;
    }
    return;
}

