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
// symbols, directly concatenated (no separator) -- i.e. all produced simultaneously. Whether all
// captured symbols are actually identical is checked separately below, since QRegularExpression
// only exposes the last repetition of a repeated capture group.
const QRegularExpression &manaFixedLinePattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^\{T\}:\s*Add\s*((?:\{[WUBRGC]\})+)\.$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Matches "{T}: Add {X} or {Y}." / "{T}: Add {X}, {Y}, or {Z}." -- symbols separated by a comma
// and/or the word "or", meaning a choice of one, unlike the directly-concatenated fixed pattern
// above (which means all listed symbols are produced simultaneously).
const QRegularExpression &manaChoiceLinePattern()
{
    // Connector between symbols is either ", " (with an optional Oxford-comma "or ", for 3+
    // items) or a bare " or " (for exactly 2 items, e.g. "Add {W} or {U}.").
    static const QRegularExpression re(
        QStringLiteral(R"(^\{T\}:\s*Add\s+(\{[WUBRGC]\}(?:\s*(?:,\s*(?:or\s+)?|\s+or\s+)\{[WUBRGC]\})+)\.$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &manaSymbolPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(\{([WUBRGC])\})"), QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Known real "any color" phrasings whose actual legal-option set depends on game state this
// parser can't inspect (a commander's color identity, or what other lands are on the
// battlefield) -- simplified to "any of the five colors" per this fork's advisory philosophy.
// Exact known phrasings only, same conservatism as the rest of this parser -- not a wildcard
// match on "any trailing text", which would risk swallowing a real restriction.
const QRegularExpression &manaAnyColorPhrasePattern()
{
    static const QRegularExpression re(
        QStringLiteral(
            R"(^\{T\}:\s*Add\s+one\s+mana\s+of\s+any\s+color(?:\s+in\s+your\s+commander's\s+color\s+identity)?\.$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &manaAlreadyProducedPhrasePattern()
{
    static const QRegularExpression re(
        QStringLiteral(
            R"(^\{T\}:\s*Add\s+a\s+color\s+of\s+mana\s+already\s+produced\s+by\s+a\s+land\s+you\s+control\.$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QStringList &allFiveColors()
{
    static const QStringList colors = {QStringLiteral("W"), QStringLiteral("U"), QStringLiteral("B"),
                                       QStringLiteral("R"), QStringLiteral("G")};
    return colors;
}

// A basic land's mana ability (rule 305.6) isn't a printed ability -- it's granted by the game
// rules based on the land's subtype, so Oracle/MTGJSON text renders it as reminder text that IS
// the entire line, e.g. "({T}: Add {G}.)". That's different from reminder text that merely
// *explains* an unrelated printed ability (which withoutReminderText() correctly discards) --
// here the parenthetical is the only source of truth for what the ability does. Recognized only
// when the parentheses wrap the *whole* line, same "reduces entirely to X" conservatism as the
// rest of this parser.
const QRegularExpression &wholeLineParensPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^\((.*)\)$)"));
    return re;
}
} // namespace

QList<ManaAbility> parse(const CardInfo &card)
{
    QList<ManaAbility> found;

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

        const QRegularExpressionMatch fixedMatch = manaFixedLinePattern().match(line);
        if (fixedMatch.hasMatch()) {
            const QString symbolsPart = fixedMatch.captured(1);
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

            if (allSame && !commonSymbol.isEmpty()) {
                found.append(ManaAbility{{commonSymbol}, amount});
                continue;
            }
            // Directly-concatenated mixed symbols (e.g. "Add {W}{U}.") would mean both produced
            // simultaneously, which is rare/nonexistent in real cards and ambiguous to guess at
            // -- skip rather than misreport as a choice.
            continue;
        }

        const QRegularExpressionMatch choiceMatch = manaChoiceLinePattern().match(line);
        if (choiceMatch.hasMatch()) {
            QStringList symbols;
            auto symbolIt = manaSymbolPattern().globalMatch(choiceMatch.captured(1));
            while (symbolIt.hasNext()) {
                const QString symbol = symbolIt.next().captured(1).toUpper();
                if (!symbols.contains(symbol)) {
                    symbols.append(symbol);
                }
            }
            if (symbols.size() >= 2) {
                found.append(ManaAbility{symbols, 1});
            }
            continue;
        }

        if (manaAnyColorPhrasePattern().match(line).hasMatch() ||
            manaAlreadyProducedPhrasePattern().match(line).hasMatch()) {
            found.append(ManaAbility{allFiveColors(), 1});
            continue;
        }
    }

    return found;
}

} // namespace ManaAbilities
