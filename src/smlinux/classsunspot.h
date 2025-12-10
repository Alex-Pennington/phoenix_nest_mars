#ifndef CLASSSUNSPOT_H
#define CLASSSUNSPOT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QString>

class classsunspot : public QObject
{
    Q_OBJECT
public:
    explicit classsunspot(QObject *parent = 0);
    void doDownloadSS();
    bool parseSS(QIODevice *data);

public slots:
    void downloadFinishedSS(QNetworkReply *reply);



private:
   QNetworkAccessManager *manager;

};

extern classsunspot getSunSpot;

#endif // CLASSSUNSPOT_H
