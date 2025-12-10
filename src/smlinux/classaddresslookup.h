#ifndef CLASSADDRESSLOOKUP_H
#define CLASSADDRESSLOOKUP_H
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDateTime>
#include <QFile>
#include <QDebug>

class classAddressLookup : public QObject
{
    Q_OBJECT
public:
    explicit classAddressLookup(QObject *parent = 0);
    QNetworkAccessManager *manager;
    void doDownload(QString Address);

signals:

public slots:
    void replyFinished (QNetworkReply *reply);

private:

};

#endif // CLASSADDRESSLOOKUP_H
