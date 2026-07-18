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

// Phase 7 Stage 4: every line pattern below is prefixed with the same optional mana-cost fragment,
// "(?:((?:\{(?:[0-9]+|[WUBRGC])\})+),\s*)?", ahead of its existing "{T}:" -- e.g. "{2}{R}, {T}: Deal
// 2 damage to any target." Each symbol must be a plain digit (generic) or one of W/U/B/R/G/C (a
// colored or colorless pip), directly concatenated with no separator between symbols, followed by a
// comma and "{T}:" -- {X}, hybrid ("{W/U}"), and Phyrexian ("{W/P}") symbols don't fit this token
// shape at all, so a line using one of those simply fails to match and is skipped entirely, same
// "exact shape or skip" conservatism as the rest of this parser. The fragment is copy-pasted into
// each pattern below (not a shared regex object) so every pattern stays a single self-contained
// literal, matching this file's existing one-pattern-per-shape convention.

const QRegularExpression &costTokenPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(\{([0-9]+|[WUBRGC])\})"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Parses a captured cost-prefix string (e.g. "{2}{R}", or empty/null when there was no cost) into a
// ManaCost: digit tokens accumulate into `generic`, letter tokens accumulate into `coloredPips`,
// keyed by the mana-pool counter name convention (C -> "x", everything else lowercased -- same
// mapping player_actions.cpp's manaCounterNameForSymbol() already uses).
ManaCost parseManaCost(const QString &costTokens)
{
    ManaCost cost;
    auto it = costTokenPattern().globalMatch(costTokens);
    while (it.hasNext()) {
        const QString token = it.next().captured(1);
        bool isNumber = false;
        const int value = token.toInt(&isNumber);
        if (isNumber) {
            cost.generic += value;
            continue;
        }
        const QString symbol = token.toUpper();
        const QString counterName = symbol == QLatin1String("C") ? QStringLiteral("x") : symbol.toLower();
        cost.coloredPips[counterName] = cost.coloredPips.value(counterName, 0) + 1;
    }
    return cost;
}

const QRegularExpression &drawCardLinePattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(?:((?:\{(?:[0-9]+|[WUBRGC])\})+),\s*)?\{T\}:\s*Draw a card\.$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &gainLifeLinePattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(?:((?:\{(?:[0-9]+|[WUBRGC])\})+),\s*)?\{T\}:\s*You gain (\d+) life\.$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &loseLifeLinePattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(?:((?:\{(?:[0-9]+|[WUBRGC])\})+),\s*)?\{T\}:\s*You lose (\d+) life\.$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Stage 2: only the modern "any target" templating is recognized (a target that can be a
// creature/permanent or a player, resolved at activation time via the client's board-click
// targeting interaction -- see AbilityTargetPicker). Older phrasings ("target creature",
// "target player", "target creature or player") are deliberately not matched -- same "exact
// shape or skip" conservatism as every other pattern here; broadening this whitelist is a cheap
// follow-up once this shape is proven, not attempted now.
const QRegularExpression &dealDamageLinePattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(?:((?:\{(?:[0-9]+|[WUBRGC])\})+),\s*)?\{T\}:\s*Deal (\d+) damage to any target\.$)"),
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

        const QRegularExpressionMatch drawMatch = drawCardLinePattern().match(line);
        if (drawMatch.hasMatch()) {
            found.append(
                ActivatedAbility{true, CardEffect{EffectKind::DrawCards, 1}, parseManaCost(drawMatch.captured(1))});
            continue;
        }

        const QRegularExpressionMatch gainMatch = gainLifeLinePattern().match(line);
        if (gainMatch.hasMatch()) {
            found.append(ActivatedAbility{true, CardEffect{EffectKind::GainLife, gainMatch.captured(2).toInt()},
                                          parseManaCost(gainMatch.captured(1))});
            continue;
        }

        const QRegularExpressionMatch loseMatch = loseLifeLinePattern().match(line);
        if (loseMatch.hasMatch()) {
            found.append(ActivatedAbility{true, CardEffect{EffectKind::LoseLife, loseMatch.captured(2).toInt()},
                                          parseManaCost(loseMatch.captured(1))});
            continue;
        }

        const QRegularExpressionMatch damageMatch = dealDamageLinePattern().match(line);
        if (damageMatch.hasMatch()) {
            found.append(ActivatedAbility{
                true, CardEffect{EffectKind::DealDamage, damageMatch.captured(2).toInt(), TargetKind::AnyTarget},
                parseManaCost(damageMatch.captured(1))});
            continue;
        }
    }

    return found;
}

} // namespace ActivatedAbilities
