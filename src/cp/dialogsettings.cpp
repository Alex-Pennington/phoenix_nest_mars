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
#include "globals.h"
#include "dialogsettings.h"
#include "ui_dialogsettings.h"
#include "cipherlibraryclass.h"


Settings localConfigSettings;

static const char blankString[] = QT_TRANSLATE_NOOP("MainWindow", "N/A");

void DialogSettings::moveEvent(QMoveEvent *event)
{
    localConfigSettings.settings_Dialog.pos = this->pos();
    event->accept();
}//moveEvent

void DialogSettings::resizeEvent(QResizeEvent *event)
{
    localConfigSettings.settings_Dialog.size = this->size();
    event->accept();
}//resizeEvent

void DialogSettings::setupDialogUI()
{
    this->setWindowTitle("Settings - "+globalConfigSettings.generalSettings.positionIdentifier);
    this->move(globalConfigSettings.settings_Dialog.pos);
    this->resize(globalConfigSettings.settings_Dialog.size);

    ui->comboBoxSelectDMT->setEnabled(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP");
    ui->pushButtonAutoConnectDMT->setEnabled(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP");
    ui->checkBoxShowModemDialog->setChecked(globalConfigSettings.dataComPortSettings.showModemDialog);
    ui->radioButtonALEMode->setChecked(true);
    on_radioButtonALEMode_clicked(ui->radioButtonALEMode->isChecked());

    ui->groupBoxExtendedChecksum->hide();

}//setupDialogUI

void DialogSettings::createReloadKeysTimer()
{
    reloadKeysTimer = new QTimer(this);
    connect(reloadKeysTimer, &QTimer::timeout,
            this, &DialogSettings::initializeCipher);

    //need a precise timer to ensure keys are rset at exactly midnight
    reloadKeysTimer->setTimerType(Qt::PreciseTimer);
}//createReloadKeysTimer

void DialogSettings::setReloadKeysTimerInterval()
{
    QDateTime currentDateTimeUTC = QDateTime::currentDateTimeUtc();
    int millisecsUntilMidnight = currentDateTimeUTC.time().msecsTo(QTime(23,59,59,999))+500;

    //reload keys at midnight ZULU/UTC
    reloadKeysTimer->setInterval(millisecsUntilMidnight);
}//setReloadKeysTimerInterval

void DialogSettings::initializeReloadKeysTimer()
{
    createReloadKeysTimer();

    setReloadKeysTimerInterval();

    reloadKeysTimer->start();
}//initializeReloadKeysTimer

void DialogSettings::createQSSEditorProcess()
{
    QSSEditorProcess = new QProcess(this);
    connect(QSSEditorProcess, &QProcess::readyRead,
            this, &DialogSettings::slotReloadStyleSheet);
}//createQSSEditorProcess

DialogSettings::DialogSettings(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogSettings)
{
    ui->setupUi(this);

    localConfigSettings = globalConfigSettings;
    isInitializing = true;
    setupDialogUI();


    fillPortsParameters();
    fillPortsInfo();

    initializeReloadKeysTimer();

    g_IsCipherInit = initializeCipher();

    loadQStyleSheetFolder();
    loadStyleSheet();

    updateSettings();

    createQSSEditorProcess();
    isInitializing = false;

    on_comboBoxCurrentDevice_currentTextChanged(globalConfigSettings.dataComPortSettings.currentDevice);

}//DialogSettings

DialogSettings::~DialogSettings()
{
    delete ui;
}

void DialogSettings::updateLocalSettings()
{
    localConfigSettings = globalConfigSettings;
    ui->labelRecommendedDelay->setText("Recommended delay: "+QString::number(globalConfigSettings.aleSettings.recommendedTimeout));
}//updateLocalSettings

void DialogSettings::fillSelectDMTComboBoc(QStringList availableConnections)
{
    ui->comboBoxSelectDMT->clear();
    ui->comboBoxSelectDMT->addItems(availableConnections);
}//fillSelectDMTComboBoc

void DialogSettings::setConfiguredConnection()
{
    QString currentConnection = ui->comboBoxSelectDMT->currentText();

    if(localConfigSettings.MSDMTclientSettings.positionID.isEmpty())
        ui->comboBoxSelectDMT->setCurrentText(currentConnection);

    else
        ui->comboBoxSelectDMT->setCurrentText(localConfigSettings.MSDMTclientSettings.positionID);
}//setConfiguredConnection

void DialogSettings::slotUpdateDMTConnections(QStringList availableConnections)
{
    fillSelectDMTComboBoc(availableConnections);
    setConfiguredConnection();
}//slotUpdateDMTConnections

void DialogSettings::setupForExternalCipherDevice()
{
    ui->checkBoxAllowPT->setChecked(true);
    ui->checkBoxAllowPT->setEnabled(false);
    on_checkBoxAllowPT_clicked(true);
    emit signalUpdateCriticalNotifications("Using external cipher device...");
    emit signalUpdateCriticalNotifications("Online cipher module not initialized.");
    g_IsCipherInit = false;
}//setupForExternalCipherDevice

void DialogSettings::addPLAINTEXTOption()
{
    if(!keyNameList.contains("PLAINTEXT"))
        keyNameList.append("PLAINTEXT");
}//addPLAINTEXTOption

void DialogSettings::removePLAINTEXTOption()
{
    keyNameList.removeAll("PLAINTEXT");
}//removePLAINTEXTOption

void DialogSettings::addOrRemovePLAINTEXTOption()
{
    globalConfigSettings.cipherSettings.allowPT ? addPLAINTEXTOption() : removePLAINTEXTOption();
}//checkIfPlaintextAllowed

void DialogSettings::fillDefaultCipherKeyComboBox()
{
    ui->comboBoxDefaultCipherKey->clear();
    ui->comboBoxDefaultCipherKey->addItems(keyNameList);
    ui->comboBoxDefaultCipherKey->setEnabled(globalConfigSettings.ipSettings.rawUseRMICipher);
}//fillDefaultCipherKeyComboBox

void DialogSettings::setDefaultCipherKey()
{
    QString configuredKey = globalConfigSettings.ipSettings.rawDefaultCipherKey;
    QString currentlySelectedKey = ui->comboBoxDefaultCipherKey->currentText();

    if(configuredKey.isEmpty())
        globalConfigSettings.ipSettings.rawDefaultCipherKey = currentlySelectedKey;

    else
        ui->comboBoxDefaultCipherKey->setCurrentText(configuredKey);

}//setDefaultCipherKey

void DialogSettings::displayAvailableKeys()
{
    ui->textBrowserCryptoLoadResults->append("Found keys... OK");
    foreach (QString keyName, keyNameList) {
        ui->textBrowserCryptoLoadResults->append(keyName);
    }
    QString reloadKeyTime = QDateTime::currentDateTimeUtc().addMSecs(reloadKeysTimer->interval()).toString("ddMMM-hh:mm:ss");
    ui->textBrowserCryptoLoadResults->append("Reloading keys again at - "+reloadKeyTime);
    emit signalSendStatusChange();
}//displayAvailableKeys

bool DialogSettings::initializeCipher()
{
    //useExternalCipherDevice is used in CP Mil but ignored (permenantly set as false) in CP MARS
    if(globalConfigSettings.cipherSettings.useExternalCipherDevice){
        setupForExternalCipherDevice();
        return false;
    }

    setReloadKeysTimerInterval();

    MSCCipherLibraryClass MSC_Cipher;
#ifdef CIPHER_DEBUG_ON
    if(MSC_Cipher.QT_DebugMode(1) == 1){
        ui->textBrowserCryptoLoadResults->append("Debug mode ON... OK");
    }
    else {
        ui->textBrowserCryptoLoadResults->append("Debug mode OFF... OK");
    }
#endif

    /*Cipher initialization returns:
         *  1 - init OK or already initialized
         *  0 - init failed - unspecified error
         * -1 - libcrypto dll not found
         * -2 - ../Cipher/MSCCipherKeys.db not found
         * -3 - key database hash check failed
         * -4 - no keys loaded in database
         * -5 - failed self test
         * -6 - Internet access found
         * -7 - failed self test 1 - Random Number Generator failed
         * -8 - failed self test 2 - cipher texts need to be different
         * -9 - failed self test 3 - plaintexts are not the same
         * -10 - failed self test 4 - passphrase encrypt/decrypt failed
         * -11 - failed self test 5 - decrypted plaintext did not match
         * -12 - TBD
         * */

    QString applicationDirectory = QApplication::applicationDirPath();
    int retValueInt = MSC_Cipher.QT_Init(applicationDirectory);
    switch (retValueInt) {
    case 1:
        keyNameList = MSC_Cipher.QT_GetKeys();
        addOrRemovePLAINTEXTOption();
        fillDefaultCipherKeyComboBox();
        setDefaultCipherKey();

        ui->textBrowserCryptoLoadResults->append("Cipher module initialized... OK");
        if(keyNameList.count() > 0){
            displayAvailableKeys();
            g_IsCipherInit = true;
            return true;
        }
        else {
            emit signalUpdateCriticalNotifications("MSCCipherKeys.db found: There are no keys available to use!");
            g_IsCipherInit = false;
            return false;
        }
        break;
    case 0:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -0 unspecified error!");
        break;
    case -1:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -1 libcrypto-1_1.dll (32 bit) or libcrypto-1_1-x64.dll (64 bit) not found!");
        break;
    case -2:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -2 ../Cipher/MSCCipherKeys.db not found!");
        break;
    case -3:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -3 key database hash check failed!");
        break;
    case -4:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -4 no keys found in database!");
        break;
    case -5:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -5 self check failed!");
        break;
    case -6:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -6 System is connected to Internet!");
        break;
    case -7:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -7 failed self test 1 - Random Number Generator failed!");
        break;
    case -8:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -8 cipher texts need to be different!");
        break;
    case -9:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -9 plaintexts are not the same!");
        break;
    case -10:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -10 passphrase encrypt/decrypt failed!");
        break;
    case -11:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -11 decrypted plaintext did not match!");
        break;
    case -12:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error -12 unknown error!");
        break;
    default:
        emit signalUpdateCriticalNotifications("ERROR: Cipher initialization error (default) - unknown error!");
        break;
    }//switch
    g_IsCipherInit = false;
    return false;
}//initializeCipher

void DialogSettings::on_buttonBox_accepted()
{
    g_MyOldPositionID = localConfigSettings.generalSettings.positionIdentifier;
    localConfigSettings.generalSettings.positionIdentifier = ui->lineEditPositionIdentifier->text();
    g_MyNewPositionID = localConfigSettings.generalSettings.positionIdentifier;
    globalConfigSettings = localConfigSettings;
    emit signalUpdateMainWindow();
}//on_buttonBox_accepted

void DialogSettings::on_buttonBox_rejected()
{
    isInitializing = true;
    //reset changes made to Settings Dialog UI
    localConfigSettings = globalConfigSettings;
    updateSettings();
    isInitializing = false;
}//on_buttonBox_rejected

void DialogSettings::fillIPComboBox(const QHostAddress &address)
{
    bool onlyShowIP4Address = localConfigSettings.ipSettings.onlyShowIP4;
    bool isIP4Address = !address.toString().contains(":");

    if(isIP4Address){
        ui->comboBoxListenIPAddress->addItem(address.toString());
    }
    else if(!onlyShowIP4Address){
        ui->comboBoxListenIPAddress->addItem(address.toString());
    }
}//fillIPComboBox

void DialogSettings::fillRAWIPComboBox(const QHostAddress &address)
{
    bool onlyShowIP4Address = localConfigSettings.ipSettings.rawOnlyShowIP4;
    bool isIP4Address = !address.toString().contains(":");

    if(isIP4Address){
        ui->comboBoxRawListenAddress->addItem(address.toString());
    }
    else if(!onlyShowIP4Address){
        ui->comboBoxRawListenAddress->addItem(address.toString());
    }
}//fillRAWIPComboBox

void DialogSettings::fillIPAddressComboBoxes()
{
    QString currentIPAddress = ui->comboBoxListenIPAddress->currentText();
    QString currentRAWIPAddress = ui->comboBoxRawListenAddress->currentText();

    ui->comboBoxRawListenAddress->clear();
    ui->comboBoxListenIPAddress->clear();

    QList<QHostAddress> ipAddressesList = QNetworkInterface::allAddresses();
    foreach (QHostAddress address, ipAddressesList) {
        if(!address.isLinkLocal()){
            fillIPComboBox(address);
            fillRAWIPComboBox(address);
        }
        else {
            qDebug() << "";
        }
    }//foreach address in list
    ui->comboBoxListenIPAddress->setCurrentText(currentIPAddress);
    ui->comboBoxRawListenAddress->setCurrentText(currentRAWIPAddress);
}//fillIPAddressComboBox

void DialogSettings::updateSettings()
{
    //TODO needs refactoring
    //General
    ui->checkBoxUseProgressBar->setChecked(globalConfigSettings.generalSettings.useProgressBar);
    ui->lineEditPositionIdentifier->setText(globalConfigSettings.generalSettings.positionIdentifier);
    ui->lineEditCallsign->setText(globalConfigSettings.generalSettings.callSign);
    ui->checkBoxUseRetryQueue->setChecked(globalConfigSettings.generalSettings.useRetryQueue);
    ui->checkBoxAutoTXRetryQueue->setEnabled(globalConfigSettings.generalSettings.useRetryQueue);
    ui->checkBoxAutoTXRetryQueue->setChecked(globalConfigSettings.generalSettings.autoTXRetryQueue);
    ui->checkBoxShowChatDialog->setChecked(globalConfigSettings.chat_Dialog.showChatDialog);
    ui->checkBoxConfirmExit->setChecked(globalConfigSettings.generalSettings.confirmApplicationExit);
    ui->checkBoxShowModemDialogOnError->setChecked(globalConfigSettings.modem_Dialog.displayModemOnRecvError);

    //Modem
    if(!globalConfigSettings.dataComPortSettings.portName.isEmpty()){
        ui->comboBoxModemComPort->setCurrentText(globalConfigSettings.dataComPortSettings.portName);
        if(ui->comboBoxModemComPort->currentText() != globalConfigSettings.dataComPortSettings.portName){
            globalConfigSettings.dataComPortSettings.portName = ui->comboBoxModemComPort->currentText();
            localConfigSettings.dataComPortSettings.portName = globalConfigSettings.dataComPortSettings.portName;
        }
    }
    else{
        globalConfigSettings.dataComPortSettings.portName = ui->comboBoxModemComPort->currentText();
        localConfigSettings.dataComPortSettings.portName = globalConfigSettings.dataComPortSettings.portName;
    }
    ui->comboBoxModemBaud->setCurrentText(globalConfigSettings.dataComPortSettings.stringBaudRate);
    ui->comboBoxModemData->setCurrentText(globalConfigSettings.dataComPortSettings.stringDataBits);
    ui->comboBoxModemFlow->setCurrentText(globalConfigSettings.dataComPortSettings.stringFlowControl);
    ui->comboBoxModemParity->setCurrentText(globalConfigSettings.dataComPortSettings.stringParity);
    ui->comboBoxModemStop->setCurrentText(globalConfigSettings.dataComPortSettings.stringStopBits);
    globalConfigSettings.dataComPortSettings.baudRate = static_cast<QSerialPort::BaudRate>(
        ui->comboBoxModemBaud->itemData(ui->comboBoxModemBaud->currentIndex()).toInt());
    globalConfigSettings.dataComPortSettings.dataBits = static_cast<QSerialPort::DataBits>(
        ui->comboBoxModemData->itemData(ui->comboBoxModemData->currentIndex()).toInt());
    globalConfigSettings.dataComPortSettings.flowControl = static_cast<QSerialPort::FlowControl>(
        ui->comboBoxModemFlow->itemData(ui->comboBoxModemFlow->currentIndex()).toInt());
    globalConfigSettings.dataComPortSettings.parity = static_cast<QSerialPort::Parity>(
        ui->comboBoxModemParity->itemData(ui->comboBoxModemParity->currentIndex()).toInt());
    globalConfigSettings.dataComPortSettings.stopBits = static_cast<QSerialPort::StopBits>(
        ui->comboBoxModemStop->itemData(ui->comboBoxModemStop->currentIndex()).toInt());
    ui->comboBoxModemTimeout->setCurrentText(globalConfigSettings.dataComPortSettings.serialTimeout);

    ui->checkBoxAllowPT->setChecked(globalConfigSettings.cipherSettings.allowPT);
    ui->checkBoxShowOnlyIP4->setChecked(globalConfigSettings.ipSettings.onlyShowIP4);
    ui->checkBoxProtectConfig->setChecked(globalConfigSettings.generalSettings.protectConfig);

    fillIPAddressComboBoxes();

    //MSC TCP Server Settings
    if(!globalConfigSettings.ipSettings.listenIPAddress.isEmpty()){
        ui->comboBoxListenIPAddress->setCurrentText(globalConfigSettings.ipSettings.listenIPAddress);
    }

    if(ui->comboBoxListenIPAddress->currentText() != globalConfigSettings.ipSettings.listenIPAddress){
        globalConfigSettings.ipSettings.listenIPAddress = ui->comboBoxListenIPAddress->currentText();
    }
    ui->lineEditListenPort->setText(globalConfigSettings.ipSettings.listenTCPPort);
    ui->spinBoxMaxPorts->setValue(globalConfigSettings.ipSettings.maxPorts);

    //Raw TCP Server Settings
    if(!globalConfigSettings.ipSettings.rawListenIPAddress.isEmpty()){
        ui->comboBoxRawListenAddress->setCurrentText(globalConfigSettings.ipSettings.rawListenIPAddress);
    }

    if(ui->comboBoxRawListenAddress->currentText() != globalConfigSettings.ipSettings.rawListenIPAddress){
        globalConfigSettings.ipSettings.rawListenIPAddress = ui->comboBoxRawListenAddress->currentText();
    }
    ui->lineEditRawListenPort->setText(globalConfigSettings.ipSettings.rawListenTCPPort);
    ui->checkBoxRawShowOnlyIP4->setChecked(globalConfigSettings.ipSettings.rawOnlyShowIP4);
    ui->checkBoxUseRMICipher->setChecked(globalConfigSettings.ipSettings.rawUseRMICipher);

    //TCPClient Settings
    ui->lineEditClientIPAddress->setText(globalConfigSettings.clientSettings.IPAddress);
    ui->lineEditClientTCPPort->setText(globalConfigSettings.clientSettings.TCPPort);
    ui->checkBoxUseUuencode->setChecked(globalConfigSettings.clientSettings.useUuencode);

    ui->checkBoxUseExtendedChecksum->setChecked(globalConfigSettings.cipherSettings.useExtendedChecksum);

    //Data Interface Settings
    if(!localConfigSettings.MSDMTclientSettings.positionID.isEmpty()){
        ui->pushButtonAutoConnectDMT->setText("Disconnect");
    }
    ui->comboBoxCurrentDevice->setCurrentText(globalConfigSettings.dataComPortSettings.currentDevice);
    ui->checkBoxUseRTS->setChecked(globalConfigSettings.dataComPortSettings.useRTSforPTT);
    ui->spinBoxRTSDelay->setValue(globalConfigSettings.dataComPortSettings.msDelayBeforeDroppingRTS);
    ui->checkBoxWaitForCTS->setChecked(globalConfigSettings.dataComPortSettings.waitForCTS);
    ui->spinBoxmsDelayAfterCTS->setValue(globalConfigSettings.dataComPortSettings.msDelayAfterCTS);
    ui->checkBoxWaitForDCD->setChecked(globalConfigSettings.dataComPortSettings.waitForDCD);
    ui->checkBoxUseMSDMTFile->setChecked(globalConfigSettings.MSDMTclientSettings.useMSDMTFile);

    ui->lineEditDMTFilePath->setText(globalConfigSettings.MSDMTclientSettings.DMTFilePath);
    ui->checkBoxAutoStartDMT->setChecked(globalConfigSettings.dataComPortSettings.autoStartSoftwareModem);
    if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial"){
        ui->checkBoxUseMSDMTFile->setChecked(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial");
        ui->checkBoxWaitForDCD->setChecked(true);
        ui->spinBoxmsDelayAfterCTS->setEnabled(false);
        ui->labelmsDelayAfterCTS->setEnabled(false);
    }
    else {
        ui->checkBoxUseMSDMTFile->setChecked(false);
        ui->checkBoxUseMSDMTFile->setEnabled(false);
        ui->spinBoxmsDelayAfterCTS->setEnabled(true);
        ui->labelmsDelayAfterCTS->setEnabled(false);
    }
    if(globalConfigSettings.dataComPortSettings.currentDevice == "Custom Serial"){
        ui->checkBoxUseRTS->setEnabled(true);
        ui->checkBoxUseDTR->setEnabled(true);
        ui->checkBoxWaitForCTS->setEnabled(true);
        ui->checkBoxWaitForDCD->setEnabled(true);
        ui->spinBoxmsDelayAfterCTS->setEnabled(true);
        ui->labelmsDelayAfterCTS->setEnabled(true);
    }
    ui->spinBoxDelayBeforeTX->setValue(globalConfigSettings.dataComPortSettings.delayBeforeNextTX);
    ui->spinBoxPercentRunOn->setValue(globalConfigSettings.dataComPortSettings.percentRunOn);
    ui->checkBoxResetModemRunOn->setChecked(globalConfigSettings.dataComPortSettings.resetSoftwareModem);
    ui->checkBoxResetModemRunOn->setEnabled(ui->comboBoxCurrentDevice->currentText() == "MS-DMT Serial");
    ui->checkBoxHideWindowFrame->setChecked(globalConfigSettings.generalSettings.hideWindowFrame);

    ui->checkBoxUseSystemTray->setChecked(globalConfigSettings.generalSettings.useSystemTray);

    ui->checkBoxLoadStyleSheet->setChecked(globalConfigSettings.generalSettings.loadStyleSheet);
    if(globalConfigSettings.generalSettings.loadStyleSheet){
        ui->comboBoxSelectStyleSheet->setCurrentText(QStyleSheetMap.key(globalConfigSettings.generalSettings.styleSheetFilename));
        loadStyleSheet();
    }

    //ALE
    ui->lineEditALEIP->setText(globalConfigSettings.aleSettings.ALEIPAddress);
    ui->lineEditALEPort->setText(globalConfigSettings.aleSettings.ALETCPPort);
    ui->checkBoxEnableALE->setChecked(globalConfigSettings.aleSettings.enableALE);
    ui->checkBoxStartScan->setChecked(globalConfigSettings.aleSettings.startScan);
    ui->groupBoxALEOptions->setEnabled(globalConfigSettings.aleSettings.enableALE);
    ui->lineEditaleLinkTimeOut->setText(globalConfigSettings.aleSettings.aleLinkTimeOut);
}//updateSettings

void DialogSettings::fillPortsParameters()
{
    ui->comboBoxModemBaud->addItem(QStringLiteral("75"), 75);
    ui->comboBoxModemBaud->addItem(QStringLiteral("150"), 150);
    ui->comboBoxModemBaud->addItem(QStringLiteral("300"), 300);
    ui->comboBoxModemBaud->addItem(QStringLiteral("600"), 600);
    ui->comboBoxModemBaud->addItem(QStringLiteral("1200"), QSerialPort::Baud1200);
    ui->comboBoxModemBaud->addItem(QStringLiteral("2400"), QSerialPort::Baud2400);
    ui->comboBoxModemBaud->addItem(QStringLiteral("4800"), QSerialPort::Baud4800);
    ui->comboBoxModemBaud->addItem(QStringLiteral("9600"), QSerialPort::Baud9600);
    ui->comboBoxModemBaud->addItem(QStringLiteral("19200"), QSerialPort::Baud19200);
    ui->comboBoxModemBaud->addItem(QStringLiteral("38400"), QSerialPort::Baud38400);
    ui->comboBoxModemBaud->addItem(QStringLiteral("57600"), QSerialPort::Baud57600);
    ui->comboBoxModemBaud->addItem(QStringLiteral("115200"), QSerialPort::Baud115200);

    ui->comboBoxModemData->addItem(QStringLiteral("5"), QSerialPort::Data5);
    ui->comboBoxModemData->addItem(QStringLiteral("6"), QSerialPort::Data6);
    ui->comboBoxModemData->addItem(QStringLiteral("7"), QSerialPort::Data7);
    ui->comboBoxModemData->addItem(QStringLiteral("8"), QSerialPort::Data8);
    ui->comboBoxModemData->setCurrentIndex(3);

    ui->comboBoxModemParity->addItem(tr("None"), QSerialPort::NoParity);
    ui->comboBoxModemParity->addItem(tr("Even"), QSerialPort::EvenParity);
    ui->comboBoxModemParity->addItem(tr("Odd"), QSerialPort::OddParity);
    ui->comboBoxModemParity->addItem(tr("Mark"), QSerialPort::MarkParity);
    ui->comboBoxModemParity->addItem(tr("Space"), QSerialPort::SpaceParity);

    ui->comboBoxModemStop->addItem(QStringLiteral("1"), QSerialPort::OneStop);
#ifdef Q_OS_WIN
    ui->comboBoxModemStop->addItem(tr("1.5"), QSerialPort::OneAndHalfStop);
#endif
    ui->comboBoxModemStop->addItem(QStringLiteral("2"), QSerialPort::TwoStop);

    ui->comboBoxModemFlow->addItem(tr("None"), QSerialPort::NoFlowControl);
    ui->comboBoxModemFlow->addItem(tr("RTS/CTS"), QSerialPort::HardwareControl);
    ui->comboBoxModemFlow->addItem(tr("XON/XOFF"), QSerialPort::SoftwareControl);
}//fillPortsParameters

void DialogSettings::fillPortsInfo()
{
    ui->comboBoxModemComPort->clear();
    QString description;
    QString manufacturer;
    QString serialNumber;

    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts()) {
        QStringList list;
        description = info.description();
        manufacturer = info.manufacturer();
        serialNumber = info.serialNumber();
        list << info.portName()
             << (!description.isEmpty() ? description : blankString)
             << (!manufacturer.isEmpty() ? manufacturer : blankString)
             << (!serialNumber.isEmpty() ? serialNumber : blankString)
             << info.systemLocation()
             << (info.vendorIdentifier() ? QString::number(info.vendorIdentifier(), 16) : blankString)
             << (info.productIdentifier() ? QString::number(info.productIdentifier(), 16) : blankString);

        ui->comboBoxModemComPort->addItem(list.first(), list);
    }//for each

    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts()) {
        QStringList list;
        description = info.description();
        manufacturer = info.manufacturer();
        serialNumber = info.serialNumber();
        list << info.portName()
             << (!description.isEmpty() ? description : blankString)
             << (!manufacturer.isEmpty() ? manufacturer : blankString)
             << (!serialNumber.isEmpty() ? serialNumber : blankString)
             << info.systemLocation()
             << (info.vendorIdentifier() ? QString::number(info.vendorIdentifier(), 16) : blankString)
             << (info.productIdentifier() ? QString::number(info.productIdentifier(), 16) : blankString);
    }//for each
}//fillPortsInfo

