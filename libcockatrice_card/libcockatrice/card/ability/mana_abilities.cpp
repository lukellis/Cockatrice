#include "mana_abilities.h"

#include <QRegularExpression>
#include <libcockatrice/card/card_info.h>

namespace ManaAbilities
{

namespace
{
// Same convention as CardKeywords::withoutReminderText() / CommanderRules::withoutReminderText()
// (rule 207.2): reminder text in parentheses is flavor/explanation, not a printed ability. Kept as
// its own copy here rather than shared, same as the other two existing copies -- see the plan
// doc's note that a shared helper is worth considering later, not required for this increment.
QString withoutReminderText(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(\([^)]*\))"));
    QString stripped = text;
    stripped.remove(re);
    return stripped;
}

// Matches a full line of the shape "{T}: Add {X}{X}...{X}." where each {X} is one of the six mana
// symbols. Whether all captured symbols are actually identical is checked separately below, since
// QRegularExpression only exposes the last repetition of a repeated capture group.
const QRegularExpression &manaAbilityLinePattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^\{T\}:\s*Add\s*((?:\{[WUBRGC]\})+)\.$)"),
                                        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &manaSymbolPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(\{([WUBRGC])\})"), QRegularExpression::CaseInsensitiveOption);
    return re;
}
} // namespace

QList<ManaAbility> parse(const CardInfo &card)
{
    QList<ManaAbility> found;

    const QStringList lines = withoutReminderText(card.getText()).split(QChar('\n'), Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QRegularExpressionMatch lineMatch = manaAbilityLinePattern().match(line);
        if (!lineMatch.hasMatch()) {
            continue;
        }

        const QString symbolsPart = lineMatch.captured(1);
        QString commonSymbol;
        int amount = 0;
        bool allSame = true;
        auto symbolIt = manaSymbolPattern().globalMatch(symbolsPart);
        while (symbolIt.hasNext()) {
            const QString symbol = symbolIt.next().captured(1).toUpper();
            if (commonSymbol.isEmpty()) {
                commonSymbol = symbol;
            } else if (symbol != commonSymbol) {
                allSame = false;
                break;
            }
            ++amount;
        }

        if (!allSame || commonSymbol.isEmpty()) {
            // Mixed symbols (e.g. "Add {W} or {U}") represent a player choice, which this
            // increment deliberately does not offer -- skip rather than guess.
            continue;
        }

        found.append(ManaAbility{commonSymbol, amount});
    }

    return found;
}

} // namespace ManaAbilities
