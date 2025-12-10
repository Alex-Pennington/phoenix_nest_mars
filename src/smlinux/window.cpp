#include "main.h"
#include "window.h"
#include "renderarea.h"
#include "updatecheck.h"
#include "dlgsettingstab.h"
//#include "geocalc.h"
//#include "classsolar.h"
#include "classweather.h"
#include "classgetlightning.h"
#include "debug.h"
#include "heatmap.h"
#include "classsunspot.h"

#include <QMenu>
#include <QMenuBar>
#include <QtWidgets>
#include <QSettings>
#include <QStatusBar>
#include <QProcess>
#include <QProgressDialog>
#include <QHBoxLayout>
#include <QLayout>
#include <QLabel>
#include <QTextEdit>
#include <QDialog>
#include <QFont>
#include <QtConcurrent/QtConcurrent>
#include <QLineSeries>
#include <QHttpMultiPart>
#include <QHttpPart>

//const int IdRole = Qt::UserRole;

QTextEdit * Dialog::r_textEdit = 0;
QTextEdit * Dialog2::r_textEdit = 0;
QTextEdit * clsDlgNotice::r_textEdit = 0;

//heatmap varHeatMap;
RenderArea *ptrRenderArea;
classsunspot getSunSpot;

int varRelFreqSize = 0;

void logChanged(const QString file)
{
    QDir dir;
    dir.setPath(file);
    QFileInfoList list = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
}

Dialog::Dialog()
{
    r_textEdit = new QTextEdit;

    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->addWidget(r_textEdit);
    setLayout(mainLayout);

    setWindowTitle(tr("Errors in address.csv"));
    resize(600,400);
    readFile();
}

Dialog2::Dialog2()
{
    r_textEdit = new QTextEdit;

    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->addWidget(r_textEdit);
    setLayout(mainLayout);

    setWindowTitle(tr("License.txt"));
    resize(600,400);
    readFile();
}

ChanGraph::ChanGraph(QString Title)
{
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(ShowContextMenu(const QPoint &)));

    QBoxLayout *mainLayout = new QBoxLayout(QBoxLayout::TopToBottom);

    //QValueAxis *axisX = new QValueAxis;
    QValueAxis *axisY = new QValueAxis;

    QBarCategoryAxis *axisXChanID = new QBarCategoryAxis();
    QBarCategoryAxis *axisXFreq = new QBarCategoryAxis();

    axisY->setTitleText("%");
    axisY->setRange(0,100);
    setWindowTitle(tr("Channel Reliability(%) Graph"));
    QBarSeries *barseries = new QBarSeries();
    QBarSet *set0 = new QBarSet("1");

    QStringList ChanIdList;
    QStringList FreqList;

    for (int i = 0; i < varRelFreqSize ; i++) {
        *set0 << varRelFreq[i].rel*100;
        barseries->append(set0);

        ChanIdList << varRelFreq[i].ChanId;
        FreqList << varRelFreq[i].padFreq;

        //qDebug() << varRelFreq[i].ChanId <<varRelFreq[i].Mhz << varRelFreq[i].rel*100;
    }
    QChart *chartBar = new QChart();

    chartBar->addSeries(barseries);
    chartBar->legend()->hide();
    QFont tFont;
    QFont font;
    font.setPixelSize(10);
    chartBar->setTitleFont(font);
    chartBar->setTitleBrush(QBrush(Qt::black));
    chartBar->setTitle(Title);

    axisXChanID->append(ChanIdList);
    axisXFreq->append(FreqList);
    chartBar->addAxis(axisXChanID, Qt::AlignTop);
    barseries->attachAxis(axisXChanID);
    chartBar->addAxis(axisY, Qt::AlignLeft);

    QChartView *chartViewBar = new QChartView(chartBar);
    chartViewBar->setRenderHint(QPainter::Antialiasing);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->addWidget(chartViewBar);
    setLayout(mainLayout);
    show();
}

void ChanGraph::ShowContextMenu(const QPoint &pos){
   QMenu contextMenu(tr("Context menu"), this);

   QAction action1("close", this);
   connect(&action1, SIGNAL(triggered()), this, SLOT(closeChart()));
   contextMenu.addAction(&action1);

   contextMenu.exec(mapToGlobal(pos));
}

void ChanGraph::closeChart(){
    close();
}

FreqGraph::FreqGraph(QString Title)
{
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(ShowContextMenu(const QPoint &)));

    QBoxLayout *mainLayout = new QBoxLayout(QBoxLayout::TopToBottom);

    setWindowTitle(tr("Frequency Reliability(%) Graph"));

    QLineSeries *lineseries = new QLineSeries();

    for (int i = 0; i < varRelFreqSize ; i++) {
        *lineseries << QPoint(int(varRelFreq[i].Mhz*1000),int(varRelFreq[i].rel*100));

        //qDebug() << varRelFreq[i].Mhz << varRelFreq[i].rel*100;
    }

    QValueAxis *axisX = new QValueAxis;
    QValueAxis *axisY = new QValueAxis;

    axisX->setMax(12000);
    axisX->setMin(2000);
    axisX->setTickCount(11);
    axisX->setMinorTickCount(4);

    axisY->setTitleText("%");
    axisY->setRange(0,100);
    axisY->setTickCount(10);
    axisY->setTitleText("%");

    QChart *lineChart = new QChart();
    lineChart->addSeries(lineseries);
    lineChart->addAxis(axisY, Qt::AlignLeft);
    lineChart->addAxis(axisX, Qt::AlignBottom);
    lineChart->legend()->hide();
    //lineChart->createDefaultAxes();
    QFont tFont;
    QFont font;
    font.setPixelSize(10);
    lineChart->setTitleFont(font);
    lineChart->setTitleBrush(QBrush(Qt::black));
    lineChart->setTitle(Title);

    QChartView *chartViewBar = new QChartView(lineChart);
    chartViewBar->setRenderHint(QPainter::Antialiasing);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->addWidget(chartViewBar);
    setLayout(mainLayout);
    show();
}

void FreqGraph::ShowContextMenu(const QPoint &pos){
   QMenu contextMenu(tr("Context menu"), this);

   QAction action1("close", this);
   connect(&action1, SIGNAL(triggered()), this, SLOT(closeChart()));
   contextMenu.addAction(&action1);

   contextMenu.exec(mapToGlobal(pos));
}

void FreqGraph::closeChart(){
    close();
}

clsDlgNotice::clsDlgNotice()
{
    r_textEdit = new QTextEdit;

    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->addWidget(r_textEdit);
    setLayout(mainLayout);

    setWindowTitle(tr("StationMapper Notice.txt"));
    resize(600,400);
    readFile();
}

namespace Ui {
class Window;
}

Window::Window()
{
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}

    connectTcp();

    thread = new QThread;
//    varHeatMap.moveToThread(thread);
//    connect(thread, SIGNAL(started()), &varHeatMap, SLOT(parseHeatmap()));
//    connect(&varHeatMap, SIGNAL(finished()), thread, SLOT(quit()));
//    connect(&varHeatMap, SIGNAL(finished()),this,SLOT(restoreCursor()));

    statusBar = new QStatusBar;
    statusLabel = new QLabel(this);
    statusUpdateLbl = new QPushButton(this);
    //statusUpdateLbl->setFlat(true);
    statusUpdateLbl->setVisible(false);
    statusLightningLbl = new QLabel(this);
    statusLightningLbl->setToolTip("Number of lightning strikes in set radius.\r\nUpdates every 5 minutes.");
    statusSSNLbl = new QLabel(this);
    statusSSNLbl->setToolTip("Sun Spot Number\r\nSILSO data, Royal Observatory of Belgium, Brussels");

    statusBar->addPermanentWidget(statusLabel,1);
    statusBar->addPermanentWidget(statusSSNLbl);
    statusBar->addPermanentWidget(statusLightningLbl);
    statusBar->addPermanentWidget(statusUpdateLbl);
    createStatusBar();
    renderArea = new RenderArea;
    ptrRenderArea = renderArea;


//    QScrollArea* scrollArea = new QScrollArea;
    wMainWindowTab = new mainwindowtab(this);
//    scrollArea->setWidget(wMainWindowTab);

