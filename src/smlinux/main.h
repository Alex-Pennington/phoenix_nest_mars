#ifndef MAIN_H
#define MAIN_H

#include "window.h"
#include "classdb.h"
#include "classroster.h"
//#include "classlightning.h"
#include "classchan.h"

#include <QCoreApplication>
#include <QString>
#include <QPainter>
#include <QMessageBox>
#include <QFileSystemWatcher>
#include <QWidget>
#include <QDebug>
#include <QDir>
#include <QFileInfoList>
#include <QDateTime>
#include <QFont>
#include <QPainterPath>

//Classes

extern Window * ptrWindow;
extern dlgNCS * ptrExternDlgNCS;
extern QFileSystemWatcher logWatcher;
extern QFileSystemWatcher watcher;
extern clsDB WorkingDB;
extern clsRoster RosterVar;
//extern classlightning varLightning;
extern classchan ChanDB;

//Settings Vars
    static const int MaxRecordCount = 1000;
    //extern QString AddressCSVfilenameDB;
    extern QString VERSION;
    extern QString DB_Filename;
    extern QString MAPbmp;
    //RenderArea Vars
        extern QPoint points[MaxRecordCount];
        extern int Xoff;
        extern int Yoff;
        extern bool FirstRun;
        extern bool PointChanged;
        extern bool ScaleChanged;
        extern float ScaleFactor;
        extern float PrevScaleFactor;
        extern bool dragging;
        extern int tmpWidth;
        extern int tmpHeight;
        extern int MAP_HEIGHT;
        extern int MAP_WIDTH;
        extern float minLat;  //Leftmost
        extern float maxLat;  //Righmost
        extern float maxLon;  //Top
        extern float minLon;  //Bottom
        extern float WTerm1;  //Leftmost
        extern float WTerm2;  //Righmost
        extern float WTerm3;  //Top
        extern float WTerm4;  //Bottom

//Vars to be classed later

extern int DebugFileWatcher;

extern QString CallSignDB[MaxRecordCount];
extern QString AbrCallSignDB[MaxRecordCount];
extern QString MGRSDB[MaxRecordCount];
extern QString LatDB[MaxRecordCount];
extern QString LonDB[MaxRecordCount];
extern int db_countDB;
extern bool doFlagDB;
extern int posDB;

extern bool doFlag;

extern bool DoPrintFunction;
extern bool DoPrintFunctionfileLinesplit;
//extern bool ShowAll;



struct Coords {
    QString lat;
    QString lon;
};


struct structMAPS {
    QString FileName;
    int MAP_HEIGHT;
    int MAP_WIDTH;
    int minLon;
    int maxLon;
    int maxLat;
    int minLat;
    float WTerm1;
    float WTerm2;
    float WTerm3;
    float WTerm4;
    QAction *ptr;
};

struct fileMeta{
    QString fileNamePath;
    QDateTime lastMod;
};

struct structWorkingVar {
    QString NetAddress = "";
    bool isChannellsCSV = false;
    QString curLogfile;
    bool isDBToolsFlag = false;
    bool FirstRunLogCheck = true;
    int RightClickCounter = 0;
    int RelayColor = 1;
    QPainterPath SolarTerminator;
    bool firstRunTimerGL = true;
    bool firstRunTimerWeather = true;
    bool firstRunTimerLightning = true;
    bool haveDisplayedAerisAccessError = false;
    bool drawHeatMap;
    int SSN = -1;
    bool VOCAPAreaIsRunning = false;
    QString DB_Filename;
    QString MAPbmp;
    QString NFdBm = "";
    QString minSNR;
    QString ChanInput;
    int HMindex = 0;
    QPushButton * ptrCloseButton;
    int deleteHMindex;
    QMap<QPushButton*, int> mapPBtoHMI;
    QMap<int, QPushButton*> mapHMItoPB;
    bool Closing = false;
    bool dlgNCS_CommandPB = false;
    bool Debug_to_File =  false;
    QString lastRX;
    bool NotesIsFloat = false;
};

