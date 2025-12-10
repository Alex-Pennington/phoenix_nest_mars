#include "dbcreator.h"


#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QMessageBox>
//#include "proj_api.h"
#include "debug.h"

//using namespace GeographicLib;

bool doFlag = false;

QString CallSignDB[MaxRecordCount] = {""};
QString AbrCallSignDB[MaxRecordCount] = {""};
QString MGRSDB[MaxRecordCount] = {""};
QString LatDB[MaxRecordCount] = {""};
QString LonDB[MaxRecordCount] = {""};
QString WGSLatDB[MaxRecordCount] = {""};
QString WGSLonDB[MaxRecordCount] = {""};

int db_countDB = 0;
//int posDB = 0;

//QString AddressCSVfilenameDB = "";

void writeCSVfile() {/*
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString tmpFilename = SettingsVars.WorkingDir +  DB_Filename;
    QFile file( tmpFilename );
    if ( file.open(QIODevice::WriteOnly) )
    {
        QTextStream stream( &file );
        qDebug() << "Writing " << DB_Filename;
        for(int idx=0; idx <= (db_countDB -1 ); idx++) {
            //qDebug().noquote() << CallSignDB[idx] << "," << AbrCallSignDB[idx] <<  "," << LatDB[idx] << "," << LonDB[idx] << "," << idx;
            stream << CallSignDB[idx] << "," << AbrCallSignDB[idx]<<  "," << LatDB[idx] << "," << LonDB[idx] << "," << idx << "," << "0," << WGSLatDB[idx] << "," << WGSLonDB[idx] << endl ;
        }
    }
    file.close();*/
}

bool parseRosterCSV(){/*
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (SettingsVars.addressCSV.fileNamePath == "") {
        SettingsVars.addressCSV.fileNamePath = "C://MSC//StationManagerV2//data//address.csv";
    }

    QFile address(SettingsVars.addressCSV.fileNamePath);

    if (!address.open(QIODevice::ReadOnly)) {
        qDebug() << address.errorString() << endl ;
        qDebug() << "C://MSC//StationManagerV2//data//address.csv" << endl;
        return false;
    }

    QTextStream in(&address);                 // read to text stream

    QString FirstfileLine = in.readLine(); // Collumn Headings Discarded

    if (FirstfileLine != "CALLSIGN,ABR,,,MGR,ST") {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("File does not mach format.\r\nThe first line should be:\r\nCALLSIGN,ABR,,,MGR,ST");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret == QMessageBox::Ok) {
            return false;
        }
    }

    db_countDB = 0;

    while (!in.atEnd()) {

           // read one line from textstream(separated by "\n")
            QString fileLine = in.readLine();


            // parse the read line into separate pieces(tokens) with "," as the delimiter
            QStringList lineToken = fileLine.split(",");
            //qDebug() << lineToken.at(0) << " " << lineToken.at(1)<< " " << lineToken.at(4);
            //qDebug() << fileLine << endl;
            // load parsed data to model accordingly
            if((lineToken.size() >= 5) && (lineToken.at(4) != "") && (lineToken.at(0) != "") ) {
                CallSignDB[db_countDB] = lineToken.at(0);
                    //qDebug() << lineToken.at(0);
                AbrCallSignDB[db_countDB] = lineToken.at(1);
                    //qDebug() << lineToken.at(1);
                MGRSDB[db_countDB] = lineToken.at(4);
                    //qDebug() << lineToken.at(5);
                db_countDB++;
            }
    }

    address.close();

    qDebug() << "dbcreator.cpp 102 parseRosterCSV() DB Size " << db_countDB;
    return true;*/
}

void getMGRStoLatLon(int tmpPOS) {/*
    if (DebugFunctions > 5) {qDebug() << Q_FUNC_INFO;}
    QString Errorsfilename = SettingsVars.WorkingDir + "MapperDBCreatorErrors.txt";
    QFile Errorsfile(Errorsfilename);
    Errorsfile.open(QIODevice::Append);
    QTextStream Errorsfilestream(&Errorsfile);

    QString tempLon = "";
    QString tempLat = "";
    double lon=-86.4753;
    double lat= 31.183;
    double height=0;
    bool GeoCoordError = false;
    try {
        QString tmpMGRS = MGRSDB[tmpPOS];
        //GeoCoords c(tmpMGRS.toStdString());
//        tempLon =  QString::number(c.Longitude());
        WGSLonDB[tmpPOS] = tempLon;
        tempLat = QString::number(c.Latitude());
        WGSLatDB[tmpPOS] = tempLat;
    } catch (std::exception& e) {
    }
    try {
        QString tmpMGRS = MGRSDB[tmpPOS] + "5555555555";
//        GeoCoords c(tmpMGRS.toStdString());
//        tempLon =  QString::number(c.Longitude());
//        lon = c.Longitude();
        tempLat = QString::number(c.Latitude());
//        lat = c.Latitude();
    } catch (std::exception& e) {
        //qDebug() << "Caught exception: " << e.what();
        Errorsfilestream << CallSignDB[tmpPOS] << "," << MGRSDB[tmpPOS] << ", " << e.what() << "\r\n";
        Errorsfilestream.flush();
        GeoCoordError = true;
    }
    if (!GeoCoordError) {
        QString qsSrc = "+proj=longlat +datum=WGS84 +no_defs +to +proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m +nadgrids=@null +wktext  +no_defscs2cs +proj=longlat +datum=WGS84 +no_defs";
        QString qsDst = "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m +nadgrids=@null +wktext  +no_defs";
        //QString qsSrc = "+proj=longlat +datum=WGS84 +no_defs";
        //QString qsDst = "+proj=tmerc +lat_0=0 +lon_0=105 +k=1 +x_0=18500000 +y_0=0 +ellps=krass +units=m +no_defs  +towgs84=0,0,0,0,0,0,0";

//        projPJ src = pj_init_plus(qsSrc.toStdString().c_str());
//        projPJ dst = pj_init_plus(qsDst.toStdString().c_str());


//        lon*=DEG_TO_RAD;
//        lat*=DEG_TO_RAD;

//        int flag=pj_transform(src,dst,1,1,&lon,&lat,&height);

//        if (!flag) {
//            LatDB[tmpPOS] = QString::number(lat,'f');
//            LonDB[tmpPOS] = QString::number(lon,'f');
//        } else {
//            Errorsfilestream << CallSignDB[tmpPOS] << "," << MGRSDB[tmpPOS] << ", " << flag << "\r\n";
//            Errorsfilestream.flush();
//        }

        //qDebug() << QString::number(lon, 'f') << " " << QString::number(lat, 'f');

//        pj_free(src);
//        pj_free(dst);
    } else {
        lat = 0;
        lon = 0;
    }

    Errorsfile.close();
    return;
    */
}

