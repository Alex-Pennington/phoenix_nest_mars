#include "main.h"
#include "debug.h"
#include "dbcreator.h"
#include "mainwindowtab.h"
#include "globals.h"


#include <QApplication>
#include <QtDebug>
#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QSettings>
#include <QMessageBox>
#include <QLockFile>


//Structs
structWorkingVar WorkingVars;
structSettingsVar SettingsVars;
structTermVar settingsTermVars;

//Classes
    Window * ptrWindow;
    dlgNCS * ptrExternDlgNCS = 0;
    QFileSystemWatcher logWatcher;
    QFileSystemWatcher watcher;
    clsDB WorkingDB;
    clsRoster RosterVar;
    classchan ChanDB;
//Settings Vars
    QString VERSION = "v0.71";
//Working Vars
    QString DB_Filename = "Working_DB_v312.csv";
    QString MAPbmp = "USNG_US_NOGRID.bmp";
    QString tempStream = "";
    //QList<QString> WorkingChans;
//    QString curLogfile = "";
//    QString prevLogfile = "";
    structMAPS varMAPS[20] = {};
    int currentMAP = 0;
//    QFile * ptrLogFile = new QFile;
    //QString WChanIdx[30];
    relFreq varRelFreq[25] = {};
    //Debug
        bool Debug_DrawBoxes = false;
        int DebugFunctions = 0;
        int DebugRenderAreaPaintEvent = 0;
        int DebugGeneralLevel = 0;
        int DebugCheckLogLevel = 0;
        int DebugclsRosterRemove = 0;
        int DebugFileWatcher = 0;
        int Debug_clsDB = 0;
        int Debug_clsRoster = 0;
        int Debug_Window =0;
        int DebugSettings = 0;
        int DebugProp = 0;
        int DebugNCS = 0;
        int DebugTerminal = 0;
    //RenderArea
        float ScaleFactor = 1;
        float PrevScaleFactor = 1;
        int Xoff = 0;
        int Yoff = 0;
        int MAP_HEIGHT = 869;
        int MAP_WIDTH = 1602;
        float minLon = -13998419;
        float maxLon = -7386334;
        float maxLat = 6386966;
        float minLat = 2804207;
        float WTerm1 = 0;
        float WTerm2 = 0;
        float WTerm3 = 0;
        float WTerm4 = 0;
        bool FirstRun = true;
        bool PointChanged  = false;
        bool ScaleChanged = false;
        bool dragging = false;
        int tmpWidth;
        int tmpHeight;

//Debug Vars
    bool DoPrintFunction = false;
    bool DoPrintFunctionfileLinesplit = false;


QTextStream& qStdOut()
{
    static QTextStream ts( stdout );
    return ts;
}

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString txt;
    switch (type) {
    case QtDebugMsg:
        txt = qFormatLogMessage(type, context, msg);
        break;
    case QtWarningMsg:
        txt = qFormatLogMessage(type, context, msg);
    break;
    case QtCriticalMsg:
        txt = qFormatLogMessage(type, context, msg);
    break;
    case QtInfoMsg:
        txt = qFormatLogMessage(type, context, msg);
    break;
    case QtFatalMsg:
        txt = qFormatLogMessage(type, context, msg);
        abort();
    }

    if (WorkingVars.Debug_to_File) {
        QFile outFile(SettingsVars.WorkingDir + "log.txt");
        outFile.open(QIODevice::WriteOnly | QIODevice::Append);
        //ptrLogFile = &outFile;
        QTextStream ts(&outFile);
        ts << txt << endl;
        outFile.close();
    } else {
        if((dlgLog::Log_textEdit != 0) && !WorkingVars.Closing && !txt.contains("unknown")) {
            dlgLog::Log_textEdit->append(txt);
        }
    }

}

QTextEdit * FeatureInstall::f_textEdit = 0;

FeatureInstall::FeatureInstall()
{
    f_textEdit = new QTextEdit;

    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->addWidget(f_textEdit);
    setLayout(mainLayout);

    setWindowTitle(tr("Extracting Feature"));
    resize(600,400);
}

bool CheckSig(QString tempVar) {
    QProcess process2;
    process2.setProgram(SettingsVars.WorkingDir + "portable_gpg/bin/gpg.exe");
    process2.setProcessChannelMode(QProcess::MergedChannels);
    process2.setArguments({"--verify",tempVar + ".sig"});

    QObject::connect(&process2, &QProcess::readyRead, [&process2](){
        QTextStream stream( &tempStream );
        stream << process2.readAll();
    });
    process2.start();
    process2.waitForFinished();
    process2.close();
    QTextStream in(&tempStream);                 // read to text stream
    qDebug() << tempStream;
    int TokenCount = 0;
    while (!in.atEnd()) {
            QString tempVar = in.readLine();
                //"Good signature from \"Alexander Keith Pennington <aar4te@organicengineer.com>\""
                //"Primary key fingerprint: 307E 1887 9713 820A CD98  7A23 3A4B F15C 60C7 5202"
                if  (tempVar.contains("Good signature from \"Alexander Keith Pennington <aar4te@organicengineer.com>\"")) {
                    TokenCount++;
                }
                if  (tempVar.contains("307E 1887 9713 820A CD98  7A23 3A4B F15C 60C7 5202") || tempVar.contains("307E18879713820ACD987A233A4BF15C60C75202") ) {
                    TokenCount++;
                }

    }
    if (TokenCount >= 2) {
        return 1;
    } else {
        return 0;
    }
}