void DialogSettings::slotReloadStyleSheet()
{
    QString styleSheet = ui->comboBoxSelectStyleSheet->currentText();
    loadQStyleSheetFolder();
    ui->comboBoxSelectStyleSheet->setCurrentText(styleSheet);
    on_comboBoxSelectStyleSheet_currentTextChanged(styleSheet);
}//slotReloadStyleSheet

void DialogSettings::on_lineEditPositionIdentifier_textChanged(const QString &arg1)
{
    if(isInitializing)
        return;
    localConfigSettings.generalSettings.positionIdentifier = arg1;
    g_TCPServerChanged = true;
}//on_lineEditPositionIdentifier_textChanged

void DialogSettings::on_checkBoxUseProgressBar_clicked()
{
    localConfigSettings.generalSettings.useProgressBar = ui->checkBoxUseProgressBar->isChecked();
}//on_checkBoxUseProgressBar_clicked

void DialogSettings::on_comboBoxListenIPAddress_currentTextChanged(const QString &arg1)
{
    if(isInitializing)
        return;
    localConfigSettings.ipSettings.listenIPAddress = arg1;
    g_TCPServerChanged = true;
}//on_comboBoxListenIPList_currentTextChanged

void DialogSettings::on_lineEditListenPort_textChanged(const QString &arg1)
{
    if(isInitializing)
        return;
    localConfigSettings.ipSettings.listenTCPPort = arg1;
    g_TCPServerChanged = true;
}//on_lineEditListenPort_textChanged

