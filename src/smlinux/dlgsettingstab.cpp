#include "globals.h"
#include "dlgsettingstab.h"
#include "dbcreator.h"
#include "debug.h"
#include "main.h"
#include "classaddresslookup.h"
#include "classauthtable.h"

#include <QVBoxLayout>
#include <QDebug>

dlgSettingsTab::dlgSettingsTab(QWidget *parent): QDialog(parent) {
    if ((DebugFunctions > 1) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}

    tabWidget = new QTabWidget;
    ptrdlggeneralsettings = new dlggeneralsettings(this);
    tabWidget->addTab(ptrdlggeneralsettings, tr("General"));
    ptrdlgVOACAPsettings = new dlgVOACAPsettings(this);
    tabWidget->addTab(ptrdlgVOACAPsettings, tr("Station"));
    ptrdlgNCSsettings = new dlgNCSsettings(this);
    tabWidget->addTab(ptrdlgNCSsettings, tr("NCS"));
    ptrdlgTerminalSettings = new dlgTerminalSettings(this);
    tabWidget->addTab(ptrdlgTerminalSettings, tr("Terminal"));
    ptrdlgweathersettings = new dlgweathersettings(this);
    tabWidget->addTab(ptrdlgweathersettings, tr("Weather"));
    ptrdlgDatabaseSettings = new dlgDatabaseSettings(this);
    tabWidget->addTab(ptrdlgDatabaseSettings, tr("Database"));
    ptrdlgDebug = new dlgDebug(this);
    tabWidget->addTab(ptrdlgDebug, tr("Debug"));
    ptrdlgHighLighterSettings = new dlgHighLighterSettings(this);
    tabWidget->addTab(ptrdlgHighLighterSettings, tr("Highlighter"));
    ptrdlgDatabaseEditor = new dlgDatabaseEditor(this, &WorkingDB);
    tabWidget->addTab(ptrdlgDatabaseEditor, tr("DB Viewer"));

    connect(ptrdlgDatabaseSettings, &dlgDatabaseSettings::updateMyCall, this, &dlgSettingsTab::saveMyCall);

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel|QDialogButtonBox::Save);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &dlgSettingsTab::OK);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &dlgSettingsTab::Cancel);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(tabWidget);
    mainLayout->addWidget(buttonBox);
    setLayout(mainLayout);

    setWindowTitle(tr("Settings"));
}

void dlgSettingsTab::saveMyCall() {
    ptrdlgVOACAPsettings->leMyCallSign->setText(WorkingDB[SettingsVars.MyCallSign].CallSign);
}

dlggeneralsettings::dlggeneralsettings(QWidget *parent) : QWidget(parent) {
    if ((DebugFunctions > 1) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    QVBoxLayout *mainLayout = new QVBoxLayout;

    QGroupBox *horizontalGroupBox0 = new QGroupBox(tr("Select NCS Application"));
    QGridLayout *layout0 = new QGridLayout;
    rdoACP = new QRadioButton("ACP Deluxe NCS");
    rdoSMv2 = new QRadioButton("StationManager NCSPro");
    rdoAdHoc = new QRadioButton("StationMapper");
    if (SettingsVars.cfgNCSapp == 1) {
        rdoACP->setChecked(true);
    } else if (SettingsVars.cfgNCSapp == 2) {
        rdoSMv2->setChecked(true);
    } else if (SettingsVars.cfgNCSapp == 3) {
        rdoAdHoc->setChecked(true);
    }
    layout0->addWidget(rdoACP,0,0,Qt::AlignLeft);
    layout0->addWidget(rdoSMv2,0,1,Qt::AlignCenter);
    layout0->addWidget(rdoAdHoc,0,3,Qt::AlignRight);
    horizontalGroupBox0->setLayout(layout0);
    /*-----------------------------------------------*/
    loadQSS();
    cmboQSSFile = new QComboBox;
    cmboQSSFile->addItems(lstQssFile);
    cmboQSSFile->setCurrentIndex(SettingsVars.QssFileIndex);
    QGroupBox *horizontalGroupBox01 = new QGroupBox(tr("QSS File Select"));
    QGridLayout *layout00 = new QGridLayout;
    //QPushButton * pbSelectQSSFile;
    //pbSelectQSSFile = new QPushButton;
    //connect(pbSelectQSSFile,&QPushButton::clicked,this,&dlggeneralsettings::selectedpbSelectQSSFileActn);
    //pbSelectQSSFile->setText("...");
    layout00->addWidget(cmboQSSFile,0,0,1,7);
    //layout00->addWidget(pbSelectQSSFile,0,6,1,1);
    horizontalGroupBox01->setLayout(layout00);
    /*-----------------------------------------------*/
    MARSALEDir = new QLineEdit;
    MARSALEDir->setText(SettingsVars.MARSALEDir);
    QPushButton * MARSALEDirBtn;
    MARSALEDirBtn = new QPushButton;
    QGroupBox *horizontalGroupBox112 = new QGroupBox(tr("Select Root MARS-ALE Folder"));
    QGridLayout *layout112 = new QGridLayout;
    connect(MARSALEDirBtn,&QPushButton::clicked,this,&dlggeneralsettings::selectedMARSALEDirBtn);
    MARSALEDirBtn->setText("...");
    layout112->addWidget(MARSALEDir,0,0,1,6);
    layout112->addWidget(MARSALEDirBtn,0,6,1,1);
    horizontalGroupBox112->setLayout(layout112);
    /*-----------------------------------------------*/
    MessagesFolder = new QLineEdit;
    MessagesFolder->setText(SettingsVars.MessagesFolderRoot);
    QPushButton * MessagesSelectFolderBtn;
    MessagesSelectFolderBtn = new QPushButton;
    QGroupBox *horizontalGroupBox11 = new QGroupBox(tr("Select Root Messages Folder"));
    QGridLayout *layout11 = new QGridLayout;
    connect(MessagesSelectFolderBtn,&QPushButton::clicked,this,&dlggeneralsettings::selectedMessagesSelectFolderBtn);
    MessagesSelectFolderBtn->setText("...");
    layout11->addWidget(MessagesFolder,0,0,1,6);
    layout11->addWidget(MessagesSelectFolderBtn,0,6,1,1);
    horizontalGroupBox11->setLayout(layout11);
    /*-----------------------------------------------*/
    ArchiveFolder = new QLineEdit;
    ArchiveFolder->setText(SettingsVars.ArchiveFolder);
    QPushButton * ArchiveSelectFolderBtn;
    ArchiveSelectFolderBtn = new QPushButton;
    QGroupBox *horizontalGroupBox111 = new QGroupBox(tr("Select Archive Folder"));
    QGridLayout *layout111 = new QGridLayout;
    connect(ArchiveSelectFolderBtn,&QPushButton::clicked,this,&dlggeneralsettings::selectedArchiveSelectFolderBtn);
    ArchiveSelectFolderBtn->setText("...");
    layout111->addWidget(ArchiveFolder,0,0,1,6);
    layout111->addWidget(ArchiveSelectFolderBtn,0,6,1,1);
    horizontalGroupBox111->setLayout(layout111);
    /*-----------------------------------------------*/
    ACPFolderTB = new QLineEdit;
    ACPFolderTB->setText(SettingsVars.ACPDDir);
    QPushButton * ACPSelectFolderBtn;
    ACPSelectFolderBtn = new QPushButton;
    QGroupBox *horizontalGroupBox1 = new QGroupBox(tr("Select ACP Deluxe Folder"));
    QGridLayout *layout1 = new QGridLayout;
    connect(ACPSelectFolderBtn,&QPushButton::clicked,this,&dlggeneralsettings::selectedACPSlctFldrBtnActn);
    ACPSelectFolderBtn->setText("...");
    layout1->addWidget(ACPFolderTB,0,0,1,6);
    layout1->addWidget(ACPSelectFolderBtn,0,6,1,1);
    horizontalGroupBox1->setLayout(layout1);
    /*-----------------------------------------------*/
    SMV2FolderTB = new QLineEdit;
    SMV2FolderTB->setText(SettingsVars.SMV2PENDOUTDir);
    QPushButton * SMV2SelectFolderBtn;
    SMV2SelectFolderBtn = new QPushButton;
    QGroupBox *horizontalGroupBox2 = new QGroupBox(tr("Select Station Manager V2 PENDING_OUT Folder"));
    connect(SMV2SelectFolderBtn,&QPushButton::clicked,this,&dlggeneralsettings::selectedPENDOUTSlctFldrBtnActn);
    QGridLayout *layout2 = new QGridLayout;
    SMV2SelectFolderBtn->setText("...");
    layout2->addWidget(SMV2FolderTB,0,0,1,6);
    layout2->addWidget(SMV2SelectFolderBtn,0,6,1,1);
    horizontalGroupBox2->setLayout(layout2);
    /*-----------------------------------------------*/
    ServiceCodes = new QLineEdit;
    ServiceCodes->setText(SettingsVars.ServiceCodes);
    QGroupBox *horizontalGroupBoxServiceCodes = new QGroupBox(tr("Enter Service Codes (Optional)"));
    QGridLayout *layoutServiceCodes = new QGridLayout;
    layoutServiceCodes->addWidget(ServiceCodes,0,0,1,6);
    horizontalGroupBoxServiceCodes->setLayout(layoutServiceCodes);
    /*-----------------------------------------------*/
    QGroupBox *horizontalGroupBox21 = new QGroupBox(tr("Settings"));
    QGridLayout *layout21 = new QGridLayout;
    chkbxAspect = new QCheckBox("Preserve Aspect Ratio", this);
    chkbxAspect->setToolTip("When checked map display aspect ratio is locked,\r\nwhen unchecked the map is allowed to distort to conform to the aspect ratio of the resized window.");
    chkbxAspect->setChecked(SettingsVars.PreserveAspectRatio);
    chkbxToolsAreaisShown = new QCheckBox("Calculator", this);
    chkbxToolsAreaisShown->setChecked(SettingsVars.ToolsAreaisShown);
    chkbxToolsAreaisShown->setToolTip("Displays the distance and bearing calculator.");
    chkbxRelayLinesisShown = new QCheckBox("Relay Lines", this);
    chkbxRelayLinesisShown->setChecked(SettingsVars.DrawRelayLines);
    chkbxRelayLinesisShown->setToolTip("Displays dash-dot lines connecting stations with a status of (T=4XX).");
    chkbxClosedStationisShown = new QCheckBox("Station Status", this);
    chkbxClosedStationisShown->setChecked(SettingsVars.DrawClosedStation);
    chkbxClosedStationisShown->setToolTip("Shows the status of the station on the map.\r\nClosed station appear in red and crossed out.");
    chkbxGreyLineisShown = new QCheckBox("Grey Line", this);
    chkbxGreyLineisShown->setChecked(SettingsVars.GreyLineisShown);
    chkbxGreyLineisShown->setToolTip("Shows a thin black line representing the solar terminator (grey line).");
    //QPushButton * selectFont = new QPushButton("Font",this);
    //selectFont->setToolTip("Select font for station display");
    //connect(selectFont,&QPushButton::clicked, this, &dlggeneralsettings::selectedFont);
    layout21->addWidget(chkbxAspect,0,0,Qt::AlignLeft);
    layout21->addWidget(chkbxToolsAreaisShown,0,1,Qt::AlignLeft);
    //layout21->addWidget(selectFont,0,2,Qt::AlignCenter);
    layout21->addWidget(chkbxRelayLinesisShown,1,0,Qt::AlignLeft);
    layout21->addWidget(chkbxClosedStationisShown,1,1,Qt::AlignLeft);
    layout21->addWidget(chkbxGreyLineisShown,1,2,Qt::AlignLeft);
    horizontalGroupBox21->setLayout(layout21);
    mainLayout->addWidget(horizontalGroupBox01);
    mainLayout->addWidget(horizontalGroupBox0);
    mainLayout->addWidget(horizontalGroupBox112);
    mainLayout->addWidget(horizontalGroupBox11);
    mainLayout->addWidget(horizontalGroupBox111);
    mainLayout->addWidget(horizontalGroupBox1);
    mainLayout->addWidget(horizontalGroupBox2);
    mainLayout->addWidget(horizontalGroupBox21);
    mainLayout->addWidget(horizontalGroupBoxServiceCodes);
    setLayout(mainLayout);

}

void dlggeneralsettings::selectedMARSALEDirBtn() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString ret = QFileDialog::getExistingDirectory(this, ("Select MARS-ALE Root Folder"),"C:/MARS-ALE/",QFileDialog::ShowDirsOnly);
    if (ret !="") {
        MARSALEDir->setText(ret + "/");
        SettingsVars.MARSALEDir = ret + "/";
    }
}

