#ifndef UPDATECHECK_H
#define UPDATECHECK_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDateTime>
#include <QFile>
#include <QDebug>

class DownloaderMQ : public QObject
{
    Q_OBJECT
public:
    explicit DownloaderMQ(QObject *parent = 0);
    void doDownload();

signals:

public slots:
    void replyFinished (QNetworkReply *reply);

private:
   QNetworkAccessManager *manager;

};

void parseCurrentVersionTxt();

extern DownloaderMQ d;

#endif // UPDATECHECK_H
