#include "classmetar.h"
#include "main.h"
#include "debug.h"

QTextEdit * classmetar::teInfo = 0;

classmetar::classmetar(QWidget *parent) : QWidget(parent)
{
    mainLayout = new QGridLayout;

    teInfo = new QTextEdit;
    teInfo->setFont(SettingsVars.InfoFont);
    teInfo->setReadOnly(true);

    toolbar = new QToolBar;
    toolbar->setContentsMargins(0,0,0,0);
    toolbar->setMaximumHeight(23);

    QAction *newAct = new QAction("ICAO", this);
    newAct->setShortcuts(QKeySequence::New);
    newAct->setStatusTip(tr("Reload currently selected source."));
    connect(newAct, &QAction::triggered, this, &classmetar::selectedReload);
    toolbar->addAction(newAct);

    leICAO = new QLineEdit();
    leICAO->setPlaceholderText("Enter ICAO Codes: KSEA,KDFW,KLAX");
    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    QString tempICAO = settings.value("leICAO","KSEA,KDFW,KLAX").toString();
    leICAO->setText(tempICAO);
    connect(leICAO,&QLineEdit::returnPressed,this, &classmetar::selectedReload);
    toolbar->addWidget(leICAO);

    toolbar->addSeparator();

    QFontComboBox * cmbxFontFamily = new QFontComboBox();
    cmbxFontFamily->setCurrentFont(SettingsVars.NotesFont);
    connect(cmbxFontFamily, &QFontComboBox::currentFontChanged, this, &classmetar::FontChanged);
    toolbar->addWidget(cmbxFontFamily);

    QAction *actFontScalarDec = new QAction("-", this);
    actFontScalarDec->setStatusTip(tr("Reduce font scalar"));
    connect(actFontScalarDec, &QAction::triggered, this, &classmetar::selectedFontSizeDec);
    toolbar->addAction(actFontScalarDec);

    QAction *actFontScalarInc = new QAction("+", this);
    actFontScalarInc->setStatusTip(tr("Increase font scalar"));
    connect(actFontScalarInc, &QAction::triggered, this, &classmetar::selectedFontSizeInc);
    toolbar->addAction(actFontScalarInc);

    toolbar->addSeparator();

    mainLayout->addWidget(toolbar,0,0,1,10);
    mainLayout->addWidget(teInfo,1,0,10,10);
    setLayout(mainLayout);
}

void classmetar::FontChanged(const QFont &font) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    SettingsVars.InfoFont.setFamily(font.family());
    teInfo->setFont(SettingsVars.InfoFont);
    teInfo->update();
}

void classmetar::selectedFontSizeInc() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (SettingsVars.InfoFont.pointSize() < 30) {
        SettingsVars.InfoFont.setPointSize( SettingsVars.InfoFont.pointSize() + 1 );
        teInfo->setFont(SettingsVars.InfoFont);
        teInfo->update();
    }
}

void classmetar::selectedFontSizeDec() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (SettingsVars.InfoFont.pointSize() > 8) {
        SettingsVars.InfoFont.setPointSize( SettingsVars.InfoFont.pointSize() - 1 );
        teInfo->setFont(SettingsVars.InfoFont);
        teInfo->update();
    }
}

void classmetar::selectedReload() {
    if (DebugFunctions > 0) {qDebug() << Q_FUNC_INFO;}
    teInfo->setFont(SettingsVars.InfoFont);
    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.setValue("leICAO",leICAO->text());
    doDownload(leICAO->text());
}

void classmetar::selectedIndexChanged(int index) {
    teInfo->setFont(SettingsVars.InfoFont);
    if (DebugFunctions > 0) {qDebug() << Q_FUNC_INFO;}
    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    int size = settings.beginReadArray("InfoURL");
    settings.setArrayIndex(index);
    qDebug() << settings.value("Url").toString();
    doDownload(settings.value("Url").toString());

}

void classmetar::doDownload(QString searchTerm) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO  << QSslSocket::supportsSsl() << QSslSocket::sslLibraryBuildVersionString() << QSslSocket::sslLibraryVersionString();}

    manager = new QNetworkAccessManager(this);

    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(replyFinished(QNetworkReply*)));


    //https://aviationweather.gov/metar/data?ids=ksdf&format=raw&hours=0&taf=off&layout=off
    //https://aviationweather.gov/adds/dataserver_current/httpparam?dataSource=metars&requestType=retrieve&format=xml&hoursBeforeNow=1&mostRecentForEachStation=true&stationString=ksdf,klou
    QString url;
    QString tmpSSLstatus = "";
    if (QSslSocket::supportsSsl()) {
        url = "https://www.KYHiTech.com/StationMapper/uploads/proxyicao.php";
        tmpSSLstatus = "True";
    } else {
        url = "http://www.KYHiTech.com/StationMapper/uploads/proxyicao.php";
        tmpSSLstatus = "False";
    }

    QNetworkRequest request0((QUrl(url)));
    QUrlQuery postData;
    postData.addQueryItem("param1", WorkingDB[SettingsVars.MyCallSign].CallSign);
    postData.addQueryItem("param2", QString::number(WorkingDB.size()));
    postData.addQueryItem("param3", tmpSSLstatus);
    postData.addQueryItem("param4", VERSION);
    postData.addQueryItem("param5", searchTerm);
    QString concatenated0 = "StationMapper:2MLxB1KK38radR9j";
    QByteArray AuthData = concatenated0.toLocal8Bit().toBase64();
    QString headerData0 = "Basic " + AuthData;
    request0.setRawHeader( "Authorization", headerData0.toLocal8Bit() );
    request0.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    manager->post(request0, postData.toString(QUrl::FullyEncoded).toUtf8());
}

void classmetar::replyFinished (QNetworkReply *reply) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}

    if(reply->error())
    {
        qDebug() << "ERROR!";
        qDebug() << reply->errorString();
        return;
    } else {
        //qDebug() << reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    }

    QTextStream ts(reply->readAll());

    teInfo->clear();
    while (!ts.atEnd()) {
        QString fileLine = ts.readLine();
        if (fileLine.contains("<raw_text>") && fileLine.contains("</raw_text>")) {
            QString tempLine = fileLine.remove("<raw_text>").remove("</raw_text>").trimmed();
            if (1==1) {
                tempLine = "METAR/ " + tempLine + "//";
            }
            teInfo->append(tempLine);
        }
    }
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO  << reply->readAll();}
    reply->deleteLater();
}
