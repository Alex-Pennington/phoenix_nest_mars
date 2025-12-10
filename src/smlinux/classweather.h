#ifndef CLASSWEATHER_H
#define CLASSWEATHER_H


#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

class classweather : public QObject
{
    Q_OBJECT
public:
    explicit classweather(QObject *parent = 0);
    void doDownload();

signals:

public slots:
    void downloadFinished (QNetworkReply *reply);
    bool saveToDisk(const QString &filename, QIODevice *data);

private:
   QNetworkAccessManager *manager;

};

extern classweather dWeather;
#endif // CLASSWEATHER_H