void dlggeneralsettings::selectedMessagesSelectFolderBtn() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString ret = QFileDialog::getExistingDirectory(this, ("Select Messages Root Folder"),"C:/MSC/StationMapper/MESSAGES/",QFileDialog::ShowDirsOnly);
    if (ret !="") {
        MessagesFolder->setText(ret + "/");
        SettingsVars.MessagesFolderRoot = ret + "/";
    }
}

void dlggeneralsettings::selectedArchiveSelectFolderBtn() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString ret = QFileDialog::getExistingDirectory(this, ("Select Archive Folder"),"C:/MSC/StationMapper/MESSAGES/ARCHIVE/",QFileDialog::ShowDirsOnly);
    if (ret !="") {
        ArchiveFolder->setText(ret + "/");
        SettingsVars.ArchiveFolder = ret + "/";
    }
}

void dlggeneralsettings::loadQSS() {
    if ((DebugFunctions > 1) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    QString tmpDir = SettingsVars.WorkingDir + "../QStyleSheets";
    QDir samplesDir(tmpDir);
    if (samplesDir.exists()) {
        QFileInfoList filesList = samplesDir.entryInfoList(QStringList() << "*.qss",QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
        //qDebug() << filesList;
        lstQssFile << "";
        for (int i = 0 ; i < filesList.size(); i++) {
            lstQssFile << filesList.at(i).filePath();
        }
    } else {
        //error msg
        qDebug() << tmpDir << "Not Found";
    }
    if ((DebugFunctions > 1) || (DebugSettings > 1)) {qDebug() << Q_FUNC_INFO << "EXIT";}
}

void dlggeneralsettings::selectedACPSlctFldrBtnActn() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString ret = QFileDialog::getExistingDirectory(this, ("Select ACP Deluxe root folder"),"C:\\MSC\\",QFileDialog::ShowDirsOnly);
    if (ret !="") {
        ACPFolderTB->setText(ret);
    }
}

void dlggeneralsettings::selectedpbSelectQSSFileActn() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString ret = QFileDialog::getOpenFileName(this, ("Select Qss File"),"C:\\MSC\\QStyleSheets");
    if (ret !="") {
        cmboQSSFile->addItem(ret);
        cmboQSSFile->setCurrentIndex(cmboQSSFile->count()-1);
    }
}

void dlggeneralsettings::selectedPENDOUTSlctFldrBtnActn() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString ret = QFileDialog::getExistingDirectory(this, ("Select Station Manager V2 PENDNIG_OUT folder"),"C:\\MSC\\",QFileDialog::ShowDirsOnly);
    if (ret !="") {
        SMV2FolderTB->setText(ret);
    }
}

void dlggeneralsettings::selectedFont() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (!SettingsVars.FontWarning) {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("Font size selected from the following window applies only to the widest zoom and only when not preserving aspect ratio. Use font scalar to change font size while zoomed in or when preserving aspect ratio is selected. Selecting cancel below will prevent this box from showing in the future.");
        msgBox.setStandardButtons(QMessageBox::Ok|QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret == QMessageBox::Cancel) {
            //QSettings settings(QCoreApplication::organizationName(), QoreApplication::applicationName());
            QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
            QSettings settings(fileNameSettings,QSettings::IniFormat);
            SettingsVars.FontWarning = true;
            settings.setValue("FontWarning", SettingsVars.FontWarning);
        }
    }
    bool ok;
    QFont tmpMyFont = QFontDialog::getFont(&ok, QFont(SettingsVars.MyFont.family(), SettingsVars.MyFont.pointSize()), this, "Select Font",QFontDialog::ScalableFonts);

    if (ok) {
        SettingsVars.MyFont = tmpMyFont;
        //QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
        QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
        QSettings settings(fileNameSettings,QSettings::IniFormat);
        settings.setValue("MyFont", SettingsVars.MyFont.family());
        settings.setValue("MyFontPointSize", SettingsVars.MyFont.pointSize() );
        SettingsVars.PointSize = SettingsVars.MyFont.pointSize();
    } else {

    }
}

