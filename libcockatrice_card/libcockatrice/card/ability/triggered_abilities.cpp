#include "triggered_abilities.h"

#include <QRegularExpression>
#include <libcockatrice/card/card_info.h>
#include <optional>

namespace TriggeredAbilities
{

namespace
{
// Same convention as ActivatedAbilities::withoutReminderText() / ManaAbilities' own copy (rule
// 207.2): reminder text in parentheses is flavor/explanation, not a printed ability. Kept as its
// own copy here rather than shared, same as the other existing copies of this helper.
QString withoutReminderText(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(\([^)]*\))"));
    QString stripped = text;
    stripped.remove(re);
    return stripped;
}

const QRegularExpression &wholeLineParensPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^\((.*)\)$)"));
    return re;
}

// The same three effect-phrase shapes ActivatedAbilities::parse() recognizes, minus the "{T}:" cost
// prefix (irrelevant here -- a trigger fires unconditionally) and minus DealDamage (see this file's
// header doc comment for why targeted triggers are out of scope this stage).
const QRegularExpression &drawCardEffectPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^Draw a card$)"), QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &gainLifeEffectPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^You gain (\d+) life$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &loseLifeEffectPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^You lose (\d+) life$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Matches an already-isolated effect clause (trigger-condition prefix and trailing period already
// stripped by the caller) against the whitelist above. std::nullopt if it doesn't match any
// recognized shape -- the caller skips the whole line in that case, never guessing at intent.
std::optional<CardEffect> parseEffectClause(const QString &clause)
{
    if (drawCardEffectPattern().match(clause).hasMatch()) {
        return CardEffect{EffectKind::DrawCards, 1};
    }

    const QRegularExpressionMatch gainMatch = gainLifeEffectPattern().match(clause);
    if (gainMatch.hasMatch()) {
        return CardEffect{EffectKind::GainLife, gainMatch.captured(1).toInt()};
    }

    const QRegularExpressionMatch loseMatch = loseLifeEffectPattern().match(clause);
    if (loseMatch.hasMatch()) {
        return CardEffect{EffectKind::LoseLife, loseMatch.captured(1).toInt()};
    }

    return std::nullopt;
}
} // namespace

QList<TriggeredAbility> parse(const CardInfo &card)
{
    QList<TriggeredAbility> found;

    // Built per-card (not a static module-level singleton like the sibling parsers' patterns),
    // since real Oracle text is self-referential by the card's own literal printed name, not a
    // placeholder like "~" -- see this file's header doc comment.
    const QString escapedName = QRegularExpression::escape(card.getName());
    const QRegularExpression entersPattern(
        QStringLiteral(R"(^When(?:ever)?\s+%1\s+enters\s+the\s+battlefield,\s*(.+)\.$)").arg(escapedName),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression diesPattern(
        QStringLiteral(R"(^When\s+%1\s+dies,\s*(.+)\.$)").arg(escapedName), QRegularExpression::CaseInsensitiveOption);

    const QStringList rawLines = card.getText().split(QChar('\n'), Qt::SkipEmptyParts);
    for (const QString &rawLine : rawLines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QRegularExpressionMatch wholeLineMatch = wholeLineParensPattern().match(line);
        line = wholeLineMatch.hasMatch() ? wholeLineMatch.captured(1).trimmed() : withoutReminderText(line).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QRegularExpressionMatch entersMatch = entersPattern.match(line);
        if (entersMatch.hasMatch()) {
            if (const std::optional<CardEffect> effect = parseEffectClause(entersMatch.captured(1).trimmed())) {
                found.append(TriggeredAbility{TriggerKind::EntersBattlefield, *effect});
            }
            continue;
        }

        const QRegularExpressionMatch diesMatch = diesPattern.match(line);
        if (diesMatch.hasMatch()) {
            if (const std::optional<CardEffect> effect = parseEffectClause(diesMatch.captured(1).trimmed())) {
                found.append(TriggeredAbility{TriggerKind::Dies, *effect});
            }
            continue;
        }
    }

    return found;
}

} // namespace TriggeredAbilities
