#ifndef DLGAUTHFORM_H
#define DLGAUTHFORM_H

#include <QObject>
#include <QWidget>
#include <QDialog>
#include <QSettings>
#include <QMenuBar>
#include <QMenu>
#include <QPushButton>
#include <QGridLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>

struct AuthTable {
    QString line[29];
};

class dlgAuthForm : public QDialog {
    Q_OBJECT
public:
    explicit dlgAuthForm(QWidget *parent = 0);
    ~dlgAuthForm();
    void loadINI();
    void currentMonth();

public slots:
    void fitHeightToDocument();
    void selectedPBlookup();

private:
    AuthTable table[15];
    QMenuBar *menuBar;
    QPushButton *pbLookup;
    QGridLayout *mainLayout;
    QTextEdit *txtAuthTable;
    QLabel *lblChallengeSet;
    QLineEdit *set, *response;
    int fitted_height;
    QMenu *fileMenu;
    QAction *exitAction;
    char int_to_char[27] = {'0', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
    int char_to_int(char inChar) {
        int i = 0;
        switch (inChar) {
        case 'A':
            i = 1;
            break;
        case 'B':
            i = 2;
            break;
        case 'C':
            i = 3;
            break;
        case 'D':
            i = 4;
            break;
        case 'E':
            i = 5;
            break;
        case 'F':
            i = 6;
            break;
        case 'G':
            i = 7;
            break;
        case 'H':
            i = 8;
            break;
        case 'I':
            i = 9;
            break;
        case 'J':
            i = 10;
            break;
        case 'K':
            i = 11;
            break;
        case 'L':
            i = 12;
            break;
        case 'M':
            i = 13;
            break;
        case 'N':
            i = 14;
            break;
        case 'O':
            i = 15;
            break;
        case 'P':
            i = 16;
            break;
        case 'Q':
            i = 17;
            break;
        case 'R':
            i = 18;
            break;
        case 'S':
            i = 19;
            break;
        case 'T':
            i = 20;
            break;
        case 'U':
            i = 21;
            break;
        case 'V':
            i = 22;
            break;
        case 'W':
            i = 23;
            break;
        case 'X':
            i = 24;
            break;
        case 'Y':
            i = 25;
            break;
        case 'Z':
            i = 26;
            break;
        }
        return i;
    }
};

#endif // DLGAUTHFORM_H