dlgVOACAPsettings::dlgVOACAPsettings(QWidget *parent)  : QWidget(parent) {
    if ((DebugFunctions > 1) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    addressLookup = new classAddressLookup(this);
    QVBoxLayout *mainLayout = new QVBoxLayout;
    /*-----------------------------------------------*/
    QGroupBox *horizontalGroupBox01 = new QGroupBox(tr("Station Paramaters"));
    QGridLayout *layout01 = new QGridLayout;
    QLabel * lblMyCallSign = new QLabel;
    lblMyCallSign->setText("Call Sign");
    leMyCallSign = new QLineEdit;
    if (SettingsVars.MyCallSign != 9999){
        tmpCallSign = WorkingDB[SettingsVars.MyCallSign].CallSign;
    } else {
        tmpCallSign = "";
    }
    leMyCallSign->setText(tmpCallSign);
    leMyCallSign->setToolTip("Your call sign is used for the lightning feature to determine the centerpoint of the 25mi warning radius.\r\nCall sign QTH is determined from the centerpoint of your MGRS square or City,St data.");
    QLabel * lblLat = new QLabel;
    lblLat->setText("Latitude");
    leLat = new QLineEdit;
    leLat->setText(SettingsVars.myLat);
    leLat->setToolTip("Your lat/long is used for the lightning feature to determine the centerpoint of the 25mi warning radius.\r\nThis is more accurate than entering your CallSign.");
    QLabel * lblLon = new QLabel;
    lblLon->setText("Longitude");
    leLon = new QLineEdit;
    leLon->setText(SettingsVars.myLon);
    leLon->setToolTip("Your lat/long is used for the lightning feature to determine the centerpoint of the 25mi warning radius.\r\nThis is more accurate than entering your CallSign.");
    QLabel * lblAntAngle = new QLabel;
    lblAntAngle->setText("Ant >");
    leAntAngle = new QLineEdit;
    leAntAngle->setText(SettingsVars.AntAngle);
    leAntAngle->setToolTip("Antenna Main Lobe Angle (Perpendicular for Dipole)");
    QLabel * lblTrxPower = new QLabel;
    lblTrxPower->setText("PEP");
    leTrxPower = new QLineEdit;
    leTrxPower->setText(SettingsVars.TrxPEP);
    leTrxPower->setToolTip("PEP in kW");
    layout01->addWidget(lblMyCallSign,0,0,1,1,Qt::AlignRight);
    layout01->addWidget(leMyCallSign,0,2,1,1,Qt::AlignLeft);
    layout01->addWidget(lblAntAngle,1,0,1,1,Qt::AlignRight);
    layout01->addWidget(leAntAngle,1,2,1,1,Qt::AlignLeft);
    layout01->addWidget(lblTrxPower,2,0,1,1,Qt::AlignRight);
    layout01->addWidget(leTrxPower,2,2,1,1,Qt::AlignLeft);
    layout01->addWidget(lblLat,0,4,1,1,Qt::AlignRight);
    layout01->addWidget(leLat,0,5,1,2,Qt::AlignLeft);
    layout01->addWidget(lblLon,1,4,1,1,Qt::AlignRight);
    layout01->addWidget(leLon,1,5,1,2,Qt::AlignLeft);
    QPushButton *LookupAddress = new QPushButton("Lookup Lat/Lon", this);
    LookupAddress->setToolTip("Select font for station display");
    connect(LookupAddress,&QPushButton::clicked, this, &dlgVOACAPsettings::lookupAddress);
    layout01->addWidget(LookupAddress,2,4,1,3);
    horizontalGroupBox01->setLayout(layout01);
    /*-----------------------------------------------*/
    QGroupBox *horizontalGroupBox011 = new QGroupBox(tr("Enter Working Channel Designators (CSV) 24 MAX"));
    QGridLayout *layout011 = new QGridLayout;
    leWorkingChan = new QLineEdit;
    leWorkingChan->setText(SettingsVars.WorkingChanCSV);
    leWorkingChan->setToolTip("Enter your designated working channels to be used with VOACAP");
    //QPushButton * SelectWChanBtn;
    //SelectWChanBtn = new QPushButton;
    //SelectWChanBtn->setText("...");
    layout011->addWidget(leWorkingChan,1,0,1,6);
    //layout011->addWidget(SelectWChanBtn,1,6,1,1);
    horizontalGroupBox011->setLayout(layout011);
    /*-----------------------------------------------*/
    QGroupBox *horizontalGroupBox0111 = new QGroupBox(tr("Select Transmit Antenna Model"));
    QGridLayout *layout0111 = new QGridLayout;
    cmboAnt = new QComboBox;
    //loadVOACAPAnt();
    cmboAnt->addItems(lstAntDesc);
    cmboAnt->setCurrentIndex(SettingsVars.AntennaIndex);
    layout0111->addWidget(cmboAnt,1,0,1,6);
    horizontalGroupBox0111->setLayout(layout0111);
    /*-----------------------------------------------*/


    mainLayout->addWidget(horizontalGroupBox01);
    mainLayout->addWidget(horizontalGroupBox011);
    mainLayout->addWidget(horizontalGroupBox0111);
    setLayout(mainLayout);

}

void dlgVOACAPsettings::lookupAddress() {
    QString ret = QInputDialog::getText(this,"Address","Address:", QLineEdit::Normal,"123 Main St. Atlants, GA");
    addressLookup->doDownload(ret);
}

void dlgVOACAPsettings::addressLookupFinished(QNetworkReply *reply) {
    if(reply->error())
    {
        qDebug() << "ERROR!";
        qDebug() << reply->errorString();
    }
    else
    {
        //qDebug() << reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    }



    QTextStream in2(reply->readAll());                 // read to text stream
    QString fileLine1 = in2.readLine();
    QString fileLine2 = in2.readLine();
    QStringList lineToken = fileLine2.split("\",\"");
    if (lineToken.size() >=16) {
        leLat->setText(lineToken.at(14));
        QString t = lineToken.at(15);
        t.chop(3);
        leLon->setText(t);
    }
    reply->deleteLater();

    return;
}

void dlgVOACAPsettings::loadVOACAPAnt() {/*
    if ((DebugFunctions > 1) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    QString tmpDir = SettingsVars.WorkingDir + "itshfbc/antennas/samples";
    QDir samplesDir(tmpDir);
    if (samplesDir.exists()) {
        QFileInfoList filesList = samplesDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
        //qDebug() << filesList;
        for (int i = 0 ; i < filesList.size(); i++) {
            QFile fileAnt(filesList.at(i).filePath());
            if ( fileAnt.open(QIODevice::ReadOnly) ) {
                QString fileLine = fileAnt.readLine();
                QStringList lineToken = fileLine.split(":");
                if (lineToken.size() == 2) {
                    AntennaVar[i].fileName = filesList.at(i).fileName();
                    QString tmpString = lineToken.at(1);
                    AntennaVar[i].Description = tmpString.remove("\r\n");
                    lstAntDesc << AntennaVar[i].Description;
                    if (DebugSettings > 5) {qDebug() << Q_FUNC_INFO << AntennaVar[i].fileName << AntennaVar[i].Description;}
                } else {
                    AntennaVar[i].fileName = filesList.at(i).fileName();
                    QString tmpString = fileLine;
                    AntennaVar[i].Description = tmpString.remove("\r\n");
                    lstAntDesc << AntennaVar[i].Description;
                    if (DebugSettings > 5) {qDebug() << Q_FUNC_INFO << AntennaVar[i].fileName << AntennaVar[i].Description;}
                }
            }
        }
    } else {
        //error msg
        qDebug() << tmpDir << "Not Found";
    }
    if ((DebugFunctions > 1) || (DebugSettings > 1)) {qDebug() << Q_FUNC_INFO << "EXIT";}
    */
}

dlgweathersettings::dlgweathersettings(QWidget *parent) : QWidget(parent) {
    if ((DebugFunctions > 1) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    QVBoxLayout *mainLayout = new QVBoxLayout;
    QGroupBox *OverlaysGBox = new QGroupBox("Select Overlays");
    QHBoxLayout *layout0 = new QHBoxLayout;
//    QPushButton * gotoAeris;
//    gotoAeris = new QPushButton;
//    gotoAeris->setText("Signup for IDs");
//    connect(gotoAeris,&QPushButton::clicked,this,&dlgweathersettings::gotoAeris);
    QGroupBox *horizontalGroupBox211 = new QGroupBox(tr("Weather via https://www.aerisweather.com/signup/developer/"));
    QGridLayout *layout211 = new QGridLayout;

    chkbxWeatherShown = new QCheckBox("Radar", this);
    chkbxWeatherShown->setChecked(SettingsVars.WeatherShown);
    chkbxWeatherShown->setToolTip("Displays weather radar overlay on map. Updated every 5 minutes.");
    chkbxDrawLightning = new QCheckBox("Lightning", this);
    chkbxDrawLightning->setChecked(SettingsVars.drawLightning);
    chkbxDrawLightning->setToolTip("Displays lightning strike data on map.");
    chkbxgetCyclone = new QCheckBox("Cyclone", this);
    chkbxgetCyclone->setToolTip("Displays cyclone tracks overlay on map. Updated every 5 minutes.");
    chkbxgetCyclone->setChecked(SettingsVars.getCyclone);
    chkbxgetAlerts = new QCheckBox("Alerts", this);
    chkbxgetAlerts->setToolTip("Displays cyclone tracks overlay on map. Updated every 5 minutes.");
    chkbxgetAlerts->setChecked(SettingsVars.getAlerts);

    layout0->addWidget(chkbxWeatherShown);
    layout0->addWidget(chkbxDrawLightning);
    layout0->addWidget(chkbxgetCyclone);
    layout0->addWidget(chkbxgetAlerts);
    OverlaysGBox->setLayout(layout0);

    chkbxGetLightningSummary = new QCheckBox("Lightning Summary", this);
    chkbxGetLightningSummary->setChecked(SettingsVars.getLightningSummary);
    chkbxGetLightningSummary->setToolTip("Displays lightning sumary data, number of strikes for a given radius, every 5 minutes in status area.");
    QLabel * lblLightningRadius = new QLabel;
    lblLightningRadius->setText("Summary Radius (mi)");
    leLightningRadius = new QLineEdit;
    leLightningRadius->setText(QString::number(SettingsVars.LightningSummaryRadius,10));
//    lblWAccessID = new QLabel;
//    lblWAccessID->setText("AccessID");
//    leWAccessID = new QLineEdit;
//    leWAccessID->setText(SettingsVars.WAccessID);
//    lblWSecretID = new QLabel;
//    lblWSecretID->setText("SecretID");
//    leWSecretKey = new QLineEdit;
//    leWSecretKey->setText(SettingsVars.WSecretKey);
    layout211->addWidget(OverlaysGBox,0,0,1,4);
    layout211->addWidget(chkbxGetLightningSummary,1,0,1,1,Qt::AlignLeft);
    layout211->addWidget(lblLightningRadius,1,1,Qt::AlignRight);
    layout211->addWidget(leLightningRadius,1,2,1,2);
//    layout211->addWidget(lblWAccessID,2,0,1,1,Qt::AlignRight);
//    layout211->addWidget(leWAccessID,2,1,1,3);
//    layout211->addWidget(lblWSecretID,3,0,1,1,Qt::AlignRight);
//    layout211->addWidget(leWSecretKey,3,1,1,3);
//    layout211->addWidget(gotoAeris,4,0,1,4);
    horizontalGroupBox211->setLayout(layout211);
    mainLayout->addWidget(horizontalGroupBox211);
    setLayout(mainLayout);

}

void dlgweathersettings::gotoAeris() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString link = "https://www.aerisweather.com/signup/developer/";
    QDesktopServices::openUrl(QUrl(link));
}

void dlgSettingsTab::OK() {
    if ((DebugFunctions > 1) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    bool restartRequired = false;
    bool updateMyCallsign = false;
    if (SettingsVars.ACPDDir != ptrdlggeneralsettings->ACPFolderTB->text()){
        if (ptrdlggeneralsettings->ACPFolderTB->text().at(ptrdlggeneralsettings->ACPFolderTB->text().size() -1) == "/") {
        SettingsVars.ACPDDir  = ptrdlggeneralsettings->ACPFolderTB->text();
        restartRequired = true;
        } else {
            SettingsVars.ACPDDir  = ptrdlggeneralsettings->ACPFolderTB->text() + "/";
            restartRequired = true;
        }
    }  
    if (SettingsVars.SMV2PENDOUTDir  != ptrdlggeneralsettings->SMV2FolderTB->text()){
        if (ptrdlggeneralsettings->SMV2FolderTB->text().at(ptrdlggeneralsettings->SMV2FolderTB->text().size() - 1) == "/") {
            SettingsVars.SMV2PENDOUTDir  = ptrdlggeneralsettings->SMV2FolderTB->text();
            restartRequired = true;
        } else {
            SettingsVars.SMV2PENDOUTDir  = ptrdlggeneralsettings->SMV2FolderTB->text() + "/";
            restartRequired = true;
        }
    }
    if (((ptrdlggeneralsettings->chkbxAspect->checkState() == Qt::Checked) & !SettingsVars.PreserveAspectRatio) || ((ptrdlggeneralsettings->chkbxAspect->checkState() == Qt::Unchecked) & SettingsVars.PreserveAspectRatio)) {
        if (ptrdlggeneralsettings->chkbxAspect->checkState() == Qt::Checked) {
            SettingsVars.PreserveAspectRatio = true;
            restartRequired = true;
        } else {
            SettingsVars.PreserveAspectRatio = false;
            restartRequired = true;
        }
    }
    if (((ptrdlggeneralsettings->rdoACP->isChecked() == true) & (SettingsVars.cfgNCSapp != 1)) || (((ptrdlggeneralsettings->rdoSMv2->isChecked() == true) & (SettingsVars.cfgNCSapp != 2))) || (((ptrdlggeneralsettings->rdoAdHoc->isChecked() == true) & (SettingsVars.cfgNCSapp != 3)))) {
        if (ptrdlggeneralsettings->rdoSMv2->isChecked() == true) {
            SettingsVars.cfgNCSapp = 2;
            SettingsVars.NCSTab = false;
            restartRequired = true;
        } else if (ptrdlggeneralsettings->rdoACP->isChecked() == true) {
            SettingsVars.cfgNCSapp = 1;
            SettingsVars.NCSTab = false;
            restartRequired = true;
        } else if (ptrdlggeneralsettings->rdoAdHoc->isChecked() == true) {
            SettingsVars.cfgNCSapp = 3;
            SettingsVars.NCSTab = true;
            restartRequired = true;
        }
    }
    if (((ptrdlggeneralsettings->chkbxToolsAreaisShown->checkState() == Qt::Checked) & (SettingsVars.ToolsAreaisShown == false)) || ((ptrdlggeneralsettings->chkbxToolsAreaisShown->checkState() == Qt::Unchecked) & (SettingsVars.ToolsAreaisShown == true)))   {
        if (ptrdlggeneralsettings->chkbxToolsAreaisShown->checkState() == Qt::Checked) {
            SettingsVars.ToolsAreaisShown = true;
            restartRequired = true;
        } else if (ptrdlggeneralsettings->chkbxToolsAreaisShown->checkState() == Qt::Unchecked) {
            SettingsVars.ToolsAreaisShown = false;
            restartRequired = true;
        }
    }
    if (((ptrdlggeneralsettings->chkbxRelayLinesisShown->checkState() == Qt::Checked) & (SettingsVars.DrawRelayLines == false)) || ((ptrdlggeneralsettings->chkbxRelayLinesisShown->checkState() == Qt::Unchecked) & (SettingsVars.DrawRelayLines == true)))   {
        if (ptrdlggeneralsettings->chkbxRelayLinesisShown->checkState() == Qt::Checked) {
            SettingsVars.DrawRelayLines = true;
        } else if (ptrdlggeneralsettings->chkbxRelayLinesisShown->checkState() == Qt::Unchecked) {
            SettingsVars.DrawRelayLines = false;
        }
    }
    if (((ptrdlggeneralsettings->chkbxClosedStationisShown->checkState() == Qt::Checked) & (SettingsVars.DrawClosedStation == false)) || ((ptrdlggeneralsettings->chkbxClosedStationisShown->checkState() == Qt::Unchecked) & (SettingsVars.DrawClosedStation == true)))   {
        if (ptrdlggeneralsettings->chkbxClosedStationisShown->checkState() == Qt::Checked) {
            SettingsVars.DrawClosedStation = true;
        } else if (ptrdlggeneralsettings->chkbxClosedStationisShown->checkState() == Qt::Unchecked) {
            SettingsVars.DrawClosedStation = false;
        }
    }
    if (((ptrdlggeneralsettings->chkbxGreyLineisShown->checkState() == Qt::Checked) & (SettingsVars.GreyLineisShown == false)) || ((ptrdlggeneralsettings->chkbxGreyLineisShown->checkState() == Qt::Unchecked) & (SettingsVars.GreyLineisShown == true)))   {
        if (ptrdlggeneralsettings->chkbxGreyLineisShown->checkState() == Qt::Checked) {
            SettingsVars.GreyLineisShown = true;
        } else if (ptrdlggeneralsettings->chkbxGreyLineisShown->checkState() == Qt::Unchecked) {
            SettingsVars.GreyLineisShown = false;
        }
    }
    bool UpdateWeatherNow = false;
    if (((ptrdlgweathersettings->chkbxWeatherShown->checkState() == Qt::Checked) & (SettingsVars.WeatherShown == false)) || ((ptrdlgweathersettings->chkbxWeatherShown->checkState() == Qt::Unchecked) & (SettingsVars.WeatherShown == true)))   {
        if (ptrdlgweathersettings->chkbxWeatherShown->checkState() == Qt::Checked) {
            SettingsVars.WeatherShown = true;
        } else if (ptrdlgweathersettings->chkbxWeatherShown->checkState() == Qt::Unchecked) {
            SettingsVars.WeatherShown = false;
        }
        UpdateWeatherNow = true;
    }
    QString tmp_leCallSign = ptrdlgVOACAPsettings->leMyCallSign->text().toUpper();
    if (tmp_leCallSign == "") {
        SettingsVars.MyCallSign = 9999;
    }
    if (tmp_leCallSign != ptrdlgVOACAPsettings->tmpCallSign) {
        if (WorkingDB.searchCS(tmp_leCallSign))  {
            SettingsVars.MyCallSign = WorkingDB.searchResult;
            updateMyCallsign = true;
        } else if (tmp_leCallSign == "") {
            SettingsVars.MyCallSign = 9999;
        }else {
            QMessageBox msgBox;
            msgBox.setText("Attention");
            msgBox.setInformativeText("Parameter entered for Call Sign was not found in the database.  Please check your entry.");
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            int ret = msgBox.exec();
            if (ret) {}
            SettingsVars.MyCallSign = 9999;
            SettingsVars.getLightningSummary = false;
        }
    }
    if (((ptrdlgweathersettings->chkbxDrawLightning->checkState() == Qt::Checked) & (SettingsVars.drawLightning == false)) || ((ptrdlgweathersettings->chkbxDrawLightning->checkState() == Qt::Unchecked) & (SettingsVars.drawLightning == true)))   {
        if (ptrdlgweathersettings->chkbxDrawLightning->checkState() == Qt::Checked) {
            SettingsVars.drawLightning = true;
        } else if (ptrdlgweathersettings->chkbxDrawLightning->checkState() == Qt::Unchecked) {
            SettingsVars.drawLightning = false;
        }
        UpdateWeatherNow = true;
    }
    int tmpRadius = ptrdlgweathersettings->leLightningRadius->text().toInt();
    if (tmpRadius != SettingsVars.LightningSummaryRadius) {
        if (tmpRadius < 5) {
            SettingsVars.LightningSummaryRadius = 5;
        } else if (tmpRadius > 800) {
            SettingsVars.LightningSummaryRadius = 800;
        } else {
            SettingsVars.LightningSummaryRadius = ptrdlgweathersettings->leLightningRadius->text().toInt();
        }
        ptrWindow->updateLightning();
    }
    if (ptrdlgVOACAPsettings->leLat->text() != SettingsVars.myLat) {
        SettingsVars.myLat = ptrdlgVOACAPsettings->leLat->text();
    }
    if (ptrdlgVOACAPsettings->leLon->text() != SettingsVars.myLon) {
        SettingsVars.myLon = ptrdlgVOACAPsettings->leLon->text();
    }

    bool haveLoc = false;
    if (SettingsVars.MyCallSign != 9999) {
        haveLoc = true;
    } else if ((SettingsVars.myLon != "") & (SettingsVars.myLat != "")) {
        haveLoc = true;
    } else {
        haveLoc = false;
    }
    if (((ptrdlgweathersettings->chkbxGetLightningSummary->checkState() == Qt::Checked) & (SettingsVars.getLightningSummary == false)) || ((ptrdlgweathersettings->chkbxGetLightningSummary->checkState() == Qt::Unchecked) & (SettingsVars.getLightningSummary == true)))   {
        if (ptrdlgweathersettings->chkbxGetLightningSummary->checkState() == Qt::Checked) {
            SettingsVars.getLightningSummary = true;
            ptrWindow->updateLightning();
        } else if (ptrdlgweathersettings->chkbxGetLightningSummary->checkState() == Qt::Unchecked) {
            SettingsVars.getLightningSummary = false;
        }
    }
    if (((ptrdlgweathersettings->chkbxgetCyclone->checkState() == Qt::Checked) & (SettingsVars.getCyclone == false)) || ((ptrdlgweathersettings->chkbxgetCyclone->checkState() == Qt::Unchecked) & (SettingsVars.getCyclone == true)))   {
        if (ptrdlgweathersettings->chkbxgetCyclone->checkState() == Qt::Checked) {
            SettingsVars.getCyclone = true;
        } else if (ptrdlgweathersettings->chkbxgetCyclone->checkState() == Qt::Unchecked) {
            SettingsVars.getCyclone = false;
        }
        UpdateWeatherNow = true;
    }
    if (SettingsVars.getLightningSummary & !haveLoc)    {
        SettingsVars.getLightningSummary = false;
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("Call sign or Station Lat/Lon must be set to use the lightning feature.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {}
    }
    if (updateMyCallsign) {
        if (SettingsVars.getLightningSummary) {
            ptrWindow->updateLightning();
        }
    }

    /*//ptrdlgVOACAPsettings->leAntAngle
    if (ptrdlgVOACAPsettings->leAntAngle->text() != SettingsVars.AntAngle) {
        int x = ptrdlgVOACAPsettings->leAntAngle->text().toInt();
        if ((x < 360) & (x >=0)) {
             SettingsVars.AntAngle = ptrdlgVOACAPsettings->leAntAngle->text();
        } else {
            //error
        }
    }
    //ptrdlgVOACAPsettings->leTrxPower
    if (ptrdlgVOACAPsettings->leTrxPower->text() != SettingsVars.TrxPEP) {
        float x = ptrdlgVOACAPsettings->leTrxPower->text().toFloat();
        if (x < 0.005f) {
             SettingsVars.TrxPEP = "0.005";
        } else if (x >= 2.0f) {
            SettingsVars.TrxPEP = "2.000";
        } else {
            SettingsVars.TrxPEP = ptrdlgVOACAPsettings->leTrxPower->text();
        }
    }
    SettingsVars.WorkingChanCSV = ptrdlgVOACAPsettings->leWorkingChan->text().toUpper().remove(" ");
    if (ptrdlgVOACAPsettings->cmboAnt->currentIndex() != SettingsVars.AntennaIndex) {
        SettingsVars.AntennaIndex = ptrdlgVOACAPsettings->cmboAnt->currentIndex();
        SettingsVars.AntennaFile = ptrdlgVOACAPsettings->AntennaVar[ptrdlgVOACAPsettings->cmboAnt->currentIndex()].fileName;
        //qDebug() << SettingsVars.AntennaIndex << SettingsVars.AntennaFile;
    }
    */

    //chkbxgetAlerts
    if (((ptrdlgweathersettings->chkbxgetAlerts->checkState() == Qt::Checked) & (SettingsVars.getAlerts == false)) || ((ptrdlgweathersettings->chkbxgetAlerts->checkState() == Qt::Unchecked) & (SettingsVars.getAlerts == true)))   {
        if (ptrdlgweathersettings->chkbxgetAlerts->checkState() == Qt::Checked) {
            SettingsVars.getAlerts = true;
        } else if (ptrdlgweathersettings->chkbxgetAlerts->checkState() == Qt::Unchecked) {
            SettingsVars.getAlerts = false;
        }
        UpdateWeatherNow = true;
    }
    if (UpdateWeatherNow) {
        ptrWindow->updateWeather();
    }

    /*//chkTerminalTab

    if (((ptrdlgTerminalSettings->chkTerminalTab->checkState() == Qt::Checked) & (SettingsVars.TerminalTab == false)) || ((ptrdlgTerminalSettings->chkTerminalTab->checkState() == Qt::Unchecked) & (SettingsVars.TerminalTab == true)))   {
        if (ptrdlgTerminalSettings->chkTerminalTab->checkState() == Qt::Checked) {
            SettingsVars.TerminalTab = true;
        } else if (ptrdlgTerminalSettings->chkTerminalTab->checkState() == Qt::Unchecked) {
            SettingsVars.TerminalTab = false;
        }
        restartRequired = true;
    }*/
    if (ptrdlggeneralsettings->cmboQSSFile->currentIndex() != SettingsVars.QssFileIndex) {
        SettingsVars.QssFileIndex = ptrdlggeneralsettings->cmboQSSFile->currentIndex();
        SettingsVars.QssFile = ptrdlggeneralsettings->lstQssFile.at(ptrdlggeneralsettings->cmboQSSFile->currentIndex());
        QFile QssFile(SettingsVars.QssFile);
        if ((SettingsVars.QssFileIndex != 0 ) && QssFile.open(QFile::ReadOnly)) {
            QString StyleSheet = QLatin1String(QssFile.readAll());
            qApp->setStyleSheet(StyleSheet);
            QssFile.close();
        } else {
            qApp->setStyleSheet("");
        }
    }
    /*
    if ((ptrdlgTerminalSettings->leGuardedRI->text() != settingsTermVars.GuardedRI))   {
        settingsTermVars.GuardedRI = ptrdlgTerminalSettings->leGuardedRI->text();
        restartRequired = true;
    }
    SettingsVars.MARSALEDir = ptrdlggeneralsettings->MARSALEDir->text();
    */

    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.setValue("geometry", ptrWindow->saveGeometry());
    settings.setValue("cfgNCSapp", SettingsVars.cfgNCSapp);
    settings.setValue("Xoff", Xoff);
    settings.setValue("Yoff", Yoff);
    settings.setValue("ScaleFactor", ScaleFactor);
    settings.setValue("PrevScaleFactor", PrevScaleFactor);
    settings.setValue("WorkingDir", SettingsVars.WorkingDir);
    settings.setValue("ACPDDir", SettingsVars.ACPDDir);
    settings.setValue("SMV2PENDOUTDir", SettingsVars.SMV2PENDOUTDir);
    settings.setValue("MyFont", SettingsVars.MyFont.family());
    settings.setValue("MyFontPointSize", SettingsVars.MyFont.pointSize());
    settings.setValue("FontScalar", SettingsVars.FontScalar);
    settings.setValue("currentMAP", currentMAP);
    settings.setValue("PreserveAspectRatio", SettingsVars.PreserveAspectRatio);
    settings.setValue("ToolsAreaisShown",SettingsVars.ToolsAreaisShown);
    settings.setValue("DrawRelayLines", SettingsVars.DrawRelayLines);
    settings.setValue("DrawClosedStation", SettingsVars.DrawClosedStation);
    settings.setValue("GreyLineisShown", SettingsVars.GreyLineisShown);
    settings.setValue("WeatherShown", SettingsVars.WeatherShown);
    settings.setValue("WAccessID", SettingsVars.WAccessID);
    settings.setValue("WSecretKey", SettingsVars.WSecretKey);
    settings.setValue("drawLightning", SettingsVars.drawLightning);
    settings.setValue("MyCallSign", SettingsVars.MyCallSign);
    settings.setValue("myLat", SettingsVars.myLat);
    settings.setValue("myLon", SettingsVars.myLon);
    settings.setValue("getLightningSummary", SettingsVars.getLightningSummary);
    settings.setValue("LightningSummaryRadius", SettingsVars.LightningSummaryRadius);
    settings.setValue("WorkingChanCSV", SettingsVars.WorkingChanCSV);
    settings.setValue("AntAngle", SettingsVars.AntAngle);
    settings.setValue("TrxPEP", SettingsVars.TrxPEP);
    settings.setValue("AntennaIndex", SettingsVars.AntennaIndex);
    settings.setValue("AntennaFile", SettingsVars.AntennaFile);
    settings.setValue("getCyclone", SettingsVars.getCyclone);
    settings.setValue("getAlerts", SettingsVars.getAlerts);
    DebugFunctions = ptrdlgDebug->cmboDebugFunctions->currentIndex();
    settings.setValue("DebugFunctions", DebugFunctions);
    DebugRenderAreaPaintEvent = ptrdlgDebug->cmboDebugRenderAreaPaintEvent->currentIndex();
    settings.setValue("DebugRenderAreaPaintEvent", DebugRenderAreaPaintEvent);
    DebugGeneralLevel = ptrdlgDebug->cmboDebugGeneralLevel->currentIndex();
    settings.setValue("DebugGeneralLevel", DebugGeneralLevel);
    DebugCheckLogLevel = ptrdlgDebug->cmboDebugCheckLogLevel->currentIndex();
    settings.setValue("DebugCheckLogLevel", DebugCheckLogLevel);
    DebugclsRosterRemove = ptrdlgDebug->cmboDebugclsRosterRemove->currentIndex();
    settings.setValue("DebugclsRosterRemove", DebugclsRosterRemove);
    Debug_clsDB = ptrdlgDebug->cmboDebug_clsDB->currentIndex();
    settings.setValue("Debug_clsDB", Debug_clsDB);
    Debug_clsRoster = ptrdlgDebug->cmboDebug_clsRoster->currentIndex();
    settings.setValue("Debug_clsRoster", Debug_clsRoster);
    Debug_Window = ptrdlgDebug->cmboDebug_Window->currentIndex();
    settings.setValue("Debug_Window", Debug_Window);
    Debug_DrawBoxes = ptrdlgDebug->cmboDebug_DrawBoxes->currentIndex();
    settings.setValue("Debug_DrawBoxes", Debug_DrawBoxes);
    DebugSettings = ptrdlgDebug->cmboDebugSettings->currentIndex();
    settings.setValue("DebugSettings", DebugSettings);
    DebugProp = ptrdlgDebug->cmboDebugProp->currentIndex();
    settings.setValue("DebugProp", DebugProp);
    settings.setValue("NCSTab",SettingsVars.NCSTab);
    SettingsVars.ComspotRI = ptrdlgNCSsettings->leRoutingIndicators->text();
    settings.setValue("ComspotRI",SettingsVars.ComspotRI);
    SettingsVars.ComspotFM = ptrdlgNCSsettings->leFM->text();
    settings.setValue("ComspotFM",SettingsVars.ComspotFM);
    SettingsVars.ComspotTO = ptrdlgNCSsettings->leTO->text();
    settings.setValue("ComspotTO",SettingsVars.ComspotTO);
    SettingsVars.ComspotINFO = ptrdlgNCSsettings->leINFO->text();
    settings.setValue("ComspotINFO",SettingsVars.ComspotINFO);
    SettingsVars.ComspotLOC = ptrdlgNCSsettings->leLOC->text();
    settings.setValue("ComspotLOC",SettingsVars.ComspotLOC);
    settingsTermVars.GuardedCallSigns = ptrdlgTerminalSettings->leGuardedCallSigns->text();
    settings.setValue("GuardedCallSigns",settingsTermVars.GuardedCallSigns);
    settingsTermVars.PositionID = ptrdlgTerminalSettings->lePositionID->text();
    settings.setValue("PositionID",settingsTermVars.PositionID);
    settingsTermVars.IgnoreNotforMe = ptrdlgTerminalSettings->chkIgnoreNotforMe->checkState();
    settings.setValue("IgnoreNotforMe",settingsTermVars.IgnoreNotforMe);
    settingsTermVars.NoDisplayACK = ptrdlgTerminalSettings->chkNoDisplayACK->checkState();
    settings.setValue("NoDisplayACK",settingsTermVars.NoDisplayACK);
    settingsTermVars.TrackRMI = ptrdlgTerminalSettings->chkTrackRMI->checkState();
    settings.setValue("TrackRMI",settingsTermVars.TrackRMI);
    settings.setValue("TerminalTab",SettingsVars.TerminalTab);
    settingsTermVars.sendToFile = ptrdlgTerminalSettings->chkSaveToDisk->checkState();
    settings.setValue("sendToFile",settingsTermVars.sendToFile);
    settingsTermVars.autoImportRoster = ptrdlgTerminalSettings->chkAutoImportRoster->checkState();
    settings.setValue("autoImportRoster",settingsTermVars.autoImportRoster);
    settings.setValue("GuardedRI",settingsTermVars.GuardedRI);
    settings.setValue("QssFile",SettingsVars.QssFile);
    settings.setValue("QssFileIndex", SettingsVars.QssFileIndex);
    settings.setValue("ReceivedFolder", settingsTermVars.ReceivedFolder);
    settings.setValue("MessagesFolderRoot", SettingsVars.MessagesFolderRoot);
    settings.setValue("ArchiveFolder", SettingsVars.ArchiveFolder);
    settings.setValue("MARSALEDir", SettingsVars.MARSALEDir);
    settings.setValue("FT817USB", SettingsVars.FT817USB);
    settingsTermVars.sendVZCZMMM = ptrdlgTerminalSettings->chkSaveVZCZMMM->checkState();
    settings.setValue("sendVZCZMMM", settingsTermVars.sendVZCZMMM);
    SettingsVars.ServiceCodes = ptrdlggeneralsettings->ServiceCodes->text();
    settings.setValue("ServiceCodes", SettingsVars.ServiceCodes);

    ptrdlgHighLighterSettings->writeSettings();

    if (restartRequired){
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("StationMapper restart required to load new configuration.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret == QMessageBox::Ok) {
            qApp->quit();
            QProcess::startDetached(qApp->arguments()[0], qApp->arguments());
        }
    } else {
        if (DebugFunctions > 2) {qDebug() << Q_FUNC_INFO << "EXIT";}
        //this->close();
    }
}

void dlgSettingsTab::Cancel() {
    if ((DebugFunctions > 1) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    this->close();
}

dlgDatabaseSettings::dlgDatabaseSettings(QWidget *parent)  : QWidget(parent) {
    if ((DebugFunctions > 1) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    QVBoxLayout *mainLayout = new QVBoxLayout;
    QGroupBox *GroupBox0 = new QGroupBox(tr("address.csv"));
    QGroupBox *GroupBox1 = new QGroupBox(tr("channel.csv"));
    QGroupBox *GroupBox2 = new QGroupBox(tr("City, State Lookup CSV"));
    QGroupBox *GroupBox3 = new QGroupBox(tr("MARS-ALE QRG Generator from channel.csv"));
    QGroupBox *GroupBox4 = new QGroupBox(tr("Imort AM6 ANNEX H Auth Table PDF"));
    QGridLayout *layout0 = new QGridLayout;
    QGridLayout *layout1 = new QGridLayout;
    QGridLayout *layout2 = new QGridLayout;
    QGridLayout *layout3 = new QGridLayout;
    QGridLayout *layout4 = new QGridLayout;

    leAddressCSV = new QLineEdit();
    leAddressCSV->setText(SettingsVars.addressCSV.fileNamePath);
    lblAddressCSVmod = new QLabel("Modified: " + SettingsVars.addressCSV.lastMod.toString());
    QPushButton * ImportAddCSV = new QPushButton("...",this);
    ImportAddCSV->setToolTip("Import the address.csv file.");
    connect(ImportAddCSV,&QPushButton::clicked,this,&dlgDatabaseSettings::selectedChooseAddressFile);
    layout0->addWidget(leAddressCSV,0,0,1,14);
    layout0->addWidget(ImportAddCSV,0,14,1,1);
    layout0->addWidget(lblAddressCSVmod,1,0,1,15);



    lechannelCSV = new QLineEdit();
    lechannelCSV->setText(SettingsVars.channelCSV.fileNamePath);
    lblchannelCSVmod = new QLabel("Modified: " + SettingsVars.channelCSV.lastMod.toString());
    QPushButton * ImportChannelCSV = new QPushButton("...",this);
    ImportChannelCSV->setToolTip("Import the channel.csv file.");
    connect(ImportChannelCSV,&QPushButton::clicked,this,&dlgDatabaseSettings::selectedCopyChannelCSV);
    layout1->addWidget(lechannelCSV,0,0,1,14);
    layout1->addWidget(ImportChannelCSV,0,14,1,1);
    layout1->addWidget(lblchannelCSVmod,1,0,1,15);



    QPushButton * ImportMapperDBCreator = new QPushButton("Advanced",this);
    ImportMapperDBCreator->setToolTip("Import a custom csv file, using City,State data.\r\nRequires an internet connection.");
    connect(ImportMapperDBCreator,&QPushButton::clicked,this,&dlgDatabaseSettings::selectedMapperDBCreator);
    layout2->addWidget(ImportMapperDBCreator,1,0);

    QPushButton * qrgAction = new QPushButton("Gen QRG from channel.csv",this);
    connect(qrgAction,&QPushButton::clicked,this,&dlgDatabaseSettings::selectedQrgAction);
    QTextEdit * MARSALEQrgDesc = new QTextEdit();
    MARSALEQrgDesc->setText("Use this feature to generate a StationMapper.QRG file in your MARS-ALE root folder.  This qrg file contains all of the settings from your AUTO.QRG file plus, the frqs from your channels file starting at CH 1.  These channels are added to group 15.\r\n\r\nTo use the StationMapper QSY feature:\r\n1)  Fill the generated StationMapper.QRG file in MARS-ALE.\r\n2)  Enble telnet 127.0.0.1 port 23 in MARS-ALE.\r\n3)  Click QSY in StationMApper(MAP Tab)\r\n4)  Enter channel index i.e. M037.\r\n5)  Click OK.  MARS-ALE will then report group and channel changes.");
    MARSALEQrgDesc->setSizePolicy(QSizePolicy::Minimum ,QSizePolicy::Maximum);
    MARSALEQrgDesc->setReadOnly(true);
    layout3->addWidget(qrgAction,0,0);
    layout3->addWidget(MARSALEQrgDesc,1,0);

    QPushButton * authAction = new QPushButton("Import Auth Table",this);
    connect(authAction,&QPushButton::clicked,this,&dlgDatabaseSettings::parseAuthTable);
    layout4->addWidget(authAction,0,0);

    GroupBox0->setLayout(layout0);
    GroupBox1->setLayout(layout1);
    GroupBox2->setLayout(layout2);
    GroupBox3->setLayout(layout3);
    GroupBox4->setLayout(layout4);
    mainLayout->addWidget(GroupBox0);
    mainLayout->addWidget(GroupBox1);
    mainLayout->addWidget(GroupBox2);
    mainLayout->addWidget(GroupBox3);
    mainLayout->addWidget(GroupBox4);
    setLayout(mainLayout);
}

void dlgDatabaseSettings::parseAuthTable() {
    classAuthTable clsAuthTable;
    clsAuthTable.callPDFtoTXT();
}

void dlgDatabaseSettings::selectedQrgAction() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}


    QString fileNameAUTO = SettingsVars.MARSALEDir + "AUTO.QRG";
    QFile AUTOqrgFile(fileNameAUTO);
    AUTOqrgFile.open(QFile::ReadOnly);
    QTextStream AUTOqrgFileStream(AUTOqrgFile.readAll());
    AUTOqrgFile.close();

    QFile SMqrgFile(SettingsVars.MARSALEDir + "StationMapperAUTO.QRG");
    SMqrgFile.open(QFile::WriteOnly);
    while (!AUTOqrgFileStream.atEnd()) {
        QString fileLine = AUTOqrgFileStream.readLine();
        if (fileLine.contains("RESET CHANNELS")) {
            fileLine.append("\n");
            SMqrgFile.write(fileLine.toUtf8());
            for (int i = 0 ; i < ChanDB.size(); i++ ){
                //"ADD CHANNEL 4027000 USB 4027000 USB 32768 3 4TEAAR 0 0 1 0 1 0  25\n"
                double tmpHz = ChanDB[i].Freq * 1000.0;
                QString frq = QString::number(int(tmpHz));
                QString fileLineOUT = "ADD CHANNEL " + frq +" USB " + frq + " USB 32768 3 " + WorkingDB[SettingsVars.MyCallSign].CallSign + " 0 0 1 0 1 0  25\n";
                SMqrgFile.write(fileLineOUT.toUtf8());
            }
        }
        else {
                fileLine.append("\n");
                SMqrgFile.write(fileLine.toUtf8());
        }

    }

    SMqrgFile.close();




//    QFile file(SettingsVars.WorkingDir + "ANNEX-D.QRG");
//    file.open(QFile::WriteOnly);
//    for (int i = 0 ; i < ChanDB.size(); i++ ){
//        //"ADD CHANNEL 4027000 USB 4027000 USB 32768 3 4TEAAR 0 0 1 0 1 0  25\n"
//        double tmpHz = ChanDB[i].Freq * 1000.0;
//        QString frq = QString::number(int(tmpHz));
//        QString fileLineOUT = "ADD CHANNEL " + frq +" USB " + frq + " USB 32768 3 " + WorkingDB[SettingsVars.MyCallSign].CallSign + " 0 0 1 0 1 0  25\n";
//        file.write(fileLineOUT.toUtf8());
//    }
//    file.close();
}

void dlgDatabaseSettings::selectedCopyChannelCSV(){
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString ret =  QFileDialog::getOpenFileName(this, ("Select channels.csv File"),"C:\\MSC\\StationManagerV2\\data",("CSV (*.csv)"));
    SettingsVars.channelCSV.fileNamePath = ret;
    SettingsVars.channelCSV.lastMod = QFileInfo(ret).lastModified();
    lechannelCSV->setText(SettingsVars.channelCSV.fileNamePath);
    lblchannelCSVmod->setText(SettingsVars.channelCSV.lastMod.toString());

    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.setValue("channelCSV_fileNamePath", SettingsVars.channelCSV.fileNamePath);
    settings.setValue("channelCSV_lastMod", SettingsVars.channelCSV.lastMod);

    if (!ret.isNull()) {
        if (QFile(SettingsVars.WorkingDir + "channels.csv").exists()){
            QFile(SettingsVars.WorkingDir + "channels.csv").remove();
        }
        QFile::copy(ret,(SettingsVars.WorkingDir + "channels.csv"));
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("StationMapper restart required to load new database.  All other settings changes made at this time are lost.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret == QMessageBox::Ok) {
            qApp->quit();
            QProcess::startDetached(qApp->arguments()[0], qApp->arguments());
        }
    }
}

void dlgDatabaseSettings::selectedChooseAddressFile() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString tmpAddressCSVfilenameDB = QFileDialog::getOpenFileName(this, ("Open address.csv File"),"C:\\MSC\\StationManagerV2\\data",("CSV (*.csv)"));
    if (tmpAddressCSVfilenameDB == ""){ return;}
    SettingsVars.addressCSV.fileNamePath = tmpAddressCSVfilenameDB;
    SettingsVars.addressCSV.lastMod = QFileInfo(tmpAddressCSVfilenameDB).lastModified();
    leAddressCSV->setText(SettingsVars.addressCSV.fileNamePath);
    lblAddressCSVmod->setText(SettingsVars.addressCSV.lastMod.toString());

    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.setValue("addressCSV_fileNamePath", SettingsVars.addressCSV.fileNamePath);
    settings.setValue("addressCSV_lastMod", SettingsVars.addressCSV.lastMod);

    if (parseRosterCSV()) {
        QString Errorsfilename = SettingsVars.WorkingDir + "MapperDBCreatorErrors.txt";
        if (QFile::exists(Errorsfilename)) {
            QFile::remove(Errorsfilename);
        }
        QProgressDialog progress("Converting MGRS to EPSG:3857", "Abort Conversion", 0, db_countDB, this);
        progress.setWindowModality(Qt::WindowModal);
        for (int i=0;i < db_countDB;i++) {
            progress.setValue(i);
            if (progress.wasCanceled()) {
                return;
            }
            getMGRStoLatLon(i);
        }
        if (QFile::exists(SettingsVars.WorkingDir +  DB_Filename)) {
            QFile::remove(SettingsVars.WorkingDir +  DB_Filename);
        }
        writeCSVfile();
        progress.setValue(db_countDB);

        Dialog *ShowErrorDialog = new Dialog();
        ShowErrorDialog->setModal(true);
        ShowErrorDialog->exec();
        SettingsVars.MyCallSign = 9999;
        LoadWorkingDB();
        if (checkMyCallsign()) {
            emit updateMyCall();
        }
    } else {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("address.csv contained errors.  Nothing done.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
    }
}

void dlgDatabaseSettings::selectedMapperDBCreator() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QMessageBox msgBox;
    msgBox.setText("Attention");
    msgBox.setInformativeText("StationMapper will now close and run the advanced database creation and merge tool.");
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Ok);
    int ret = msgBox.exec();
    if (ret == QMessageBox::Ok) {
        qApp->quit();
        QString tmpVar = SettingsVars.WorkingDir + "MapperDBCreator.exe";
        QProcess::startDetached(tmpVar);
    }
}

dlgDebug::dlgDebug(QWidget *parent)  : QWidget(parent) {
    if ((DebugFunctions > 0) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    QGridLayout *mainLayout = new QGridLayout;

    QLabel * lblDebugFunctions = new QLabel("Functions");
    cmboDebugFunctions = new QComboBox;
    QLabel * lblDebugRenderAreaPaintEvent = new QLabel("Render Area");
    cmboDebugRenderAreaPaintEvent = new QComboBox;
    QLabel * lblDebugGeneralLevel = new QLabel("General");
    cmboDebugGeneralLevel = new QComboBox;
    QLabel * lblDebugCheckLogLevel = new QLabel("Check Log");
    cmboDebugCheckLogLevel = new QComboBox;
    QLabel * lblDebugclsRosterRemove = new QLabel("clsRoster.remove()");
    cmboDebugclsRosterRemove = new QComboBox;
    QLabel * lblDebug_clsDB = new QLabel("clsDB");
    cmboDebug_clsDB = new QComboBox;
    QLabel * lblDebug_clsRoster = new QLabel("clsRoster");
    cmboDebug_clsRoster = new QComboBox;
    QLabel * lblDebug_Window = new QLabel("Main Window");
    cmboDebug_Window = new QComboBox;
    QLabel * lblDebug_DrawBoxes = new QLabel("Draw Bounding Boxes");
    cmboDebug_DrawBoxes = new QComboBox;
    QLabel * lblDebugSettings = new QLabel("Settings");
    cmboDebugSettings = new QComboBox;
    QLabel * lblDebugProp = new QLabel("Propagation Functions");
    cmboDebugProp = new QComboBox;

    cmboDebug_DrawBoxes->addItem("Level 0");
    cmboDebug_DrawBoxes->addItem("Level 1");

    for(int i = 0; i <= 10; i++)
    {
        cmboDebugFunctions->addItem("Level " + QString::number(i));
        cmboDebugRenderAreaPaintEvent->addItem("Level " + QString::number(i));
        cmboDebugGeneralLevel->addItem("Level " + QString::number(i));
        cmboDebugCheckLogLevel->addItem("Level " + QString::number(i));
        cmboDebugclsRosterRemove->addItem("Level " + QString::number(i));
        cmboDebug_clsDB->addItem("Level " + QString::number(i));
        cmboDebug_clsRoster->addItem("Level " + QString::number(i));
        cmboDebug_Window->addItem("Level " + QString::number(i));
        cmboDebugSettings->addItem("Level " + QString::number(i));
        cmboDebugProp->addItem("Level " + QString::number(i));
    }

    mainLayout->addWidget(lblDebugFunctions,0,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebugFunctions,0,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebugRenderAreaPaintEvent,1,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebugRenderAreaPaintEvent,1,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebugGeneralLevel,2,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebugGeneralLevel,2,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebugCheckLogLevel,3,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebugCheckLogLevel,3,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebugclsRosterRemove,4,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebugclsRosterRemove,4,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebug_clsDB,5,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebug_clsDB,5,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebug_clsRoster,6,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebug_clsRoster,6,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebug_Window,7,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebug_Window,7,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebug_DrawBoxes,8,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebug_DrawBoxes,8,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebugSettings,9,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebugSettings,9,1,Qt::AlignLeft);

    mainLayout->addWidget(lblDebugProp,10,0,Qt::AlignRight);
    mainLayout->addWidget(cmboDebugProp,10,1,Qt::AlignLeft);


    setLayout(mainLayout);
}

dlgNCSsettings::dlgNCSsettings(QWidget *parent)  : QWidget(parent) {
    QVBoxLayout * mainLayout = new QVBoxLayout;
    QGroupBox * GroupBox = new QGroupBox(tr("Comspot Paramaters"));
    QGridLayout *layout = new QGridLayout;

    QLabel * lblleRoutingIndicators = new QLabel;

    lblleRoutingIndicators->setText("FL2 RI");
    leRoutingIndicators = new QLineEdit;
    leRoutingIndicators->setText(SettingsVars.ComspotRI);
    leRoutingIndicators->setToolTip("");

    QLabel * lblleFM = new QLabel;
    lblleFM->setText("FM");
    leFM = new QLineEdit;
    leFM->setText(SettingsVars.ComspotFM);
    leFM->setToolTip("From RI/PLA");

    QLabel * lblleTO = new QLabel;
    lblleTO->setText("TO");
    leTO = new QLineEdit;
    leTO->setText(SettingsVars.ComspotTO);
    leTO->setToolTip("To RI/PLA");

    QLabel * lblleINFO = new QLabel;
    lblleINFO->setText("INFO");
    leINFO = new QLineEdit;
    leINFO->setText(SettingsVars.ComspotINFO);
    leINFO->setToolTip("INFO RI/PLA");

    QLabel * lblleLOC = new QLabel;
    lblleLOC->setText("LOC");
    leLOC = new QLineEdit;
    leLOC->setText(SettingsVars.ComspotLOC);
    leLOC->setToolTip("Location RGN4/US/MGRS");

    layout->addWidget(lblleRoutingIndicators,0,0,1,1,Qt::AlignRight);
    layout->addWidget(leRoutingIndicators,0,1,1,3);
    layout->addWidget(lblleFM,1,0,1,1,Qt::AlignRight);
    layout->addWidget(leFM,1,1,1,3);
    layout->addWidget(lblleTO,2,0,1,1,Qt::AlignRight);
    layout->addWidget(leTO,2,1,1,3);
    layout->addWidget(lblleINFO,3,0,1,1,Qt::AlignRight);
    layout->addWidget(leINFO,3,1,1,3);
    layout->addWidget(lblleLOC,4,0,1,1,Qt::AlignRight);
    layout->addWidget(leLOC,4,1,1,3);

    GroupBox->setLayout(layout);
    mainLayout->addWidget(GroupBox);
    setLayout(mainLayout);

}

dlgTerminalSettings::dlgTerminalSettings (QWidget *parent)  : QWidget(parent) {
    QVBoxLayout * mainLayout = new QVBoxLayout;
    QGroupBox * GroupBox = new QGroupBox(tr("Terminal Paramaters"));
    QGridLayout *layout = new QGridLayout;

    QLabel * lblTerminalTab = new QLabel("Show Terminal");
    chkTerminalTab = new QCheckBox();
    chkTerminalTab->setChecked(SettingsVars.TerminalTab);
    QLabel * lblGuardedCallSigns = new QLabel("Guarded Call Sign");
    leGuardedCallSigns = new QLineEdit();
    leGuardedCallSigns->setText(settingsTermVars.GuardedCallSigns);
    QLabel * lblPositionID = new QLabel("Position ID");
    lePositionID = new QLineEdit();
    lePositionID->setText(settingsTermVars.PositionID);
    QLabel * lblIgnoreNotforMe = new QLabel("Ignore MSG not for me");
    chkIgnoreNotforMe = new QCheckBox();
    chkIgnoreNotforMe->setChecked(settingsTermVars.IgnoreNotforMe);
    QLabel * lblNoDisplayACK = new QLabel("Do not display ACK");
    chkNoDisplayACK = new QCheckBox();
    chkNoDisplayACK->setChecked(settingsTermVars.NoDisplayACK);
    QLabel * lblTrackRMI = new QLabel("Track Changes in RMI");
    chkTrackRMI = new QCheckBox();
    chkTrackRMI->setChecked(settingsTermVars.TrackRMI);
    QLabel * lblSaveToDisk = new QLabel("Save to disk");
    chkSaveToDisk = new QCheckBox();
    chkSaveToDisk->setChecked(settingsTermVars.sendToFile);
    QLabel * lblSaveVZCZMMM = new QLabel("Must Contain VZCZMMM");
    chkSaveVZCZMMM = new QCheckBox();
    chkSaveVZCZMMM->setChecked(settingsTermVars.sendVZCZMMM);
    QLabel * lblAutoImportRoster = new QLabel("Auto-Import Roster");
    chkAutoImportRoster = new QCheckBox();
    chkAutoImportRoster->setChecked(settingsTermVars.autoImportRoster);
    QLabel * lblGuardedRI = new QLabel("Guarded RI");
    leGuardedRI = new QLineEdit();
    leGuardedRI->setText(settingsTermVars.GuardedRI);
    leReceivedFolder = new QLineEdit();
    QLabel * lblReceivedFolder = new QLabel("Received Message Foler");
    leReceivedFolder->setText(settingsTermVars.ReceivedFolder);
    QPushButton * pbSelectReceivedFolder;
    pbSelectReceivedFolder = new QPushButton;
    pbSelectReceivedFolder->setText("...");
    int width = pbSelectReceivedFolder->fontMetrics().boundingRect("...").width() + 7;
    pbSelectReceivedFolder->setMaximumWidth(width);
    connect(pbSelectReceivedFolder,&QPushButton::clicked,this,&dlgTerminalSettings::selectedpbSelectReceivedFolder);


    layout->addWidget(lblTerminalTab,0,0,1,1,Qt::AlignRight);
    layout->addWidget(chkTerminalTab,0,1,1,3);
    layout->addWidget(lblGuardedCallSigns,1,0,1,1,Qt::AlignRight);
    layout->addWidget(leGuardedCallSigns,1,1,1,3);
    layout->addWidget(lblPositionID,2,0,1,1,Qt::AlignRight);
    layout->addWidget(lePositionID,2,1,1,3);
    layout->addWidget(lblIgnoreNotforMe,3,0,1,1,Qt::AlignRight);
    layout->addWidget(chkIgnoreNotforMe,3,1,1,3);
    layout->addWidget(lblNoDisplayACK,4,0,1,1,Qt::AlignRight);
    layout->addWidget(chkNoDisplayACK,4,1,1,3);
    layout->addWidget(lblTrackRMI,5,0,1,1,Qt::AlignRight);
    layout->addWidget(chkTrackRMI,5,1,1,3);
    layout->addWidget(lblSaveToDisk,6,0,1,1,Qt::AlignRight);
    layout->addWidget(chkSaveToDisk,6,1,1,1);
    layout->addWidget(lblSaveVZCZMMM,6,3,1,1,Qt::AlignRight);
    layout->addWidget(chkSaveVZCZMMM,6,4,1,1);
    layout->addWidget(lblReceivedFolder,7,0,1,1,Qt::AlignRight);
    layout->addWidget(leReceivedFolder,7,1,1,3);
    layout->addWidget(pbSelectReceivedFolder,7,5,1,1);
    layout->addWidget(lblAutoImportRoster,8,0,1,1,Qt::AlignRight);
    layout->addWidget(chkAutoImportRoster,8,1,1,3);
    layout->addWidget(lblGuardedRI,9,0,1,1,Qt::AlignRight);
    layout->addWidget(leGuardedRI,9,1,1,3);

    GroupBox->setLayout(layout);
    mainLayout->addWidget(GroupBox);
    setLayout(mainLayout);

}

void dlgTerminalSettings::selectedpbSelectReceivedFolder() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString ret = QFileDialog::getExistingDirectory(this, "Select Received Message Folder","C:\\MSC");
    if (ret !="") {
        leReceivedFolder->setText(ret + "/");
    }
}

dlgHighLighterSettings::dlgHighLighterSettings(QWidget *parent)  : QWidget(parent) {
    if ((DebugFunctions > 0) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    QGridLayout *mainLayout = new QGridLayout;
    mainLayout->setContentsMargins(0,0,0,0);

    tblRules = new QTableWidget(0,6);
    QStringList labels; labels << "Description" << "Active" << "Color" << "Weight" << "Italics" << "RegEx Rules";
    tblRules->setHorizontalHeaderLabels(labels);
    tblRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblRules->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
    tblRules->setColumnWidth(3,100);
    tblRules->horizontalHeader()->setStretchLastSection(true);
    tblRules->verticalHeader()->hide();
    tblRules->setShowGrid(true);
    tblRules->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tblRules, SIGNAL( customContextMenuRequested(const QPoint &) ), this, SLOT(on_tblRules_ContextMenuRequest(const QPoint &)));

    readSettings();

    mainLayout->addWidget(tblRules);

    setLayout(mainLayout);
}

void dlgHighLighterSettings::on_tblRules_ContextMenuRequest(const QPoint &pos) {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    int row = tblRules->rowAt(pos.y());
    if (row == -1) {return;}
    QMenu contextMenu(tr("Context menu"), this);
    contextMenu.setToolTipsVisible(true);
    QAction NewRule("New Rule", this);
    NewRule.setToolTip("Create line for new rule");




    contextMenu.addAction(&NewRule);
    connect(&NewRule, SIGNAL(triggered()), this, SLOT(on_NewRule()));

    contextMenu.exec(tblRules->mapToGlobal(pos));
}

void dlgHighLighterSettings::on_NewRule() {
    tblRules->insertRow(tblRules->rowCount());
    int row = tblRules->rowCount() -1;

    QTableWidgetItem * itmDescription = new QTableWidgetItem;
    itmDescription->setText("");
    itmDescription->setTextAlignment(Qt::AlignCenter);
    tblRules->setItem(row, 0, itmDescription);

    QCheckBox * itmIsActive = new QCheckBox;
    itmIsActive->setTristate(false);
    itmIsActive->setChecked(false);
    tblRules->setCellWidget(row, 1, itmIsActive);

    QComboBox * itmSetForeground = new QComboBox;
    QMetaEnum metaEnum = QMetaEnum::fromType<Qt::GlobalColor>();
    for (int i = 0 ; i < metaEnum.keyCount(); i++) {
        itmSetForeground->addItem(metaEnum.key(i));
    }
    itmSetForeground->setCurrentIndex(0);
    itmSetForeground->setEditable(false);
    tblRules->setCellWidget(row, 2, itmSetForeground);

    QMetaEnum metaEnumWeight = QMetaEnum::fromType<QFont::Weight>();
    QComboBox * itmSetFontWeight = new QComboBox;
    int weightIdx = 0;
    for (int i = 0 ; i < metaEnumWeight.keyCount(); i++) {
        itmSetFontWeight->addItem(metaEnumWeight.key(i));
    }
    itmSetFontWeight->setCurrentIndex(5);
    itmSetFontWeight->setEditable(false);
    tblRules->setCellWidget(row, 3, itmSetFontWeight);


    QCheckBox * itmSetFontItalic = new QCheckBox;
    itmSetFontItalic->setTristate(false);
    itmSetFontItalic->setChecked(false);
    tblRules->setCellWidget(row, 4, itmSetFontItalic);


    QTableWidgetItem * itmKeywordPatterns = new QTableWidgetItem;
    itmKeywordPatterns->setText("");
    tblRules->setItem(row, 5, itmKeywordPatterns);


}

void dlgHighLighterSettings::readSettings() {
    if ((DebugFunctions > 0) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    int row;
    QString fileNameSettings = "/home/user/MSC/StationMapper/highlighter.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    size = settings.beginReadArray("HighlightingRule");

    qDebug() << size;

    for (int i = 1; i < size; ++i) {
        row = tblRules->rowCount();
        settings.setArrayIndex(i);
        int color = settings.value("setForeground").toInt();
        int weight = settings.value("setFontWeight").toInt();
        bool italic = settings.value("setFontItalic").toBool();
        QString tmpDesc = settings.value("Description").toString();
        QStringList keywordPatterns;
        keywordPatterns = settings.value("keywordPatterns").toStringList();
        bool active = settings.value("isActive", "false").toBool();
        const QSignalBlocker blocker(tblRules);

        tblRules->insertRow(row);


        QTableWidgetItem * itmDescription = new QTableWidgetItem;
        itmDescription->setText(tmpDesc);
        itmDescription->setTextAlignment(Qt::AlignCenter);
        tblRules->setItem(row, 0, itmDescription);

        QCheckBox * itmIsActive = new QCheckBox;
        itmIsActive->setTristate(false);
        itmIsActive->setChecked(active);
        tblRules->setCellWidget(row, 1, itmIsActive);

        QComboBox * itmSetForeground = new QComboBox;
        QMetaEnum metaEnum = QMetaEnum::fromType<Qt::GlobalColor>();
        for (int i = 0 ; i < metaEnum.keyCount(); i++) {
            itmSetForeground->addItem(metaEnum.key(i));
        }
        itmSetForeground->setCurrentIndex(color);
        itmSetForeground->setEditable(false);
        tblRules->setCellWidget(row, 2, itmSetForeground);

        QMetaEnum metaEnumWeight = QMetaEnum::fromType<QFont::Weight>();
        QComboBox * itmSetFontWeight = new QComboBox;
        int weightIdx = 0;
        for (int i = 0 ; i < metaEnumWeight.keyCount(); i++) {
            if (metaEnumWeight.value(i) == weight) { weightIdx = i;}
            itmSetFontWeight->addItem(metaEnumWeight.key(i));
        }

        itmSetFontWeight->setCurrentIndex(weightIdx);
        itmSetFontWeight->setEditable(false);
        tblRules->setCellWidget(row, 3, itmSetFontWeight);


        QCheckBox * itmSetFontItalic = new QCheckBox;
        itmSetFontItalic->setTristate(false);
        itmSetFontItalic->setChecked(italic);
        tblRules->setCellWidget(row, 4, itmSetFontItalic);


        QString patternCSV;
        patternCSV = keywordPatterns.at(0);
        for (int i = 1 ; i < keywordPatterns.size(); i++) {
            patternCSV.append(",");
            patternCSV.append(keywordPatterns.at(i));
        }


        QTableWidgetItem * itmKeywordPatterns = new QTableWidgetItem;
        itmKeywordPatterns->setText(patternCSV);
        tblRules->setItem(row, 5, itmKeywordPatterns);
        keywordPatterns.clear();

    }
    //tblRules->resizeRowsToContents();
    settings.endArray();
}

bool dlgHighLighterSettings::writeSettings() {
    if ((DebugFunctions > 0) || (DebugSettings > 0)) {qDebug() << Q_FUNC_INFO;}
    QString fileNameSettings = "/home/user/MSC/StationMapper/highlighter.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.beginWriteArray("HighlightingRule");

    for (int i = 0; i < tblRules->rowCount(); ++i) {
        if (tblRules->item(i,0)->text() != "") {
            settings.setArrayIndex(i+1);
            settings.setValue("Description", tblRules->item(i,0)->text());

            QCheckBox * itmIsActive = new QCheckBox;
            itmIsActive = (QCheckBox*)tblRules->cellWidget(i,1);
            settings.setValue("isActive", itmIsActive->isChecked());

            QComboBox * itmSetForeground = new QComboBox;
            itmSetForeground = (QComboBox*)tblRules->cellWidget(i,2);
            settings.setValue("setForeground", itmSetForeground->currentIndex());


            QComboBox * itmSetFontWeight = new QComboBox;
            itmSetFontWeight = (QComboBox*)tblRules->cellWidget(i,3);
            QMetaEnum metaEnumWeight = QMetaEnum::fromType<QFont::Weight>();
            settings.setValue("setFontWeight", metaEnumWeight.value(itmSetFontWeight->currentIndex()));


            QCheckBox * itmSetFontItalic = new QCheckBox;
            itmSetFontItalic = (QCheckBox*)tblRules->cellWidget(i,4);
            settings.setValue("setFontItalic", bool(itmSetFontItalic->isChecked()));


            QTableWidgetItem * itmKeywordPatterns = new QTableWidgetItem;
            itmKeywordPatterns = tblRules->item(i,5);
            QString patternCSV = itmKeywordPatterns->text();
            QStringList keywordPatterns = patternCSV.split(",");
            settings.setValue("keywordPatterns", keywordPatterns);
        }
    }
    settings.endArray();
    ptrWindow->wMainWindowTab->ptrdlgNotes->highlighter->reload();
    return false;
}
