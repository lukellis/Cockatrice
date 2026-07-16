#include "card_keywords.h"

#include <QRegularExpression>
#include <libcockatrice/card/card_info.h>

namespace CardKeywords
{

const QStringList &evergreenKeywords()
{
    static const QStringList keywords = {
        QStringLiteral("Deathtouch"),   QStringLiteral("Defender"),  QStringLiteral("Double strike"),
        QStringLiteral("First strike"), QStringLiteral("Flash"),     QStringLiteral("Flying"),
        QStringLiteral("Haste"),        QStringLiteral("Hexproof"),  QStringLiteral("Indestructible"),
        QStringLiteral("Lifelink"),     QStringLiteral("Menace"),    QStringLiteral("Reach"),
        QStringLiteral("Trample"),      QStringLiteral("Vigilance"),
    };
    return keywords;
}

namespace
{
// Same convention as CommanderRules::withoutReminderText() (rule 207.2): reminder text in
// parentheses is flavor/explanation, not a printed keyword.
QString withoutReminderText(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(\([^)]*\))"));
    QString stripped = text;
    stripped.remove(re);
    return stripped;
}

QString canonicalKeywordFor(const QString &token)
{
    for (const QString &keyword : evergreenKeywords()) {
        if (QString::compare(token, keyword, Qt::CaseInsensitive) == 0) {
            return keyword;
        }
    }
    return {};
}

// Splits a single keyword-ability line (e.g. "First strike, trample and vigilance") into its
// comma/"and"-separated tokens.
QStringList splitTokens(const QString &line)
{
    QString normalized = line;
    normalized.replace(QStringLiteral(" and "), QStringLiteral(","), Qt::CaseInsensitive);
    QStringList tokens = normalized.split(QChar(','), Qt::SkipEmptyParts);
    for (QString &token : tokens) {
        token = token.trimmed();
    }
    return tokens;
}
} // namespace

QSet<QString> parse(const CardInfo &card)
{
    QSet<QString> found;

    const QStringList lines = withoutReminderText(card.getText()).split(QChar('\n'), Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.endsWith(QChar('.'))) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }

        const QStringList tokens = splitTokens(line);
        if (tokens.isEmpty()) {
            continue;
        }

        QStringList canonicalTokens;
        canonicalTokens.reserve(tokens.size());
        for (const QString &token : tokens) {
            const QString canonical = canonicalKeywordFor(token);
            if (canonical.isEmpty()) {
                // At least one token on this line isn't a bare keyword -- the whole line is some
                // other kind of ability text (possibly one that merely mentions a keyword), so
                // skip it entirely rather than guess which tokens were "real".
                canonicalTokens.clear();
                break;
            }
            canonicalTokens.append(canonical);
        }

        for (const QString &canonical : canonicalTokens) {
            found.insert(canonical);
        }
    }

    return found;
}

} // namespace CardKeywords
