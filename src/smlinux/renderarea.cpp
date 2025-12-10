#include "main.h"
#include "renderarea.h"
//#include "classsolar.h"
#include "debug.h"
#include "heatmap.h"

#include <QPainter>
#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QComboBox>

//Variables

point p;
point prev;
heatmap varHeatMap;

RenderArea::RenderArea(QWidget *parent)
    : QWidget(parent)
{

    shape = Polygon;
    antialiased = true;
    transformed = false;
    pixmap.load(":/images/qt-logo.png");

    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

}

QSize RenderArea::minimumSizeHint() const {
    if (DebugFunctions > 3) {qDebug() << Q_FUNC_INFO;}
    return QSize(100,100);
}

QSize RenderArea::sizeHint() const {
    if (DebugFunctions > 3) {qDebug() << Q_FUNC_INFO;}
    return QSize(MAP_WIDTH, MAP_HEIGHT);
//    return QSize(100,100);
}

void RenderArea::CheckLog() {
    if ((DebugFunctions > 0) || (DebugCheckLogLevel > 1)) {qDebug() << Q_FUNC_INFO;}
    RosterVar.reset();
    int LineNumber = 0;
    if (SettingsVars.cfgNCSapp == 1) {
        if (WorkingVars.curLogfile != "") {
            QFile file(WorkingVars.curLogfile);
            if (!file.open(QIODevice::ReadOnly)) {
                qDebug() << file.errorString();
            } //else
            QTextStream in(&file);
            QString fileLineFirst = in.readLine();
            if (DebugCheckLogLevel > 5) {qDebug() << "ACPNCSLOG " << LineNumber << fileLineFirst;}
            QStringList lineTokenFirst = fileLineFirst.split(" ",QString::SkipEmptyParts);
            if (lineTokenFirst.size() == 4){
                WorkingVars.NetAddress = lineTokenFirst.at(3);
            }
            while (!in.atEnd()) {
                QString tmpCS = "";
                QString fileLine = in.readLine();
                if (DebugCheckLogLevel > 5) {qDebug() << "ACPNCSLOG " << LineNumber << fileLine;}
                if (fileLine.contains("Checked In", Qt::CaseInsensitive)){
                    QStringList lineToken = fileLine.split(" ", QString::SkipEmptyParts);
                    if (lineToken.size() >= 4) {
                        tmpCS = lineToken.at(3).toUpper();
                    }
                    //300447Z Checked In: ZZZ4AB XX
                    if (DebugCheckLogLevel > 1) {qDebug() << LineNumber << tmpCS;}
                    if (WorkingDB.searchCS(tmpCS)) {
                        if (!RosterVar.searchCS(tmpCS)) {
                            RosterVar.add(WorkingDB[WorkingDB.searchResult].CallSign
                                    ,WorkingDB[WorkingDB.searchResult].AbrCallSign
                                    ,WorkingDB[WorkingDB.searchResult].Lat
                                    ,WorkingDB[WorkingDB.searchResult].Lon
                                    ,false
                                    ,WorkingDB[WorkingDB.searchResult].isMerged
                                    ,WorkingDB.searchResult);
                        }
                    } else { //Not Found in WorkingDB - Place in Gulf
                        RosterVar.add(tmpCS
                                ,tmpCS
                                ,"3250000"
                                ,"-9900000"
                                ,false
                                ,false
                                ,0);
                        if (DebugCheckLogLevel > 1) {qDebug() << LineNumber <<tmpCS << "Not Found";}
                    }
                    if(fileLine.contains("T=", Qt::CaseInsensitive)) {
                        int i = fileLine.indexOf("T=", 0, Qt::CaseInsensitive);
                        QString Status = fileLine.mid(i,i+5).toUpper();
                        if (DebugCheckLogLevel > 1) {qDebug() << LineNumber <<Status << RosterVar.searchCS(tmpCS);}
                        if (RosterVar.searchCS(tmpCS)) {
                            RosterVar[RosterVar.searchResult].Status = Status;
                            RosterVar.updateTable(RosterVar.searchResult);
                        }
                    }
                }
                if (fileLine.contains("New Status", Qt::CaseSensitive)) {
                    QStringList lineToken = fileLine.split(" ", QString::SkipEmptyParts);
                    if (lineToken.size() == 5) {
                        QString searchTermCS = lineToken.at(1);
                        QString Status = lineToken.at(4);
                        if (DebugCheckLogLevel > 1) {qDebug() << LineNumber << searchTermCS << Status;}
                        if (RosterVar.searchCS(searchTermCS)) {
                            RosterVar[RosterVar.searchResult].Status = Status;
                            RosterVar.updateTable(RosterVar.searchResult);
                        }
                    }

                }
                if (fileLine.contains("Roster Correction", Qt::CaseSensitive)) {
                    if (DebugCheckLogLevel > 4) {qDebug() << LineNumber << "Roster Correction Found";}
                    QStringList lineToken = fileLine.split(" ", QString::SkipEmptyParts);
                    if (DebugCheckLogLevel > 7) {qDebug() << LineNumber << "lineToken.size() =" << lineToken.size();}
                    if ((lineToken.size() >= 9) & (fileLine.contains("changed", Qt::CaseSensitive))) {
                        //041437Z Roster Correction: ZZZ4AZ XX   changed to ZZZ4BZ XX
                        QString searchTermCS = lineToken.at(3);
                        int Idx = fileLine.indexOf("changed to",0,Qt::CaseInsensitive);
                        QString MidFileLine = fileLine.mid((Idx+11),(fileLine.size()-1));
                        QStringList MidLineToken = MidFileLine.split(" ", QString::SkipEmptyParts);
                        QString NewCS = MidLineToken.at(0);
                        if (DebugCheckLogLevel > 7) {qDebug() << LineNumber << "searchTerm =" << searchTermCS << RosterVar.searchCS(searchTermCS) << " NewCS =" << NewCS << WorkingDB.searchCS(NewCS);}
                        if (RosterVar.searchCS(searchTermCS)) { //Previous CS was in RosterVar
                            if (WorkingDB.searchCS(NewCS)) {
                                int rIdx = RosterVar.searchResult;
                                int wIdx = WorkingDB.searchResult;
                                if (SettingsVars.ToolsAreaisShown) {
                                    ptrWindow->cmboStation1->removeItem(rIdx);
                                    ptrWindow->cmboStation2->removeItem(rIdx);
                                    ptrWindow->cmboStation1->addItem(WorkingDB[wIdx].AbrCallSign,rIdx);
                                    ptrWindow->cmboStation2->addItem(WorkingDB[wIdx].AbrCallSign,rIdx);
                                }
                                if (DebugCheckLogLevel > 8) {qDebug() << LineNumber << "WorkingDB[wIdx].CallSign = " << WorkingDB[wIdx].CallSign;}
                                RosterVar[rIdx].CallSign = WorkingDB[wIdx].CallSign;
                                RosterVar[rIdx].AbrCallSign = WorkingDB[wIdx].AbrCallSign;
                                RosterVar[rIdx].Lat = WorkingDB[wIdx].Lat;
                                RosterVar[rIdx].Lon = WorkingDB[wIdx].Lon;
                                RosterVar[rIdx].isMerged = WorkingDB[wIdx].isMerged;
                                RosterVar[rIdx].relDB = wIdx;
                                RosterVar.updateTable(rIdx);
                            }
                        } else if (WorkingDB.searchCS(NewCS)) {//Add corrected CS to RosterVar if it exists.
                                qDebug() << LineNumber << "Vestigial code excecuted.  Not Expected due to other changes." << WorkingVars.curLogfile;
                            RosterVar.add(WorkingDB[WorkingDB.searchResult].CallSign
                                    ,WorkingDB[WorkingDB.searchResult].AbrCallSign
                                    ,WorkingDB[WorkingDB.searchResult].Lat
                                    ,WorkingDB[WorkingDB.searchResult].Lon
                                    ,false
                                    ,WorkingDB[WorkingDB.searchResult].isMerged
                                    ,WorkingDB.searchResult);
                        }
                    }
                    if ((lineToken.size() == 7) & (fileLine.contains("removed", Qt::CaseSensitive))) {
                    //041504Z Roster Correction: ZZZ4BB removed from roster.
                        QString searchTermCS = lineToken.at(3);
                        if (DebugCheckLogLevel > 7) {qDebug() << LineNumber << "searchTerm =" << searchTermCS << RosterVar.searchCS(searchTermCS);}
                        if (RosterVar.searchCS(searchTermCS)) {
                            RosterVar.remove(RosterVar.searchResult);
                        }
                    }
                }
                LineNumber++;
            }
        }
    } else if (SettingsVars.cfgNCSapp == 2) {
        if (WorkingVars.curLogfile != "") {
            QFile file(WorkingVars.curLogfile);
            if (!file.open(QIODevice::ReadOnly)) {
                qDebug() << file.errorString();
            }
            QTextStream in(&file);
            QString tmpCS = "";
            QString fileLine = in.readLine(); //First line is blank
            bool moveOn = false;
            while (!in.atEnd() & !moveOn) {
                fileLine = in.readLine(); // Line should be 'NCS:'
                if (fileLine.contains("NCS",Qt::CaseSensitive)){
                    QStringList lineToken = fileLine.split(" ", QString::SkipEmptyParts);
                    tmpCS = lineToken.at(1);
                    if (WorkingDB.searchCS(tmpCS)) { // add NCS Station to RoasterVar
                        if (!RosterVar.searchCS(tmpCS)) {
                            RosterVar.add(WorkingDB[WorkingDB.searchResult].CallSign
                                    ,WorkingDB[WorkingDB.searchResult].AbrCallSign
                                    ,WorkingDB[WorkingDB.searchResult].Lat
                                    ,WorkingDB[WorkingDB.searchResult].Lon
                                    ,false
                                    ,WorkingDB[WorkingDB.searchResult].isMerged
                                    ,WorkingDB.searchResult);
                        }
                    }
                    moveOn = true;
                }
            }
            while (!in.atEnd()) { //add stations to roster
                fileLine = in.readLine();
                QStringList lineToken = fileLine.split(",", QString::SkipEmptyParts);
                QString tmpCS = lineToken.at(0);
                if (WorkingDB.searchCS(tmpCS)) { // add NCS Station to RoasterVar
                    if (!RosterVar.searchCS(tmpCS)) {
                        RosterVar.add(WorkingDB[WorkingDB.searchResult].CallSign
                                ,WorkingDB[WorkingDB.searchResult].AbrCallSign
                                ,WorkingDB[WorkingDB.searchResult].Lat
                                ,WorkingDB[WorkingDB.searchResult].Lon
                                ,false
                                ,WorkingDB[WorkingDB.searchResult].isMerged
                                ,WorkingDB.searchResult);
                    }
                }
            }
        }
    }

    for (int pri = 0 ; pri <= RosterVar.size(); pri ++) {
        if(!RosterVar[pri].LocDupe) {
            for (int snd=pri; snd <= RosterVar.size(); snd++) {
                if (pri != snd) {
                    if((RosterVar[pri].Lat == RosterVar[snd].Lat) & (RosterVar[pri].Lon == RosterVar[snd].Lon)){
                        if (!RosterVar[pri].AbrCallSign.contains(RosterVar[snd].AbrCallSign)){
                            if (DebugCheckLogLevel > 7) {qDebug() << "LocDupe: " << RosterVar[pri].AbrCallSign << " : " << RosterVar[snd].AbrCallSign;}
                            RosterVar[pri].AbrCallSign += "\n" + RosterVar[snd].AbrCallSign;
                            RosterVar[snd].LocDupe = true;
                        }
                    }
                }
            }
        }
    }

    if ((DebugCheckLogLevel >= 9) || (1==2)) {
        qDebug() << "RosterVar.size() =" << RosterVar.size() <<  "Roster Follows:";
        for ( int i = 0 ; i <= RosterVar.size(); i ++) {
            qDebug() << RosterVar[i].CallSign << RosterVar[i].AbrCallSign << RosterVar[i].Lat << RosterVar[i].Lon << RosterVar[i].LocDupe << RosterVar[i].Status;

        }
    }
    ptrWindow->updateStatusBar();
}