void DialogSettings::on_spinBoxMaxPorts_valueChanged(int arg1)
{
    if(isInitializing)
        return;
    localConfigSettings.ipSettings.maxPorts = arg1;
}//on_spinBox_valueChanged

void DialogSettings::on_checkBoxWaitForCTS_clicked()
{
    if(isInitializing)
        return;
    localConfigSettings.dataComPortSettings.waitForCTS = ui->checkBoxWaitForCTS->isChecked();
    g_ModemSerialPortChanged = true;
}

void DialogSettings::on_checkBoxUseRTS_clicked()
{
    if(isInitializing)
        return;
    localConfigSettings.dataComPortSettings.useRTSforPTT = ui->checkBoxUseRTS->isChecked();
    g_ModemSerialPortChanged = true;
}//on_checkBoxUseRTSCTS_clicked

void DialogSettings::on_checkBoxUseDTR_clicked()
{
    if(isInitializing)
        return;
    localConfigSettings.dataComPortSettings.useDTR = ui->checkBoxUseDTR->isChecked();
    g_ModemSerialPortChanged = true;
}//on_checkBoxUseDTR_clicked

void DialogSettings::on_pushButtonAbout_clicked()
{
    QString compilerString;
#ifdef ENV32
    compilerString =  " Qt " + QString(qVersion()) + " MinGW(32) ";
#else
    compilerString =  " Qt " + QString(qVersion()) + " MinGW(64) ";
#endif
    QMessageBox messageBoxAbout(this);

    messageBoxAbout.setWindowTitle(APP_NAME);

    QString version = VERSION;
    if(version.contains("alpha") || version.contains("beta"))
    {
        version.append(" "+tr(__TIME__));
    }
    QString VersionCompileDate;
    VersionCompileDate.append(tr(__DATE__));

    messageBoxAbout.setText(tr("") + APP_NAME + "\n" +
                            tr(" - Version: ") + version +"\n" +
                            tr(" - Release date: ") + VersionCompileDate+ "\n" +
                            tr(" - Compiler: ") +compilerString+ "\n" +
                            tr(" - Running on: ") +SYSTEMINFO+ "\n" +
                            " - "+ COPYRIGHT+ "\n" +
                            " - "+ RIGHTS+ "\n" +
                            " - "+ APP_LICENSE+ "\n" +
                            tr("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n") +
                            tr("") + WARRANTY + "\n" +
                            tr("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n") +
                            tr("This product uses Qt Libraries: \n") +
                            " - "+ qt_COPYRIGHT+ "\n" +
                            tr(" - QT Library License: ") + qt_LICENSE +
                            tr("\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n") +
                            BUGREPORTS);
    messageBoxAbout.exec();
}//on_pushButtonAbout_clicked

