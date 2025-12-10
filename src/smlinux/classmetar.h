#ifndef CLASSMETAR_H
#define CLASSMETAR_H

#include <QDialog>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QComboBox>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QToolBar>
#include <QWidget>

class classmetar : public QWidget
{
    Q_OBJECT
public:
    explicit classmetar(QWidget *parent = nullptr);
    QGridLayout *mainLayout;
    static QTextEdit * teInfo;
    QToolBar *toolbar;
    void doDownload(QString Url);
    QLineEdit * leICAO;

signals:

public slots:
    void replyFinished (QNetworkReply *reply);
    void selectedIndexChanged(int index);
    void selectedReload();
    void FontChanged(const QFont &font);
    void selectedFontSizeInc();
    void selectedFontSizeDec();
    //void loadICAO();

private:
   QNetworkAccessManager *manager;
};

#endif // CLASSMETAR_H
