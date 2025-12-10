#ifndef CLSDB_H
#define CLSDB_H


#include <QString>

struct sDB
{
    QString CallSign;
    QString AbrCallSign;
    QString Lat;
    QString Lon;
    QString WGSLat;
    QString WGSLon;
    int isMerged;
};

class clsDB {
private:
    sDB *pa; // points to the array
    int length; // the # elements
    int nextIndex; // the next highest index value
public:
    clsDB(); // the constructor
    ~clsDB(); // the destructor
    sDB& operator[](int index); // the indexing operation
    void add(QString vCallSign, QString vAbrCallSign, QString vLat, QString vLon, int vIsMerged, QString WGSLat, QString WGSLon); // add a new value to the end
    int size(); // return length
    bool searchCS(QString);
    int searchResult;
    void reset();
    bool searchAbrCS(QString searchTerm);
};

#endif // CLSDB_H