void DialogSettings::configForMILSTDModem()
{
    g_TCPSocketChanged = false;
    g_ModemSerialPortChanged = true;
    emit signalDisconnectDMT();
    on_pushButtonAutoConnectDMT_clicked();
    //        ui->checkBoxRTSDelay->setEnabled(true);
    ui->spinBoxRTSDelay->setEnabled(true);

    ui->groupBoxDMT->setEnabled(false);
    ui->groupBoxDataComport->setEnabled(true);

    ui->checkBoxUseRTS->setChecked(true);
    ui->checkBoxUseDTR->setChecked(false);
    ui->checkBoxWaitForDCD->setChecked(false);
    ui->checkBoxWaitForDCD->setEnabled(false);
    ui->checkBoxWaitForCTS->setChecked(true);
    ui->checkBoxUseRTS->setEnabled(false);
    ui->checkBoxUseDTR->setEnabled(false);
    ui->checkBoxWaitForCTS->setEnabled(false);
    ui->spinBoxmsDelayAfterCTS->setEnabled(true);
    ui->labelmsDelayAfterCTS->setEnabled(true);
    ui->checkBoxUseMSDMTFile->setChecked(false);
    ui->checkBoxUseMSDMTFile->setEnabled(false);
    ui->checkBoxResetModemRunOn->setEnabled(false);
    ui->comboBoxModemBaud->setEnabled(true);
    ui->comboBoxModemData->setEnabled(true);
    ui->comboBoxModemParity->setEnabled(true);
    ui->comboBoxModemStop->setEnabled(true);
    ui->comboBoxModemFlow->setEnabled(false);
    on_checkBoxUseRTS_clicked();
    on_checkBoxWaitForCTS_clicked();
    on_checkBoxWaitForDCD_clicked();
}//configForMILSTDModem