int main(int argc, char *argv[]) {
    SettingsVars.VERSION = VERSION;
    QApplication app(argc, argv);

    QLockFile applicationLockFile("/home/user/MSC/StationMapper/StationMapper.lock");
    if(!applicationLockFile.tryLock(100)){
        //        qDebug() << "LOCK FILE LOCKED!!!";

        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText("This application is already running.");
        msgBox.exec();
        return 1;
    }

    qSetMessagePattern("%{file}(%{line}): %{message}");

    if (QFile::exists("C:/MSC/StationMapper/log.txt")) {
        QFile::remove("C:/MSC/StationMapper/log.txt");
        qDebug() << "Deleted old log.txt file.";
    }

    if (argc > 1) {
        QString tmpArg1 = argv[1];
        qDebug() << tmpArg1;
        if (tmpArg1.contains("--debug")){
            qDebug() << "running in --debug mode.";
            DebugFunctions = 9;
            DebugRenderAreaPaintEvent = 0;
            DebugGeneralLevel = 9;
            DebugCheckLogLevel = 9;
            DebugclsRosterRemove= 9;
            DebugFileWatcher = 9;
            Debug_clsDB = 7;
            Debug_clsRoster = 7;
            Debug_Window = 9;
            DebugSettings = 9;
            DebugProp = 9;
            DebugNCS = 9;
            DebugTerminal = 9;
            WorkingVars.Debug_to_File = true;
            qInstallMessageHandler(myMessageOutput);
        }
    } else if (1==1){
        DebugFunctions = 0;
        DebugRenderAreaPaintEvent = 0;
        DebugGeneralLevel = 0;
        DebugCheckLogLevel = 0;
        DebugclsRosterRemove = 0;
        DebugFileWatcher = 0;
        Debug_clsDB = 0;
        Debug_clsRoster = 0;
        Debug_Window = 5;
        DebugSettings = 0;
        DebugProp = 0;
        DebugNCS = 0;
        DebugTerminal = 0;
        WorkingVars.Debug_to_File = false;
        //qInstallMessageHandler(myMessageOutput);
    }

    app.setOrganizationName("MARS");
    app.setOrganizationDomain("KYHiTech.com");
    app.setApplicationName("StationMapper");
    QSettings settingsREG(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);

    SettingsVars.WorkingDir = settings.value("WorkingDir", "C:/MSC/StationMapper/").toString();
    SettingsVars.WorkingDir = "/home/user/MSC/StationMapper/";

    //Begin Wizard
    if (!settings.contains("cfgNCSapp")){
        SettingsVars.cfgNCSapp = 3;
        SettingsVars.NCSTab = true;
        settings.setValue("cfgNCSapp", SettingsVars.cfgNCSapp);
    }
    if (!settings.contains("WorkingChanCSV")){
        bool ok;
        SettingsVars.WorkingChanCSV = QInputDialog::getText(NULL,"Input Working Channels","Working channel list (CSV Format):", QLineEdit::Normal,"M135,M121,M037,M160,M166,M156",&ok).toUpper().remove(" ");
        settings.setValue("WorkingChanCSV", SettingsVars.WorkingChanCSV);
    }
    if (!settings.contains("AntAngle")) {
        bool ok;
        SettingsVars.AntAngle = QInputDialog::getText(NULL,"Input Antenna Angle","Antenna Main Lobe Bearing:", QLineEdit::Normal,"180",&ok);
        if (ok == true) {
            if (SettingsVars.AntAngle.toInt() > 359) {SettingsVars.AntAngle = "359";} else if (SettingsVars.AntAngle.toInt() <= 0) { SettingsVars.AntAngle = "1";}
            settings.setValue("AntAngle", SettingsVars.AntAngle);
        } else {
            SettingsVars.AntAngle = "180";
            settings.setValue("AntAngle", SettingsVars.AntAngle);
        }
    }
    if (!settings.contains("TrxPEP")) {
        bool ok;
        SettingsVars.TrxPEP = QInputDialog::getText(NULL,"Input PEP","Transmit Power (kW):", QLineEdit::Normal,"0.100",&ok);
        if (ok == true) {
            if (SettingsVars.TrxPEP.toFloat() < float(0.005)) {SettingsVars.TrxPEP = "0.005";} else if (SettingsVars.TrxPEP.toFloat() > float(2.000)) { SettingsVars.TrxPEP = "2.000";}
            settings.setValue("TrxPEP", SettingsVars.TrxPEP);
        } else {
            SettingsVars.TrxPEP = "0.100";
            settings.setValue("TrxPEP", SettingsVars.TrxPEP);
        }
    }
    if (!settings.contains("QssFile")) {
        QStringList lstQssFile;
        lstQssFile << "";
        QString tmpDir = SettingsVars.WorkingDir + "../QStyleSheets";
        QDir samplesDir(tmpDir);
        if (samplesDir.exists()) {
            QFileInfoList filesList = samplesDir.entryInfoList(QStringList() << "*.qss",QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
            for (int i = 0 ; i < filesList.size(); i++) {
                lstQssFile << filesList.at(i).filePath();
            }
            bool ok;
            QString qsRet = QInputDialog::getItem(NULL, "Select Qss Style Sheet", "Qss:", lstQssFile,0,false,&ok,Qt::Window);
            for (int i = 0 ; i < lstQssFile.size(); i++) {
                if (lstQssFile.at(i) == qsRet)
                    settings.setValue("QssFileIndex", i);
            }
            settings.setValue("QssFile", qsRet);
        } else {
            qDebug() << tmpDir << "Not Found";
        }
    }
    if (!settings.contains("GuardedRI")) {

    }
    //End Wizard

    //SettingsVars
    Xoff = settings.value("Xoff", 0).toInt();
    Yoff = settings.value("Yoff", 0).toInt();
    ScaleFactor = settings.value("ScaleFactor", 1.0).toFloat();
    PrevScaleFactor = settings.value("PrevScaleFactor", 1.0).toFloat();
    //SettingsVars.WorkingDir = settings.value("WorkingDir", "C:/MSC/StationMapper/").toString();
    SettingsVars.cfgNCSapp = settings.value("cfgNCSapp", 1).toInt();
    SettingsVars.ACPDDir = settings.value("ACPDDir", "").toString();
    SettingsVars.ACPncslogsDir = SettingsVars.ACPDDir + "ncslogs/";
    SettingsVars.SMV2PENDOUTDir = settings.value("SMV2PENDOUTDir", "C:/MSC/StationManagerV2/PENDING_OUT/").toString();
    SettingsVars.MyFont.setFamily(settings.value("MyFont","Arial").toString());
    SettingsVars.MyFont.setPointSize(settings.value("MyFontPointSize", 12 ).toInt());
    SettingsVars.MyFont.setStyleStrategy(QFont::PreferAntialias);
    SettingsVars.PointSize = SettingsVars.MyFont.pointSize();
    SettingsVars.FontScalar = settings.value("FontScalar", 3 ).toInt();
    SettingsVars.FontWarning = settings.value("FontWarning", false).toBool();
    currentMAP = settings.value("currentMAP", 0).toInt();
    SettingsVars.PreserveAspectRatio = settings.value("PreserveAspectRatio" , true).toBool();
    SettingsVars.ToolsAreaisShown = settings.value("ToolsAreaisShown", true).toBool();
    SettingsVars.DrawRelayLines = settings.value("DrawRelayLines", true).toBool();
    SettingsVars.DrawClosedStation = settings.value("DrawClosedStation", true).toBool();
    SettingsVars.GreyLineisShown = settings.value("GreyLineisShown", true).toBool();
    SettingsVars.WeatherShown = settings.value("WeatherShown", false).toBool();
    SettingsVars.WAccessID = settings.value("WAccessID","").toString();
    SettingsVars.WSecretKey = settings.value("WSecretKey","").toString();
    SettingsVars.drawLightning = settings.value("drawLightning",false).toBool();
    SettingsVars.MyCallSign = settings.value("MyCallSign", 9999).toInt();
    SettingsVars.myLat = settings.value("myLat", "").toString();
    SettingsVars.myLon = settings.value("myLon", "").toString();
    SettingsVars.getLightningSummary = settings.value("getLightningSummary",false).toBool();
    SettingsVars.LightningSummaryRadius = settings.value("LightningSummaryRadius", 100).toInt();
    SettingsVars.WorkingChanCSV = settings.value("WorkingChanCSV", "A1,A2,A3").toString();
    SettingsVars.AntAngle = settings.value("AntAngle", "90").toString();
    SettingsVars.TrxPEP = settings.value("TrxPEP", "0.100").toString();
    SettingsVars.AntennaIndex = settings.value("AntennaIndex", 17).toInt();
    SettingsVars.AntennaFile = settings.value("AntennaFile", "SAMPLE.23").toString();
    SettingsVars.getCyclone = settings.value("getCyclone", false).toBool();
    SettingsVars.channelCSV.fileNamePath = settings.value("channelCSV_fileNamePath", "").toString();
    SettingsVars.channelCSV.lastMod = settings.value("channelCSV_lastMod", "").toDateTime();
    SettingsVars.addressCSV.fileNamePath = settings.value("addressCSV_fileNamePath", "").toString();
    SettingsVars.addressCSV.lastMod = settings.value("addressCSV_lastMod", "").toDateTime();
    SettingsVars.getAlerts =  settings.value("getAlerts", false).toBool();
    SettingsVars.NCSTab = settings.value("NCSTab", true).toBool();
    SettingsVars.ComspotRI = settings.value("ComspotRI", "").toString();
    SettingsVars.ComspotFM = settings.value("ComspotFM", "").toString();
    SettingsVars.ComspotTO = settings.value("ComspotTO", "").toString();
    SettingsVars.ComspotINFO = settings.value("ComspotINFO", "").toString();
    SettingsVars.ComspotLOC = settings.value("ComspotLOC", "").toString();
    settingsTermVars.GuardedCallSigns = settings.value("GuardedCallSigns", "NONE").toString();
    settingsTermVars.PositionID = settings.value("PositionID", "StationMapper_v3Term").toString();
    settingsTermVars.IgnoreNotforMe = settings.value("IgnoreNotforMe", false).toBool();
    settingsTermVars.NoDisplayACK = settings.value("NoDisplayACK", false).toBool();
    settingsTermVars.TrackRMI = true; //settings.value("TrackRMI", true).toBool();
    SettingsVars.TerminalTab = settings.value("TerminalTab",true).toBool();
    settingsTermVars.sendToFile = settings.value("sendToFile",false).toBool();
    settingsTermVars.dbgRcvdcount = true; //settings.value("dbgRcvdcount",true).toBool();
    settingsTermVars.autoImportRoster = settings.value("autoImportRoster",true).toBool();
    SettingsVars.CoverageDefaultChanList = settings.value("CoverageDefaultChanList", "").toStringList();
    SettingsVars.CoverageDefaultChan = settings.value("CoverageDefaultChan", 0).toInt();
    SettingsVars.DefaultNetList = settings.value("DefaultNetList", "").toStringList();
    SettingsVars.DefaultNet = settings.value("DefaultNet", 0).toInt();
    settingsTermVars.GuardedRI = settings.value("GuardedRI","").toString();
    SettingsVars.QssFile = settings.value("QssFile","").toString();
    SettingsVars.QssFileIndex = settings.value("QssFileIndex","0").toInt();
    SettingsVars.NotesFont.setFamily(settings.value("NotesFontFamily","Arial").toString());
    SettingsVars.NotesFont.setPointSize(settings.value("NotesFontPointSize", 12 ).toInt());
    SettingsVars.NotesFont.setStyleStrategy(QFont::PreferAntialias);
    settingsTermVars.ReceivedFolder = "/home/user/MSC/StationMapper/MESSAGES/PENDING/"; //settings.value("ReceivedFolder","C:/home/user/MSC/StationMapper/MESSAGES/PENDING/").toString();
    SettingsVars.MessagesFolderRoot = "/home/user/MSC/StationMapper/MESSAGES/"; //settings.value("MessagesFolderRoot","/home/user/MSC/StationMapper/MESSAGES/").toString();
    SettingsVars.ArchiveFolder = "/home/user/MSC/StationMapper/MESSAGES/ARCHIVE/"; //settings.value("ArchiveFolder","/home/user/MSC/StationMapper/MESSAGES/ARCHIVE/").toString();
    if ((SettingsVars.ACPDDir == "") || SettingsVars.ACPDDir == "/") { SettingsVars.ACPDMsgNR = false;} else {SettingsVars.ACPDMsgNR = true;}
    SettingsVars.MARSALEDir = settings.value("MARSALEDir","/home/user/MSC/StationMapper/").toString();
    SettingsVars.FT817USB = settings.value("FT817USB",false).toBool();
    SettingsVars.InfoFont.setFamily(settings.value("InfoFontFamily","Arial").toString());
    SettingsVars.InfoFont.setPointSize(settings.value("InfoFontPointSize", 12 ).toInt());
    SettingsVars.InfoFont.setStyleStrategy(QFont::PreferAntialias);
    settingsTermVars.sendVZCZMMM = settings.value("sendVZCZMMM",false).toBool();
    SettingsVars.ServiceCodes = settings.value("ServiceCodes","ALL_MARS").toString();
    settings.setValue("GPGderyptionEnabled", 1);
    //SettingsVars.WeatherProxy = false;

    //END SettingsVars


    //QSS File Code
    QFile QssFile(SettingsVars.QssFile);
    if ((SettingsVars.QssFileIndex != 0 ) && QssFile.open(QFile::ReadOnly)) {
        QString StyleSheet = QLatin1String(QssFile.readAll());
        qApp->setStyleSheet(StyleSheet);
        QssFile.close();
    } else {
        //error qss file msg
    }//END QSS File Code

    // GPG decryption
    if (settings.contains("GPGderyptionEnabled")){
        QDir wDirGPGinstall;
        wDirGPGinstall.setPath(SettingsVars.WorkingDir);
        QStringList nameFilter0;
        nameFilter0 << "SC_*.gpg";
        QFileInfoList list0 = wDirGPGinstall.entryInfoList(nameFilter0, QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
        if (list0.size() > 0) {
            if (DebugFileWatcher > 0){ qDebug() << list0; }
            QString wFile = "";
            for (int i = 0;i<list0.size();i++) {
                QMessageBox msgInstaller;
                msgInstaller.setText("Attention");
                QString tmpString = "Decrypting new feature: " + QString::number(i+1) + " of " + QString::number(list0.size()) + "\r\n" + wFile + "\r\nStationMapper will start after the decryption and installation of the last feature.";
                msgInstaller.setInformativeText(tmpString);
                msgInstaller.setStandardButtons(QMessageBox::Ok);
                msgInstaller.setDefaultButton(QMessageBox::Ok);
                int ret = msgInstaller.exec();
                if (ret) { }
                FeatureInstall *FeatureInstallDialog = new FeatureInstall();
                FeatureInstallDialog->setModal(true);
                FeatureInstallDialog->show();
                FeatureInstallDialog->f_textEdit->append("Starting decryption");
                QApplication::processEvents();
                wFile = list0.at(i).filePath();
                if (DebugFileWatcher > 0){qDebug() << wFile << "decrypting";}
                QProcess process;
                process.setWorkingDirectory(SettingsVars.WorkingDir);
                process.setProgram("gpg");
                //C4583CE09383064C "Alex Pennington (StationMapperDistro) (Software Key 1) <StationMapper@KYHiTech.com>" Signing Key Fingerprint
                //gpg --pinentry-mode loopback --passphrase "2MLxB1KK38radR9j" -o file.ext -d file.ext.gpg
                //gpg -e -r C4583CE09383064C -o file.ext.gpg file.ext
                process.setArguments({"-o", wFile.left(wFile.size() - 4), "-d", wFile});
                qDebug() << wFile.left(wFile.size() - 4) <<  wFile;
                process.setProcessChannelMode(QProcess::MergedChannels);
                process.start();
                if (process.waitForStarted(-1)) {
                    while(process.waitForReadyRead(-1)) {
                        QApplication::processEvents();
                        FeatureInstallDialog->f_textEdit->append(process.readAll());
                    }
                }
                process.waitForFinished();
                QString tmpBuff = FeatureInstallDialog->f_textEdit->toPlainText();
                if (tmpBuff.contains("decryption failed")) {
                } else{
                    FeatureInstallDialog->close();
                    if (QFile::exists(wFile)) {
                        QFile::remove(wFile);
                    }
                }
                process.close();
            }
        }
    }

    if (settings.contains("GPGderyptionEnabled") && ( 1 == 2)){
        QDir wDirGPGinstall;
        wDirGPGinstall.setPath(SettingsVars.WorkingDir);
        QStringList nameFilter0;
        nameFilter0 << "*.gpg";
        QFileInfoList list0 = wDirGPGinstall.entryInfoList(nameFilter0, QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
        if (list0.size() > 0) {
            if (DebugFileWatcher > 0){ qDebug() << list0; }
            QString wFile = "";
            for (int i = 0;i<list0.size();i++) {
                QMessageBox msgInstaller;
                msgInstaller.setText("Attention");
                QString tmpString = "Decrypting new feature: " + QString::number(i+1) + " of " + QString::number(list0.size()) + "\r\n" + wFile + "\r\nStationMapper will start after the decryption and installation of the last feature.";
                msgInstaller.setInformativeText(tmpString);
                msgInstaller.setStandardButtons(QMessageBox::Ok);
                msgInstaller.setDefaultButton(QMessageBox::Ok);
                int ret = msgInstaller.exec();
                if (ret) { }
                FeatureInstall *FeatureInstallDialog = new FeatureInstall();
                FeatureInstallDialog->setModal(true);
                FeatureInstallDialog->show();
                FeatureInstallDialog->f_textEdit->append("Starting decryption");
                QApplication::processEvents();
                wFile = list0.at(i).filePath();
                if (DebugFileWatcher > 0){qDebug() << wFile << "decrypting";}
                QProcess process;
                process.setWorkingDirectory(SettingsVars.WorkingDir);
                process.setProgram("gpg");
                //C4583CE09383064C "Alex Pennington (StationMapperDistro) (Software Key 1) <StationMapper@KYHiTech.com>" Signing Key Fingerprint
                //gpg --pinentry-mode loopback --passphrase "2MLxB1KK38radR9j" -o file.ext -d file.ext.gpg
                //gpg -e -r C4583CE09383064C -o file.ext.gpg file.ext
                process.setArguments({"--pinentry-mode", "loopback","--passphrase","2MLxB1KK38radR9j",  "-o", wFile.left(wFile.size() - 4), "-d", wFile});
                qDebug() << wFile.left(wFile.size() - 4) <<  wFile;
                process.setProcessChannelMode(QProcess::MergedChannels);
                process.start();
                if (process.waitForStarted(-1)) {
                    while(process.waitForReadyRead(-1)) {
                        QApplication::processEvents();
                        FeatureInstallDialog->f_textEdit->append(process.readAll());
                    }
                }
                process.waitForFinished();
                //FeatureInstallDialog->close();
                process.close();
                if (QFile::exists(wFile)) {
                    QFile::remove(wFile);
                }
            }
        }
    }

    // End GPG decryption

    // ZIP File installer
    QDir wDir;
    wDir.setPath(SettingsVars.WorkingDir);
    QStringList nameFilter;
    nameFilter << "*.zip";
    QFileInfoList list = wDir.entryInfoList(nameFilter, QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
    if (list.size() > 0) {
        if (DebugFileWatcher > 0){ qDebug() << list; }
        QString wFile = "";
        for (int i = 0;i<list.size();i++) {
            QMessageBox msgInstaller;
            msgInstaller.setText("Attention");
            QString tmpString = "Installing new feature: " + QString::number(i+1) + " of " + QString::number(list.size()) + "\r\nStationMapper will start after the extraction of the last feature.";
            msgInstaller.setInformativeText(tmpString);
            msgInstaller.setStandardButtons(QMessageBox::Ok);
            msgInstaller.setDefaultButton(QMessageBox::Ok);
            int ret = msgInstaller.exec();
            if (ret) { }
            FeatureInstall *FeatureInstallDialog = new FeatureInstall();
            FeatureInstallDialog->setModal(true);
            FeatureInstallDialog->show();
            FeatureInstallDialog->f_textEdit->append("Starting Unzip");
            QApplication::processEvents();
            wFile = list.at(i).filePath();
            if (DebugFileWatcher > 0){qDebug() << wFile << "Unzipping";}
            QProcess process;
            process.setWorkingDirectory(SettingsVars.WorkingDir);
            process.setProgram("unzip");
            process.setArguments({"-o", wFile, "-d", SettingsVars.WorkingDir});
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.start();
            if (process.waitForStarted(-1)) {
                while(process.waitForReadyRead(-1)) {
                    QApplication::processEvents();
                    FeatureInstallDialog->f_textEdit->append(process.readAll());
                }
            }
            process.waitForFinished();
            FeatureInstallDialog->close();
            process.close();
            if (QFile::exists(wFile)) {
                QFile::remove(wFile);
            }
        }
    }// END ZIP File installer

    if (DebugGeneralLevel > 0) {
        qDebug() << Q_FUNC_INFO;
        qDebug() << QDir::currentPath();
    }

    QString tmpFile = SettingsVars.WorkingDir + DB_Filename;
    QFile file(tmpFile);
    if (!file.exists()) {
        qDebug() << DB_Filename <<  "!exists()";
        QString tmpAdressCSVfilename = QFileDialog::getOpenFileName(nullptr, ("Select address.csv File"),"C:\\MSC\\StationManagerV2\\data",("CSV (*.csv)"));
        if (tmpAdressCSVfilename == ""){
            QMessageBox msgBox;
            msgBox.setText("Attention");
            msgBox.setInformativeText("address.csv file import failed.\r\nLoading fictional roster.\r\nContains callsigns: ZZZ4AA - ZZZ4BE\r\nMyCall set to ZZZ4AA");
            msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Ok);
            int ret = msgBox.exec();
            QFile file(SettingsVars.WorkingDir + "WorkingDB_fictional.csv");
            file.rename("Working_DB_v312.csv");
            WorkingDB.searchCS("ZZZ4AA");
            SettingsVars.MyCallSign = WorkingDB.searchResult;
            settings.setValue("MyCallSign", SettingsVars.MyCallSign);
            LoadWorkingDB();
        } else if (importAddressCSVfile(tmpAdressCSVfilename)) {
            LoadWorkingDB();
            checkMyCallsign();
        } else {
            QMessageBox msgBox;
            msgBox.setText("Attention");
            msgBox.setInformativeText("address.csv file import failed.\r\nLoading fictional roster.\r\nContains callsigns: ZZZ4AA - ZZZ4BE\r\nMyCall set to ZZZ4AA");
            msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Ok);
            int ret = msgBox.exec();
            if (ret) {
                QFile file(SettingsVars.WorkingDir + "WorkingDB_fictional.csv");
                file.rename("Working_DB_v312.csv");
                WorkingDB.searchCS("ZZZ4AA");
                SettingsVars.MyCallSign = WorkingDB.searchResult;
                settings.setValue("MyCallSign", SettingsVars.MyCallSign);
            } else {

            }
        }
    } else if ((SettingsVars.addressCSV.lastMod.isValid()) & (SettingsVars.addressCSV.lastMod < QFileInfo(SettingsVars.addressCSV.fileNamePath).lastModified())) {
        qDebug() << SettingsVars.addressCSV.fileNamePath <<  "updated";
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("address.csv file has been updated.  Click OK to load new database.  Caution! this will erase any changes made by the Advanced Database Tool.");
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Ok);
        if (msgBox.exec()) {
            importAddressCSVfile(SettingsVars.addressCSV.fileNamePath);
            LoadWorkingDB();
            checkMyCallsign();
        } else {
            LoadWorkingDB();
        }
    } else {
        LoadWorkingDB();
    }

    if (SettingsVars.cfgNCSapp == 1) {
        QDir dir;
        QString tmpDir = SettingsVars.ACPDDir + "ncslogs/";
        if (dir.exists(tmpDir)) {
            dir.setPath(tmpDir);
            if (DebugGeneralLevel > 1) {qDebug() << tmpDir << "Found";}
            QFileInfoList list = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
            if (DebugGeneralLevel > 1) {qDebug() << list.size()  << "Files Found" << SettingsVars.ACPDDir+"ncslogs/";}
            if (list.size() !=0) {WorkingVars.curLogfile = list.first().filePath();}
            if (DebugGeneralLevel > 1) {qDebug() << WorkingVars.curLogfile;}
            watcher.addPath(tmpDir);
            MyClass* mc = new MyClass;
            QObject::connect(&watcher, SIGNAL(directoryChanged(QString)), mc, SLOT(showModified(QString)));
            logWatcher.addPath(WorkingVars.curLogfile);
        } else {
            QMessageBox msgBox;
            msgBox.setText("Attention");
            msgBox.setInformativeText("ACP Deluxe Directory not found.  Use settings dialog to set which NCS application to use and watch folder location.");
            msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Ok);
            int ret = msgBox.exec();
            if (ret) { }
        }
    } else if (SettingsVars.cfgNCSapp == 2) {
        QDir dir;
        dir.setPath(SettingsVars.SMV2PENDOUTDir);
        if (dir.exists()) {
            if (DebugGeneralLevel > 1) {qDebug() << SettingsVars.SMV2PENDOUTDir << "Found";}
            QStringList nameFilter;
            nameFilter << "*ROSTER*.txt";
            QFileInfoList list = dir.entryInfoList(nameFilter, QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
            if (DebugGeneralLevel > 1) {qDebug() << list.size() << "*ROSTER*.txt" << "Files Found";}
            if (list.size() != 0) {
                WorkingVars.curLogfile = list.first().filePath();
            }
            if (DebugGeneralLevel > 1) {qDebug() << WorkingVars.curLogfile;}
            watcher.addPath(SettingsVars.SMV2PENDOUTDir);
            QStringList directoryList = watcher.directories();
            MyClass* mc = new MyClass;
            QObject::connect(&watcher, SIGNAL(directoryChanged(QString)), mc, SLOT(showModified(QString)));
        } else {
            QMessageBox msgBox;
            msgBox.setText("Attention");
            msgBox.setInformativeText("Station Manager directory not found.Use settings dialog to set which NCS application to use and watch folder location.");
            msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Ok);
            int ret = msgBox.exec();
            if (ret) { }
        }
    }



    QString MAPScsvFile = SettingsVars.WorkingDir + "MAPS.csv";
    QFile MAPScsv(MAPScsvFile);
    if (!MAPScsv.open(QIODevice::ReadOnly)) {
        qDebug() << MAPScsv.errorString();
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("MAPS.csv database not found.");
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) { }
    } else {
        QTextStream inMAPScsv(&MAPScsv);
        int inMAPScsvLineCount = 0;
        QString fileLine = inMAPScsv.readLine();
        //qDebug() << fileLine;
        while (!inMAPScsv.atEnd()) {
            /* Handle Empty File or no token exception */
            fileLine = inMAPScsv.readLine();
            if (DebugGeneralLevel > 3) { qDebug() << fileLine;}
            QStringList lineToken = fileLine.split(",", QString::SkipEmptyParts);
            if ((lineToken.size() >=11)) {
                varMAPS[inMAPScsvLineCount].FileName = lineToken.at(0);
                varMAPS[inMAPScsvLineCount].MAP_HEIGHT = lineToken.at(1).toInt();
                varMAPS[inMAPScsvLineCount].MAP_WIDTH = lineToken.at(2).toInt();
                varMAPS[inMAPScsvLineCount].minLon = lineToken.at(3).toInt();
                varMAPS[inMAPScsvLineCount].maxLon = lineToken.at(4).toInt();
                varMAPS[inMAPScsvLineCount].maxLat = lineToken.at(5).toInt();
                varMAPS[inMAPScsvLineCount].minLat = lineToken.at(6).toInt();
                varMAPS[inMAPScsvLineCount].WTerm1 = lineToken.at(7).toFloat();
                varMAPS[inMAPScsvLineCount].WTerm2 = lineToken.at(8).toFloat();
                varMAPS[inMAPScsvLineCount].WTerm3 = lineToken.at(9).toFloat();
                varMAPS[inMAPScsvLineCount].WTerm4 = lineToken.at(10).toFloat();
                if (currentMAP == inMAPScsvLineCount) {
                    MAPbmp = varMAPS[currentMAP].FileName;
                    minLat = varMAPS[currentMAP].minLat;
                    minLon = varMAPS[currentMAP].minLon;
                    maxLat = varMAPS[currentMAP].maxLat;
                    maxLon = varMAPS[currentMAP].maxLon;
                    WTerm1 = varMAPS[currentMAP].WTerm1;
                    WTerm2 = varMAPS[currentMAP].WTerm2;
                    WTerm3 = varMAPS[currentMAP].WTerm3;
                    WTerm4 = varMAPS[currentMAP].WTerm4;
                    MAP_WIDTH = varMAPS[currentMAP].MAP_WIDTH;
                    MAP_HEIGHT = varMAPS[currentMAP].MAP_HEIGHT;
                }
                if (inMAPScsvLineCount > 19) {break;}
                inMAPScsvLineCount++;
            } else {
                QMessageBox msgBox;
                msgBox.setText("Attention");
                msgBox.setInformativeText("MAPS.csv database corrupt.");
                msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
                msgBox.setDefaultButton(QMessageBox::Ok);
                int ret = msgBox.exec();
                if (ret) { }
            }
        }
        MAPScsv.close();
    }

    QString CHANcsvFile = SettingsVars.WorkingDir + "channels.csv";
    QFile CHANcsv(CHANcsvFile);
    if (!CHANcsv.exists()) {
        QString ret =  QFileDialog::getOpenFileName(NULL, ("Select channels.csv File"),"C:\\MSC\\StationManagerV2\\data",("CSV (*.csv)"));
        SettingsVars.channelCSV.fileNamePath = ret;
        SettingsVars.channelCSV.lastMod = QFileInfo(ret).lastModified();
        settings.setValue("channelCSV_fileNamePath", SettingsVars.channelCSV.fileNamePath);
        settings.setValue("channelCSV_lastMod", SettingsVars.channelCSV.lastMod);
        if (!ret.isNull()) {
            if (QFile(SettingsVars.WorkingDir + "channels.csv").exists()){
                QFile(SettingsVars.WorkingDir + "channels.csv").remove();
            }
            QFile::copy(ret,(SettingsVars.WorkingDir + "channels.csv"));
        } else {
            QMessageBox msgBox;
            msgBox.setText("Attention");
            msgBox.setInformativeText("channel.csv file required for progagation functions.");
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            int ret = msgBox.exec();
            if (ret == QMessageBox::Ok) {
                qApp->quit();
                QProcess::startDetached(qApp->arguments()[0], qApp->arguments());
            }
            WorkingVars.isChannellsCSV = false;
        }
    }

    if (CHANcsv.open(QIODevice::ReadOnly)) {
        bool badFile = false;
        QTextStream inCHANcsv(&CHANcsv);
        int inCHANcsvLineCount = 0;
        QString fileLine = inCHANcsv.readLine();
        while (!inCHANcsv.atEnd()) {
            fileLine = inCHANcsv.readLine();
            QStringList lineToken = fileLine.split(" ", QString::SkipEmptyParts);
            if ((lineToken.size() == 2) && (fileLine.trimmed() != "")) {
                ChanDB.add(lineToken.at(0),lineToken.at(1).toDouble());
                inCHANcsvLineCount++;
                WorkingVars.isChannellsCSV = true;
            } else {
                badFile = true;
            }
        }
        CHANcsv.close();
        if (badFile) {
            QMessageBox msgBox;
            msgBox.setText("Attention");
            msgBox.setInformativeText("channels.csv database corrupt.\r\nPlease submit bug report.");
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            int ret = msgBox.exec();
            if (ret) { }
            WorkingVars.isChannellsCSV = false;
        }
    }

    QString tmpFilename = SettingsVars.WorkingDir + "Weather.png";
    if (QFile::exists(tmpFilename)) {
        QFile::remove(tmpFilename);
    }

    QString tmpDirScripts = SettingsVars.WorkingDir + "scripts/";
    QDir wDirScripts;
    wDirScripts.mkdir(tmpDirScripts);

    /*// Script File Execution
    wDir.setPath(tmpDirScripts);
    QStringList nameFilterScripts;
    nameFilterScripts << "*.bat";
    QFileInfoList listScripts = wDir.entryInfoList(nameFilterScripts, QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
    if (listScripts.size() > 0) {
        if (DebugFileWatcher > 0){ qDebug() << listScripts; }
        QString wFile = "";
        for (int i = 0;i<listScripts.size();i++) {
            wFile = listScripts.at(i).filePath();
            QMessageBox msgInstaller;
            msgInstaller.setText("Attention");
            QString tmpString = "Running script: " + QString::number(i+1) + " of " + QString::number(listScripts.size()) + "\r\n " + wFile;
            msgInstaller.setInformativeText(tmpString);
            msgInstaller.setStandardButtons(QMessageBox::Ok);
            msgInstaller.setDefaultButton(QMessageBox::Ok);
            int ret = msgInstaller.exec();
            if (ret == QMessageBox::Ok) {
                qDebug() << wFile;
            }
            if (CheckSig(wFile)) {
                FeatureInstall *FeatureInstallDialog = new FeatureInstall();
                FeatureInstallDialog->setModal(true);
                FeatureInstallDialog->show();
                FeatureInstallDialog->f_textEdit->append("Starting Script");
                QApplication::processEvents();
                if (DebugFileWatcher > 0){qDebug() << wFile << "Running Script";}
                QProcess process;
                process.setWorkingDirectory(SettingsVars.WorkingDir);
                process.setProgram(wFile);
                process.setProcessChannelMode(QProcess::MergedChannels);
                process.start();
                if (process.waitForStarted(-1)) {
                    while(process.waitForReadyRead(-1)) {
                        QApplication::processEvents();
                        FeatureInstallDialog->f_textEdit->append(process.readAll());
                    }
                }
                process.waitForFinished();
                FeatureInstallDialog->close();
                process.close();
                if (QFile::exists(wFile)) {
                    QFile::remove(wFile);
                    QFile::remove(wFile+".sig");
                }
            }
        }
    }// END Script File Execution*/

    Window *window;
    window = new Window;
    ptrWindow = window;

    window->show();

    if (SettingsVars.cfgNCSapp ==1) {
        QObject::connect(&logWatcher, SIGNAL(fileChanged(QString)), window, SLOT(callLogChecker(QString)));
    }

    return app.exec();
}
