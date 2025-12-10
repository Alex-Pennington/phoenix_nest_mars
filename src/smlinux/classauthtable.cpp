#include "classauthtable.h"
#include "main.h"

classAuthTable::classAuthTable()
{

}

void classAuthTable::parseTxtEntryPoint(QString filename) {
    QFile file(filename);
    file.open(QFile::ReadOnly);
    int monthIndex = 0, lineIndex = 0, fileLineCount = 0;
    while (!file.atEnd()) {

        QString fileLine = file.readLine();
        fileLineCount++;
        if (fileLine.contains("EDITION")) {
            fileLine = file.readLine();
            fileLine = file.readLine();//A - JAN
            if (1 == 1) {
                //qDebug() << "1 == 1" << fileLineCount << fileLine;
                if (fileLine.at(0)== 'A') {
                    //qDebug() << "A";
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - FEB
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }

                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - MAR
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }


                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - APR
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }


                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - MAY
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }


                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - JUN
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }


                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - JUL
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }


                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - AUG
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }


                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - SEP
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }


                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - OCT
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }


                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - NOV
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                fileLine = file.readLine();
                fileLine = file.readLine();//A - DEC
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }


                }
                do { fileLine = file.readLine(); } while (!fileLine.contains("EDITION"));
                do { fileLine = file.readLine(); } while (!fileLine.contains("A "));
                if (fileLine.at(0)== 'A') {
                    monthIndex++;
                    lineIndex = 0;
//                    for (int i = 0 ; i < 10 ; i++) {
//                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
//                        fileLine = file.readLine();
//                    }
                    lineIndex++;
//                    for (int i = 0 ; i < 10 ; i++) {
//                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
//                        fileLine = file.readLine();
//                    }
                    lineIndex++;//A
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//B
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//C
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//D
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//E
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//F
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//G
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//H
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//I
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//J
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//K
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//L
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//M
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//N
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//O
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//P
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Q
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//R
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//S
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//T
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//U
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//V
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//W
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//X
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Y
                    for (int i = 0 ; i < 10 ; i++) {
                        table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                        fileLine = file.readLine();
                    }
                    lineIndex++;//Z
                    for (int i = 0 ; i < 10 ; i++) {
                    table[monthIndex].line[lineIndex] += fileLine.remove(' ').remove("\r").remove("\n");fileLine = file.readLine();
                    fileLine = file.readLine();
                }


                }
            }
        }

    }

    QString fileNameSettings = SettingsVars.WorkingDir+"/authTable.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.beginWriteArray("authTable");

    for (int m = 0 ; m <= 13 ; m++) {
        settings.setArrayIndex(m);
        settings.beginWriteArray("lines");

        for (int line = 0 ; line < 28 ; line ++ ) {
            settings.setArrayIndex(line);
            settings.setValue("a",table[m].line[line]);
        }
        settings.endArray();
    }
    settings.endArray();
}

void classAuthTable::callPDFtoTXT() {
    QString searchList = "*.pdf";
    QString filePDF = QFileDialog::getOpenFileName(nullptr,"Select Auth Table PDF","C:/",searchList);

    if (filePDF == "") {
        //error
        return;
    }
    QProcess process;
    process.setWorkingDirectory(SettingsVars.WorkingDir);
    process.setProgram(SettingsVars.WorkingDir+"pdftotext.exe");
    process.setArguments({filePDF, "out.txt"});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (process.waitForStarted(-1)) {
        while(process.waitForReadyRead(-1)) {
            QApplication::processEvents();
        }
    }
    process.waitForFinished();
    process.close();

    if (QFile::exists(SettingsVars.WorkingDir+"out.txt")) {
        parseTxtEntryPoint(SettingsVars.WorkingDir+"out.txt");
    }

    if (QFile::exists(SettingsVars.WorkingDir+"out.txt")) {
        QFile::remove(SettingsVars.WorkingDir+"out.txt");
    }



}
