#include "dlgauthform.h"
#include "main.h"



dlgAuthForm::dlgAuthForm(QWidget *parent): QDialog(parent) {
    loadINI();

    mainLayout = new QGridLayout;

    menuBar = new QMenuBar;

    fileMenu = new QMenu("File");
    menuBar->addMenu(fileMenu);

    exitAction = new QAction("Exit");
    connect(exitAction,SIGNAL(triggered()),this,SLOT(close()));
    fileMenu->addAction(exitAction);


    lblChallengeSet = new QLabel("Challenge/Set");
    set = new QLineEdit;
    response = new QLineEdit;
    pbLookup = new QPushButton("Lookup");
    connect(pbLookup, SIGNAL(clicked()), this, SLOT(selectedPBlookup()));
    txtAuthTable = new QTextEdit;

    txtAuthTable->setFont(SettingsVars.NotesFont);
    txtAuthTable->setFontPointSize(SettingsVars.NotesFont.pointSize());
    txtAuthTable->update();

    mainLayout->setMenuBar(menuBar);
    mainLayout->addWidget(lblChallengeSet,0,0,1,1);
    mainLayout->addWidget(set,1,0,1,1);
    mainLayout->addWidget(pbLookup,1,1,1,1);
    mainLayout->addWidget(response,1,2,1,1);
    mainLayout->addWidget(txtAuthTable,2,0,10,10);


    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    const QByteArray geometry = settings.value("AUTHgeometry", QByteArray()).toByteArray();
    if (geometry.isEmpty()) {
        fitHeightToDocument();
    } else {
        this->restoreGeometry(geometry);
    }

    setLayout(mainLayout);
    currentMonth();
    setAttribute(Qt::WA_DeleteOnClose);
}

void dlgAuthForm::selectedPBlookup() {
    char s1 = set->text().toUpper().at(0).toLatin1();
    char s2 = set->text().toUpper().at(1).toLatin1();
    if (char_to_int(s1) <26) {
        int m = QDate::currentDate().month();
        QString r;
        QString line = table[m].line[char_to_int(s1)+1];
        qDebug() << line;
        for (int i = 0 ; i < line.size() ; i++) {
            if (line.at(i) == s2) {
                r = table[m].line[char_to_int(s1)+2].at(i);
            }
        }

        qDebug() << r << char_to_int(s1) << s1 << char_to_int(s2) << s2;
        response->setText(r);
    } else {
        response->setText("ERROR");
    }

}

void dlgAuthForm::fitHeightToDocument() {
    txtAuthTable->setMinimumHeight(410);
    txtAuthTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setMinimumWidth(290);
    this->resize(290,410);
    this->resize(sizeHint());

}

void dlgAuthForm::currentMonth() {
    int m = QDate::currentDate().month();
    QString month = "";

    switch (m) {
    case 1:
        month = "JANUARY";
        break;
    case 2:
        month = "FEBRUARY";
        break;
    case 3:
        month = "MARCH";
        break;
    case 4:
        month = "APRIL";
        break;
    case 5:
        month = "MAY";
        break;
    case 6:
        month = "JUNE";
        break;
    case 7:
        month = "JULY";
        break;
    case 8:
        month = "AUGUST";
        break;
    case 9:
        month = "SEPTEMBER";
        break;
    case 10:
        month = "OCTOBER";
        break;
    case 11:
        month = "NOVEMBER";
        break;
    case 12:
        month = "DECEMBER";
    }
    this->setWindowTitle(month);

    for (int line = 0 ; line < 28 ; line ++ ) {
        QString lineConstruct = "";
        if (line != 1) {
            lineConstruct.append(table[m].line[line].at(0)).append(' ');
            lineConstruct.append(table[m].line[line].at(1)).append(table[m].line[line].at(2)).append(table[m].line[line].at(3)).append(table[m].line[line].at(4)).append(" ");
            lineConstruct.append(table[m].line[line].at(5)).append(table[m].line[line].at(6)).append(table[m].line[line].at(7)).append(' ');
            lineConstruct.append(table[m].line[line].at(8)).append(table[m].line[line].at(9)).append(table[m].line[line].at(10)).append(' ');
            lineConstruct.append(table[m].line[line].at(11)).append(table[m].line[line].at(12)).append(' ');
            lineConstruct.append(table[m].line[line].at(13)).append(table[m].line[line].at(14)).append(' ');
            lineConstruct.append(table[m].line[line].at(15)).append(table[m].line[line].at(16)).append(table[m].line[line].at(17)).append(' ');
            lineConstruct.append(table[m].line[line].at(18)).append(table[m].line[line].at(19)).append(' ');
            lineConstruct.append(table[m].line[line].at(20)).append(table[m].line[line].at(21)).append(' ');
            lineConstruct.append(table[m].line[line].at(22)).append(table[m].line[line].at(23)).append(' ');
            lineConstruct.append(table[m].line[line].at(24)).append(table[m].line[line].at(25)).append(' ');
        } else {
            lineConstruct.append(' ').append(' ');
            lineConstruct.append('0').append(' ').append(' ').append(' ').append(" ");
            lineConstruct.append('1').append(' ').append(' ').append(' ');
            lineConstruct.append('2').append(' ').append(' ').append(' ');
            lineConstruct.append('3').append(' ').append(' ');
            lineConstruct.append('4').append(' ').append(' ');
            lineConstruct.append('5').append(' ').append(' ').append(' ');
            lineConstruct.append('6').append(' ').append(' ');
            lineConstruct.append('7').append(' ').append(' ');
            lineConstruct.append('8').append(' ').append(' ');
            lineConstruct.append('9').append(' ').append(' ');
        }
        txtAuthTable->append(lineConstruct);
    }
}

void dlgAuthForm::loadINI() {
    QString fileNameSettings = SettingsVars.WorkingDir+"/authTable.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.beginWriteArray("authTable");

    for (int m = 0 ; m <= 13 ; m++) {
        settings.setArrayIndex(m);
        settings.beginWriteArray("lines");

        for (int line = 0 ; line < 28 ; line ++ ) {
            settings.setArrayIndex(line);
            table[m].line[line] = settings.value("a").toString();
        }
        settings.endArray();
    }
    settings.endArray();
}

dlgAuthForm::~dlgAuthForm() {
    if (1==1) {qDebug() << Q_FUNC_INFO;}
    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.setValue("AUTHgeometry", this->saveGeometry());
}
