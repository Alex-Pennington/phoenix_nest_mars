#ifndef CLASSGETLIGHTNING_H
#define CLASSGETLIGHTNING_H

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

class aerisLightning : public QObject
{
    Q_OBJECT
public:
    explicit aerisLightning(QObject *parent = 0);
    void doDownloadSummary(int idx);
    bool parseJSONSummary(QIODevice *data);

signals:

public slots:

    void downloadFinishedSummary(QNetworkReply *reply);



private:
   QNetworkAccessManager *manager;

};

extern aerisLightning getLightning;

#endif // CLASSGETLIGHTNING_H
