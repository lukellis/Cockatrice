#include "commander_rules.h"

#include <QRegularExpression>
#include <libcockatrice/card/card_info.h>

namespace CommanderRules
{

bool canBeCommander(const CardInfo &card)
{
    bool isLegendaryCreature = card.getCardType().contains("Legendary", Qt::CaseInsensitive) &&
                               card.getCardType().contains("Creature", Qt::CaseInsensitive);
    return isLegendaryCreature || card.getText().contains("can be your commander", Qt::CaseInsensitive);
}

namespace
{
// Matches mana symbols such as {W}, {U/B}, {2/W}, {W/P}.
const QRegularExpression &manaSymbolPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(\{([^}]+)\})"));
    return re;
}

// Rule 903.4: reminder text (in parentheses) does not contribute to color identity.
QString withoutReminderText(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(\([^)]*\))"));
    QString stripped = text;
    stripped.remove(re);
    return stripped;
}

void addColorsFromManaSymbols(const QString &text, QSet<QChar> &identity)
{
    auto it = manaSymbolPattern().globalMatch(withoutReminderText(text));
    while (it.hasNext()) {
        const QString symbol = it.next().captured(1).toUpper();
        for (QChar c : {QChar('W'), QChar('U'), QChar('B'), QChar('R'), QChar('G')}) {
            if (symbol.contains(c)) {
                identity.insert(c);
            }
        }
    }
}
} // namespace

QSet<QChar> colorIdentity(const CardInfo &card)
{
    QSet<QChar> identity;

    for (QChar c : card.getColors()) {
        const QChar upper = c.toUpper();
        if (upper == QChar('W') || upper == QChar('U') || upper == QChar('B') || upper == QChar('R') ||
            upper == QChar('G')) {
            identity.insert(upper);
        }
    }

    addColorsFromManaSymbols(card.getManaCost(), identity);
    addColorsFromManaSymbols(card.getText(), identity);

    // Note: mana symbols printed only on a related back/DFC face (looked up by name via the
    // card database) also contribute to color identity per rule 903.4, but that requires a
    // database lookup unavailable at this layer; callers dealing with double-faced cards should
    // union in the back face's colorIdentity() separately when available.

    return identity;
}

bool isWithinColorIdentity(const QSet<QChar> &cardIdentity, const QSet<QChar> &commanderIdentity)
{
    for (QChar c : cardIdentity) {
        if (!commanderIdentity.contains(c)) {
            return false;
        }
    }
    return true;
}

bool formatUsesColorIdentity(const QString &format)
{
    // Matches the singleton, commander-style formats registered in
    // OracleImporter::createDefaultMagicFormats() (kSingletonCounts).
    static const QSet<QString> commanderFamilyFormats = {
        "commander", "duel", "brawl", "standardbrawl", "oathbreaker", "paupercommander", "predh"};
    return commanderFamilyFormats.contains(format.toLower());
}

} // namespace CommanderRules
