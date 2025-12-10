#ifndef DLGSETTINGSTAB_H
#define DLGSETTINGSTAB_H
#include "classaddresslookup.h"
#include "dlgdatabaseeditor.h"

#include <QDialog>
#include <QTableWidget>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>

struct structAntennaTabs {
    QString fileName;
    QString Description;
};

class dlggeneralsettings : public QWidget
{
    Q_OBJECT

public:
    explicit dlggeneralsettings(QWidget *parent = 0);
    void selectedACPSlctFldrBtnActn();
    void selectedPENDOUTSlctFldrBtnActn();
    void selectedFont();
    void selectedChooseAddressFile();
    void selectedMapperDBCreator();
    void selectedCopyChannelCSV();
    void selectedpbSelectQSSFileActn();
    void loadQSS();
    QRadioButton * rdoACP;
    QRadioButton * rdoSMv2;
    QRadioButton * rdoAdHoc;
    QLineEdit * ACPFolderTB;
    QLineEdit * MessagesFolder;
    QLineEdit * ArchiveFolder;
    QLineEdit * SMV2FolderTB;
    QLineEdit * MARSALEDir;
    QLineEdit * ServiceCodes;
    QCheckBox * chkbxAspect;
    QCheckBox * chkbxToolsAreaisShown;
    QCheckBox * chkbxRelayLinesisShown;
    QCheckBox * chkbxClosedStationisShown;
    QCheckBox * chkbxGreyLineisShown;
    QComboBox * cmboQSSFile;
    QStringList lstQssFile;
    structAntennaTabs QssVar[100];

private slots:
    void selectedMessagesSelectFolderBtn();
    void selectedArchiveSelectFolderBtn();
    void selectedMARSALEDirBtn();

};

class dlgweathersettings : public QWidget {
    Q_OBJECT
public:
    explicit dlgweathersettings(QWidget *parent = 0);
    QCheckBox * chkbxWeatherShown;
    QCheckBox * chkbxDrawLightning;
    QCheckBox * chkbxgetCyclone;
    QCheckBox * chkbxgetAlerts;
    QCheckBox * chkbxGetLightningSummary;
    QLineEdit * leWAccessID;
    QLineEdit * leWSecretKey;
    QLabel  * lblWAccessID;
    QLabel  * lblWSecretID;
    QLineEdit * leLightningRadius;
    void gotoAeris();

};

class dlgVOACAPsettings : public QWidget {
    Q_OBJECT
public:
    explicit dlgVOACAPsettings(QWidget *parent = 0);
    QLineEdit * leMyCallSign;
    QString tmpCallSign;
    QLineEdit * leLat;
    QLineEdit * leLon;
    QLineEdit * leWorkingChan;
    QLineEdit * leAntAngle;
    QLineEdit * leTrxPower;
    QComboBox * cmboAnt;
    QStringList lstAntDesc;
    void loadVOACAPAnt();
    structAntennaTabs AntennaVar[100];
    void lookupAddress();
    classAddressLookup * addressLookup;
public slots:
    void addressLookupFinished(QNetworkReply *reply);
};

class dlgDatabaseSettings : public QWidget {
    Q_OBJECT
public:
    explicit dlgDatabaseSettings(QWidget *parent = 0);
    void selectedChooseAddressFile();
    void selectedMapperDBCreator();
    void selectedCopyChannelCSV();
    void selectedQrgAction();
    void parseAuthTable();
    QLineEdit * leAddressCSV;
    QLabel * lblAddressCSVmod;
    QLineEdit * lechannelCSV;
    QLabel * lblchannelCSVmod;

signals:
    void updateMyCall();
};

class dlgDebug : public QWidget {
    Q_OBJECT
public:
    explicit dlgDebug(QWidget *parent = 0);
    QComboBox * cmboDebugFunctions;
    QComboBox * cmboDebugRenderAreaPaintEvent;
    QComboBox * cmboDebugGeneralLevel;
    QComboBox * cmboDebugCheckLogLevel;
    QComboBox * cmboDebugclsRosterRemove;
    QComboBox * cmboDebug_clsDB;
    QComboBox * cmboDebug_clsRoster;
    QComboBox * cmboDebug_Window;
    QComboBox * cmboDebug_DrawBoxes;
    QComboBox * cmboDebugSettings;
    QComboBox * cmboDebugProp;
};

class dlgNCSsettings : public QWidget {
    Q_OBJECT
public:
    explicit dlgNCSsettings(QWidget *parent = 0);
    QLineEdit * leRoutingIndicators;
    QLineEdit * leFM;
    QLineEdit * leTO;
    QLineEdit * leINFO;
    QLineEdit * leLOC;
};

class dlgTerminalSettings : public QWidget {
    Q_OBJECT
public:
    explicit dlgTerminalSettings(QWidget *parent = 0);
    QCheckBox * chkTerminalTab;
    QLineEdit * leGuardedCallSigns;
    QLineEdit * lePositionID;
    QLineEdit * leGuardedRI;
    QCheckBox * chkIgnoreNotforMe;
    QCheckBox * chkNoDisplayACK;
    QCheckBox * chkTrackRMI;
    QCheckBox * chkAutoImportRoster;
    QCheckBox * chkSaveToDisk;
    QCheckBox * chkSaveVZCZMMM;
    QLineEdit * leReceivedFolder;

private slots:
    void selectedpbSelectReceivedFolder();

};

class dlgHighLighterSettings : public QWidget {
    Q_OBJECT
public:
    explicit dlgHighLighterSettings(QWidget *parent = 0);
    QTableWidget * tblRules;
    bool writeSettings();

private:
    void readSettings();
    int size;

private slots:
    void on_tblRules_ContextMenuRequest(const QPoint &);
    void on_NewRule();
};

class dlgSettingsTab : public QDialog
{
    Q_OBJECT

public slots:
    void saveMyCall();

public:
    explicit dlgSettingsTab(QWidget *parent = 0);
    dlggeneralsettings * ptrdlggeneralsettings;
    dlgVOACAPsettings * ptrdlgVOACAPsettings;
    dlgNCSsettings * ptrdlgNCSsettings;
    dlgTerminalSettings * ptrdlgTerminalSettings;
    dlgweathersettings * ptrdlgweathersettings;
    dlgDatabaseSettings * ptrdlgDatabaseSettings;
    dlgDebug * ptrdlgDebug;
    dlgHighLighterSettings * ptrdlgHighLighterSettings;
    dlgDatabaseEditor * ptrdlgDatabaseEditor;

private:
    QTabWidget *tabWidget;
    QDialogButtonBox *buttonBox;
    void OK();
    void Cancel();
};

#endif // DLGSETTINGSTAB_H

