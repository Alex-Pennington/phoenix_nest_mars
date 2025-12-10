#include "classsunspot.h"
#include "main.h"
#include "debug.h"

classsunspot::classsunspot(QObject *parent) :
    QObject(parent)
{
}

void classsunspot::doDownloadSS(){
    if ((DebugFunctions > 2)) {qDebug() << Q_FUNC_INFO;}
    manager = new QNetworkAccessManager(this);
    QString tAccessID;
    QString tSecretID;
    connect(manager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(downloadFinishedSS(QNetworkReply*)));

    QNetworkRequest request;

    QString tmpURL = "http://www.sidc.be/silso/DATA/EISN/EISN_current.csv";
    if (DebugGeneralLevel > 5) {
        qDebug() << tmpURL;
    }
    request.setUrl(tmpURL);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setMaximumRedirectsAllowed(3);

    manager->get(request);
}

bool classsunspot::parseSS(QIODevice *data){
    if ((DebugFunctions > 2)) {qDebug() << Q_FUNC_INFO;}
    QTextStream CSV(data->readAll());
    while (!CSV.atEnd()) {
        QString fileLine = CSV.readLine();
        QStringList lineToken = fileLine.split(",",QString::KeepEmptyParts);
        WorkingVars.SSN = lineToken.at(4).toInt();
    }
    return true;
}

void classsunspot::downloadFinishedSS(QNetworkReply *reply){
    if ((DebugFunctions > 2)) {qDebug() << Q_FUNC_INFO;}
    if(reply->error())
    {
        qDebug() << "ERROR!";
        qDebug() << reply->errorString();
    } else {
        parseSS(reply);
        QString s = QChar(0x2609);
        QString tmpStr = QString::number(WorkingVars.SSN) + " ";
        ptrWindow->statusSSNLbl->setText(tmpStr + s);
    }
    reply->deleteLater();
}