struct structSettingsVar {
    QString VERSION;
    QString WorkingDir;
    QString ACPDDir;
    QString ACPncslogsDir;
    QString SMV2PENDOUTDir;
    QFont MyFont;
    int PointSize;
    int FontScalar;
    bool FontWarning;
    //QString DB_Filename;
    //QString MAPbmp;
    int cfgNCSapp; //1 for ACP, 2 for NCSPro
    bool DrawRelayLines;
    bool DrawClosedStation;
    bool GreyLineisShown;
    bool WeatherShown;
    bool getAlerts;
    QString WAccessID;
    QString WSecretKey;
    int MyCallSign;
    bool drawLightning;
    QString myLat;
    QString myLon;
    bool getLightningSummary;
    int LightningSummaryRadius;
    QString WorkingChanCSV;
    QString AntAngle;
    QString TrxPEP;
    QString AntennaFile;
    int AntennaIndex;
    int QssFileIndex;
    bool PreserveAspectRatio = false;
    bool ToolsAreaisShown = false;
    QString DB_Filename;
    QString MAPbmp;
    bool getCyclone;
    fileMeta addressCSV;
    fileMeta channelCSV;
    bool NCSTab = true;
    QString ComspotRI;
    QString ComspotFM;
    QString ComspotTO;
    QString ComspotINFO;
    QString ComspotLOC;
    bool TerminalTab;
    QStringList CoverageDefaultChanList;
    int CoverageDefaultChan;
    QStringList DefaultNetList;
    int DefaultNet;
    QString QssFile;
    QFont NotesFont;
    QString MessagesFolderRoot;
    QString ArchiveFolder;
    bool ACPDMsgNR;
    QString MARSALEDir;
    bool FT817USB;
    QFont InfoFont;
    bool WeatherProxy = true;
    QString ServiceCodes;
};

extern structWorkingVar WorkingVars;

extern structSettingsVar SettingsVars;

extern structMAPS varMAPS[20];

extern int currentMAP;

class FeatureInstall : public QDialog
{
    Q_OBJECT

public:
    FeatureInstall();
    static QTextEdit * f_textEdit;

private:

};

class MyClass : public QWidget
{
    Q_OBJECT

public:
    MyClass(QWidget* parent=0)
        :QWidget(parent){}

    ~MyClass(){}

public slots:
    void showModified(const QString& str)
    {
        if (DebugFileWatcher > 0){ qDebug() << "showModified" << str; }
        Q_UNUSED(str)
        if (SettingsVars.cfgNCSapp ==1) {
            QDir dir;
            dir.setPath(str);
            QFileInfoList list = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
            if (list.size() > 0) {
                WorkingVars.curLogfile = list.first().filePath();
                logWatcher.addPath(WorkingVars.curLogfile);
                if (DebugFileWatcher > 0){qDebug() << logWatcher.files();}
                if (DebugFileWatcher > 0){qDebug() << "currLogFile =" << WorkingVars.curLogfile;}
            }
        } else if (SettingsVars.cfgNCSapp ==2) {
            QDir dir;
            dir.setPath(SettingsVars.SMV2PENDOUTDir);
            QStringList nameFilter;
            nameFilter << "*ROSTER*.txt";
            QFileInfoList list = dir.entryInfoList(nameFilter, QDir::NoDotAndDotDot | QDir::Files, QDir::Time);
            if (DebugFileWatcher > 0){ qDebug() << list; }
            if (list.size() > 0) {
                WorkingVars.curLogfile = list.first().filePath();
                if (DebugFileWatcher > 0){ qDebug() << WorkingVars.curLogfile << "PENDING_OUT Modified"; }
                ptrWindow->callLogChecker(WorkingVars.curLogfile);
            }
        }
    }

};


#endif // MAIN_H
