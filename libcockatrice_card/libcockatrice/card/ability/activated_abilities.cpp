#include "activated_abilities.h"

#include <QRegularExpression>
#include <libcockatrice/card/card_info.h>

namespace ActivatedAbilities
{

namespace
{
// Same convention as CardKeywords::withoutReminderText() / ManaAbilities' own copy (rule 207.2):
// reminder text in parentheses is flavor/explanation, not a printed ability. Kept as its own copy
// here rather than shared, same as the other existing copies of this helper.
QString withoutReminderText(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(\([^)]*\))"));
    QString stripped = text;
    stripped.remove(re);
    return stripped;
}

// Same whole-line-parenthetical handling as ManaAbilities (a basic land's granted ability isn't
// printed text at all, so Oracle/MTGJSON renders it as reminder text that IS the entire line) --
// not expected to matter for the effect shapes below in practice, but kept for consistency with
// the sibling parser in case a future qualifying line is ever printed that way.
const QRegularExpression &wholeLineParensPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^\((.*)\)$)"));
    return re;
}

const QRegularExpression &drawCardLinePattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^\{T\}:\s*Draw a card\.$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &gainLifeLinePattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^\{T\}:\s*You gain (\d+) life\.$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &loseLifeLinePattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^\{T\}:\s*You lose (\d+) life\.$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}
} // namespace

QList<ActivatedAbility> parse(const CardInfo &card)
{
    QList<ActivatedAbility> found;

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

        if (drawCardLinePattern().match(line).hasMatch()) {
            found.append(ActivatedAbility{true, CardEffect{EffectKind::DrawCards, 1}});
            continue;
        }

        const QRegularExpressionMatch gainMatch = gainLifeLinePattern().match(line);
        if (gainMatch.hasMatch()) {
            found.append(ActivatedAbility{true, CardEffect{EffectKind::GainLife, gainMatch.captured(1).toInt()}});
            continue;
        }

        const QRegularExpressionMatch loseMatch = loseLifeLinePattern().match(line);
        if (loseMatch.hasMatch()) {
            found.append(ActivatedAbility{true, CardEffect{EffectKind::LoseLife, loseMatch.captured(1).toInt()}});
            continue;
        }
    }

    return found;
}

} // namespace ActivatedAbilities