QPoint MakePoint(int idx, int w, int h) {
    float targetLat = RosterVar[idx].Lat.toFloat();
    float targetLon = RosterVar[idx].Lon.toFloat();


    int bmY = int(((targetLat - minLat) / (maxLat - minLat)) * (MAP_HEIGHT - 1));
    int bmX = int(((targetLon - minLon) / (maxLon - minLon)) * (MAP_WIDTH - 1));


    float minX = 0;  //Leftmost
    float maxX = MAP_WIDTH;  //Righmost
    int Screen_WIDTH = w;

    int pixelX = int(((bmX - minX) / (maxX - minX)) * (Screen_WIDTH - 1));

    float minY = MAP_HEIGHT;  //Top
    float maxY= 0;  //Bottom
    int Screen_HEIGHT = h;

    int pixelY = int(((bmY - minY) / (maxY - minY)) * (Screen_HEIGHT - 1));

    return(QPoint(pixelX,pixelY));
}

QPoint MakePointLatLon(float targetLat, float targetLon, int w, int h) {
    int bmY = int(((targetLat - minLat) / (maxLat - minLat)) * (MAP_HEIGHT - 1));
    int bmX = int(((targetLon - minLon) / (maxLon - minLon)) * (MAP_WIDTH - 1));


    float minX = 0;  //Leftmost
    float maxX = MAP_WIDTH;  //Righmost
    int Screen_WIDTH = w;

    int pixelX = int(((bmX - minX) / (maxX - minX)) * (Screen_WIDTH - 1));

    float minY = MAP_HEIGHT;  //Top
    float maxY= 0;  //Bottom
    int Screen_HEIGHT = h;

    int pixelY = int(((bmY - minY) / (maxY - minY)) * (Screen_HEIGHT - 1));

    return(QPoint(pixelX,pixelY));
}