//    connect(&varHeatMap, SIGNAL(finished()), renderArea, SLOT(repaint()));

    //Create Menus
    QMenuBar *mBar = new QMenuBar();
    QMenu *fileMenu = new QMenu("File");
    QMenu *cfgMenu = new QMenu("Configure");
    QMenu *dbg = new QMenu("Debug");
    QMenu *helpMenu = new QMenu("Help");
    QMenu *fontScalarMenu = new QMenu("Font Scalar");
    QMenu *MAPMenu = new QMenu("Choose Map");
    mBar->addMenu(fileMenu);
    //mBar->addMenu(cfgMenu);
    mBar->addMenu(dbg);

    QAction * selectFileMenu = new QAction(tr("&Exit"), this);
    connect(selectFileMenu, &QAction::triggered, this, &Window::close);
    fileMenu->addAction(selectFileMenu);

    QAction * selectSettingsTab = new QAction(tr("&Settings"), this);
    connect(selectSettingsTab, &QAction::triggered, this, &Window::selectedSettings);
    mBar->addAction(selectSettingsTab);

    selectUploadFile = new QAction(tr("&Upload file to Library"), this);
    connect(selectUploadFile, &QAction::triggered, this, &Window::selectedUploadFile);
    dbg->addAction(selectUploadFile);

    selectDebug = new QAction(tr("&Copy Roster Log to Debug Folder"), this);
    connect(selectDebug, &QAction::triggered, this, &Window::selectedDebug);
    dbg->addAction(selectDebug);


    if (!WorkingVars.Debug_to_File) {
        selectDebugToFile = new QAction(tr("&Restart in Debug Mode"), this);
    } else {
        selectDebugToFile = new QAction(tr("&Upload Encrypted Debug Log"), this);
    }
    connect(selectDebugToFile, &QAction::triggered, this, &Window::selectedDebugToFile);
    dbg->addAction(selectDebugToFile);

    selectDebugAuth = new QAction(tr("&Auth"), this);
    connect(selectDebugAuth, &QAction::triggered, this, &Window::selecedtDebugAuth);
    mBar->addAction(selectDebugAuth);
    mBar->addMenu(helpMenu);

    selectResetView= new QAction(tr("&Reset View"), this);
    selectResetView->setToolTip(tr("Reset viewport Zoom and Pan"));
    connect(selectResetView, &QAction::triggered, this, &Window::selectedResetView);
    //cfgMenu->addAction(selectResetView);///////////////////////////////////////////////////////////////////

    //Help Menu Mouse
    selectUsingZoom = new QAction(tr("Mouse HowTo"),this);
    connect(selectUsingZoom, &QAction::triggered, this, &Window::selectedUsingZoom);
    helpMenu->addAction(selectUsingZoom);

    //Help Menu AbouT QT
    selectGotoPDF = new QAction(tr("Offline Documentation"),this);
    connect(selectGotoPDF, &QAction::triggered, this, &Window::selectedGotoPDF);
    helpMenu->addAction(selectGotoPDF);

    //Help Menu Wiki
    selectGotoWiki = new QAction(tr("Online Documentation"),this);
    connect(selectGotoWiki, &QAction::triggered, this, &Window::selectedGotoWiki);
    helpMenu->addAction(selectGotoWiki);

    //Bug Report
    selectBugReport = new QAction(tr("Report Bug"),this);
    connect(selectBugReport, &QAction::triggered, this, &Window::selectedBugReport);
    helpMenu->addAction(selectBugReport);

    //Help Menu groups.io messages
    selectGotoMessages = new QAction(tr("Message Board"),this);
    connect(selectGotoMessages, &QAction::triggered, this, &Window::selectedGotoMessages);
    helpMenu->addAction(selectGotoMessages);

    //Help Menu About
    selectAbout = new QAction(tr("&About"), this);
    connect(selectAbout, &QAction::triggered, this, &Window::selectedAbout);
    helpMenu->addAction(selectAbout);

    //ShowLicense
    selectShowLicense = new QAction(tr("License"), this);
    connect(selectShowLicense, &QAction::triggered, this, &Window::selectedShowLicense);
    helpMenu->addAction(selectShowLicense);

    //Help Menu Update
    selectUpdate = new QAction(tr("&Update"), this);
    selectUpdate->setToolTip(tr("Update StationMapper"));
    connect(selectUpdate, &QAction::triggered, this, &Window::selectedUpdate);
    helpMenu->addAction(selectUpdate);

    //Help Menu AbouT QT
    selectQTLicense = new QAction(tr("About QT"), this);
    connect(selectQTLicense, &QAction::triggered, this, &Window::selectedQTLicense);
    helpMenu->addAction(selectQTLicense);   


    //TBD   //////////////////////////////////////////////////
    selectFontScalar2 = new QAction(tr("2"), this);
    connect(selectFontScalar2, &QAction::triggered, this, &Window::selectedFontScalar2);

    selectFontScalar3 = new QAction(tr("3"), this);
    connect(selectFontScalar3, &QAction::triggered, this, &Window::selectedFontScalar3);

    selectFontScalar4 = new QAction(tr("4"), this);
    connect(selectFontScalar4, &QAction::triggered, this, &Window::selectedFontScalar4);

    selectFontScalar5 = new QAction(tr("5"), this);
    connect(selectFontScalar5, &QAction::triggered, this, &Window::selectedFontScalar5);
    fontScalarMenu->addAction(selectFontScalar5);

    selectFontScalar6 = new QAction(tr("6"), this);
    connect(selectFontScalar6, &QAction::triggered, this, &Window::selectedFontScalar6);
    fontScalarMenu->addAction(selectFontScalar6);

    selectFontScalar7 = new QAction(tr("7"), this);
    connect(selectFontScalar7, &QAction::triggered, this, &Window::selectedFontScalar7);
    fontScalarMenu->addAction(selectFontScalar7);

    selectFontScalar8 = new QAction(tr("8"), this);
    connect(selectFontScalar8, &QAction::triggered, this, &Window::selectedFontScalar8);
    fontScalarMenu->addAction(selectFontScalar8);

    selectFontScalar12 = new QAction(tr("12"), this);
    connect(selectFontScalar12, &QAction::triggered, this, &Window::selectedFontScalar12);
    fontScalarMenu->addAction(selectFontScalar12);

    selectFontScalar16 = new QAction(tr("16"), this);
    connect(selectFontScalar16, &QAction::triggered, this, &Window::selectedFontScalar16);
    fontScalarMenu->addAction(selectFontScalar16);

    selectFontScalar20 = new QAction(tr("20"), this);
    connect(selectFontScalar20, &QAction::triggered, this, &Window::selectedFontScalar20);
    fontScalarMenu->addAction(selectFontScalar20);
    //cfgMenu->addMenu(fontScalarMenu);///////////////////////////////////////////////////////////////////////


    //Choose MAP Menu
//    for (int idx = 0; idx <=19 ; idx++) {
//        if (varMAPS[idx].FileName != "") {
//            QAction * action = new QAction(varMAPS[idx].FileName, this);
//            varMAPS[idx].ptr = action;
//            connect(action, &QAction::triggered, this, &Window::selectedMAPS);
//            MAPMenu->addAction(action);
//        }
//    }
//    cfgMenu->addMenu(MAPMenu);

    mainLayout = new QGridLayout;

    mainLayout->setMenuBar(mBar);
    //mainLayout->setContentsMargins(0,0,0,0);

    readSettings();

    if (SettingsVars.ToolsAreaisShown) {
        createToolsArea();
    }

//    mainLayout->addWidget(scrollArea, 0, 0, 1, 4);
    mainLayout->addWidget(wMainWindowTab, 0, 0, 1, 4);
    mainLayout->addWidget(statusBar,5,0,1,1);
    setLayout(mainLayout);

    QString tmpTitle = "Station Mapper " + VERSION;
    setWindowTitle(tmpTitle);

    parseCurrentVersionTxt();

    bool tmpNoticeDisplayed = false;
    QString tmpFile = SettingsVars.WorkingDir + "Notice.txt";
    QFile file(tmpFile);

    if (file.open(QIODevice::ReadOnly)) {
        clsDlgNotice *NoticeDialog = new clsDlgNotice();
        NoticeDialog->setModal(true);
        NoticeDialog->setParent(ptrWindow);
        NoticeDialog->exec();
        tmpNoticeDisplayed = true;
    }
    file.close();
    if (tmpNoticeDisplayed) {
        QString tmpCurrentNoticeFilename = SettingsVars.WorkingDir + "CurrentNotice.txt";
        if (QFile::exists(tmpCurrentNoticeFilename)) {
            QFile::remove(tmpCurrentNoticeFilename);
        }
        file.rename(tmpCurrentNoticeFilename);
    }

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateSolarTerminator()));
    timer->start(1000*3); //Every 0.5 Min.

    timerWeather = new QTimer(this);
    connect(timerWeather, SIGNAL(timeout()), this, SLOT(updateWeather()));
    timerWeather->start(1000*3);

    timerLightning = new QTimer(this);
    connect(timerLightning, SIGNAL(timeout()), this, SLOT(updateLightning()));
    timerLightning->start(1000*3);

    d.doDownload();
    getSunSpot.doDownloadSS();

    installEventFilter(this);
    //resize(100,100);
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << "exit" << Q_FUNC_INFO;}
}

void Window::selecedtDebugAuth() {
    if (QFile::exists(SettingsVars.WorkingDir + "authTable.ini")) {
        formAuthForm = new dlgAuthForm;
        formAuthForm->setModal(false);
        formAuthForm->show();
    }
    else {
        QMessageBox mBox;
        mBox.setText("Auth Tables have not been imported.  Use Settings->Database Tab->Auth Table Import feature to import the tables.");
        mBox.exec();
    }

}

void Window::connectTcp() {
//    QByteArray data; // <-- fill with data

//    pSocket = new QTcpSocket( this ); // <-- needs to be a member variable: QTcpSocket * _pSocket;
//    //connect( pSocket, SIGNAL(readyRead()), SLOT(readTcpData()) );

//    pSocket->connectToHost("127.0.0.1", 23);
//    if( pSocket->waitForConnected() ) {
//        pSocket->write( data );
//    }
}

void Window::readTcpData()
{
    QByteArray data = pSocket->readAll();
}

void Window::selectedSetFreqALE() {
    pSocket = new QTcpSocket( this ); // <-- needs to be a member variable: QTcpSocket * _pSocket;
    pSocket->connectToHost("127.0.0.1", 23);
    if( pSocket->waitForConnected() ) {
        QString data = "";
        bool ok;
        WorkingVars.ChanInput = QInputDialog::getItem(this,"Input Channel Identifier","Id:", SettingsVars.CoverageDefaultChanList,SettingsVars.CoverageDefaultChan,true,&ok).toUpper();
        if (ok) {
            if (!SettingsVars.CoverageDefaultChanList.contains(WorkingVars.ChanInput)) {
                SettingsVars.CoverageDefaultChanList << WorkingVars.ChanInput;
                SettingsVars.CoverageDefaultChan = SettingsVars.CoverageDefaultChanList.size() -1;
                QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
                QSettings settings(fileNameSettings,QSettings::IniFormat);
                settings.setValue("CoverageDefaultChanList", SettingsVars.CoverageDefaultChanList);
                settings.setValue("CoverageDefaultChan", SettingsVars.CoverageDefaultChan);
            } else {
                SettingsVars.CoverageDefaultChan = SettingsVars.CoverageDefaultChanList.indexOf(WorkingVars.ChanInput);
                QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
                QSettings settings(fileNameSettings,QSettings::IniFormat);
                settings.setValue("CoverageDefaultChan", SettingsVars.CoverageDefaultChan);
            }
            if (ChanDB.searchIdx(WorkingVars.ChanInput)) {
                data = "CMD STOP\n";
                pSocket->write( data.toUtf8() );
                data = "CMD SET GROUP 15\n";
                pSocket->write( data.toUtf8() );
                data = "CMD SET CHANNEL " + QString::number(ChanDB.searchResult+1) + "\n";
                pSocket->write( data.toUtf8() );
                if (SettingsVars.FT817USB) {
                    data = "CMD HEXRADCMD 01 00 00 00 07\n";
                    pSocket->write( data.toUtf8() );
                }
            }
        }
    }
    pSocket->close();
}

void Window::writeSettings(){
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.setValue("geometry", saveGeometry());
    settings.setValue("Xoff", Xoff);
    settings.setValue("Yoff", Yoff);
    settings.setValue("ScaleFactor", ScaleFactor);
    settings.setValue("PrevScaleFactor", PrevScaleFactor);
    settings.setValue("WorkingDir", SettingsVars.WorkingDir);
    settings.setValue("MyFont", SettingsVars.MyFont.family());
    settings.setValue("MyFontPointSize", SettingsVars.MyFont.pointSize());
    settings.setValue("FontScalar", SettingsVars.FontScalar);
    settings.setValue("currentMAP", currentMAP);

    if ((SettingsVars.NCSTab == true) & (ptrExternDlgNCS != 0)) {
        settings.setValue("splitterState", wMainWindowTab->ptrdlgNCS->splitter->saveState());
        settings.setValue("splitterGeo", wMainWindowTab->ptrdlgNCS->splitter->saveGeometry());
        settings.setValue("splitter2State", wMainWindowTab->ptrdlgNCS->splitter2->saveState());
        settings.setValue("splitter2Geo", wMainWindowTab->ptrdlgNCS->splitter2->saveGeometry());
    }

    settings.setValue("NotesFontFamily", SettingsVars.NotesFont.family());
    settings.setValue("NotesFontPointSize", SettingsVars.NotesFont.pointSize());
    settings.setValue("InfoFontFamily", SettingsVars.InfoFont.family());
    settings.setValue("InfoFontPointSize", SettingsVars.InfoFont.pointSize());

}

