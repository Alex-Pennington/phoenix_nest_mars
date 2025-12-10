#ifndef DBCREATOR_H
#define DBCREATOR_H

#include "main.h"

#include <QString>
#include <QObject>
#include <QUrl>
#include <QDateTime>
#include <QFile>
#include <QDebug>
//#include <GeographicLib/GeoCoords.hpp>

bool parseRosterCSV();
void writeCSVfile();
void getMGRStoLatLon(int tmpPOS);
void doWork();
bool importAddressCSVfile(QString tmpAddressCSVFilename);
bool checkMyCallsign();
bool LoadWorkingDB();

#endif // DBCREATOR_H