void DialogSettings::configForCustomSerial()
{
    g_TCPSocketChanged = false;
    g_ModemSerialPortChanged = true;
    emit signalDisconnectDMT();
    on_pushButtonAutoConnectDMT_clicked();
    ui->spinBoxRTSDelay->setEnabled(true);
    ui->groupBoxDMT->setEnabled(false);
    ui->checkBoxUseRTS->setChecked(globalConfigSettings.dataComPortSettings.useRTSforPTT);
    ui->checkBoxUseDTR->setChecked(globalConfigSettings.dataComPortSettings.useDTR);
    ui->checkBoxWaitForDCD->setChecked(globalConfigSettings.dataComPortSettings.waitForDCD);
    ui->checkBoxWaitForCTS->setChecked(globalConfigSettings.dataComPortSettings.waitForCTS);
    ui->checkBoxUseRTS->setEnabled(true);
    ui->checkBoxUseDTR->setEnabled(true);
    ui->checkBoxWaitForDCD->setEnabled(true);
    ui->checkBoxWaitForCTS->setEnabled(true);
    ui->spinBoxmsDelayAfterCTS->setEnabled(true);
    ui->labelmsDelayAfterCTS->setEnabled(true);
    ui->checkBoxUseMSDMTFile->setChecked(false);
    ui->checkBoxUseMSDMTFile->setEnabled(false);
    ui->checkBoxResetModemRunOn->setEnabled(false);
    ui->comboBoxModemBaud->setEnabled(true);
    ui->comboBoxModemData->setEnabled(true);
    ui->comboBoxModemParity->setEnabled(true);
    ui->comboBoxModemStop->setEnabled(true);
    ui->comboBoxModemFlow->setEnabled(false);
    on_checkBoxUseRTS_clicked();
    on_checkBoxWaitForCTS_clicked();
    on_checkBoxWaitForDCD_clicked();
}//configForCustomSerial

void DialogSettings::configForDMTSerial()
{
    g_TCPSocketChanged = false;
    g_ModemSerialPortChanged = true;
    emit signalDisconnectDMT();
    on_pushButtonAutoConnectDMT_clicked();
    ui->spinBoxRTSDelay->setEnabled(false);

    ui->groupBoxDMT->setEnabled(true);
    ui->groupBoxDataComport->setEnabled(true);
    ui->groupBoxRunOnMitigator->setEnabled(false);
    ui->checkBoxUseRTS->setChecked(false);
    ui->checkBoxUseDTR->setChecked(false);
    ui->checkBoxWaitForDCD->setChecked(true);
    ui->checkBoxWaitForDCD->setEnabled(false);
    ui->checkBoxWaitForCTS->setChecked(false);
    ui->checkBoxUseRTS->setEnabled(false);
    ui->checkBoxUseDTR->setEnabled(false);
    ui->checkBoxWaitForCTS->setEnabled(false);
    ui->spinBoxmsDelayAfterCTS->setEnabled(false);
    ui->labelmsDelayAfterCTS->setEnabled(false);
    ui->checkBoxUseMSDMTFile->setChecked(true);
    on_checkBoxUseMSDMTFile_clicked();
    ui->checkBoxUseMSDMTFile->setEnabled(false);
    ui->checkBoxResetModemRunOn->setEnabled(true);
    ui->comboBoxModemBaud->setEnabled(true);
    ui->comboBoxModemData->setEnabled(true);
    ui->comboBoxModemParity->setEnabled(true);
    ui->comboBoxModemStop->setEnabled(true);
    ui->comboBoxModemFlow->setEnabled(false);
    on_checkBoxUseRTS_clicked();
    on_checkBoxWaitForCTS_clicked();
    on_checkBoxWaitForDCD_clicked();
}//configForDMTSerial

void DialogSettings::configForDMTTCP()
{
    g_TCPSocketChanged = false;
    g_ModemSerialPortChanged = false;
    g_MSDMTTCPChanged = true;
    ui->spinBoxRTSDelay->setEnabled(false);

    ui->groupBoxDMT->setEnabled(true);
    ui->groupBoxDataComport->setEnabled(false);

    ui->groupBoxRunOnMitigator->setEnabled(true);
    ui->groupBoxDataComport->setEnabled(false);
    ui->checkBoxUseRTS->setChecked(false);
    ui->checkBoxUseDTR->setChecked(false);
    ui->checkBoxWaitForDCD->setChecked(false);
    ui->checkBoxWaitForDCD->setEnabled(false);
    ui->checkBoxWaitForCTS->setChecked(false);
    ui->checkBoxUseRTS->setEnabled(false);
    ui->checkBoxUseDTR->setEnabled(false);
    ui->checkBoxWaitForCTS->setEnabled(false);
    ui->spinBoxmsDelayAfterCTS->setEnabled(false);
    ui->labelmsDelayAfterCTS->setEnabled(false);
    ui->checkBoxUseMSDMTFile->setChecked(false);
    on_checkBoxUseMSDMTFile_clicked();
    ui->checkBoxUseMSDMTFile->setEnabled(false);
    ui->checkBoxResetModemRunOn->setEnabled(true);
    ui->comboBoxModemBaud->setEnabled(false);
    ui->comboBoxModemData->setEnabled(false);
    ui->comboBoxModemParity->setEnabled(false);
    ui->comboBoxModemStop->setEnabled(false);
    ui->comboBoxModemFlow->setEnabled(false);
    on_checkBoxUseRTS_clicked();
    on_checkBoxWaitForCTS_clicked();
    on_checkBoxWaitForDCD_clicked();
}//configForDMTTCP

void DialogSettings::configForTCPSocket()
{
    g_TCPSocketChanged = true;
    g_ModemSerialPortChanged = false;
    emit signalDisconnectDMT();
    on_pushButtonAutoConnectDMT_clicked();
    ui->spinBoxRTSDelay->setEnabled(false);
    ui->groupBoxDataComport->setEnabled(false);
    ui->groupBoxTCPSocket->setEnabled(true);
    ui->checkBoxUseMSDMTFile->setChecked(false);
}//configForTCPSocket

