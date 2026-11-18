#pragma once

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QRegularExpression>
#include <QString>
#include <QTextDocument>
#include <QTextOption>

// Mathematical subscripts are a presentation concern.  Keep the original
// result-array names and the plain Qt::DisplayRole text unchanged for loading,
// saving, searching and accessibility.  Rich text is generated only for paint.
namespace PostScientificText
{
inline const QRegularExpression& symbolPattern()
{
    // Order the two-character symbols first, so εp_x is rendered as εpₓ.
    // Do not interpret general underscore-separated file/field identifiers.
    static const QRegularExpression pattern(
        QStringLiteral("((?:[εγ]p|[uUvVσεγτ]))_([A-Za-z0-9]+)"));
    return pattern;
}

inline bool hasSubscript(const QString& plain)
{
    return symbolPattern().match(plain).hasMatch();
}

inline QString toHtml(const QString& plain)
{
    QString result;
    result.reserve(plain.size() + 32);
    auto matchIt = symbolPattern().globalMatch(plain);
    int cursor = 0;
    while (matchIt.hasNext())
    {
        const QRegularExpressionMatch match = matchIt.next();
        result += plain.mid(cursor, match.capturedStart() - cursor).toHtmlEscaped();
        const QString symbol = match.captured(1);
        if (symbol == QStringLiteral("εp") || symbol == QStringLiteral("γp"))
            result += symbol.left(1).toHtmlEscaped() + QStringLiteral("<sup>p</sup>");
        else
            result += symbol.toHtmlEscaped();
        result += QStringLiteral("<sub>");
        result += match.captured(2).toHtmlEscaped();
        result += QStringLiteral("</sub>");
        cursor = match.capturedEnd();
    }
    result += plain.mid(cursor).toHtmlEscaped();
    return result;
}

inline void paint(QPainter* painter, const QRect& rect, const QString& plain,
                  const QFont& font, const QColor& color)
{
    if (!painter || rect.isEmpty())
        return;

    QTextDocument document;
    document.setDefaultFont(font);
    document.setDocumentMargin(0.0);
    QTextOption textOptions = document.defaultTextOption();
    textOptions.setWrapMode(QTextOption::NoWrap);
    document.setDefaultTextOption(textOptions);
    document.setHtml(QStringLiteral("<span style=\"color:%1\">%2</span>")
                         .arg(color.name(QColor::HexRgb), toHtml(plain)));
    document.setTextWidth(-1);

    painter->save();
    painter->setClipRect(rect);
    const qreal y = rect.top() + (rect.height() - document.size().height()) * 0.5;
    painter->translate(rect.left(), y);
    document.drawContents(painter);
    painter->restore();
}
} // namespace PostScientificText