void doWork(){/*
    if (DebugFunctions > 5) {qDebug() << Q_FUNC_INFO;}
    for (int i=0;i < db_countDB;i++) {
        qDebug() << "Processing " << i ;
        getMGRStoLatLon(i);
    }
    writeCSVfile();
    return;*/
}

bool importAddressCSVfile(QString tmpAddressCSVFilename) {/*
    SettingsVars.addressCSV.fileNamePath = tmpAddressCSVFilename;
    if (parseRosterCSV()) {
        QString Errorsfilename = SettingsVars.WorkingDir + "MapperDBCreatorErrors.txt";
        if (QFile::exists(Errorsfilename)) {
            QFile::remove(Errorsfilename);
        }
        for (int i=0;i < db_countDB;i++) {
            getMGRStoLatLon(i);
        }
        if (QFile::exists(SettingsVars.WorkingDir +  DB_Filename)) {
            QFile::remove(SettingsVars.WorkingDir +  DB_Filename);
        }
        writeCSVfile();
        QString fileNameSettings = QCoreApplication::applicationDirPath()+"/"+qAppName()+".ini";
        QSettings settings(fileNameSettings,QSettings::IniFormat);
        settings.setValue("addressCSV_fileNamePath", SettingsVars.addressCSV.fileNamePath);
        SettingsVars.addressCSV.lastMod = QFileInfo(SettingsVars.addressCSV.fileNamePath).lastModified();
        settings.setValue("addressCSV_lastMod", SettingsVars.addressCSV.lastMod);
        SettingsVars.MyCallSign = 9999;
        settings.setValue("MyCallSign", SettingsVars.MyCallSign);
        return true;
    } else {
        return false;
    }*/
}

bool checkMyCallsign() {
    bool ok;
    QString tmpCallSign = QInputDialog::getText(NULL,"Your Call Sign","Your Call Sign", QLineEdit::Normal,"",&ok).toUpper();
    if (ok == false) {
        qApp->quit();
    }
    if (WorkingDB.searchCS(tmpCallSign))  {
        SettingsVars.MyCallSign = WorkingDB.searchResult;
        QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
        QSettings settings(fileNameSettings,QSettings::IniFormat);
        settings.setValue("MyCallSign", SettingsVars.MyCallSign);
    }else {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("Parameter entered for Call Sign was not found in the database.  Please check your entry.");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret != QMessageBox::Ok) {
            qApp->quit();
        }
        else {
            qDebug() << "Callsign not found";
            checkMyCallsign();
        }
    }
    return true;
}

bool LoadWorkingDB() {
    WorkingDB.reset();
    QString tmpFile = SettingsVars.WorkingDir + DB_Filename;
    QFile file(tmpFile);
    bool tmpDBBadFlag = false;
    QList<int> errors = {};
    int linenumber = 0;
    if (file.open(QFile::ReadOnly)) {
        if (DebugGeneralLevel > 1) {qDebug() << DB_Filename << "Found";}
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString fileLine = in.readLine();
            /* Handle Empty File or no token exception */
            QStringList lineToken = fileLine.split(",", QString::SplitBehavior::KeepEmptyParts);
            if (lineToken.size() >= 8) {
                if ((lineToken.at(0) != "") & (lineToken.at(2) != "") & (lineToken.at(3) != "") & (lineToken.at(4) != "") & (lineToken.at(5) != "") & (lineToken.at(6) != "") & (lineToken.at(7) != "")) {
                    if (lineToken.at(1) != "") {
                        WorkingDB.add(lineToken.at(0),lineToken.at(1),lineToken.at(2), lineToken.at(3),lineToken.at(5).toInt(),lineToken.at(6),lineToken.at(7));
                    } else {
                        WorkingDB.add(lineToken.at(0),lineToken.at(0),lineToken.at(2), lineToken.at(3),lineToken.at(5).toInt(),lineToken.at(6),lineToken.at(7));
                    }
                }
                WorkingVars.isDBToolsFlag = true;
                linenumber ++;
            } else {
                tmpDBBadFlag = true;
                errors << linenumber;
            }
        }
    } else {
        qDebug() << DB_Filename <<  file.errorString();
    }
    file.close();
    if (tmpDBBadFlag == true) {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("Database corrupt or outdated.\r\nFrom the menu use:\r\nConfig->Settings->Import address.csv");
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Ok);
        int ret = msgBox.exec();
        if (ret) { return false;}
        qDebug() << errors;
    }
    qDebug() << "WorkingDB loaded: " << linenumber;
    return true;
}