void DialogSettings::configForBaudot()
{
    g_TCPSocketChanged = false;
    g_ModemSerialPortChanged = true;
    emit signalDisconnectDMT();
    on_pushButtonAutoConnectDMT_clicked();
    ui->spinBoxRTSDelay->setEnabled(false);
    ui->groupBoxDMT->setEnabled(false);
    ui->groupBoxDataComport->setEnabled(true);
    ui->groupBoxTCPSocket->setEnabled(false);
    ui->checkBoxUseRTS->setChecked(false);
    ui->checkBoxUseDTR->setChecked(false);
    ui->checkBoxWaitForDCD->setChecked(false);
    ui->checkBoxWaitForCTS->setChecked(false);
    ui->spinBoxmsDelayAfterCTS->setEnabled(false);
    ui->labelmsDelayAfterCTS->setEnabled(false);
    ui->checkBoxUseRTS->setEnabled(false);
    ui->checkBoxUseDTR->setEnabled(false);
    ui->checkBoxWaitForDCD->setEnabled(false);
    ui->checkBoxWaitForCTS->setEnabled(false);
    ui->checkBoxUseMSDMTFile->setChecked(false);
    ui->checkBoxUseMSDMTFile->setEnabled(false);
    ui->checkBoxResetModemRunOn->setEnabled(false);
    ui->comboBoxModemBaud->setCurrentText("9600");
    ui->comboBoxModemBaud->setEnabled(false);
    ui->comboBoxModemData->setCurrentText("8");
    ui->comboBoxModemData->setEnabled(false);
    ui->comboBoxModemParity->setCurrentText("None");
    ui->comboBoxModemParity->setEnabled(false);
    ui->comboBoxModemStop->setCurrentText("1");
    ui->comboBoxModemStop->setEnabled(false);
    ui->comboBoxModemFlow->setCurrentText("None");
    ui->comboBoxModemFlow->setEnabled(false);
    on_checkBoxUseRTS_clicked();
    on_checkBoxWaitForCTS_clicked();
    on_checkBoxWaitForDCD_clicked();
}//configForBaudot

