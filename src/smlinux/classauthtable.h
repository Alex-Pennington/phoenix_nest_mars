#ifndef CLASSAUTHTABLE_H
#define CLASSAUTHTABLE_H
#include <QString>
#include <QFile>
#include <QDebug>

class classAuthTable
{
    struct AuthTable {
        QString line[28] = {""};
    };
    AuthTable table[15];

public:
    classAuthTable();
    void parseTxtEntryPoint(QString filename);
    void callPDFtoTXT();
};

#endif // CLASSAUTHTABLE_H
