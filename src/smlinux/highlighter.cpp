#include "highlighter.h"
#include "main.h"

Highlighter::Highlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{

    SpellCheckerRule ruleSP;
    HighlightingRule rule;

    QString fileNameSettings = "/home/user/MSC/StationMapper/highlighter.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);

    int size = settings.beginReadArray("HighlightingRule");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        int color = settings.value("setForeground").toInt();
        int weight = settings.value("setFontWeight").toInt();
        bool italic = settings.value("setFontItalic").toBool();
        QString tmpDesc = settings.value("Description").toString();
        QStringList keywordPatterns = settings.value("keywordPatterns").toStringList();
        bool isActive = settings.value("isActive").toBool();

        if (isActive) {
            QBrush brush;
            keywordFormat.setForeground((Qt::GlobalColor)color);
            keywordFormat.setFontWeight(weight);
            keywordFormat.setFontItalic(italic);
            foreach (const QString &pattern, keywordPatterns) {
                rule.pattern = QRegularExpression(pattern);
                rule.format = keywordFormat;
                highlightingRules.append(rule);

            }
        }

    }

    multiLineCommentFormat.setForeground(Qt::darkGreen);
    commentStartExpression = QRegularExpression("\\bGENTEXT\\b");
    commentEndExpression = QRegularExpression("//");

    SpellCheckerFormat.setUnderlineColor(Qt::red);
    SpellCheckerFormat.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    ruleSP.pattern = QRegularExpression("(\\w)+\\b");
    ruleSP.format = SpellCheckerFormat;
    SpellCheckerRules.append(ruleSP);

}

void Highlighter::reload() {
    highlightingRules.clear();

    HighlightingRule rule;

    QString fileNameSettings = "/home/user/MSC/StationMapper/highlighter.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);

    int size = settings.beginReadArray("HighlightingRule");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        int color = settings.value("setForeground").toInt();
        int weight = settings.value("setFontWeight").toInt();
        bool italic = settings.value("setFontItalic").toBool();
        QString tmpDesc = settings.value("Description").toString();
        QStringList keywordPatterns = settings.value("keywordPatterns").toStringList();
        bool isActive = settings.value("isActive").toBool();

        if (isActive) {
            QBrush brush;
            keywordFormat.setForeground((Qt::GlobalColor)color);
            keywordFormat.setFontWeight(weight);
            keywordFormat.setFontItalic(italic);
            foreach (const QString &pattern, keywordPatterns) {
                rule.pattern = QRegularExpression(pattern);
                rule.format = keywordFormat;
                highlightingRules.append(rule);

            }
        }

    }
}

void Highlighter::highlightBlock(const QString &text)
{

    foreach (const SpellCheckerRule &ruleSP, SpellCheckerRules) {
        QRegularExpressionMatchIterator matchIterator = ruleSP.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            /*if (!spelchecker.isSpellingCorrect(match.captured(0))) {
                setFormat(match.capturedStart(), match.capturedLength(), ruleSP.format);
            }*/
        }
    }

    foreach (const HighlightingRule &rule, highlightingRules) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    setCurrentBlockState(0);

    /*/Comment Expression------------------------/

    int startIndex = 0;
    if (previousBlockState() != 1)
        startIndex = text.indexOf(commentStartExpression);


    while (startIndex >= 0) {

        QRegularExpressionMatch match = commentEndExpression.match(text, startIndex);
        int endIndex = match.capturedStart();
        int commentLength = 0;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex
                            + match.capturedLength();
        }
        setFormat(startIndex, commentLength, multiLineCommentFormat);
        startIndex = text.indexOf(commentStartExpression, startIndex + commentLength);
    }

    //END Comment Expression------------------------*/
}

void Highlighter::addWord(QString word) {/*
    spelchecker.addWord(word);
    QString fileNameSettings = SettingsVars.WorkingDir+"personal_dictionary.ini";
    QFile file( fileNameSettings );
    if ( file.open(QIODevice::Append) )
    {
        QTextStream stream( &file );
        stream << word << "\r\n";
    }
    file.close();*/
}
