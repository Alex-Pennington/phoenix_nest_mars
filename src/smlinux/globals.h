#ifndef GLOBALS_H
#define GLOBALS_H

#include <QString>

struct structTermVar {
    QString GuardedCallSigns;
    QString PositionID;
    bool IgnoreNotforMe;
    bool NoDisplayACK;
    bool TrackRMI;
    bool sendToFile;
    bool dbgRcvdcount;
    bool autoImportRoster;
    QString GuardedRI;
    QString ReceivedFolder;
    bool sendVZCZMMM = false;
};

extern structTermVar settingsTermVars;

#endif // GLOBALS_H