QPoint MakePointWorkingDB(int idx, int w, int h) { //Under Construction
    float targetLat = WorkingDB[idx].Lat.toFloat();
    float targetLon = WorkingDB[idx].Lon.toFloat();


    int bmY = int(((targetLat - minLat) / (maxLat - minLat)) * (MAP_HEIGHT - 1));
    int bmX = int(((targetLon - minLon) / (maxLon - minLon)) * (MAP_WIDTH - 1));


    float minX = 0;  //Leftmost
    float maxX = MAP_WIDTH;  //Righmost
    int Screen_WIDTH = w;

    int pixelX = int(((bmX - minX) / (maxX - minX)) * (Screen_WIDTH - 1));

    float minY = MAP_HEIGHT;  //Top
    float maxY= 0;  //Bottom
    int Screen_HEIGHT = h;

    int pixelY = int(((bmY - minY) / (maxY - minY)) * (Screen_HEIGHT - 1));

    return(QPoint(pixelX,pixelY));
}

void RenderArea::paintEvent(QPaintEvent * /* event */)
{
    if (DebugRenderAreaPaintEvent > 0) {qDebug() << Q_FUNC_INFO;}

    QFont tmpFont;
    if(WorkingVars.FirstRunLogCheck == true) {
        CheckLog();
        WorkingVars.FirstRunLogCheck = false;
    }

    if (SettingsVars.PreserveAspectRatio) {
        tmpWidth = MAP_WIDTH;
        tmpHeight = MAP_HEIGHT;
    } else {
        tmpWidth = width();
        tmpHeight = height();
    }


    QPoint points[MaxRecordCount] = {};

    /*if (WorkingVars.drawHeatMap) {
        int row = 0;
        int col = 0;
        for (int i = 0 ; i < varHeatMap.arrHM[WorkingVars.HMindex].nextIndex -1; i++) {
            varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Point = MakePointLatLon(varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].lat.toFloat(),varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].lon.toFloat(),tmpWidth,tmpHeight);
            if ((row != varHeatMap.gridSize - 1) & (col != varHeatMap.gridSize - 1)) {
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Point2 = MakePointLatLon(varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i + varHeatMap.gridSize + 1].lat.toFloat(),varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i + varHeatMap.gridSize + 1].lon.toFloat(),tmpWidth,tmpHeight);
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Point2.setX(varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Point2.x()-1);
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Point2.setY(varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Point2.y()-1);
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].draw = true;
            } else {
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].draw = false;
            }
            float x = float(varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].val);
            if (x > 0.9f) {
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Color = QColor(255*x,0, 0, int(x * 100.0f));
            } else if (x > 0.7f) {
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Color = QColor(0, 0, 255*x, int(x * 100.0f));
            } else if (x > 0.5f) {
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Color = QColor(0,255*x, 255*x, int(x * 100.0f));
            } else if (x > 0.3f) {
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Color = QColor(0, 255*x, 0, int(x * 100.0f));
            } else {
                varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Color = QColor(0, 0, 0, 0);
            }
            if (row == varHeatMap.gridSize - 1 ) {
                row = 0;
                col++;
            } else {
                row++;
            }
        }
    }*/
    for(int idx=0;idx <= RosterVar.size(); idx++){
     //points[idx] = MakePoint(Roster[idx],tmpWidth,tmpHeight);
        if (RosterVar[idx].LocDupe == false) {
            points[idx] = MakePoint(idx,tmpWidth,tmpHeight);
        }
        if (RosterVar[idx].LocDupe == true) {
            points[idx] = MakePoint(idx,tmpWidth,tmpHeight);
        }
    }
    if (DebugRenderAreaPaintEvent > 1) {qDebug() << "RenderArea::paintEvent";}
    QRectF target(0, 0, tmpWidth, tmpHeight);
    QRectF source(0.0, 0.0, MAP_WIDTH, MAP_HEIGHT);
    QPixmap pixmap;
    pixmap.fill(Qt::transparent);
    pixmap.load(SettingsVars.WorkingDir + MAPbmp);
    QPixmap weather(SettingsVars.WorkingDir + "weather.png");
    QPainter painter(this);
    if (FirstRun) {
        if (SettingsVars.PreserveAspectRatio) {
            p.x = width()/2;
            p.y = height()/2;

        } else {
            p.x = tmpWidth/2;
            p.y = tmpHeight/2;
        }
        FirstRun = false;
    }
    if ((PointChanged == true) || (FirstRun == true)) {
        if (SettingsVars.PreserveAspectRatio) {
            int bpX = int(p.x - Xoff);
            int bpY = int(p.y - Yoff);

            Xoff = (width()/2) - bpX;
            Yoff = (height()/2) - bpY;
            PointChanged  = false;
        } else {
            int bpX = int(p.x - Xoff);
            int bpY = int(p.y - Yoff);

            Xoff = (tmpWidth/2) - bpX;
            Yoff = (tmpHeight/2) - bpY;
            PointChanged  = false;
        }
    }
    if (ScaleChanged == true) {
        if (SettingsVars.PreserveAspectRatio) {
            int bpX = int(((width()/2) - Xoff)/PrevScaleFactor);
            int bpY = int(((height()/2) - Yoff)/PrevScaleFactor);

            Xoff = ((width()/2)) - int(float(bpX) * ScaleFactor);
            Yoff = ((height()/2)) - int(float(bpY) * ScaleFactor);
        } else {
            int bpX = int(((tmpWidth/2) - Xoff)/PrevScaleFactor);
            int bpY = int(((tmpHeight/2) - Yoff)/PrevScaleFactor);

            Xoff = (int(tmpWidth/2)) - int(bpX * ScaleFactor);
            Yoff = (int(tmpHeight/2)) - int(bpY * ScaleFactor);
        }
        PrevScaleFactor = ScaleFactor;
        ScaleChanged = false;
    }

    painter.translate(Xoff ,Yoff);

    painter.scale(double(ScaleFactor),double(ScaleFactor));

    painter.setPen(Qt::SolidLine);
    int tmpFontScalarOffset = SettingsVars.FontScalar;
    if ((ScaleFactor <= 1.0) & !SettingsVars.PreserveAspectRatio) {
        tmpFont = QFont(SettingsVars.MyFont.family(),SettingsVars.PointSize);
        tmpFont.setBold(true);
        painter.setFont(tmpFont);
        tmpFontScalarOffset = SettingsVars.PointSize;

    } else {
        tmpFont = QFont(SettingsVars.MyFont.family(), SettingsVars.FontScalar);
        tmpFont.setBold(true);
        painter.setFont(tmpFont);
    }

    if (DebugRenderAreaPaintEvent > 1) {qDebug() << "RenderArea::paintEvent";}
    painter.drawPixmap(target, pixmap, source);
    if (SettingsVars.WeatherShown || SettingsVars.drawLightning) {
        painter.drawPixmap(target, weather,source);
    }
    painter.save();

    // Draw Station Location Text -+-+-+-+
    for(int idx=0;idx <= RosterVar.size(); idx++){
        if (RosterVar[idx].isMerged == 1) {
            painter.drawPoint(points[idx]);
        }
        if (RosterVar[idx].LocDupe == false) {
            if (RosterVar[idx].AbrCallSign.contains("\n")){
                if (DebugRenderAreaPaintEvent > 1) {qDebug() << "DupeFound : " << idx;}
                QStringList CSList = RosterVar[idx].AbrCallSign.split("\n");
                int x = points[idx].x();
                int y = points[idx].y() - ((CSList.size()-1) * SettingsVars.FontScalar)/2;
                QRect tmpRect0 = QRect(points[idx].x(), points[idx].y(),1,1);
                QRect tmpRect = painter.boundingRect(tmpRect0 ,(Qt::AlignCenter + Qt::TextWordWrap), RosterVar[idx].AbrCallSign);
                tmpRect.setHeight(CSList.size() * SettingsVars.FontScalar);
                tmpRect.moveCenter(points[idx]);
                if(Debug_DrawBoxes) {painter.drawRect(tmpRect);}
                RosterVar.addBounds(idx,tmpRect);
                int itrCount = 0;
                foreach (const QString &CS,CSList){
                    QRect tmpRect = painter.boundingRect(QRect(x,y,1,1),Qt::AlignCenter, CS);
                    if ( itrCount == 0 ){
                        if (DebugRenderAreaPaintEvent > 1) {qDebug() << CS;}
                        if (RosterVar[idx].Status.contains("closed",Qt::CaseInsensitive) & SettingsVars.DrawClosedStation) {
                            painter.setPen(Qt::red);
                            painter.drawText(tmpRect ,Qt::AlignCenter,CS);
                            painter.setPen(Qt::black);
                            painter.drawLine(tmpRect.topLeft(),tmpRect.bottomRight());
                            painter.drawLine(tmpRect.topRight(),tmpRect.bottomLeft());
                        } else if((RosterVar[idx].Status.contains("T=",Qt::CaseInsensitive) || RosterVar[idx].HowCopy.contains("T=",Qt::CaseInsensitive)) & SettingsVars.DrawRelayLines) {
                            painter.drawText(tmpRect ,Qt::AlignCenter,CS);
                            QString tmpStatus;
                            if (SettingsVars.cfgNCSapp == 3) {
                                tmpStatus = RosterVar[idx].HowCopy;
                            } else {
                                tmpStatus = RosterVar[idx].Status;
                            }
                            tmpStatus.remove("T=",Qt::CaseInsensitive);
                            tmpStatus.remove("(",Qt::CaseInsensitive);
                            tmpStatus.remove(")",Qt::CaseInsensitive);
                            if (RosterVar.searchAbrCS(tmpStatus)) {
                                painter.setPen(QPen(RosterVar[RosterVar.searchResult].rColor, 1, Qt::DashDotLine, Qt::SquareCap));
                                painter.drawLine(points[idx],points[RosterVar.searchResult]);
                                painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::SquareCap));
                            } else if (RosterVar.searchAbrCSContains(tmpStatus, idx)) {
                                painter.setPen(QPen(RosterVar[RosterVar.searchResult].rColor, 1, Qt::DashDotLine, Qt::SquareCap));
                                painter.drawLine(points[idx],points[RosterVar.searchResult]);
                                painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::SquareCap));
                            }
                        } else {
                            painter.drawText(tmpRect ,Qt::AlignCenter,CS);
                        }
                        //if(Debug_DrawBoxes) {painter.drawRect(tmpRect);}
                        y = y + tmpFontScalarOffset;
                        itrCount++;
                    } else { // not first CS in CSList
                        RosterVar.searchAbrCS(CS);
                        //qDebug() << CS;
                        if (RosterVar[RosterVar.searchResult].Status.contains("closed",Qt::CaseInsensitive) & SettingsVars.DrawClosedStation) {
                            painter.setPen(Qt::red);
                            painter.drawText(tmpRect ,Qt::AlignCenter,CS);
                            painter.setPen(Qt::black);
                            painter.drawLine(tmpRect.topLeft(),tmpRect.bottomRight());
                            painter.drawLine(tmpRect.topRight(),tmpRect.bottomLeft());
                        } else if((RosterVar[RosterVar.searchResult].Status.contains("T=",Qt::CaseInsensitive) || RosterVar[RosterVar.searchResult].HowCopy.contains("T=",Qt::CaseInsensitive)) & SettingsVars.DrawRelayLines) {
                            painter.drawText(tmpRect ,Qt::AlignCenter,CS);
                            QString tmpStatus;
                            if (SettingsVars.cfgNCSapp == 3) {
                                tmpStatus = RosterVar[RosterVar.searchResult].HowCopy;
                            } else {
                                tmpStatus = RosterVar[RosterVar.searchResult].Status;
                            }
                            tmpStatus.remove("T=",Qt::CaseInsensitive);
                            tmpStatus.remove("(",Qt::CaseInsensitive);
                            tmpStatus.remove(")",Qt::CaseInsensitive);
                            RosterVar.searchAbrCS(tmpStatus);
                            if (RosterVar.searchAbrCS(tmpStatus)) {
                                painter.setPen(QPen(RosterVar[RosterVar.searchResult].rColor, 1, Qt::DashDotLine, Qt::SquareCap));
                                painter.drawLine(points[idx],points[RosterVar.searchResult]);
                                painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::SquareCap));
                            } else if (RosterVar.searchAbrCSContains(tmpStatus, idx)) {
                                painter.setPen(QPen(RosterVar[RosterVar.searchResult].rColor, 1, Qt::DashDotLine, Qt::SquareCap));
                                painter.drawLine(points[idx],points[RosterVar.searchResult]);
                                painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::SquareCap));
                            }
                        } else {
                            painter.drawText(tmpRect ,Qt::AlignCenter,CS);
                        }
                        //if(Debug_DrawBoxes) {painter.drawRect(tmpRect);}
                        y = y + tmpFontScalarOffset;
                        itrCount++;
                    }

                }
            } else { // !contains("\n")
                int x = points[idx].x();
                int y = points[idx].y();//+3;
                QRect tmpRect = painter.boundingRect(QRect(x,y,1,1),Qt::AlignCenter, RosterVar[idx].AbrCallSign);
                RosterVar.addBounds(idx,tmpRect);
                if (RosterVar[idx].Status.contains("closed",Qt::CaseInsensitive) & SettingsVars.DrawClosedStation) {
                    painter.setPen(Qt::red);
                    painter.drawText(tmpRect ,Qt::AlignCenter,RosterVar[idx].AbrCallSign);
                    painter.setPen(Qt::black);
                    painter.drawLine(tmpRect.topLeft(),tmpRect.bottomRight());
                    painter.drawLine(tmpRect.topRight(),tmpRect.bottomLeft());
                } else if((RosterVar[idx].Status.contains("T=",Qt::CaseInsensitive) || RosterVar[idx].HowCopy.contains("T=",Qt::CaseInsensitive)) & SettingsVars.DrawRelayLines) {
                    painter.drawText(tmpRect ,Qt::AlignCenter,RosterVar[idx].AbrCallSign);
                    QString tmpStatus;
                    if (SettingsVars.cfgNCSapp == 3) {
                        tmpStatus = RosterVar[idx].HowCopy;
                    } else {
                        tmpStatus = RosterVar[idx].Status;
                    }
                    tmpStatus.remove("T=",Qt::CaseInsensitive);
                    tmpStatus.remove("(",Qt::CaseInsensitive);
                    tmpStatus.remove(")",Qt::CaseInsensitive);
                    if (RosterVar.searchAbrCS(tmpStatus)) {
                        painter.setPen(QPen(RosterVar[RosterVar.searchResult].rColor, 1, Qt::DashDotLine, Qt::SquareCap));
                        painter.drawLine(points[idx],points[RosterVar.searchResult]);
                        painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::SquareCap));
                    } else if (RosterVar.searchAbrCSContains(tmpStatus, idx)) {
                        painter.setPen(QPen(RosterVar[RosterVar.searchResult].rColor, 1, Qt::DashDotLine, Qt::SquareCap));
                        painter.drawLine(points[idx],points[RosterVar.searchResult]);
                        painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::SquareCap));
                    }

                } else {
                    painter.drawText(tmpRect ,Qt::AlignCenter,RosterVar[idx].AbrCallSign);
                }
                if(Debug_DrawBoxes) {painter.drawRect(tmpRect);}
            }
        }
    }
    if (DebugRenderAreaPaintEvent > 1) {qDebug() << "RenderArea::paintEvent";}
    /*if (WorkingVars.drawHeatMap) {
        for (int i = 0 ; i < varHeatMap.arrHM[WorkingVars.HMindex].nextIndex -1; i++) {
            if (varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].draw) {
                QRect r = QRect(varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Point,varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Point2);
                painter.setBrush(QBrush(varHeatMap.arrHM[WorkingVars.HMindex].ptrHM[i].Color));
                painter.fillRect(r, painter.brush());
            }
        }
    }*/
    painter.setPen(Qt::black);
    painter.drawPoint((tmpWidth/2),(tmpHeight/2));
    painter.restore();
    painter.setPen(Qt::PenStyle::SolidLine);
    painter.drawPath(WorkingVars.SolarTerminator);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (DebugRenderAreaPaintEvent > 0) {qDebug() << "exit RenderArea::paintEvent";}
}

