/****************************************************************************
**
** Copyright (C) 2016 - 2024 Timothy Millea <timothy.j.millea@gmail.com>
**
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************************/

#ifndef DIALOGSETTINGS_H
#define DIALOGSETTINGS_H

#include <QDebug>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QDialog>
#include <QDir>
#include <QLibrary>
#include <QNetworkInterface>
#include <QDateTime>
#include <QDirIterator>
#include <QProcess>
#include <QColorDialog>

#include "keyloader.h"

namespace Ui {
class DialogSettings;
}

class DialogSettings : public QDialog
{
    Q_OBJECT

protected:
    virtual void moveEvent(QMoveEvent *event);
    virtual void resizeEvent(QResizeEvent *event);

public:
    explicit DialogSettings(QWidget *parent = nullptr);
    bool initializeCipher();

    ~DialogSettings();

    void updateLocalSettings();

    void configForMILSTDModem();

signals:
    void signalUpdateMainWindow();
    void signalBroadcastID(QByteArray type);
    void signalSendStatusChange();
    void signalUpdateStyleSheet(QString styleSheet);
    void signalUpdateCriticalNotifications(QString data);
    void signalConnectDMT(QString dmtConnection);
    void signalDisconnectDMT();

public slots:
    void slotUpdateDMTConnections(QStringList availableConnections);

private slots:
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void on_lineEditPositionIdentifier_textChanged(const QString &arg1);
    void on_lineEditListenPort_textChanged(const QString &arg1);
    void on_lineEditConfigPassphrase_textChanged(const QString &arg1);
    void on_lineEditCallsign_textChanged(const QString &arg1);
    void on_lineEditClientTCPPort_textChanged(const QString &arg1);
    void on_lineEditALEIP_textChanged(const QString &arg1);
    void on_lineEditALEPort_textChanged(const QString &arg1);
    void on_lineEditaleLinkTimeOut_textChanged(const QString &arg1);
    void on_lineEditRawListenPort_textChanged(const QString &arg1);
    void on_checkBoxUseProgressBar_clicked();
    void on_checkBoxWaitForCTS_clicked();
    void on_checkBoxUseRTS_clicked();
    void on_checkBoxUseDTR_clicked();
    void on_checkBoxWaitForDCD_clicked();
    void on_checkBoxUseMSDMTFile_clicked();
//    void on_checkBoxShowOnlyIP4_clicked();
    void on_checkBoxResetModemRunOn_clicked();
    void on_checkBoxUseExtendedChecksum_clicked();
    void on_checkBoxHideWindowFrame_stateChanged(int arg1);
    void on_checkBoxProtectConfig_clicked(bool checked);
    void on_checkBoxAutoTXRetryQueue_clicked(bool checked);
//    void on_checkBoxPingMSDMT_clicked(bool checked);
    void on_checkBoxUseSystemTray_clicked(bool checked);
    void on_checkBoxLoadStyleSheet_clicked(bool checked);
    void on_checkBoxUseRetryQueue_clicked(bool checked);
    void on_checkBoxUseUuencode_clicked(bool checked);
    void on_checkBoxEnableALE_clicked(bool checked);
    void on_checkBoxStartScan_clicked(bool checked);
    void on_checkBoxRawShowOnlyIP4_clicked(bool checked);
    void on_checkBoxUseRMICipher_clicked(bool checked);
    void on_checkBoxAllowPT_clicked(bool checked);
    void on_checkBoxAutoStartDMT_clicked(bool checked);
    void on_comboBoxModemBaud_currentTextChanged(const QString &arg1);
    void on_comboBoxModemComPort_currentTextChanged(const QString &arg1);
    void on_comboBoxListenIPAddress_currentTextChanged(const QString &arg1);
    void on_comboBoxCurrentDevice_currentTextChanged(const QString &arg1);
    void on_comboBoxModemData_currentTextChanged(const QString &arg1);
    void on_comboBoxModemParity_currentTextChanged(const QString &arg1);
    void on_comboBoxModemStop_currentTextChanged(const QString &arg1);
    void on_comboBoxModemTimeout_currentTextChanged(const QString &arg1);
    void on_comboBoxRawListenAddress_currentIndexChanged(const QString &arg1);
    void on_comboBoxDefaultCipherKey_currentIndexChanged(const QString &arg1);
    void on_comboBoxSelectStyleSheet_currentTextChanged(const QString &arg1);
    void on_spinBoxMaxPorts_valueChanged(int arg1);
    void on_spinBoxDelayBeforeTX_valueChanged(int arg1);
    void on_spinBoxPercentRunOn_valueChanged(int arg1);
    void on_spinBoxmsDelayAfterCTS_valueChanged(int arg1);
    void on_pushButtonAbout_clicked();
    void on_pushButtonBroadcast_clicked();
    void on_pushButtonUserManual_clicked();
    void on_pushButtonAppLicense_clicked();
    void on_pushButtonAboutQt_clicked();
    void on_pushButtonLGPLv3_clicked();
    void on_pushButtonLocateDMT_clicked();
    void on_radioButtonALEMode_clicked(bool checked);
    void on_radioButtonSingleChannelMode_clicked(bool checked);
    void on_checkBoxShowModemDialog_clicked(bool checked);
    void on_pushButtonAutoConnectDMT_clicked();
//    void on_checkBoxRTSDelay_clicked(bool checked);
    void on_spinBoxRTSDelay_valueChanged(int arg1);
    void on_pushButtonEditStyleSheet_clicked();

//    void on_spinBoxMSDMTSocketLatency_valueChanged(int arg1);

    void on_checkBoxShowChatDialog_clicked(bool checked);

    void on_checkBoxConfirmExit_clicked(bool checked);

    void on_checkBoxShowOnlyIP4_clicked(bool checked);

    void on_checkBoxShowModemDialogOnError_clicked(bool checked);

    void on_pushButtonKeyloader_clicked();

private:
    Ui::DialogSettings *ui;

    KeyLoader *keyLoaderApp = nullptr;

    void updateSettings();
    void fillPortsParameters();
    void fillPortsInfo();

    //cipher stuff
//    bool initializeCipher();

    bool isInitializing;

    QTimer *reloadKeysTimer;
    QProcess *QSSEditorProcess;
    void slotReloadStyleSheet();
    void loadQStyleSheetFolder();
    QMap<QString, QString> QStyleSheetMap;
    void loadDefaultStyleSheet();
    void loadStyleSheet();
    void setupDialogUI();
    void initializeReloadKeysTimer();
    void createReloadKeysTimer();
    void setReloadKeysTimerInterval();
    void createQSSEditorProcess();
    void fillSelectDMTComboBoc(QStringList availableConnections);
    void setConfiguredConnection();
    void setupForExternalCipherDevice();
    void addOrRemovePLAINTEXTOption();
    void addPLAINTEXTOption();
    void removePLAINTEXTOption();
    void fillDefaultCipherKeyComboBox();
    void setDefaultCipherKey();
    void displayAvailableKeys();
    void fillIPAddressComboBoxes();
    void fillIPComboBox(const QHostAddress &address);
    void fillRAWIPComboBox(const QHostAddress &address);
    void configForCustomSerial();
    void configForDMTSerial();
    void configForDMTTCP();
    void configForTCPSocket();
    void configForBaudot();
};

#endif // DIALOGSETTINGS_H
