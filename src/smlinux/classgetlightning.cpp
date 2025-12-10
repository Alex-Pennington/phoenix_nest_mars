#include "classgetlightning.h"
#include "main.h"
#include "renderarea.h"
#include "debug.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QLabel>

aerisLightning getLightning;

aerisLightning::aerisLightning(QObject *parent) :
    QObject(parent)
{
}

void aerisLightning::doDownloadSummary(int idx){
    if (DebugFunctions > 2) {qDebug() << "aerisLightning::doDownloadSummary";}
    manager = new QNetworkAccessManager(this);
    QString tAccessID;
    QString tSecretID;
    connect(manager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(downloadFinishedSummary(QNetworkReply*)));

    QNetworkRequest request;
    //qDebug() << QSslSocket::sslLibraryBuildVersionString();
//    if (SettingsVars.WAccessID != "") {
//        tAccessID = SettingsVars.WAccessID;
//    } else {
        tAccessID = "0toTJ4ERTl28bZhWRomeL";
//    }
//    if (SettingsVars.WSecretKey != "") {
//        tSecretID = SettingsVars.WSecretKey;
//    } else {
        tSecretID = "LukfSbybvSrrbhgru7UuKGnalwnONxocsquh50Nr";
//    }
//    QString tmpLon = WorkingDB[RosterVar[idx].relDB].Lon;
//    QString tmpLat = WorkingDB[RosterVar[idx].relDB].Lat;
    QString tmpLon = "";
    QString tmpLat = "";
    if ((SettingsVars.myLat != "") & (SettingsVars.myLon != "")) {
        tmpLat = SettingsVars.myLat;
        tmpLon = SettingsVars.myLon;
    } else {
        tmpLon = WorkingDB[idx].WGSLon;
        tmpLat = WorkingDB[idx].WGSLat;
    }
    QString tmpRadius = QString::number(SettingsVars.LightningSummaryRadius);
    QString tmpURL;
    if (QSslSocket::supportsSsl()) {
        tmpURL = "https://api.aerisapi.com/lightning/summary/"+tmpLat+","+tmpLon+"?&filter=cg&radius=" + tmpRadius + "miles&fields=summary.pulse&client_id=" + tAccessID +"&client_secret="+tSecretID;
    } else {
        tmpURL = "http://api.aerisapi.com/lightning/summary/"+tmpLat+","+tmpLon+"?&filter=cg&radius=" + tmpRadius + "miles&fields=summary.pulse&client_id=" + tAccessID +"&client_secret="+tSecretID;
    }


    if (DebugGeneralLevel > 5) {
        qDebug() << tmpURL;
    }
    //request.setUrl( QUrl( "https://api.aerisapi.com/lightning/summary/winter%20haven,%20fl?&filter=cg&radius=800miles&fields=summary.pulse&client_id=0toTJ4ERTl28bZhWRomeL&client_secret=LukfSbybvSrrbhgru7UuKGnalwnONxocsquh50Nr" ) );
    request.setUrl(tmpURL);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setMaximumRedirectsAllowed(3);

    manager->get(request);
}

bool aerisLightning::parseJSONSummary(QIODevice *data){
    if (DebugFunctions > 2) {qDebug() << "aerisLightning::parseJSONSummary";}
    QString s = QChar(0x2607);
    bool JSONsuccess = false;
    QJsonDocument jDoc = QJsonDocument::fromJson(data->readAll());

    //qDebug() << jDoc.isNull(); // <- print false :)
    QJsonObject jObject = jDoc.object();
    //() << jObject.value(QString("success"));
    //qDebug() << jObject.value(QString("error")).isNull();
    //qDebug() << jDoc;
    if (!jObject.value(QString("error")).isNull()) {
        QJsonObject jError =jObject.value(QString("error")).toObject();
        QString tmpErrorCode =  jError.value(QString("code")).toString();
        if (tmpErrorCode == "warn_no_data"){
            ptrWindow->statusLightningLbl->setStyleSheet("QLabel { background-color : #00ff00; color : black; }");
            ptrWindow->statusLightningLbl->setText("0 " + s);
            return true;
        } else if (tmpErrorCode == "invalid_location") {
            SettingsVars.drawLightning = false;
            SettingsVars.getLightningSummary = false;
            QMessageBox msgBox;
            msgBox.setText("Attention");
            msgBox.setInformativeText("Invalid station location for lightning data retrevial.  Using the settings dialog enter your callsign (must be in database) or lat/long in the format: 38.12121,-82.21212.  Disabiling lightning feature now.");
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            int ret = msgBox.exec();
            if (ret) {}
        } else {
            qDebug() << "Aeris Lightning ErrorCode :" << tmpErrorCode;
            return true;
        }
    }
    JSONsuccess = jObject.value(QString("success")).toBool();
    QJsonArray jResponse = jObject.value(QString("response")).toArray();
    QJsonObject jSummary = jResponse.at(0).toObject().value(QString("summary")).toObject();
    QJsonObject jPulse = jSummary.value(QString("pulse")).toObject();
    int tmpCount =  int(jPulse.value(QString("count")).toDouble());
    if (JSONsuccess) {
        QString tmpStr = QString::number(tmpCount) + " ";
        ptrWindow->statusLightningLbl->setStyleSheet("QLabel { background-color : yellow; color : black; }");
        ptrWindow->statusLightningLbl->setText(tmpStr + s);
    }
    return true;
}

void aerisLightning::downloadFinishedSummary(QNetworkReply *reply){
    if (DebugFunctions > 2) {qDebug() << " aerisLightning::downloadFinishedSummary";}
    //catch 404 Not Found and other errors.
    QUrl url = reply->url();

    if (parseJSONSummary(reply)) {
    }
    reply->deleteLater();
}
