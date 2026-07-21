#include "static_abilities.h"

#include <QRegularExpression>
#include <libcockatrice/card/ability/card_keywords.h>
#include <libcockatrice/card/card_info.h>
#include <optional>

namespace StaticAbilities
{

namespace
{

// Same convention as CardKeywords::withoutReminderText() (rule 207.2): reminder text in
// parentheses is flavor/explanation, not a printed ability. Duplicated rather than exported from
// card_keywords.cpp, matching that file's own precedent of a small local helper over widening a
// neighboring parser's public API.
QString withoutReminderText(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(\([^)]*\))"));
    QString stripped = text;
    stripped.remove(re);
    return stripped;
}

QString canonicalKeywordFor(const QString &token)
{
    for (const QString &keyword : CardKeywords::evergreenKeywords()) {
        if (QString::compare(token, keyword, Qt::CaseInsensitive) == 0) {
            return keyword;
        }
    }
    return {};
}

// Same tokenizing rule as CardKeywords::splitTokens().
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

// Same "exact shape or skip" rule as CardKeywords::parse(): std::nullopt if any token fails to
// canonicalize, rather than partially accepting the tokens that did.
std::optional<QSet<QString>> canonicalizeAll(const QStringList &tokens)
{
    if (tokens.isEmpty()) {
        return std::nullopt;
    }
    QSet<QString> found;
    for (const QString &token : tokens) {
        const QString canonical = canonicalKeywordFor(token);
        if (canonical.isEmpty()) {
            return std::nullopt;
        }
        found.insert(canonical);
    }
    return found;
}

const QRegularExpression &ptBoostRegex()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(Other creatures you control|Creatures you control) get ([+-]\d+)/([+-]\d+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &keywordGrantRegex()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(Other creatures you control|Creatures you control) have (.+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

StaticScope scopeFor(const QString &phrase)
{
    return QString::compare(phrase, QStringLiteral("Other creatures you control"), Qt::CaseInsensitive) == 0
               ? StaticScope::OthersYours
               : StaticScope::AllYours;
}

} // namespace

QList<StaticAbility> parse(const CardInfo &card)
{
    QList<StaticAbility> found;

    const QStringList lines = withoutReminderText(card.getText()).split(QChar('\n'), Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.endsWith(QChar('.'))) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }

        const QRegularExpressionMatch ptMatch = ptBoostRegex().match(line);
        if (ptMatch.hasMatch()) {
            StaticAbility ability;
            ability.scope = scopeFor(ptMatch.captured(1));
            ability.powerBonus = ptMatch.captured(2).toInt();
            ability.toughnessBonus = ptMatch.captured(3).toInt();
            found.append(ability);
            continue;
        }

        const QRegularExpressionMatch keywordMatch = keywordGrantRegex().match(line);
        if (keywordMatch.hasMatch()) {
            const std::optional<QSet<QString>> keywords = canonicalizeAll(splitTokens(keywordMatch.captured(2)));
            if (keywords) {
                StaticAbility ability;
                ability.scope = scopeFor(keywordMatch.captured(1));
                ability.grantedKeywords = *keywords;
                found.append(ability);
            }
            continue;
        }
    }

    return found;
}

QString serialize(const QList<StaticAbility> &abilities)
{
    QStringList entries;
    entries.reserve(abilities.size());
    for (const StaticAbility &ability : abilities) {
        const QString scopeChar = ability.scope == StaticScope::AllYours ? QStringLiteral("a") : QStringLiteral("o");
        const QString keywordList = QStringList(ability.grantedKeywords.values()).join(QChar(','));
        entries.append(QStringLiteral("%1|%2|%3|%4")
                           .arg(scopeChar)
                           .arg(ability.powerBonus)
                           .arg(ability.toughnessBonus)
                           .arg(keywordList));
    }
    return entries.join(QChar(';'));
}

QList<StaticAbility> deserialize(const QString &s)
{
    QList<StaticAbility> abilities;
    if (s.isEmpty()) {
        return abilities;
    }

    const QStringList entries = s.split(QChar(';'), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const QStringList fields = entry.split(QChar('|'));
        if (fields.size() != 4) {
            continue;
        }

        bool powerOk = false;
        bool toughnessOk = false;
        const int power = fields.at(1).toInt(&powerOk);
        const int toughness = fields.at(2).toInt(&toughnessOk);
        if (!powerOk || !toughnessOk) {
            continue;
        }

        StaticAbility ability;
        ability.scope = fields.at(0) == QStringLiteral("a") ? StaticScope::AllYours : StaticScope::OthersYours;
        ability.powerBonus = power;
        ability.toughnessBonus = toughness;
        if (!fields.at(3).isEmpty()) {
            const QStringList keywordTokens = fields.at(3).split(QChar(','));
            ability.grantedKeywords = QSet<QString>(keywordTokens.begin(), keywordTokens.end());
        }
        abilities.append(ability);
    }

    return abilities;
}

} // namespace StaticAbilities
