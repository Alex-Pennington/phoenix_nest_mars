#ifndef HIGHLIGHTER_H
#define HIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QString>

//#include "classspelchecker.h"

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE


class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    Highlighter(QTextDocument *parent = 0);
    void addWord(QString word);
    void reload();
protected:
    void highlightBlock(const QString &text) override;

private:
//    classspelchecker spelchecker;
    struct HighlightingRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    struct SpellCheckerRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<HighlightingRule> highlightingRules;
    QVector<SpellCheckerRule> SpellCheckerRules;

    QRegularExpression commentStartExpression;
    QRegularExpression commentEndExpression;

    QTextCharFormat keywordFormat;
    QTextCharFormat classFormat;
    QTextCharFormat singleLineCommentFormat;
    QTextCharFormat multiLineCommentFormat;
    QTextCharFormat quotationFormat;
    QTextCharFormat functionFormat;
    QTextCharFormat SpellCheckerFormat;
};


#endif // HIGHLIGHTER_H
