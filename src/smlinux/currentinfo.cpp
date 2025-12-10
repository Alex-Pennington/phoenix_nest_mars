#include "currentinfo.h"
#include "main.h"
#include "debug.h"

QTextEdit * currentinfo::teInfo = 0;

currentinfo::currentinfo(QWidget *parent) : QWidget(parent)
{
    mainLayout = new QGridLayout;

    teInfo = new QTextEdit;
    teInfo->setFont(SettingsVars.InfoFont);
    teInfo->setReadOnly(true);

    toolbar = new QToolBar;
    toolbar->setContentsMargins(0,0,0,0);
    toolbar->setMaximumHeight(23);

    QAction *newAct = new QAction("Reload", this);
    newAct->setShortcuts(QKeySequence::New);
    newAct->setStatusTip(tr("Reload currently selected source."));
    connect(newAct, &QAction::triggered, this, &currentinfo::selectedReload);
    toolbar->addAction(newAct);

    cmboUrls = new QComboBox();
    QString fileNameSettings = "/home/user/MSC/StationMapper/InfoURL.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    int size = settings.beginReadArray("InfoURL");
    //qDebug() << size;
    for (int idx = 0; idx < size ; idx++) {
        settings.setArrayIndex(idx);
        cmboUrls->addItem(settings.value("D").toString());
    }
    toolbar->addWidget(cmboUrls);
    connect(cmboUrls, SIGNAL(currentIndexChanged(int)), this, SLOT(selectedIndexChanged(int)));

    toolbar->addSeparator();

    QFontComboBox * cmbxFontFamily = new QFontComboBox();
    cmbxFontFamily->setCurrentFont(SettingsVars.NotesFont);
    connect(cmbxFontFamily, &QFontComboBox::currentFontChanged, this, &currentinfo::FontChanged);
    toolbar->addWidget(cmbxFontFamily);

    QAction *actFontScalarDec = new QAction("-", this);
    actFontScalarDec->setStatusTip(tr("Reduce font scalar"));
    connect(actFontScalarDec, &QAction::triggered, this, &currentinfo::selectedFontSizeDec);
    toolbar->addAction(actFontScalarDec);

    QAction *actFontScalarInc = new QAction("+", this);
    actFontScalarInc->setStatusTip(tr("Increase font scalar"));
    connect(actFontScalarInc, &QAction::triggered, this, &currentinfo::selectedFontSizeInc);
    toolbar->addAction(actFontScalarInc);

    toolbar->addSeparator();

    mainLayout->addWidget(toolbar,0,0,1,10);
    mainLayout->addWidget(teInfo,1,0,10,10);
    setLayout(mainLayout);
}

void currentinfo::FontChanged(const QFont &font) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    SettingsVars.InfoFont.setFamily(font.family());
    teInfo->setFont(SettingsVars.InfoFont);
    teInfo->update();
}

void currentinfo::selectedFontSizeInc() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (SettingsVars.InfoFont.pointSize() < 30) {
        SettingsVars.InfoFont.setPointSize( SettingsVars.InfoFont.pointSize() + 1 );
        teInfo->setFont(SettingsVars.InfoFont);
        teInfo->update();
    }
}

void currentinfo::selectedFontSizeDec() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (SettingsVars.InfoFont.pointSize() > 8) {
        SettingsVars.InfoFont.setPointSize( SettingsVars.InfoFont.pointSize() - 1 );
        teInfo->setFont(SettingsVars.InfoFont);
        teInfo->update();
    }
}

