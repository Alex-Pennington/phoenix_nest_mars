#include "classaddresslookup.h"
#include "dlgsettingstab.h"

classAddressLookup::classAddressLookup(QObject *parent) :
    QObject(parent)
{
}

void classAddressLookup::doDownload(QString paramAddress)
{
    manager = new QNetworkAccessManager(this);

//    connect(manager, SIGNAL(finished(QNetworkReply*)),
//            parent(), SLOT(replyFinished(QNetworkReply*)));

    //connect(manager, &QNetworkAccessManager::finished,parent(), &dlgVOACAPsettings::addressLookupFinished);
    connect(manager, SIGNAL(finished(QNetworkReply*)),parent(), SLOT(addressLookupFinished(QNetworkReply*)));

    //qDebug() << "http://www.mapquestapi.com/geocoding/v1/address?key=ERrF3ApiMf7icpq2Dte5ZJ9ZV19Jkaoz&inFormat=kvp&outFormat=csv&location=" + City + "," + State +"&thumbMaps=false&delimiter=%2C";
    /*  "Country","State","County","City","PostalCode","Street","Lat","Lng","DragPoint","LinkId","Type","GeocodeQualityCode","GeocodeQuality","SideOfStreet","DisplayLat","DisplayLng"
        "US","KY","Boyd County","Ashland","","","38.475807","-82.646675","false","282028574","s","A5XAX","CITY","N","38.475807","-82.646675"*/
    manager->get(QNetworkRequest(QUrl("http://www.mapquestapi.com/geocoding/v1/address?key=ERrF3ApiMf7icpq2Dte5ZJ9ZV19Jkaoz&inFormat=kvp&outFormat=csv&location=" + paramAddress +"&thumbMaps=false&delimiter=%2C")));
}

void classAddressLookup::replyFinished (QNetworkReply *reply)
{
    /*"Country","State","County","City","PostalCode","Street","Lat","Lng","DragPoint","LinkId","Type","GeocodeQualityCode","GeocodeQuality","SideOfStreet","DisplayLat","DisplayLng"
      "US","KY","Boyd County","Ashland","","","38.475807","-82.646675","false","282028574","s","A5XAX","CITY","N","38.475807","-82.646675"*/

    if(reply->error())
    {
        qDebug() << "ERROR!";
        qDebug() << reply->errorString();
    }
    else
    {
        //qDebug() << reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    }



    QTextStream in2(reply->readAll());                 // read to text stream
    QString fileLine1 = in2.readLine();
    QString fileLine2 = in2.readLine();
    QStringList lineToken = fileLine2.split("\",\"");
//    SettingsVars.myLat = lineToken.at(14);
    //qDebug() << Lat[pos].toDouble();
    QString t = lineToken.at(15);
    t.chop(3);
//    SettingsVars.myLon = t;

    reply->deleteLater();

    return;
}
