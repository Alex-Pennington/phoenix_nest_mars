#ifndef CURRENTINFO_H
#define CURRENTINFO_H

#include <QDialog>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QComboBox>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QToolBar>

class currentinfo : public QWidget
{
    Q_OBJECT

public:
    explicit currentinfo(QWidget *parent = nullptr);
    QGridLayout *mainLayout;
    static QTextEdit * teInfo;
    QToolBar *toolbar;
    void doDownload(QString Url);
    void readUrlIni();
    void writeUrlIni();
    QComboBox *cmboUrls;


signals:

public slots:
    void replyFinished (QNetworkReply *reply);
    void selectedIndexChanged(int index);
    void selectedReload();
    void FontChanged(const QFont &font);
    void selectedFontSizeInc();
    void selectedFontSizeDec();

private:
   QNetworkAccessManager *manager;


};

#endif // CURRENTINFO_H