void currentinfo::readUrlIni() {
    if ((DebugFunctions > 0) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    QString fileNameSettings = "/home/user/MSC/StationMapper/InfoURL.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    int size = settings.beginReadArray("InfoURL");

    cmboUrls = new QComboBox();
    for (int idx = 0; idx <= size ; idx++) {
        settings.setArrayIndex(idx);
        cmboUrls->addItem(settings.value("D").toString());

    }
    toolbar->addWidget(cmboUrls);



}

void currentinfo::writeUrlIni() {
//    QString fileNameSettings = QCoreApplication::applicationDirPath()+"/InfoURL.ini";
//    QSettings settings(fileNameSettings,QSettings::IniFormat);

//    settings.beginWriteArray("InfoURL");
//    settings.setArrayIndex(0);
//    settings.setValue("D","Solar and Geophysical Activity Summary");
//    settings.setValue("Url","https://services.swpc.noaa.gov/text/sgas.txt");

//    settings.setArrayIndex(1);
//    settings.setValue("D","NOAA Space Weather Events");
//    settings.setValue("Url","https://services.swpc.noaa.gov/text/solar-geophysical-event-reports.txt");

//    settings.setArrayIndex(0);
//    settings.setValue("D","");
//    settings.setValue("Url","");

//    settings.setArrayIndex(0);
//    settings.setValue("D","");
//    settings.setValue("Url","");

//    settings.setArrayIndex(0);
//    settings.setValue("D","");
//    settings.setValue("Url","");

//    settings.setArrayIndex(0);
//    settings.setValue("D","");
//    settings.setValue("Url","");

//    settings.endArray();
}

void currentinfo::selectedReload() {
    if (DebugFunctions > 0) {qDebug() << Q_FUNC_INFO;}
    teInfo->setFont(SettingsVars.InfoFont);
    QString fileNameSettings = "/home/user/MSC/StationMapper/InfoURL.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    int size = settings.beginReadArray("InfoURL");
    settings.setArrayIndex(cmboUrls->currentIndex());
    qDebug() << settings.value("Url").toString();
    doDownload(settings.value("Url").toString());
}

void currentinfo::selectedIndexChanged(int index) {
    teInfo->setFont(SettingsVars.InfoFont);
    if (DebugFunctions > 0) {qDebug() << Q_FUNC_INFO;}
    QString fileNameSettings = "/home/user/MSC/StationMapper/InfoURL.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    int size = settings.beginReadArray("InfoURL");
    settings.setArrayIndex(index);
    qDebug() << settings.value("Url").toString();
    doDownload(settings.value("Url").toString());

}


void currentinfo::doDownload(QString iUrl) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO  << QSslSocket::supportsSsl() << QSslSocket::sslLibraryBuildVersionString() << QSslSocket::sslLibraryVersionString() << iUrl;}

    manager = new QNetworkAccessManager(this);

    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(replyFinished(QNetworkReply*)));

//    QNetworkRequest request;
//    request.setUrl( QUrl(iUrl ) );

//    manager->get(request);


    //QNetworkAccessManager *am = new QNetworkAccessManager(this);
    QString url;
    QString tmpSSLstatus = "";
    if (QSslSocket::supportsSsl()) {
        url = "https://www.KYHiTech.com/StationMapper/uploads/proxy.php";
        tmpSSLstatus = "True";
    } else {
        url = "http://www.KYHiTech.com/StationMapper/uploads/proxy.php";
        tmpSSLstatus = "False";
    }

    QNetworkRequest request0((QUrl(url)));
    QUrlQuery postData;
    postData.addQueryItem("param1", WorkingDB[SettingsVars.MyCallSign].CallSign);
    postData.addQueryItem("param2", QString::number(WorkingDB.size()));
    postData.addQueryItem("param3", tmpSSLstatus);
    postData.addQueryItem("param4", VERSION);
    postData.addQueryItem("param5", iUrl);
    QString concatenated0 = "StationMapper:2MLxB1KK38radR9j";
    QByteArray AuthData = concatenated0.toLocal8Bit().toBase64();
    QString headerData0 = "Basic " + AuthData;
    request0.setRawHeader( "Authorization", headerData0.toLocal8Bit() );
    request0.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    manager->post(request0, postData.toString(QUrl::FullyEncoded).toUtf8());

}

void currentinfo::replyFinished (QNetworkReply *reply) {
    if (DebugFunctions > 3) {qDebug() << Q_FUNC_INFO;}

    if(reply->error())
    {
        qDebug() << "ERROR!";
        qDebug() << reply->errorString();
        return;
    } else {
        //qDebug() << reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    }


    teInfo->clear();
    teInfo->append(reply->readAll());
    reply->deleteLater();
}