void RenderArea::wheelEvent(QWheelEvent *event) {
    if (event->delta() > 0) { // wheel movement away from the person
        if (SettingsVars.PreserveAspectRatio) {
            if (ScaleFactor < 1) {
            ScaleFactor = ScaleFactor / float(0.8);
            } else {
                ScaleFactor = ScaleFactor + float(0.5);
            }
        } else {
            ScaleFactor = ScaleFactor + float(0.5);
        }
    }
    else if (event->delta() < 0) { // wheel movement towards the person
        if (SettingsVars.PreserveAspectRatio) {
            ScaleFactor = ScaleFactor * float(0.8);
        } else {
            ScaleFactor = ScaleFactor - float(0.5);
            if (ScaleFactor < 1) { //reset view
                ScaleFactor = 1;
                p.x = tmpWidth/2;
                p.y = tmpHeight/2;
                Xoff = 0;
                Yoff = 0;
            }
        }
    }
    ScaleChanged = true;
    update();
    event->accept();
}

void RenderArea::mousePressEvent(QMouseEvent *event){
    prev.x = event->x();
    prev.y = event->y();
}

void RenderArea::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        dragging = true;
        Xoff = Xoff - (prev.x - event->x());
        Yoff = Yoff - (prev.y - event->y());
        prev.x = event->x();
        prev.y = event->y();
        update();
    }


        //event->pos();
}