void Window::resizeEvent(QResizeEvent *event) {
    if ((DebugFunctions > 2) || (Debug_Window > 2)) {qDebug() << Q_FUNC_INFO << this->size();}

    event->accept();
}

void Window::readSettings(){
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    /*
    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    const QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (geometry.isEmpty()) {
        const QRect availableGeometry = QApplication::desktop()->availableGeometry(this);
        resize(MAP_WIDTH/2, MAP_HEIGHT/2);
        move((availableGeometry.width() - width()) / 2,
             (availableGeometry.height() - height()) / 2);
    } else {
        restoreGeometry(geometry);
    }*/
    const QRect availableGeometry = QApplication::desktop()->availableGeometry(this);
    resize(availableGeometry.width(), availableGeometry.height());
}

void Window::createStatusBar(){
}

void Window::updateStatusBar() {
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    statusLabel->setText(QStringLiteral("%1 Stations | %2").arg((RosterVar.size()+1)).arg(WorkingVars.NetAddress));
}

void Window::closeEvent(QCloseEvent *event){
    WorkingVars.Closing = true;
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}

    writeSettings();
    QCoreApplication::processEvents();
    event->accept();
}

void Window::callLogChecker(QString filename){
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    if (DebugFileWatcher > 0){ qDebug() << "Window::CheckLog()" << filename; }
    WorkingVars.curLogfile = filename;
    renderArea->CheckLog();
    renderArea->repaint();
    updateStatusBar();
}

void Window::selectedAbout() {
    QMessageBox::about(this,"About","Written by Alexander Keith Pennington <aar4te@organicengineer.com>\r\n"
                                    "(Public Key Fingerprint 3A4BF15C60C75202)\r\n"
                                    "Station operators should use the https://groups.io/g/StationMapper group to request features or to report bugs.\r\n"
                                    "This software uses the following OSS:\r\n"
                                    "GPG4win\r\n"
                                    "PROJ4\r\n"
                                    "GeographicsLIB\r\n"
                                    "v3Terminal\r\n"
                                    "Info-ZIP\r\n"
                                    "Weather data & imagery powered by AerisWeather."
                       );
}

void Window::selectedUpdate() {
    QMessageBox msgBox;
    msgBox.setText("Attention");
    msgBox.setInformativeText("StationMapper will now close and run Update");
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Ok);
    int ret = msgBox.exec();
    if (ret == QMessageBox::Ok) {
        qApp->quit();
        QString tmpVar = SettingsVars.WorkingDir + "Update";
        QProcess::startDetached(tmpVar);
    }
}

void Window::selectedResetView() {

    if (SettingsVars.PreserveAspectRatio) {
        Xoff = 0;
        Yoff = 0;
    } else {
        Xoff = 0;
        Yoff = 0;
        ScaleFactor = 1;
    }
    renderArea->update();
}

void Window::selectedQTLicense() {
    QMessageBox::aboutQt(this);
}

void Window::selectedShowAll() {
}

void Window::selectedShowLicense() {
    Dialog2 *LicenseDialog = new Dialog2();
    LicenseDialog->setModal(true);
    LicenseDialog->exec();
}

void Window::selectedUsingZoom(){
    QMessageBox msgBox;
    msgBox.setText("Using the mouse:");
    msgBox.setInformativeText("Use the mouse wheel to zoom in and out.\n\rLeft mouse click centers the map on the point clicked.\n\rHold the left mouse button down to pan(drag) the map.\n\rThe mouse forward and back buttons adjust the font scalar.\n\rRight mouse click selects station for calculations (round-robin style).");
    msgBox.setStandardButtons(QMessageBox::Ok|QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Ok);
    int ret = msgBox.exec();
    if (ret) {}
}

void Window::selectedFontScalar2(){
    SettingsVars.FontScalar = 2;
    renderArea->update();
}
void Window::selectedFontScalar3(){
    SettingsVars.FontScalar = 3;
    renderArea->update();
}
void Window::selectedFontScalar4(){
    SettingsVars.FontScalar = 4;
    renderArea->update();
}
void Window::selectedFontScalar5(){
    SettingsVars.FontScalar = 5;
    renderArea->update();
}
void Window::selectedFontScalar6(){
    SettingsVars.FontScalar = 6;
    renderArea->update();
}
void Window::selectedFontScalar7(){
    SettingsVars.FontScalar = 7;
    renderArea->update();
}
void Window::selectedFontScalar8(){
    SettingsVars.FontScalar = 8;
    renderArea->update();
}
void Window::selectedFontScalar12(){
    SettingsVars.FontScalar = 12;
    renderArea->update();
}
void Window::selectedFontScalar16(){
    SettingsVars.FontScalar = 16;
    renderArea->update();
}
void Window::selectedFontScalar20(){
    SettingsVars.FontScalar = 20;
    renderArea->update();
}

void Dialog::readFile() {
    if (DebugFunctions > 0) {
        qDebug() << "Dialog::readFile()";
    }
    QString filename = SettingsVars.WorkingDir + "MapperDBCreatorErrors.txt";
    QFile file(filename);
    QString line;
    this->r_textEdit->clear();
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
        QTextStream stream(&file);
        while (!stream.atEnd()){
            line = stream.readLine();
            this->r_textEdit->setText(this->r_textEdit->toPlainText()+line+"\n");
        }
    }
    file.close();

}

void Dialog2::readFile() {
    if (DebugFunctions > 0) {
        qDebug() << "Dialog2::readFile()";
    }
    QString filename= SettingsVars.WorkingDir + "License.txt";
    QFile file(filename);
    QString line;
    this->r_textEdit->clear();
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
        QTextStream stream(&file);
        while (!stream.atEnd()){
            line = stream.readLine();
            this->r_textEdit->setText(this->r_textEdit->toPlainText()+line+"\n");
        }
    }
    file.close();
}

void clsDlgNotice::readFile() {
    if (DebugFunctions > 0) {
        qDebug() << "clsDlgNotice::readFile()";
    }
    QString filename= SettingsVars.WorkingDir + "Notice.txt";
    QFile file(filename);
    QString line;
    this->r_textEdit->clear();
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
        r_textEdit->setText("CurrentNotice.txt\n\n");
        QTextStream stream(&file);
        while (!stream.atEnd()){
            line = stream.readLine();
            this->r_textEdit->setText(this->r_textEdit->toPlainText()+line+"\n");
        }
    }
    file.close();
}

void Window::selectedMAPS() {
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    for (int idx = 0; idx <=19 ; idx++) {
        if (varMAPS[idx].ptr == sender()) {
            MAPbmp = varMAPS[idx].FileName;
            minLat = varMAPS[idx].minLat;
            minLon = varMAPS[idx].minLon;
            maxLat = varMAPS[idx].maxLat;
            maxLon = varMAPS[idx].maxLon;
            WTerm1 = varMAPS[idx].WTerm1;
            WTerm2 = varMAPS[idx].WTerm2;
            WTerm3 = varMAPS[idx].WTerm3;
            WTerm4 = varMAPS[idx].WTerm4;
            MAP_WIDTH = varMAPS[idx].MAP_WIDTH;
            MAP_HEIGHT = varMAPS[idx].MAP_HEIGHT;
            currentMAP = idx;
            QSize availableSize = qApp->desktop()->availableGeometry().size();
            int widthA = int(float(availableSize.width()) * 0.9f);
            int heightA = int(float(availableSize.height()) * 0.9f);
            int setWidth, setHeight;
            if (widthA > (MAP_WIDTH+30)) { setWidth = MAP_WIDTH+30;} else {setWidth = widthA;}
            if (heightA > (MAP_HEIGHT+160)) { setHeight = MAP_HEIGHT+160;} else {setHeight = heightA;}
            //resize(setWidth,setHeight);
            ScaleFactor = 1;
            p.x = MAP_WIDTH/2;
            p.y = MAP_HEIGHT/2;
            Xoff = 0;
            Yoff = 0;
            if (SettingsVars.PreserveAspectRatio) {
                tmpWidth = MAP_WIDTH;
                tmpHeight = MAP_HEIGHT;
            } else {
                tmpWidth = width();
                tmpHeight = height();
            }
            if (SettingsVars.WeatherShown || SettingsVars.drawLightning || SettingsVars.getAlerts || SettingsVars.getCyclone ) {
                QString tmpFilename = SettingsVars.WorkingDir + "Weather.png";
                if (QFile::exists(tmpFilename)) {
                    QFile::remove(tmpFilename);
                }
                dWeather.doDownload();
            }
//            if (SettingsVars.GreyLineisShown) {
//                WorkingVars.SolarTerminator = getPath(tmpWidth,tmpHeight);
//            }
            renderArea->update();
            break;
        }
    }
}

void Window::updateA(QString tmpStr) {
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    connect(statusUpdateLbl, &QPushButton::clicked, this, &Window::selectedUpdate);
    statusUpdateLbl->setAutoFillBackground(true);
    statusUpdateLbl->setStyleSheet("background-color:yellow;");
    statusUpdateLbl->setText(tmpStr);
    statusUpdateLbl->setVisible(true);
}

