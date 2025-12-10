#ifndef CLASSFILES_H
#define CLASSFILES_H

#include <QString>
#include <QDebug>

struct FileVersions
{
    QString FileName;
    QString Version;

};

class Dynarray {
private:
    FileVersions *pa; // points to the array
    int length; // the # elements
    int nextIndex; // the next highest index value
public:
    Dynarray(); // the constructor
    ~Dynarray(); // the destructor
    FileVersions& operator[](int index); // the indexing operation
    void add(QString, QString); // add a new value to the end
    int size(); // return length
};

#endif // FILES_CLASS_H
