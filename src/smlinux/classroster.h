#ifndef CLASSROSTER_H
#define CLASSROSTER_H

#include <QString>
#include <QDebug>
#include <QRect>
#include <QColor>

struct sRoster
{
    QString CallSign;
    QString AbrCallSign;
    QString Lat;
    QString Lon;
    bool LocDupe;
    int isMerged;
    QRect bounds;
    QString Status;
    QColor rColor;
    int relDB;
    QString HowCopy;

};

class clsRoster {
private:
    sRoster *pa; // points to the array
    int length; // the # elements
public:
    clsRoster(); // the constructor
    ~clsRoster(); // the destructor
    sRoster& operator[](int index); // the indexing operation
    void add(QString vCallSign, QString vAbrCallSign, QString vLat, QString vLon, bool vLocDupe, int vIsMerged, int relDB); // add a new value to the end
    int size(); // return length
    bool searchCS(QString);
    int searchResult;
    void reset();
    QStringList list();
    bool searchAbrCS(QString searchTerm);
    bool searchAbrCSContains(QString searchTerm, int exclude);
    void addBounds (int index, QRect bounds);
    bool searchBounds (QPoint point);
    void updateStatus(int index, QString Status);
    QColor Color();
    void remove(int rIdx);
    int nextIndex; // the next highest index value
    void updateTable(int rIdx);
    void dupeCheck();
    void moveUp(int rIdx);
    void moveDown(int rIdx);
};


#endif // ROSTER_H