void Window::createToolsArea() {
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    SettingsVars.ToolsAreaisShown = true;
    if (!WorkingVars.isDBToolsFlag) {} //Notify user to recreate db.

    //toolbar --------------------//

    HboxToolbar = new QHBoxLayout;

    toolbar = new QToolBar;
    toolbar->setContentsMargins(0,0,0,0);
    toolbar->setMaximumHeight(30);

    QAction *newAct = new QAction("Reset", this);
    newAct->setShortcuts(QKeySequence::New);
    newAct->setStatusTip(tr("Reset viewport zoom/pan"));
    connect(newAct, &QAction::triggered, this, &Window::selectedResetView);
    toolbar->addAction(newAct);

    toolbar->addSeparator();


    QFontComboBox * cmbxFontFamily = new QFontComboBox();
    cmbxFontFamily->setCurrentFont(SettingsVars.MyFont);
    connect(cmbxFontFamily, &QFontComboBox::currentFontChanged, this, &Window::mapFontChanged);
    toolbar->addWidget(cmbxFontFamily);

    toolbar->addSeparator();

    QAction *actMapFontScalarDec = new QAction("-", this);
    actMapFontScalarDec->setStatusTip(tr("Reduce font scalar"));
    connect(actMapFontScalarDec, &QAction::triggered, this, &Window::selectedMapFontScalarDec);
    toolbar->addAction(actMapFontScalarDec);

    QAction *actMapFontScalarInc = new QAction("+", this);
    actMapFontScalarInc->setStatusTip(tr("Increase font scalar"));
    connect(actMapFontScalarInc, &QAction::triggered, this, &Window::selectedMapFontScalarInc);
    toolbar->addAction(actMapFontScalarInc);

    toolbar->addSeparator();

    cmboMap = new QComboBox();
    for (int idx = 0; idx <=19 ; idx++) {
        if (varMAPS[idx].FileName != "") {
            QAction * action = new QAction(varMAPS[idx].FileName, this);
            varMAPS[idx].ptr = action;
            connect(action, &QAction::triggered, this, &Window::selectedMAPS);
            cmboMap->addItem(varMAPS[idx].FileName,QVariant::fromValue(action));
        }
    }
    cmboMap->setCurrentIndex(currentMAP);
    toolbar->addWidget(cmboMap);
    connect(cmboMap, SIGNAL(currentIndexChanged(int)), this, SLOT(cmboMapCurrentIndexChanged(int)));

    toolbar->addSeparator();

    QAction *selectSetFreqALE = new QAction("QSY", this);
    selectSetFreqALE->setStatusTip(tr("Uses MARS-ALE see readme.txt for brief synopsys until documentation is revised."));
    connect(selectSetFreqALE, &QAction::triggered, this, &Window::selectedSetFreqALE);
    toolbar->addAction(selectSetFreqALE);

    HboxToolbar->addWidget(toolbar,0,Qt::AlignLeft);
    //END toolbar------------------------//

    HboxToolsArea = new QHBoxLayout;
    ToolsArea2 = new QHBoxLayout;
    cmboStation1 = new QComboBox(this);
    connect(cmboStation1,&QComboBox::currentTextChanged,this,&Window::clickedDist);
    cmboStation2 = new QComboBox(this);
    connect(cmboStation2,&QComboBox::currentTextChanged,this,&Window::clickedDist);
    lbllblDist = new QLabel;
    lbllblDist->setText("Distance(mi):");
    lbllblAzi = new QLabel;
    lbllblAzi->setText("Bearing:");
    lblDist = new QLabel;
    lblAzi = new QLabel;
    pbChGraph = new QPushButton("Channel Graph");
    pbChGraph->setToolTip("Use configured working channels to create a circut reliability graph.");
    pbFqGraph = new QPushButton("Freq Graph");
    pbFqGraph->setToolTip("Create a reliability graph for 2-12Mhz.");
    pbCoverageGraph = new QPushButton("Coverage");
    pbCoverageGraph->setToolTip("Create propagation coverage overlay.");

    HboxToolsArea->addWidget(cmboStation1,4,Qt::AlignLeft);
    HboxToolsArea->addWidget(cmboStation2,4,Qt::AlignLeft);
    HboxToolsArea->addWidget(lbllblDist,0,Qt::AlignLeft);
    HboxToolsArea->addWidget(lblDist,4,Qt::AlignLeft);
    HboxToolsArea->addWidget(lbllblAzi,1,Qt::AlignLeft);
    HboxToolsArea->addWidget(lblAzi,4,Qt::AlignLeft);
    HboxToolsArea->addWidget(pbChGraph,1,Qt::AlignLeft);
    HboxToolsArea->addWidget(pbFqGraph,1,Qt::AlignLeft);
    HboxToolsArea->addWidget(pbCoverageGraph,1,Qt::AlignLeft);
    connect(pbChGraph,&QPushButton::clicked,this,&Window::selectedVOACAP);
    connect(pbFqGraph,&QPushButton::clicked,this,&Window::selectedVOACAPFreq);
    connect(pbCoverageGraph,&QPushButton::clicked,this,&Window::selectedVOACAPArea);



    mainLayout->addLayout(HboxToolbar,1,0);
    mainLayout->addLayout(HboxToolsArea,2,0);
    mainLayout->addLayout(ToolsArea2,4,0);

}

void Window::cmboMapCurrentIndexChanged(int index) {
    QAction * selectedAction = cmboMap->itemData(index, Qt::UserRole).value<QAction*>();
    if (selectedAction)
    {
        selectedAction->trigger(); //do stuff with your action
    }
}

void Window::selectedMapFontScalarInc() {
    if (SettingsVars.FontScalar < 20) {
        SettingsVars.FontScalar++;
        renderArea->update();
    }
}

void Window::selectedMapFontScalarDec() {
    if (SettingsVars.FontScalar > 1) {
        SettingsVars.FontScalar--;
        renderArea->update();
    }
}

void Window::mapFontChanged(const QFont &font) {
    SettingsVars.MyFont = font;
    renderArea->update();
}

void Window::hideToolBars() {
    cmboStation1->hide();
    cmboStation2->hide();
    lbllblDist->hide();
    lblDist->hide();
    lbllblAzi->hide();
    lblAzi->hide();
    pbChGraph->hide();
    pbFqGraph->hide();
    pbCoverageGraph->hide();

//    for(int i = 0 ; i < varHeatMap.arrHMsize; i++) {
//        WorkingVars.mapHMItoPB[i]->hide();
//    }

    toolbar->hide();

}

void Window::showToolBars() {
    cmboStation1->show();
    cmboStation2->show();
    lbllblDist->show();
    lblDist->show();
    lbllblAzi->show();
    lblAzi->show();
    pbChGraph->show();
    pbFqGraph->show();
    pbCoverageGraph->show();

//    for(int i = 0 ; i < varHeatMap.arrHMsize; i++) {
//        WorkingVars.mapHMItoPB[i]->show();
//    }

    toolbar->show();

}

void Window::clickedDist() {/*
    if ((DebugFunctions > 2) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    dist(cmboStation1->currentText(),cmboStation2->currentText());*/
}

void Window::selectedGotoWiki() {
    QString link = "https://stationmapper-docs.readthedocs.io/en/latest/index.html";
    QDesktopServices::openUrl(QUrl(link));
}

void Window::selectedBugReport() {
    QString link = "https://github.com/Alex-Pennington/StationMapper-Docs/issues";
    QDesktopServices::openUrl(QUrl(link));
}

void Window::selectedGotoPDF() {
    QString link = SettingsVars.WorkingDir + "stationmapper-docs.pdf";
    QDesktopServices::openUrl(QUrl::fromLocalFile(link));
}

void Window::selectedGotoMessages() {
    QString link = "https://groups.io/g/StationMapper/messages";
    QDesktopServices::openUrl(QUrl(link));
}

void Window::updateSolarTerminator() {
    if ((DebugFunctions > 5) || (Debug_Window > 2)) {qDebug() << Q_FUNC_INFO;}
    if (WorkingVars.firstRunTimerGL) {
        timer->start(1000*30); //Every 0.5 Min.
        WorkingVars.firstRunTimerGL = false;
    }
//    if (SettingsVars.GreyLineisShown) {
//        WorkingVars.SolarTerminator = getPath(tmpWidth,tmpHeight);
//        ptrRenderArea->repaint();
//    }
}