void DialogSettings::on_comboBoxCurrentDevice_currentTextChanged(const QString &arg1)
{
    if(isInitializing)
        return;

    localConfigSettings.dataComPortSettings.currentDevice = arg1;
    ui->groupBoxDataComport->setEnabled(true);
    ui->groupBoxTCPSocket->setEnabled(false);

    if (localConfigSettings.dataComPortSettings.currentDevice == "MIL-STD Modem") {
        configForMILSTDModem();
    }
    else if (localConfigSettings.dataComPortSettings.currentDevice == "Custom Serial") {
        configForCustomSerial();
    }
    else if (localConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial") {
        configForDMTSerial();
    }
    else if (localConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP") {
        configForDMTTCP();
    }
    else if (localConfigSettings.dataComPortSettings.currentDevice == "TCPSocket") {
        configForTCPSocket();
    }
    else if (localConfigSettings.dataComPortSettings.currentDevice == "Baudot TNC") {
        configForBaudot();
    }

    ui->comboBoxSelectDMT->setEnabled(localConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP");
    ui->pushButtonAutoConnectDMT->setEnabled(localConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP");
}//on_comboBox_currentTextChanged

void DialogSettings::on_spinBoxDelayBeforeTX_valueChanged(int arg1)
{
    localConfigSettings.dataComPortSettings.delayBeforeNextTX = arg1;
}//on_spinBoxDelayBeforeTX_valueChanged

void DialogSettings::on_checkBoxWaitForDCD_clicked()
{
    localConfigSettings.dataComPortSettings.waitForDCD = ui->checkBoxWaitForDCD->isChecked();
}//on_checkBoxWaitForDCD_clicked

void DialogSettings::on_checkBoxUseMSDMTFile_clicked()
{
    localConfigSettings.MSDMTclientSettings.useMSDMTFile = ui->checkBoxUseMSDMTFile->isChecked();
}//on_checkBoxUseMSDMTFile_clicked

void DialogSettings::on_checkBoxResetModemRunOn_clicked()
{
    localConfigSettings.dataComPortSettings.resetSoftwareModem = ui->checkBoxResetModemRunOn->isChecked();
}//on_checkBoxResetModemRunOn_clicked

void DialogSettings::on_spinBoxPercentRunOn_valueChanged(int arg1)
{
    localConfigSettings.dataComPortSettings.percentRunOn = arg1;
}//on_spinBoxPercentRunOn_valueChanged

void DialogSettings::on_pushButtonBroadcast_clicked()
{
    emit signalBroadcastID("update");
}//on_pushButtonBroadcast_clicked

void DialogSettings::on_checkBoxUseExtendedChecksum_clicked()
{
    localConfigSettings.cipherSettings.useExtendedChecksum = ui->checkBoxUseExtendedChecksum->isChecked();
}//on_checkBoxUseExtendedChecksum_clicked

void DialogSettings::on_checkBoxHideWindowFrame_stateChanged(int arg1)
{
    localConfigSettings.generalSettings.hideWindowFrame = (bool) arg1;
}//on_checkBoxHideWindowFrame_stateChanged

void DialogSettings::on_checkBoxProtectConfig_clicked(bool checked)
{
    localConfigSettings.generalSettings.protectConfig = checked;
}//on_checkBoxProtectConfig_clicked

void DialogSettings::on_lineEditConfigPassphrase_textChanged(const QString &arg1)
{
    localConfigSettings.generalSettings.hashCheck = QCryptographicHash::hash(arg1.toLatin1(),QCryptographicHash::Sha256);
    ui->checkBoxProtectConfig->setChecked(!arg1.isEmpty());
    localConfigSettings.generalSettings.protectConfig = !arg1.isEmpty();
}//on_lineEditConfigPassphrase_textChanged

void DialogSettings::on_checkBoxAutoTXRetryQueue_clicked(bool checked)
{
    localConfigSettings.generalSettings.autoTXRetryQueue=checked;
}//on_checkBoxAutoTXRetryQueue_clicked

void DialogSettings::on_comboBoxModemComPort_currentTextChanged(const QString &arg1)
{
    if(isInitializing)
        return;
    g_ModemSerialPortChanged = true;
    localConfigSettings.dataComPortSettings.portName = arg1;
}//on_comboBoxModemComPort_currentTextChanged

void DialogSettings::on_comboBoxModemBaud_currentTextChanged(const QString &arg1)
{
    if(isInitializing)
        return;
    g_ModemSerialPortChanged = true;
    localConfigSettings.dataComPortSettings.baudRate = static_cast<QSerialPort::BaudRate>(
        ui->comboBoxModemBaud->itemData(ui->comboBoxModemBaud->currentIndex()).toInt());
    localConfigSettings.dataComPortSettings.stringBaudRate = arg1;
}//on_comboBoxModemBaud_currentTextChanged

void DialogSettings::on_comboBoxModemData_currentTextChanged(const QString &arg1)
{
    if(isInitializing)
        return;
    g_ModemSerialPortChanged = true;
    localConfigSettings.dataComPortSettings.dataBits = static_cast<QSerialPort::DataBits>(
        ui->comboBoxModemData->itemData(ui->comboBoxModemData->currentIndex()).toInt());
    localConfigSettings.dataComPortSettings.stringDataBits = arg1;
}//on_comboBoxModemData_currentTextChanged

void DialogSettings::on_comboBoxModemParity_currentTextChanged(const QString &arg1)
{
    if(isInitializing)
        return;
    g_ModemSerialPortChanged = true;
    localConfigSettings.dataComPortSettings.parity = static_cast<QSerialPort::Parity>(
        ui->comboBoxModemParity->itemData(ui->comboBoxModemParity->currentIndex()).toInt());
    localConfigSettings.dataComPortSettings.stringParity = arg1;
}//on_comboBoxModemParity_currentTextChanged

void DialogSettings::on_comboBoxModemStop_currentTextChanged(const QString &arg1)
{
    if(isInitializing)
        return;
    g_ModemSerialPortChanged = true;
    localConfigSettings.dataComPortSettings.stopBits = static_cast<QSerialPort::StopBits>(
        ui->comboBoxModemStop->itemData(ui->comboBoxModemStop->currentIndex()).toInt());
    localConfigSettings.dataComPortSettings.stringStopBits = arg1;
}//on_comboBoxModemStop_currentTextChanged

void DialogSettings::on_comboBoxModemTimeout_currentTextChanged(const QString &arg1)
{
    localConfigSettings.dataComPortSettings.serialTimeout = arg1;
}//on_comboBoxModemTimeout_currentTextChanged

void DialogSettings::on_pushButtonUserManual_clicked()
{
    QString fileName = "/UserManual.pdf";
    QFile lgplv3(":"+fileName);
    QFile oldlicense(qApp->applicationDirPath().append(fileName));

    if(oldlicense.exists()){
        qDebug() << "Removed Read Only Flag - " << oldlicense.setPermissions(QFile::ReadOther | QFile::WriteOther);
        if(oldlicense.remove())
            qDebug() << "Removed old license file...OK";
        else
            qDebug() << "ERROR - Can not remove license file - " << oldlicense.errorString();
    }
    else
    {
        qDebug() << "User Manual file not found...";
    }

    if(!lgplv3.copy(qApp->applicationDirPath().append(fileName))){
        qDebug() << qApp->applicationDirPath().append(fileName);
        qDebug() << "Error copying License Manual! " << lgplv3.errorString();
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(qApp->applicationDirPath().append(fileName)));
}//on_pushButtonUserManual_clicked

void DialogSettings::on_pushButtonAppLicense_clicked()
{
    QString fileName = "/MSCSuite.pdf";
    QFile lgplv3(":"+fileName);
    QFile oldlicense(qApp->applicationDirPath().append(fileName));

    if(oldlicense.exists()){
        qDebug() << "Removed Read Only Flag - " << oldlicense.setPermissions(QFile::ReadOther | QFile::WriteOther);
        if(oldlicense.remove())
            qDebug() << "Removed old license file...OK";
        else
            qDebug() << "ERROR - Can not remove license file - " << oldlicense.errorString();
    }
    else
    {
        qDebug() << "Old license file not found...";
    }

    if(!lgplv3.copy(qApp->applicationDirPath().append(fileName))){
        qDebug() << qApp->applicationDirPath().append(fileName);
        qDebug() << "Error copying License Manual! " << lgplv3.errorString();
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(qApp->applicationDirPath().append(fileName)));
}//on_pushButtonAppLicense_clicked

void DialogSettings::on_pushButtonAboutQt_clicked()
{
    qApp->aboutQt();
}//on_pushButtonAboutQt_clicked

void DialogSettings::on_pushButtonLGPLv3_clicked()
{
    QString fileName = "/LGPLv3.pdf";
    QFile lgplv3(":"+fileName);
    QFile oldlicense(qApp->applicationDirPath().append(fileName));

    if(oldlicense.exists()){
        qDebug() << "Removed Read Only Flag - " << oldlicense.setPermissions(QFile::ReadOther | QFile::WriteOther);
        if(oldlicense.remove())
            qDebug() << "Removed old license file...OK";
        else
            qDebug() << "ERROR - Can not remove license file - " << oldlicense.errorString();
    }
    else
    {
        qDebug() << "Old license file not found...";
    }

    if(!lgplv3.copy(qApp->applicationDirPath().append(fileName))){
        qDebug() << qApp->applicationDirPath().append(fileName);
        qDebug() << "Error copying License Manual! " << lgplv3.errorString();
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(qApp->applicationDirPath().append(fileName)));
}//on_pushButtonLGPLv3_clicked

void DialogSettings::on_lineEditCallsign_textChanged(const QString &arg1)
{
    localConfigSettings.generalSettings.callSign = arg1;
}//on_lineEditCallsign_textChanged

void DialogSettings::on_checkBoxUseSystemTray_clicked(bool checked)
{
    localConfigSettings.generalSettings.useSystemTray = checked;
}//on_checkBoxUseSystemTray_clicked

void DialogSettings::on_checkBoxLoadStyleSheet_clicked(bool checked)
{
    localConfigSettings.generalSettings.loadStyleSheet = checked;
}//on_checkBoxLoadStyleSheet_clicked

void DialogSettings::loadStyleSheet()
{
    QFile styleSheetFile;
    styleSheetFile.setFileName(localConfigSettings.generalSettings.styleSheetFilename);
    if(!styleSheetFile.exists()){
        qApp->setStyleSheet("");
        ui->checkBoxLoadStyleSheet->setChecked(false);
        loadDefaultStyleSheet();
        return;
    }
    styleSheetFile.open(QFile::ReadOnly);
    QString styleSheetString = QLatin1String(styleSheetFile.readAll());
    styleSheetFile.close();
    localConfigSettings.generalSettings.styleSheetFilename = styleSheetFile.fileName();
    QTimer::singleShot(0, [=] {
        qApp->setStyleSheet(styleSheetString);
    });//
}//on_pushButtonLoadStyleSheet_clicked

void DialogSettings::loadDefaultStyleSheet()
{
    QFile defaultQSS(":/default.qss");
    if(defaultQSS.exists()){
        defaultQSS.open(QFile::ReadOnly);
        QString styleSheetString(QLatin1String(defaultQSS.readAll()));
        qApp->setStyleSheet(styleSheetString);
        defaultQSS.close();
    }
}//loadDefaultStyleSheet

void DialogSettings::on_checkBoxUseRetryQueue_clicked(bool checked)
{
    localConfigSettings.generalSettings.useRetryQueue = checked;
    ui->checkBoxAutoTXRetryQueue->setEnabled(checked);
}//on_checkBox_clicked

void DialogSettings::on_lineEditClientTCPPort_textChanged(const QString &arg1)
{
    localConfigSettings.clientSettings.TCPPort = arg1;

    if(!isInitializing)
        g_TCPSocketChanged = true;
}//on_lineEditClientTCPPort_textChanged

void DialogSettings::on_checkBoxUseUuencode_clicked(bool checked)
{
    localConfigSettings.clientSettings.useUuencode = checked;
}//on_checkBoxUseUuencode_clicked

void DialogSettings::on_checkBoxEnableALE_clicked(bool checked)
{
    localConfigSettings.aleSettings.enableALE = checked;
    ui->groupBoxALEOptions->setEnabled(checked);
    g_ALEChanged = true;
}//on_checkBoxEnableALE_clicked

void DialogSettings::on_lineEditALEIP_textChanged(const QString &arg1)
{
    localConfigSettings.aleSettings.ALEIPAddress = arg1;
    g_ALEChanged = true;
}//on_lineEditALEIP_textChanged

void DialogSettings::on_lineEditALEPort_textChanged(const QString &arg1)
{
    localConfigSettings.aleSettings.ALETCPPort = arg1;
    g_ALEChanged = true;
}//on_lineEditALEPort_textChanged

void DialogSettings::on_checkBoxStartScan_clicked(bool checked)
{
    localConfigSettings.aleSettings.startScan = checked;
}//on_checkBoxStartScan_clicked

void DialogSettings::on_lineEditaleLinkTimeOut_textChanged(const QString &arg1)
{
    localConfigSettings.aleSettings.aleLinkTimeOut = arg1;
    g_ALEChanged = true;
}//on_lineEditaleLinkTimeOut_textChanged

void DialogSettings::on_radioButtonALEMode_clicked(bool checked)
{
    if(checked)
        localConfigSettings.aleSettings.opMode = "ALE";

}//on_radioButtonALEMode_clicked

void DialogSettings::on_radioButtonSingleChannelMode_clicked(bool checked)
{
    if(checked)
        localConfigSettings.aleSettings.opMode = "CHANNEL";

}//on_radioButtonSingleChannelMode_clicked

void DialogSettings::on_checkBoxShowOnlyIP4_clicked(bool checked)
{
    if(!localConfigSettings.ipSettings.listenIPAddress.isEmpty())
        ui->comboBoxListenIPAddress->setCurrentText(localConfigSettings.ipSettings.listenIPAddress);

    localConfigSettings.ipSettings.onlyShowIP4 = checked;
    fillIPAddressComboBoxes();
}//on_checkBoxShowOnlyIP4_clicked

void DialogSettings::on_checkBoxRawShowOnlyIP4_clicked(bool checked)
{
    if(isInitializing)
        return;

    localConfigSettings.ipSettings.rawOnlyShowIP4 = checked;
    fillIPAddressComboBoxes();
}//on_checkBoxRawShowOnlyIP4_clicked

void DialogSettings::on_comboBoxRawListenAddress_currentIndexChanged(const QString &arg1)
{
    if(isInitializing)
        return;

    localConfigSettings.ipSettings.rawListenIPAddress = arg1;
    g_TCPServerChanged = true;
}//on_comboBoxRawListenAddress_currentIndexChanged

void DialogSettings::on_lineEditRawListenPort_textChanged(const QString &arg1)
{
    if(isInitializing)
        return;

    localConfigSettings.ipSettings.rawListenTCPPort = arg1;
    g_TCPServerChanged = true;
}//on_lineEditRawListenPort_textChanged

void DialogSettings::on_checkBoxUseRMICipher_clicked(bool checked)
{
    if(isInitializing)
        return;

    localConfigSettings.ipSettings.rawUseRMICipher = checked;
    ui->comboBoxDefaultCipherKey->setEnabled(checked);
    g_TCPServerChanged = true;
}//on_checkBoxDoNotUseRMICipher_clicked

void DialogSettings::on_checkBoxAllowPT_clicked(bool checked)
{
    localConfigSettings.cipherSettings.allowPT = checked;
    if(localConfigSettings.cipherSettings.allowPT){
        if(!keyNameList.contains("PLAINTEXT"))
            keyNameList.append("PLAINTEXT");

    }
    else {
        if(keyNameList.contains("PLAINTEXT"))
            keyNameList.removeAll("PLAINTEXT");

    }
    emit signalSendStatusChange();
}//on_checkBoxAllowPT_clicked

void DialogSettings::on_comboBoxDefaultCipherKey_currentIndexChanged(const QString &arg1)
{
    localConfigSettings.ipSettings.rawDefaultCipherKey = arg1;
}//on_comboBoxDefaultCipherKey_currentIndexChanged

void DialogSettings::loadQStyleSheetFolder()
{
    QString QStyleSheetFolder = QApplication::applicationDirPath()+"/../QStyleSheets";
    QDirIterator fileIterator(QStyleSheetFolder, QStringList() << "*.qss", QDir::Files, QDirIterator::Subdirectories);

    while (fileIterator.hasNext()) {
        QFile styleSheetFile(fileIterator.next());
        QFileInfo fileInfo(styleSheetFile);
        QStyleSheetMap.insert(fileInfo.baseName(),styleSheetFile.fileName());
    }

    ui->comboBoxSelectStyleSheet->clear();
    ui->comboBoxSelectStyleSheet->addItem("");
    ui->comboBoxSelectStyleSheet->addItems(QStyleSheetMap.keys());
}//loadQStyleSheetFolder


void DialogSettings::on_comboBoxSelectStyleSheet_currentTextChanged(const QString &arg1)
{
    if(isInitializing)
        return;

    localConfigSettings.generalSettings.styleSheetFilename = QStyleSheetMap.value(arg1);
    loadStyleSheet();
}//on_comboBoxSelectStyleSheet_currentTextChanged

void DialogSettings::on_spinBoxmsDelayAfterCTS_valueChanged(int arg1)
{
    localConfigSettings.dataComPortSettings.msDelayAfterCTS =  arg1;
}//on_spinBoxmsDelayBeforeCTS_valueChanged

void DialogSettings::on_checkBoxAutoStartDMT_clicked(bool checked)
{
    if(isInitializing)
        return;

    localConfigSettings.dataComPortSettings.autoStartSoftwareModem = checked;
}//on_checkBoxAutoStartDMT_clicked

void DialogSettings::on_pushButtonLocateDMT_clicked()
{
    QFile DMTFile;
    QString lastFilePath;
    if(!globalConfigSettings.MSDMTclientSettings.DMTFilePath.isEmpty()){
        qDebug() << globalConfigSettings.MSDMTclientSettings.DMTFilePath;
        DMTFile.setFileName(globalConfigSettings.MSDMTclientSettings.DMTFilePath);
        QFileInfo fileInfo(DMTFile);
        lastFilePath = fileInfo.path();
    }
    DMTFile.setFileName(QFileDialog::getOpenFileName(this,tr("Locate File"), lastFilePath, tr("Exe Files (*.exe)")));

    if(!DMTFile.exists())
        return;

    ui->lineEditDMTFilePath->setText(DMTFile.fileName());
    localConfigSettings.MSDMTclientSettings.DMTFilePath = DMTFile.fileName();
}//on_pushButtonLocateDMT_clicked

void DialogSettings::on_checkBoxShowModemDialog_clicked(bool checked)
{
    localConfigSettings.dataComPortSettings.showModemDialog = checked;
}//on_checkBoxShowModemDialog_clicked

void DialogSettings::on_pushButtonAutoConnectDMT_clicked()
{
    if(localConfigSettings.MSDMTclientSettings.positionID.isEmpty()){
        if(ui->comboBoxSelectDMT->currentText().isEmpty())
            return;

        localConfigSettings.MSDMTclientSettings.positionID = ui->comboBoxSelectDMT->currentText();
        ui->pushButtonAutoConnectDMT->setText("Disconnect");
        emit signalConnectDMT(localConfigSettings.MSDMTclientSettings.positionID);
    }

    else {
        localConfigSettings.MSDMTclientSettings.positionID.clear();
        ui->pushButtonAutoConnectDMT->setText("Connect");
        emit signalDisconnectDMT();
    }
}//on_pushButtonAutoConnectDMT_clicked

void DialogSettings::on_spinBoxRTSDelay_valueChanged(int arg1)
{
    if(isInitializing)
        return;

    localConfigSettings.dataComPortSettings.msDelayBeforeDroppingRTS = arg1;
}//on_spinBoxRTSDelay_valueChanged


void DialogSettings::on_pushButtonEditStyleSheet_clicked()
{
    QString applicationString = "../StyleSheetEditor/QSSEditor32.exe";
    QStringList argumentStringList;
    argumentStringList.append(ui->comboBoxSelectStyleSheet->currentText());

    QSSEditorProcess->start(applicationString,argumentStringList);
}//on_pushButtonEditStyleSheet_clicked

void DialogSettings::on_checkBoxShowChatDialog_clicked(bool checked)
{
    if(isInitializing)
        return;

    localConfigSettings.chat_Dialog.showChatDialog = checked;
}

void DialogSettings::on_checkBoxConfirmExit_clicked(bool checked)
{
    if(isInitializing)
        return;

    localConfigSettings.generalSettings.confirmApplicationExit = checked;
}//on_checkBoxConfirmExit_clicked

void DialogSettings::on_checkBoxShowModemDialogOnError_clicked(bool checked)
{
    if(isInitializing)
        return;

    localConfigSettings.modem_Dialog.displayModemOnRecvError = checked;
}

void DialogSettings::on_pushButtonKeyloader_clicked()
{
    if(keyLoaderApp == NULL)
        keyLoaderApp = new KeyLoader(this);

    keyLoaderApp->show();
}