void RenderArea::mouseReleaseEvent(QMouseEvent *event) {
    if ((event->button() == Qt::LeftButton) && !dragging) {
        p.x=event->x();
        p.y=event->y();
        PointChanged = true;
        update();
    }
    if (dragging == true) {
        dragging = false;
    }
    if ((event->button() == Qt::RightButton)) {
        if (SettingsVars.ToolsAreaisShown) {
            QString tmpCS = "";
            if(RosterVar.searchBounds(QPoint(event->x(),event->y()))) {
                //qDebug() << RosterVar[RosterVar.searchResult].AbrCallSign;
                if (RosterVar[RosterVar.searchResult].AbrCallSign.contains("\n")){
                    QStringList CSList = RosterVar[RosterVar.searchResult].AbrCallSign.split("\n");
                    tmpCS = CSList.takeFirst();
                } else {
                    tmpCS = RosterVar[RosterVar.searchResult].AbrCallSign;
                }
                if (WorkingVars.RightClickCounter == 0) {
                    ptrWindow->cmboStation1->setCurrentText(tmpCS);
                    WorkingVars.RightClickCounter = 1;
                } else if (WorkingVars.RightClickCounter == 1) {
                    ptrWindow->cmboStation2->setCurrentText(tmpCS);
                    WorkingVars.RightClickCounter = 0;
                }
                ptrWindow->clickedDist();
            }
        }
    }
    if (event->button() == Qt::BackButton) {
        if (SettingsVars.FontScalar > 1) {
            SettingsVars.FontScalar = SettingsVars.FontScalar - 1;
            update();
        }
    }
    if (event->button() == Qt::ForwardButton) {
        if (SettingsVars.FontScalar <= 30) {
            SettingsVars.FontScalar = SettingsVars.FontScalar + 1;
            update();
        }
    }

}