void Window::updateWeather() {
    if ((DebugFunctions > 5) || (Debug_Window > 2)) {qDebug() << Q_FUNC_INFO;}
    if (WorkingVars.firstRunTimerWeather){
        timerWeather->start(1000*60*5); //Every 5 Min.
        WorkingVars.firstRunTimerWeather = false;
    }
    if (SettingsVars.WeatherShown || SettingsVars.drawLightning || SettingsVars.getCyclone) {
        dWeather.doDownload();
        ptrRenderArea->repaint();
    }

}
void Window::updateLightning() {
    if ((DebugFunctions > 5) || (Debug_Window > 2)) {qDebug() << Q_FUNC_INFO;}
    if (WorkingVars.firstRunTimerLightning){
        timerLightning->start(1000*60*5); //Every 5 Min.
        WorkingVars.firstRunTimerLightning = false;
    }
    if (SettingsVars.getLightningSummary) {
        getLightning.doDownloadSummary(SettingsVars.MyCallSign);
    }
}
void Window::selectedDebug() {
//    if ((DebugFunctions > 1) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
//    if (Debug_DrawBoxes == true) {
//        Debug_DrawBoxes = false;
//    } else if (Debug_DrawBoxes == false) {
//        Debug_DrawBoxes = true;
//    }
//    renderArea->repaint();
    if ((DebugFunctions > 1) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    QDir dir;
    QString tmpDir = SettingsVars.WorkingDir + "debug/";
    QString tmpTimestamp = QString::number(QDateTime::currentSecsSinceEpoch(), 10);
    QString tmpFilename = tmpDir + tmpTimestamp + ".txt";
    dir.setPath(tmpDir);
    if (!dir.exists()) {
        qDebug() << "Debug dir created.";
        if (dir.mkdir(tmpDir)) {
            QFile::copy(WorkingVars.curLogfile,tmpFilename);
        }
    } else {
        QFile::copy(WorkingVars.curLogfile,tmpFilename);
    }

    qDebug() << "Encrypting logfile.";
    QProcess process;
    process.setProgram(SettingsVars.WorkingDir+"portable_gpg/bin/gpg.exe");
    process.setArguments({"--recipient","307E18879713820ACD987A233A4BF15C60C75202","--trust-model","always","--encrypt-files",SettingsVars.WorkingDir+"debug/"+tmpTimestamp+".txt"});
    process.setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(&process, &QProcess::readyRead, [&process](){qDebug() << process.readAll();});
    process.start();
    process.waitForFinished();
    process.close();
    QFile::remove(tmpFilename);
}

void Window::selectedUploadFile () {

    bool ok;
    QString path = QFileDialog::getOpenFileName(this,"Select file to upload","");
    QString servicecode = QInputDialog::getText(this,"Service Code","Enter the service code for the document library:", QLineEdit::Normal,"ALL_MARS",&ok);
    QString tmpTimestamp = QString::number(QDateTime::currentSecsSinceEpoch(), 10);

    QNetworkAccessManager *am = new QNetworkAccessManager(this);

    QString url;
    //if (QSslSocket::supportsSsl()) {
        url = "https://www.KYHiTech.com/StationMapper/uploads/LibraryUpload.php";
    //} else {
    //    url = "http://www.KYHiTech.com/StationMapper/uploads/upload.php";
    //}

    QNetworkRequest request((QUrl(url)));


    QFile file(path);
    QFileInfo fileInfo(file.fileName());
    QString filename(fileInfo.fileName());
    QString bound="margin"; //name of the boundary
    //according to rfc 1867 we need to put this string here:
    QByteArray data(QString("--" + bound + "\r\n").toLatin1());
    data.append("Content-Disposition: form-data; name=\"action\"\r\n\r\n");
    data.append("LibraryUpload.php\r\n"); //our script's name, as I understood. Please, correct me if I'm wrong
    data.append("--" + bound + "\r\n"); //according to rfc 1867

    QString tmp = "Content-Disposition: form-data; name=\"userfile\"; filename=\"" + WorkingDB[SettingsVars.MyCallSign].CallSign + "_" + servicecode + "_" + tmpTimestamp + filename + "\"\r\n";
//                "Content-Disposition: form-data; name=\"userfile\"; filename=\"AAR4TE_ALL_MARS_1580902269D:/StationMapper-Build/build-StationMapperV0.31-Desktop_Qt_5_12_3_MinGW_64_bit-Release/compile_commands.json\"\r\n"
    qDebug() << tmp;
    data.append(tmp); //name of the input is "uploaded" in my form, next one is a file name.
    data.append("Content-Type: application/pdf\r\n\r\n"); //data type
    if (!file.open(QIODevice::ReadOnly))
    return;
    data.append(file.readAll()); //let's read the file
    data.append("\r\n");
    data.append("--" + bound + "--\r\n"); //closing boundary according to rfc 1867
    QString concatenated = "StationMapper:2MLxB1KK38radR9j";
    QByteArray AuthData = concatenated.toLocal8Bit().toBase64();
    QString headerData = "Basic " + AuthData;
    request.setRawHeader( "Authorization", headerData.toLocal8Bit() );
    request.setRawHeader(QString("Content-Type").toLatin1(),QString("multipart/form-data; boundary=" + bound).toLatin1());
    request.setRawHeader(QString("Content-Length").toLatin1(), QString::number(data.length()).toLatin1());
    am->post(request,data);
    //qDebug() << data.data();
    //connect(this->reply, SIGNAL(finished()), this, SLOT(replyFinished()));
}

void Window::selectedDebugToFile() {
    if (WorkingVars.Debug_to_File) {
        QDir dir;
        qDebug() << "SettingsVars.VERSION" << SettingsVars.VERSION;
        qDebug() << "SettingsVars.cfgNCSapp" << SettingsVars.cfgNCSapp;
        qDebug() << "SettingsVars.MyCallSign" << SettingsVars.MyCallSign << WorkingDB[SettingsVars.MyCallSign].CallSign;
        bool ok;
        qDebug() << QInputDialog::getMultiLineText(this,"Bug Description","Observed Behavior:","",&ok);
        QString tmpDir = SettingsVars.WorkingDir + "debug/";
        QString tmpTimestamp = QString::number(QDateTime::currentSecsSinceEpoch(), 10);
        QString tmpFilename = tmpDir + "DEBUG_" + tmpTimestamp + ".txt";
        dir.setPath(tmpDir);
        if (!dir.exists()) {(dir.mkdir(tmpDir));}
        if (QFile::exists(SettingsVars.WorkingDir + "log.txt")) {
            QFile::copy(SettingsVars.WorkingDir + "log.txt", tmpFilename);
            QFile::remove(SettingsVars.WorkingDir + "log.txt");
        }

        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText("Encrypting " + tmpFilename);
        msgBox.exec();
        QProcess process;
        process.setProgram(SettingsVars.WorkingDir+"portable_gpg/bin/gpg.exe");
        process.setArguments({"--recipient","307E18879713820ACD987A233A4BF15C60C75202","--trust-model","always","--encrypt-files",tmpFilename});
        process.setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(&process, &QProcess::readyRead, [&process](){qDebug() << process.readAll();});
        process.start();
        process.waitForFinished();
        process.close();

        QFile::remove(tmpFilename);

        QNetworkAccessManager *am = new QNetworkAccessManager(this);
        QString path(tmpFilename + ".gpg");
        QString url;
        if (QSslSocket::supportsSsl()) {
            url = "https://www.KYHiTech.com/StationMapper/uploads/upload.php";
        } else {
            url = "http://www.KYHiTech.com/StationMapper/uploads/upload.php";
        }

        QNetworkRequest request((QUrl(url)));


        QString bound="margin"; //name of the boundary
        //according to rfc 1867 we need to put this string here:
        QByteArray data(QString("--" + bound + "\r\n").toLatin1());
        data.append("Content-Disposition: form-data; name=\"action\"\r\n\r\n");
        data.append("upload.php\r\n"); //our script's name, as I understood. Please, correct me if I'm wrong
        data.append("--" + bound + "\r\n"); //according to rfc 1867
        QString tmp = "Content-Disposition: form-data; name=\"userfile\"; filename=\"" + WorkingDB[SettingsVars.MyCallSign].CallSign + "_" + tmpTimestamp + ".txt.gpg"+"\"\r\n";
        data.append(tmp); //name of the input is "uploaded" in my form, next one is a file name.
        data.append("Content-Type: text/plain\r\n\r\n"); //data type
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        return;
        data.append(file.readAll()); //let's read the file
        data.append("\r\n");
        data.append("--" + bound + "--\r\n"); //closing boundary according to rfc 1867
        QString concatenated = "StationMapper:2MLxB1KK38radR9j";
        QByteArray AuthData = concatenated.toLocal8Bit().toBase64();
        QString headerData = "Basic " + AuthData;
        request.setRawHeader( "Authorization", headerData.toLocal8Bit() );
        request.setRawHeader(QString("Content-Type").toLatin1(),QString("multipart/form-data; boundary=" + bound).toLatin1());
        request.setRawHeader(QString("Content-Length").toLatin1(), QString::number(data.length()).toLatin1());
        am->post(request,data);
        qDebug() << data.data();
        //connect(this->reply, SIGNAL(finished()), this, SLOT(replyFinished()));


    } else {
        QStringList arguments;
        arguments << "--debug";
        qApp->quit();
        QProcess::startDetached(qApp->arguments()[0], arguments);
    }
}

void Window::selectedSettings() {
    if ((DebugFunctions > 0)) {qDebug() << Q_FUNC_INFO;}
    dlgSettingsTab * SettingsTabDialog;
    SettingsTabDialog = new dlgSettingsTab(this);
    SettingsTabDialog->setModal(true);
    SettingsTabDialog->exec();
}

void Window::selectedVOACAP() {/*
    if ((DebugFunctions > 1) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    QString tmpAntFilename = SettingsVars.AntennaFile.leftJustified(13,' ',true);
    QString tmpSSN = "";
    if (WorkingVars.SSN == -1) {
        tmpSSN = QInputDialog::getText(this, tr("Input Data"), "Enter Sun Spot Number");
        WorkingVars.SSN = tmpSSN.toInt();
    } else {
        tmpSSN = QString::number(WorkingVars.SSN);
    }
    QString mSNR = "60";
    QString NFdBm = "120";
    if (WorkingVars.NFdBm == "") {
        bool ok;
        WorkingVars.NFdBm = QInputDialog::getText(this,"Noise Floor @ 3Mhz","dBm", QLineEdit::Normal,"125",&ok);
    }
    NFdBm = WorkingVars.NFdBm.rightJustified(3,' ',true);
    //Check WorkingVars.isChannellsCSV
    if (!WorkingVars.isChannellsCSV) {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("'channels.csv' file must be imported before using this function.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {
            WorkingVars.VOCAPAreaIsRunning = false;
            return;}
    }

    QString BestFreq;
    QDir dir;
    QString tmpDir = SettingsVars.WorkingDir + "itshfbc/";
    QString tmpRunDir = "run/";
    QString tmpFile = "sm.dat";
    QString tmpPathFilename = tmpDir + tmpRunDir + tmpFile;
    dir.setPath(tmpDir);
    QString y = QString::number(QDate::currentDate().year());
    QString m = QString::number(QDate::currentDate().month());
    if (m.length() < 2) {
        m = " " + m;
    }
    tmpSSN = tmpSSN.rightJustified(3,' ',true);
    QString h = QString::number(QDateTime::currentDateTimeUtc().time().hour()).rightJustified(2,' ');
//    VOCAPCircut c =  VOACAPCalc(cmboStation1->currentText(),cmboStation2->currentText());
    varRelFreqSize = ChanDB.parseWChanCSV(SettingsVars.WorkingChanCSV, varRelFreq);
    if (!dir.exists()) {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("VOACAP not found.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {
            WorkingVars.VOCAPAreaIsRunning = false;
            return;}
    } else {
        QFile inFile( tmpPathFilename );
        if (inFile.open(QIODevice::WriteOnly))
        {
            QString FreqCard = "";
            if (SettingsVars.WorkingChanCSV != "A1,A2,A3,etc...") {
                FreqCard = "FREQUENCY ";
                for (int i = 0 ; i <= 10; i++) {
                    if (i < varRelFreqSize){
                        FreqCard += varRelFreq[i].padFreq;
                    } else {
                        FreqCard += "25.60";
                    }
                }
            } else {
                FreqCard = "FREQUENCY  2.60 4.30 6.40 8.6010.5012.9015.0017.1019.5022.5025.60";
            }
            QTextStream stream( &inFile );
            stream << "LINEMAX      55       number of lines-per-page\r\n";
            stream << "COEFFS    CCIR\r\n";
            //        "TIME         18   18    1    1"
            stream << "TIME         "+h+"   "+h+"    1    1\r\n";
            //"MONTH      2019 8.00"
            stream << "MONTH      "+y+m+".00\r\n";
            //"SUNSPOT      0. 0.00"
            stream << "SUNSPOT    "+tmpSSN+". 0.00\r\n";
            stream << "LABEL     STATION1            STATION2\r\n";
            //        "CIRCUIT   38.47N    82.63W    36.62N    88.32W  S     0"
            stream << "CIRCUIT   "+c.lat1+"   "+c.lon1+"    "+c.lat2+"   "+c.lon2+"  S     0\r\n";
            //                         Noise bDm            Min SNR
            stream << "SYSTEM       1. "+NFdBm+". 3.00  90. 60.0 3.00 0.10\r\n";
            stream << "FPROB      1.00 1.00 1.00 0.00\r\n";
            stream << "ANTENNA       1    1    2   30     0.000[samples\\"+tmpAntFilename+"]"+c.bearingForward+"    "+pad(SettingsVars.TrxPEP.toDouble(),1,4)+"\r\n";
            //stream << "ANTENNA       1    1    2   30     0.000[samples\\SAMPLE.23    ]"+c.bearingForward+"    0.1000\r\n";
            stream << "ANTENNA       2    2    2   30     0.000[samples\\SAMPLE.23    ]"+c.bearingReverse+"    0.0000\r\n";
            stream << FreqCard + "\r\n";
            stream << "METHOD       30    0\r\n";
            stream << "EXECUTE\r\n";
            stream << "QUIT\r\n";
            inFile.close();
            QProcess process;
            process.setProgram(tmpDir+"/bin_win/Icepacw.exe");
            process.setArguments({"SILENT",QDir::toNativeSeparators(SettingsVars.WorkingDir + "itshfbc"),tmpFile,"sm.out"});
            process.setProcessChannelMode(QProcess::MergedChannels);
            QObject::connect(&process, &QProcess::readyRead, [&process](){qDebug() << process.readAll();});
            process.start();
            process.waitForFinished();
            process.close();
            tmpPathFilename = tmpDir + tmpRunDir + "sm.out";
            QFile outFile(tmpPathFilename);
            if (!outFile.exists()) {
                qDebug() << tmpPathFilename << "not found";
            } else {
                QFile outFile( tmpPathFilename );
                if ( outFile.open(QIODevice::ReadOnly) ) {
                    //qDebug() << tmpPathFilename << "open";
                    QString fileLine = "";
                    while(!outFile.atEnd()) {
                        fileLine = outFile.readLine();
                        QStringList lineToken = fileLine.split(" ", QString::SkipEmptyParts);
                        if (fileLine.contains("FREQ")) {
                            if (lineToken.size() == 14) {
                                BestFreq = lineToken.at(1);
                            }
                        }
                        if (fileLine.contains("REL") & (lineToken.size() == 13)) {
                            for (int i = 0 ; i <= 11 ; i++) {
                                varRelFreq[i].rel = lineToken.at(i+1).toDouble();
                                if ((i) >= varRelFreqSize) {
                                    varRelFreq[i].ChanId = "NAN";
                                }
                                //qDebug() << varRelFreq[i].ChanId << varRelFreq[i].rel;
                            }
                        }
                    }
                    outFile.close();
                }
            }
        }
        //Second Run
        tmpFile = "sm2.dat";
        tmpPathFilename = tmpDir + tmpRunDir + tmpFile;
        QFile inFile2( tmpPathFilename );
        if (inFile2.open(QIODevice::WriteOnly) )
        {
            QString FreqCard = "";
            if (SettingsVars.WorkingChanCSV != "A1,A2,A3,etc...") {
                FreqCard = "FREQUENCY ";
                for (int i = 11 ; i <= 21; i++) {
                    if (i < varRelFreqSize){
                        FreqCard += varRelFreq[i].padFreq;
                    } else {
                        FreqCard += "25.60";
                    }
                }
            } else {
                FreqCard = "FREQUENCY  2.60 4.30 6.40 8.6010.5012.9015.0017.1019.5022.5025.60";
            }
            QTextStream stream( &inFile2 );
            stream << "LINEMAX      55       number of lines-per-page\r\n";
            stream << "COEFFS    CCIR\r\n";
            //        "TIME         18   18    1    1"
            stream << "TIME         "+h+"   "+h+"    1    1\r\n";
            //"MONTH      2019 8.00"
            stream << "MONTH      "+y+m+".00\r\n";
            //"SUNSPOT      0. 0.00"
            stream << "SUNSPOT    "+tmpSSN+". 0.00\r\n";
            stream << "LABEL     STATION1            STATION2\r\n";
            //        "CIRCUIT   38.47N    82.63W    36.62N    88.32W  S     0"
            stream << "CIRCUIT   "+c.lat1+"   "+c.lon1+"    "+c.lat2+"   "+c.lon2+"  S     0\r\n";
            //                         NoiseFloor           SNRdb
            stream << "SYSTEM       1. "+NFdBm+". 3.00  90. 60.0 3.00 0.10\r\n";
            stream << "FPROB      1.00 1.00 1.00 0.00\r\n";
            stream << "ANTENNA       1    1    2   30     0.000[samples\\"+tmpAntFilename+"]"+c.bearingForward+"    "+pad(SettingsVars.TrxPEP.toDouble(),1,4)+"\r\n";
            stream << "ANTENNA       2    2    2   30     0.000[samples\\SAMPLE.23    ]"+c.bearingReverse+"    0.0000\r\n";
            //stream << "ANTENNA       2    2    2   30     0.000[samples\\SAMPLE.23    ]"+c.bearingReverse+"    0.0000\r\n";
            stream << FreqCard + "\r\n";
            stream << "METHOD       30    0\r\n";
            stream << "EXECUTE\r\n";
            stream << "QUIT\r\n";
            inFile2.close();

            QProcess process;
            process.setProgram(tmpDir+"/bin_win/Icepacw.exe");
            process.setArguments({"SILENT",QDir::toNativeSeparators(SettingsVars.WorkingDir + "itshfbc"),tmpFile,"sm2.out"});
            process.setProcessChannelMode(QProcess::MergedChannels);
            QObject::connect(&process, &QProcess::readyRead, [&process](){qDebug() << process.readAll();});
            process.start();
            process.waitForFinished();
            process.close();
            tmpFile = "sm2.out";
            tmpPathFilename = tmpDir + tmpRunDir + tmpFile;
            QFile outFile2(tmpPathFilename);
            if (!outFile2.exists()) {
                qDebug() << tmpPathFilename << "not found";
            } else {
                //qDebug() << tmpPathFilename << "found";
                QFile outFile2( tmpPathFilename );
                if ( outFile2.open(QIODevice::ReadOnly) ) {
                    QString fileLine = "";
                    while(!outFile2.atEnd()) {
                        fileLine = outFile2.readLine();
                        QStringList lineToken = fileLine.split(" ", QString::SkipEmptyParts);
                        if (fileLine.contains("FREQ")) {
                            if (lineToken.size() == 14) {
                                BestFreq = lineToken.at(1);
                            }
                        }
                        if (fileLine.contains("REL") & (lineToken.size() == 13)) {
                            for (int i = 0 ; i <= 11 ; i++) {
                                varRelFreq[i + 11].rel = lineToken.at(i+1).toDouble();
                                if ((i+11) >= varRelFreqSize) {
                                    varRelFreq[i + 11].ChanId = "NAN";
                                }
                                //qDebug() << varRelFreq[i+11].ChanId << varRelFreq[i + 11].rel;
                            }
                        }
                    }
                    outFile2.close();
                }
            }
        }

//        for(int i = 0; i < varRelFreqSize; i++) {
//            qDebug() << varRelFreq[i].ChanId << varRelFreq[i].Mhz << varRelFreq[i].rel;
//        }
    }
    QFile::remove(tmpDir + tmpRunDir + "sm.dat");
    QFile::remove(tmpDir + tmpRunDir + "sm2.dat");
    QFile::remove(tmpDir + tmpRunDir + "sm.out");
    QFile::remove(tmpDir + tmpRunDir + "sm2.out");
    ChanGraph *ChanGraphDialog = new ChanGraph(cmboStation1->currentText() + "->" + cmboStation2->currentText());
    wMainWindowTab->ptrdlgProp->mainLayout->addWidget(ChanGraphDialog);
    ChanGraphDialog->show();*/
}

void Window::selectedVOACAPFreq() {/*
    if ((DebugFunctions > 0) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    QString tmpSSN = "";
    QString tmpAntFilename = SettingsVars.AntennaFile.leftJustified(13,' ',true);
    //qDebug() << "SSN:" << WorkingVars.SSN;
    if (WorkingVars.SSN == -1) {
        tmpSSN = QInputDialog::getText(this, tr("Input Data"), "Enter Sun Spot Number");
        WorkingVars.SSN = tmpSSN.toInt();
    } else {
        tmpSSN = QString::number(WorkingVars.SSN);
    }
    QString mSNR = "60";
    QString NFdBm = "120";
    if (WorkingVars.NFdBm == "") {
        bool ok;
        WorkingVars.NFdBm = QInputDialog::getText(this,"Noise Floor @ 3Mhz","dBm", QLineEdit::Normal,"125",&ok);
    }
    NFdBm = WorkingVars.NFdBm.rightJustified(3,' ',true);
    QString BestFreq;
    QDir dir;
    QString tmpDir = SettingsVars.WorkingDir + "itshfbc/";
    QString tmpRunDir = "run/";
    QString tmpFile = "sm.dat";
    QString tmpPathFilename = tmpDir + tmpRunDir + tmpFile;
    dir.setPath(tmpDir);
    QString y = QString::number(QDate::currentDate().year());
    QString m = QString::number(QDate::currentDate().month());
    if (m.length() < 2) {
        m = " " + m;
    }
    tmpSSN = tmpSSN.rightJustified(3,' ',true);
    QString h = QString::number(QDateTime::currentDateTimeUtc().time().hour()).rightJustified(2,' ');
    VOCAPCircut c =  VOACAPCalc(cmboStation1->currentText(),cmboStation2->currentText());
    varRelFreqSize = 22;
    if (!dir.exists()) {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("VOACAP not found.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {
            WorkingVars.VOCAPAreaIsRunning = false;
            return;}
    } else {
        QFile inFile( tmpPathFilename );
        if (inFile.open(QIODevice::WriteOnly))
        {
            QString FreqCard = "FREQUENCY  2.00 2.50 3.00 3.50 4.00 4.50 5.00 5.50 6.00 6.50 7.00";

            QTextStream stream( &inFile );
            stream << "LINEMAX      55       number of lines-per-page\r\n";
            stream << "COEFFS    CCIR\r\n";
            //        "TIME         18   18    1    1"
            stream << "TIME         "+h+"   "+h+"    1    1\r\n";
            //"MONTH      2019 8.00"
            stream << "MONTH      "+y+m+".00\r\n";
            //"SUNSPOT      0. 0.00"
            stream << "SUNSPOT    "+tmpSSN+". 0.00\r\n";
            stream << "LABEL     STATION1            STATION2\r\n";
            //        "CIRCUIT   38.47N    82.63W    36.62N    88.32W  S     0"
            stream << "CIRCUIT   "+c.lat1+"   "+c.lon1+"    "+c.lat2+"   "+c.lon2+"  S     0\r\n";
            stream << "SYSTEM       1. "+NFdBm+". 3.00  90. "+mSNR+".0 3.00 0.10\r\n";
            stream << "FPROB      1.00 1.00 1.00 0.00\r\n";
            stream << "ANTENNA       1    1    2   30     0.000[samples\\"+tmpAntFilename+"]"+c.bearingForward+"    "+pad(SettingsVars.TrxPEP.toDouble(),1,4)+"\r\n";
            stream << "ANTENNA       2    2    2   30     0.000[samples\\SAMPLE.23    ]"+c.bearingReverse+"    0.0000\r\n";
            stream << FreqCard + "\r\n";
            stream << "METHOD       30    0\r\n";
            stream << "EXECUTE\r\n";
            stream << "QUIT\r\n";
            inFile.close();
            QProcess process;
            process.setProgram(tmpDir+"/bin_win/Icepacw.exe");
            process.setArguments({"SILENT",QDir::toNativeSeparators(SettingsVars.WorkingDir + "itshfbc"),tmpFile,"sm.out"});
            process.setProcessChannelMode(QProcess::MergedChannels);
            QObject::connect(&process, &QProcess::readyRead, [&process](){qDebug() << process.readAll();});
            process.start();
            process.waitForFinished();
            process.close();
            tmpPathFilename = tmpDir + tmpRunDir + "sm.out";
            QFile outFile(tmpPathFilename);
            if (!outFile.exists()) {
                qDebug() << tmpPathFilename << "not found";
            } else {
                QFile outFile( tmpPathFilename );
                if ( outFile.open(QIODevice::ReadOnly) ) {
                    //qDebug() << tmpPathFilename << "open";
                    QString fileLine = "";
                    while(!outFile.atEnd()) {
                        fileLine = outFile.readLine();
                        QStringList lineToken = fileLine.split(" ", QString::SkipEmptyParts);
                        if (fileLine.contains("FREQ")) {
                            if (lineToken.size() == 14) {
                                BestFreq = lineToken.at(1);
                                for(int i = 0; i <= 11; i++) {
                                    varRelFreq[i].Mhz = lineToken.at(i+2).toDouble();
                                }
                            }
                        }
                        if (fileLine.contains("REL") & (lineToken.size() == 13)) {
                            for (int i = 0 ; i < 11 ; i++) {
                                varRelFreq[i].rel = lineToken.at(i+1).toDouble();
                                //qDebug() << varRelFreq[i].Mhz << varRelFreq[i].rel;
                            }
                        }
                    }
                    outFile.close();
                }
            }
        }
        //Second Run
        tmpFile = "sm2.dat";
        tmpPathFilename = tmpDir + tmpRunDir + tmpFile;
        QFile inFile2( tmpPathFilename );
        if (inFile2.open(QIODevice::WriteOnly) )
        {
            QString  FreqCard = "FREQUENCY  7.50 8.00 8.50 9.00 9.5010.0010.5011.0011.5012.0012.50";
            QTextStream stream( &inFile2 );
            stream << "LINEMAX      55       number of lines-per-page\r\n";
            stream << "COEFFS    CCIR\r\n";
            //        "TIME         18   18    1    1"
            stream << "TIME         "+h+"   "+h+"    1    1\r\n";
            //"MONTH      2019 8.00"
            stream << "MONTH      "+y+m+".00\r\n";
            //"SUNSPOT      0. 0.00"
            stream << "SUNSPOT    "+tmpSSN+". 0.00\r\n";
            stream << "LABEL     STATION1            STATION2\r\n";
            //        "CIRCUIT   38.47N    82.63W    36.62N    88.32W  S     0"
            stream << "CIRCUIT   "+c.lat1+"   "+c.lon1+"    "+c.lat2+"   "+c.lon2+"  S     0\r\n";
            stream << "SYSTEM       1. "+NFdBm+". 3.00  90. "+mSNR+".0 3.00 0.10\r\n";
            stream << "FPROB      1.00 1.00 1.00 0.00\r\n";
            stream << "ANTENNA       1    1    2   30     0.000[samples\\"+tmpAntFilename+"]"+c.bearingForward+"    "+pad(SettingsVars.TrxPEP.toDouble(),1,4)+"\r\n";
            stream << "ANTENNA       2    2    2   30     0.000[samples\\SAMPLE.23    ]"+c.bearingReverse+"    0.0000\r\n";
            stream << FreqCard + "\r\n";
            stream << "METHOD       30    0\r\n";
            stream << "EXECUTE\r\n";
            stream << "QUIT\r\n";
            inFile2.close();

            QProcess process;
            process.setProgram(tmpDir+"/bin_win/Icepacw.exe");
            process.setArguments({"SILENT",QDir::toNativeSeparators(SettingsVars.WorkingDir + "itshfbc"),tmpFile,"sm2.out"});
            process.setProcessChannelMode(QProcess::MergedChannels);
            QObject::connect(&process, &QProcess::readyRead, [&process](){qDebug() << process.readAll();});
            process.start();
            process.waitForFinished();
            process.close();
            tmpFile = "sm2.out";
            tmpPathFilename = tmpDir + tmpRunDir + tmpFile;
            QFile outFile2(tmpPathFilename);
            if (!outFile2.exists()) {
                qDebug() << tmpPathFilename << "not found";
            } else {
                //qDebug() << tmpPathFilename << "found";
                QFile outFile2( tmpPathFilename );
                if ( outFile2.open(QIODevice::ReadOnly) ) {
                    QString fileLine = "";
                    while(!outFile2.atEnd()) {
                        fileLine = outFile2.readLine();
                        QStringList lineToken = fileLine.split(" ", QString::SkipEmptyParts);
                        if (fileLine.contains("FREQ")) {
                            if (lineToken.size() == 14) {
                                BestFreq = lineToken.at(1);
                                for(int i = 0; i <= 11; i++) {
                                    varRelFreq[i+11].Mhz = lineToken.at(i+2).toDouble();
                                }
                            }
                        }
                        if (fileLine.contains("REL") & (lineToken.size() == 13)) {
                            for (int i = 0 ; i < 11 ; i++) {
                                varRelFreq[i + 11].rel = lineToken.at(i+1).toDouble();

                                //qDebug() << varRelFreq[i+11].Mhz << varRelFreq[i + 11].rel;
                            }
                        }
                    }
                    outFile2.close();
                }
            }
        }

//        for(int i = 0; i < varRelFreqSize; i++) {
//            qDebug() << varRelFreq[i].ChanId << varRelFreq[i].Mhz << varRelFreq[i].rel;
//        }
    }
    QFile::remove(tmpDir + tmpRunDir + "sm.dat");
    QFile::remove(tmpDir + tmpRunDir + "sm2.dat");
    QFile::remove(tmpDir + tmpRunDir + "sm.out");
    QFile::remove(tmpDir + tmpRunDir + "sm2.out");
    FreqGraph *FreqGraphDialog = new FreqGraph(cmboStation1->currentText() + "->" + cmboStation2->currentText());
    wMainWindowTab->ptrdlgProp->mainLayout->addWidget(FreqGraphDialog);
    FreqGraphDialog->show();
    */
}
void Window::selectedVOACAPArea() {/*
    if ((DebugFunctions > 0) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    if(varHeatMap.arrHMsize > 9) {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("No more than 10 Propigation coverage area overlays may be loaded at a time.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {
            WorkingVars.VOCAPAreaIsRunning = false;
            return;}
    }
    QString mSNR = "60";
    QString NFdBm = "125";
    NFdBm = NFdBm.rightJustified(3,' ',true);
    WorkingVars.drawHeatMap = false;
    if (WorkingVars.VOCAPAreaIsRunning) {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("VOACAP running.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {return;}
    }
    WorkingVars.VOCAPAreaIsRunning = true;
    if ((DebugFunctions > 1) || (Debug_Window > 1)) {qDebug() << Q_FUNC_INFO;}
    QString tmpSSN = "";
    if (WorkingVars.SSN == -1) {
        tmpSSN = QInputDialog::getText(this, tr("Input Data"), "Enter Sun Spot Number");
        WorkingVars.SSN = tmpSSN.toInt();
    } else {
        tmpSSN = QString::number(WorkingVars.SSN);
    }
    //Check WorkingVars.isChannellsCSV
    if (!WorkingVars.isChannellsCSV) {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("'channels.csv' file must be imported before using this function.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {
            WorkingVars.VOCAPAreaIsRunning = false;
            return;}
    }
    QString NetFreq = "";
    bool ok;
//    WorkingVars.ChanInput = QInputDialog::getText(this,"Input Channel Identifier","Id:", QLineEdit::Normal,SettingsVars.CoverageDefaultChan,&ok).toUpper();
//    SettingsVars.CoverageDefaultChan = WorkingVars.ChanInput;
    WorkingVars.ChanInput = QInputDialog::getItem(this,"Input Channel Identifier","Id:", SettingsVars.CoverageDefaultChanList,SettingsVars.CoverageDefaultChan,true,&ok).toUpper();
    if (!SettingsVars.CoverageDefaultChanList.contains(WorkingVars.ChanInput)) {
        SettingsVars.CoverageDefaultChanList << WorkingVars.ChanInput;
        SettingsVars.CoverageDefaultChan = SettingsVars.CoverageDefaultChanList.size() -1;
        QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
        QSettings settings(fileNameSettings,QSettings::IniFormat);
        settings.setValue("CoverageDefaultChanList", SettingsVars.CoverageDefaultChanList);
        settings.setValue("CoverageDefaultChan", SettingsVars.CoverageDefaultChan);
    } else {
        SettingsVars.CoverageDefaultChan = SettingsVars.CoverageDefaultChanList.indexOf(WorkingVars.ChanInput);
        QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
        QSettings settings(fileNameSettings,QSettings::IniFormat);
        settings.setValue("CoverageDefaultChan", SettingsVars.CoverageDefaultChan);
    }
    if(ChanDB.searchIdx(WorkingVars.ChanInput.toUpper())) {
        NetFreq = pad(ChanDB[ChanDB.searchResult].Freq/1000, 2,3);
        //qDebug() << NetFreq << NetTimes.at(2) << WorkingVars.NetAddress;
    } else {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("Channel not found in database.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {
            WorkingVars.VOCAPAreaIsRunning = false;
            return;}
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString m = QString::number(QDate::currentDate().month());
    if (m.length() < 2) {
        m = " " + m;
    }

    tmpSSN = tmpSSN.rightJustified(3,' ',true);
    QString h = QString::number(QDateTime::currentDateTimeUtc().time().hour()).rightJustified(2,' ');

    //qDebug() << varMAPS[currentMAP].WTerm2 << varMAPS[currentMAP].WTerm4 << varMAPS[currentMAP].WTerm1 << varMAPS[currentMAP].WTerm3;
    QString t1 = pad(varMAPS[currentMAP].WTerm2,4,1);
    QString t2 = pad(varMAPS[currentMAP].WTerm4,4,1);
    QString t3 = pad(varMAPS[currentMAP].WTerm1,3,1);
    QString t4 = pad(varMAPS[currentMAP].WTerm3,3,1);



    QDir dir;
    QString tmpDir = SettingsVars.WorkingDir + "itshfbc/";
    QString tmpRunDir = "areadata/default/";
    QString tmpFile = "1.ICE";
    QString tmpPathFilename = tmpDir + tmpRunDir + tmpFile;
    dir.setPath(tmpDir);
    if (SettingsVars.MyCallSign != 9999) {
        double dLat = WorkingDB[SettingsVars.MyCallSign].WGSLat.toDouble();
        double dLon = WorkingDB[SettingsVars.MyCallSign].WGSLon.toDouble();
        QString lat = "";
        QString lon = "";
        if (dLat < 0) {
            dLat = qFabs(dLat);
            lat = pad(dLat,2,2) + "S";
        } else {
            lat = pad(dLat,2,2) + "N";
        }
        if (dLon < 0) {
            dLon = qFabs(dLon);
            lon = pad(dLon,3,2) + "W";
        } else {
            lon = pad(dLon,3,2) + "E";
        }


        if (!dir.exists()) {
            QMessageBox msgBox;
            msgBox.setText("Attention");
            msgBox.setInformativeText("VOACAP not found.");
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            int ret = msgBox.exec();
            if (ret) {
                WorkingVars.VOCAPAreaIsRunning = false;
                return;}
        } else {
            QFile inFile( tmpPathFilename );
            if (inFile.open(QIODevice::WriteOnly))
            {
                QTextStream stream( &inFile );
                stream << "Model    :ICEPAC\r\n";
                stream << "Colors   :Black    :Blue     :Ignore   :Ignore   :Red      :Black with shading\r\n";
                stream << "Cities   :RECEIVE.NUL\r\n";
                stream << "Nparms   :    1\r\n";
                stream << "Parameter:REL      0\r\n";
                stream << "Transmit : "+lat+"   "+lon+"   XXXXXXX              Short\r\n";
                stream << "Pcenter  : "+lat+"   "+lon+"   XXXXXXX\r\n";
                //qDebug() << "Area     :    -126.0     -66.0      24.0      50.0\r\n";
                //qDebug() << "Area     :   "+t1+"    "+t2+"     "+t3+"     "+t4+"\r\n";
                stream << "Area     :   "+t1+"    "+t2+"     "+t3+"     "+t4+"\r\n";
                stream << "Gridsize :  100   22\r\n";
                varHeatMap.gridSize = 100;
                stream << "Method   :   30\r\n";
                stream << "Coeffs   :CCIR\r\n";
                stream << "Months   :  "+m+".00   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00\r\n";
                stream << "Ssns     :    "+tmpSSN+"      0      0      0      0      0      0      0      0\r\n";
                stream << "Qindexs  :   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00\r\n";
                stream << "Hours    :     "+h+"      0      0      0      0      0      0      0      0\r\n";
                stream << "Freqs    : "+NetFreq+"  0.000  0.000  0.000  0.000  0.000  0.000  0.000  0.000\r\n";
                //                                                SNR db
                stream << "System   :  "+NFdBm+"     3.000   90   40     3.000     0.100\r\n";
                stream << "Fprob    : 1.00 1.00 1.00 0.00\r\n";
                QString tmpAntFilename = SettingsVars.AntennaFile.leftJustified(12,' ',true);
                stream << "Rec Ants :[samples \\SAMPLE.23   ]  gain=   0.0   0.0\r\n";
                stream << "Tx Ants  :[samples \\"+tmpAntFilename+"]  0.000 "+ pad(SettingsVars.AntAngle.toDouble(),3,1) + "     "+pad(SettingsVars.TrxPEP.toDouble(),1,4)+"\r\n";
                inFile.close();
            }
            QProcess *process = new QProcess();
            process->setProgram(tmpDir+"/bin_win/Icepacw.exe");
            process->setArguments({"SILENT",QDir::toNativeSeparators(SettingsVars.WorkingDir + "itshfbc"),"AREA","CALC","default\\1.ICE"});
            process->setProcessChannelMode(QProcess::MergedChannels);
            QObject::connect(process, &QProcess::readyRead, [&process](){qDebug() << process->readAll();});
            QObject::connect(process , SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(VOACAPAreaFinished(int, QProcess::ExitStatus)));
            QObject::connect(process, &QObject::destroyed,[] { qDebug() << "Sender got deleted!"; });
            process->start();
            if (!process->waitForStarted()){
                qDebug() << "error";
            } else {
                process->moveToThread(QCoreApplication::instance()->thread());
            }
        }
    } else {
        QApplication::restoreOverrideCursor();
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("Callsign must be set in the settings dialog to use this feature.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) {
            WorkingVars.VOCAPAreaIsRunning = false;
            return;}
    }*/
}

void Window::VOACAPAreaFinished(int, QProcess::ExitStatus) {/*
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (!thread->isRunning()) {
        thread->start();
    } else {
        qDebug() << "parseHeatMap in progress";
    }*/
}

void Window::restoreCursor() {/*
    if (DebugFunctions > 0) {qDebug() << Q_FUNC_INFO;}
    QPushButton *pbHM = new QPushButton(WorkingVars.ChanInput);
    pbHM->setContextMenuPolicy(Qt::CustomContextMenu);
    pbHM->setToolTip("Click to select overlay.\r\nRight click to close.");
    connect(pbHM, SIGNAL(customContextMenuRequested(const QPoint &)),ptrWindow, SLOT(ShowContextMenuAreaButton(const QPoint &)));
    WorkingVars.mapPBtoHMI[pbHM] = varHeatMap.arrHMsize-1;
    WorkingVars.mapHMItoPB[varHeatMap.arrHMsize-1] = pbHM;
    WorkingVars.HMindex = varHeatMap.arrHMsize-1;
    ToolsArea2->addWidget(pbHM);
    connect(pbHM,&QPushButton::clicked,this,&Window::swapHM);
    PBHighlite();
    renderArea->update();
    QApplication::restoreOverrideCursor();
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}*/
}

void Window::swapHM() {/*
    if ((DebugFunctions > 0)) {qDebug() << Q_FUNC_INFO;}
    bool tmpDrawHeatMap = WorkingVars.drawHeatMap;
    WorkingVars.drawHeatMap = false;
    WorkingVars.VOCAPAreaIsRunning = true;
    if ((WorkingVars.HMindex != WorkingVars.mapPBtoHMI[(QPushButton *)sender()]) || (((WorkingVars.HMindex == WorkingVars.mapPBtoHMI[(QPushButton *)sender()])) && (tmpDrawHeatMap == false))) {
        WorkingVars.HMindex = WorkingVars.mapPBtoHMI[(QPushButton *)sender()];
        WorkingVars.drawHeatMap = true;
        WorkingVars.VOCAPAreaIsRunning = false;
        renderArea->update();
        PBHighlite();
        return;
    } else {
        WorkingVars.drawHeatMap = false;
        WorkingVars.VOCAPAreaIsRunning = false;
        PBHighlite();
        renderArea->update();
        return;
    }*/
}

void Window::ShowContextMenuAreaButton(const QPoint &pos){/*
    if ((DebugFunctions > 0)) {qDebug() << Q_FUNC_INFO;}
    QMenu contextMenu(tr("Context menu"), this);          
    WorkingVars.deleteHMindex = WorkingVars.mapPBtoHMI[(QPushButton *)sender()];
    //qDebug() << WorkingVars.deleteHMindex;
    QAction action1("close", this);
    connect(&action1, SIGNAL(triggered()), this, SLOT(closeAreaButton()));
    contextMenu.addAction(&action1);
    QPushButton * tmpButtonPTR = (QPushButton *)sender();
    contextMenu.exec(tmpButtonPTR->mapToGlobal(pos));
    if ((DebugFunctions > 0)) {qDebug() << "exit " << Q_FUNC_INFO;}*/
 }

void Window::closeAreaButton() {/*
    if ((DebugFunctions > 0)) {qDebug() << Q_FUNC_INFO;}
    bool tmpDraw = WorkingVars.drawHeatMap;
    WorkingVars.drawHeatMap = false;
    renderArea->update();
    WorkingVars.mapHMItoPB[WorkingVars.deleteHMindex]->close();
    delete WorkingVars.mapHMItoPB[WorkingVars.deleteHMindex];
    WorkingVars.mapPBtoHMI.remove(WorkingVars.mapHMItoPB[WorkingVars.deleteHMindex]);
    WorkingVars.mapHMItoPB.remove(WorkingVars.deleteHMindex) ;
    for(int i = (WorkingVars.deleteHMindex) ; i < varHeatMap.arrHMsize -1; i++) {
        WorkingVars.mapHMItoPB[i] = WorkingVars.mapHMItoPB[i+1];
        WorkingVars.mapHMItoPB.remove(i+1);
        WorkingVars.mapPBtoHMI[WorkingVars.mapHMItoPB[i]] = i;
        WorkingVars.mapPBtoHMI.remove(WorkingVars.mapHMItoPB[i+1]);
    }
    //qDebug() << WorkingVars.deleteHMindex << WorkingVars.HMindex;
    if (WorkingVars.deleteHMindex <= WorkingVars.HMindex) {
        WorkingVars.HMindex--;
        if (WorkingVars.HMindex < 0) {
            WorkingVars.HMindex = 0;}
    }
    varHeatMap.deleteHM(WorkingVars.deleteHMindex);
    if(tmpDraw & (varHeatMap.arrHMsize > 0)){
        WorkingVars.drawHeatMap = true;}
    else { WorkingVars.drawHeatMap = false;}
    if ((varHeatMap.arrHMsize - 1) > 0) {
        PBHighlite();}
    if ((DebugFunctions > 1)) {qDebug() << "exit " << Q_FUNC_INFO;}*/
}

void Window::PBHighlite() {/*
    if ((DebugFunctions > 0)) {qDebug() << Q_FUNC_INFO;}
    for(int i = 0; i < varHeatMap.arrHMsize; i++) {
        WorkingVars.mapHMItoPB[i]->setStyleSheet("");
    }
    if (WorkingVars.drawHeatMap) {
        //qDebug() << "WorkingVars.HMindex : " << WorkingVars.HMindex;
        WorkingVars.mapHMItoPB[WorkingVars.HMindex]->setAutoFillBackground(true);
        WorkingVars.mapHMItoPB[WorkingVars.HMindex]->setStyleSheet("background-color:green;");
        WorkingVars.mapHMItoPB[WorkingVars.HMindex]->show();
    }
    if ((DebugFunctions > 1)) {qDebug() << "exit" << Q_FUNC_INFO;}*/
}

bool Window::eventFilter(QObject *obj, QEvent *event) {
    if (event->type () == QEvent::KeyPress) {
         QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == 16777264) && (keyEvent->modifiers() == Qt::CTRL)) { //F1
            if ((Debug_Window > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
            wMainWindowTab->tabWidget->setCurrentIndex(0);
            return true;
        } else if ((keyEvent->key() == 16777265) && (keyEvent->modifiers() == Qt::CTRL)) { //F2
            if ((Debug_Window > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
            wMainWindowTab->tabWidget->setCurrentIndex(1);
            return true;
        } else if ((keyEvent->key() == 16777266) && (keyEvent->modifiers() == Qt::CTRL)) { //F3
            if ((Debug_Window > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
            wMainWindowTab->tabWidget->setCurrentIndex(2);
            if (SettingsVars.NCSTab == true) {
                wMainWindowTab->ptrdlgNCS->leCommand->setFocus();
            }
            return true;
        } else if ((keyEvent->key() == 16777267) && (keyEvent->modifiers() == Qt::CTRL)) { //F4
            if ((Debug_Window > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
            wMainWindowTab->tabWidget->setCurrentIndex(3);
            return true;
        } else if ((keyEvent->key() == 16777268) && (keyEvent->modifiers() == Qt::CTRL)) { //F5
            if (SettingsVars.NCSTab == true) {
                if ((Debug_Window > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
                wMainWindowTab->tabWidget->setCurrentIndex(4);
                return true;
            }
        } else if ((keyEvent->key() == 16777269)) { //F6
            if ((Debug_Window > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
            wMainWindowTab->tabWidget->setCurrentIndex(2);
            if (SettingsVars.NCSTab == true) {
                wMainWindowTab->ptrdlgNCS->leCommand->setFocus();
            }
            return true;
        }
         return false;
    } else {
        return false;
    }
};
