#include "classweather.h"
#include "window.h"
#include "renderarea.h"
#include "main.h"
#include "debug.h"

#include <QJsonDocument>
#include <QJsonObject>
classweather dWeather;

classweather::classweather(QObject *parent) :
    QObject(parent)
{
}

void classweather::doDownload() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    manager = new QNetworkAccessManager(this);
    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(downloadFinished(QNetworkReply*)));

    QString tAccessID;
    QString tSecretID;

    QNetworkRequest request;
    QString radar = "false", lightning = "false", cyclone = "false", alerts = "false";

    QString url;
    QString tmpSSLstatus = "";
    tAccessID = "0toTJ4ERTl28bZhWRomeL";
    tSecretID = "LukfSbybvSrrbhgru7UuKGnalwnONxocsquh50Nr";
    QString mw = QString::number(MAP_WIDTH);
    QString mh = QString::number(MAP_HEIGHT);
    QString t1 = QString::number(WTerm1);
    QString t2 = QString::number(WTerm2);
    QString t3 = QString::number(WTerm3);
    QString t4 = QString::number(WTerm4);
    QString mapsRequested = "";
    bool firstRequest = true;
    if (SettingsVars.WeatherShown) {
        if (firstRequest) {
            mapsRequested = "radar";
            firstRequest = false;
            radar = "true";
        } else {
            mapsRequested += ",radar";
            radar = "true";
        }
    }
    if (SettingsVars.drawLightning) {
        if (firstRequest) {
            mapsRequested = "lightning-strikes-5m:100:blend(hard-light)";
            firstRequest = false;
            lightning = "true";
        } else {
            mapsRequested += ",lightning-strikes-5m:100:blend(hard-light)";
            lightning = "true";
        }
    }
    if (SettingsVars.getCyclone) {
        if (firstRequest) {
            mapsRequested = "tropical-cyclones";
            firstRequest = false;
            cyclone = "true";
        } else {
            mapsRequested += ",tropical-cyclones";
            cyclone = "true";
        }
    }
    if (SettingsVars.getAlerts) {
        if (firstRequest) {
            mapsRequested = "alerts";
            firstRequest = false;
            alerts = "true";
        } else {
            mapsRequested += ",alerts";
            alerts = "true";
        }
    }

    QString tmpURL;
    if (!SettingsVars.WeatherProxy) {
        if (QSslSocket::supportsSsl()) {
            tmpURL = "https://maps.aerisapi.com/" + tAccessID + "_" + tSecretID +"/terrain,rivers,roads," + mapsRequested + "/" + mw + "x" + mh + "/" + t1 + "," + t2 + "," + t3 + "," + t4 + "/current.png";
        } else {
            tmpURL = "http://maps.aerisapi.com/" + tAccessID + "_" + tSecretID +"/" + mapsRequested + "/" + mw + "x" + mh + "/" + t1 + "," + t2 + "," + t3 + "," + t4 + "/current.png";
        }
        //request.setUrl( QUrl( "https://maps.aerisapi.com/0toTJ4ERTl28bZhWRomeL_LukfSbybvSrrbhgru7UuKGnalwnONxocsquh50Nr/radar/825x858/24.4108,-92.014,39.3704,-75.0079/current.png" ) );
        request.setUrl(tmpURL);
        request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        request.setMaximumRedirectsAllowed(3);
        manager->get(request);
    }
    else {
        if (QSslSocket::supportsSsl()) {
            url = "https://www.KYHiTech.com/StationMapper/uploads/weather.php";
            tmpSSLstatus = "True";
        } else {
            url = "http://www.KYHiTech.com/StationMapper/uploads/weather.php";
            tmpSSLstatus = "False";
        }

        request.setUrl(QUrl(url));
        QUrlQuery postData;
        postData.addQueryItem("param1", WorkingDB[SettingsVars.MyCallSign].CallSign);
        postData.addQueryItem("param2", QString::number(WorkingDB.size()));
        postData.addQueryItem("param3", tmpSSLstatus);
        postData.addQueryItem("param4", VERSION);
        postData.addQueryItem("index", QString::number(currentMAP + 1));
        postData.addQueryItem("radar", radar);
        postData.addQueryItem("lightning", lightning);
        postData.addQueryItem("cyclone", cyclone);
        postData.addQueryItem("alerts", alerts);
        QString concatenated = "StationMapper:2MLxB1KK38radR9j";
        QByteArray AuthData = concatenated.toLocal8Bit().toBase64();
        QString headerData = "Basic " + AuthData;
        request.setRawHeader( "Authorization", headerData.toLocal8Bit() );
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        request.setMaximumRedirectsAllowed(3);
        manager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
        //qDebug() << postData.toString(QUrl::FullyEncoded).toUtf8();
    }
}

bool classweather::saveToDisk(const QString &filename, QIODevice *data){
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QFile file(SettingsVars.WorkingDir + filename);
    if (!file.open(QIODevice::WriteOnly)) {
        fprintf(stderr, "Could not open %s for writing: %s\n",
                qPrintable(filename),
                qPrintable(file.errorString()));
        return false;
    }
    file.write(data->readAll());
    file.close();

    QString JSONFile;
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    JSONFile = file.readAll();
    file.close();
    QJsonDocument jDoc = QJsonDocument::fromJson(JSONFile.toUtf8());
    if(jDoc.isObject() & !WorkingVars.haveDisplayedAerisAccessError) {
        QJsonObject jObject = jDoc.object();
        //{"error":{"code":"authorization_error","message":"This client does not have access to the Aeris Maps Platform: Maximum number of daily accesses reached."}}
        QJsonObject jError = jObject.value(QString("error")).toObject();
        QString tmpCode = jError.value(QString("code")).toString();
        QString tmpErrorMsg =  jError.value(QString("message")).toString();

        QMessageBox msgBox;
        msgBox.setText("Aeris Weather "  + tmpCode);
        msgBox.setInformativeText(tmpErrorMsg + "\r\n\r\nFollow instructions for creating a unique developer account set the AccessID and SecredID accordingly");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {}
        WorkingVars.haveDisplayedAerisAccessError = true;
    } else {
        ptrRenderArea->repaint();
        WorkingVars.haveDisplayedAerisAccessError = false;
    }
    return true;
}

void classweather::downloadFinished(QNetworkReply *reply) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    //catch 404 Not Found and other errors.
    QUrl url = reply->url();
    if (reply->error()) {}

    if (saveToDisk("weather.png", reply)) {
    }
    reply->deleteLater();
}
