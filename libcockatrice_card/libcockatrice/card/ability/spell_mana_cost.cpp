#include "spell_mana_cost.h"

#include <QRegularExpression>
#include <libcockatrice/card/card_info.h>

namespace SpellManaCost
{

namespace
{
// Anchoring the *whole* input against zero-or-more tokens is what makes this "exact shape or
// skip": any leftover character anywhere (a monocolored-hybrid digit before "/", Phyrexian "P",
// "S", the " // " split-cost separator) makes the overall match fail, so parse() returns nullopt
// instead of guessing at a partial cost. Two-color hybrid ("W/U"), Phyrexian ("W/P"), and bare "X"
// are all accepted tokens now (phase6-mana.md's addendum) -- monocolored hybrid ("2/W") still isn't,
// since "2" isn't in the [WUBRG] class on either side of a hybrid/Phyrexian token.
const QRegularExpression &fullCostPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^(?:\{(?:[0-9]+|X|[WUBRG]/[WUBRG]|[WUBRG]/P|[WUBRGC])\})*$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &costTokenPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(\{([0-9]+|X|[WUBRG]/[WUBRG]|[WUBRG]/P|[WUBRGC])\})"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// C -> "x" (colorless mana pool counter), everything else just lowercased -- the same mapping
// used on every side of every token shape below.
QString counterNameForSymbol(const QString &symbol)
{
    const QString upper = symbol.toUpper();
    return upper == QLatin1String("C") ? QStringLiteral("x") : upper.toLower();
}
} // namespace

std::optional<ManaCost> parse(const CardInfo &card)
{
    const QString text = card.getManaCost();
    if (!fullCostPattern().match(text).hasMatch()) {
        return std::nullopt;
    }

    ManaCost cost;
    auto it = costTokenPattern().globalMatch(text);
    while (it.hasNext()) {
        const QString token = it.next().captured(1);
        bool isNumber = false;
        const int value = token.toInt(&isNumber);
        if (isNumber) {
            cost.generic += value;
            continue;
        }
        if (token.compare(QLatin1String("X"), Qt::CaseInsensitive) == 0) {
            ++cost.xCount;
            continue;
        }
        const int slashIndex = token.indexOf(QLatin1Char('/'));
        if (slashIndex >= 0) {
            const QString left = token.left(slashIndex);
            const QString right = token.mid(slashIndex + 1);
            if (right.compare(QLatin1String("P"), Qt::CaseInsensitive) == 0) {
                cost.phyrexianPips.append(counterNameForSymbol(left));
            } else {
                cost.hybridPips.append({counterNameForSymbol(left), counterNameForSymbol(right)});
            }
            continue;
        }
        const QString counterName = counterNameForSymbol(token);
        cost.coloredPips[counterName] = cost.coloredPips.value(counterName, 0) + 1;
    }
    return cost;
}

} // namespace SpellManaCost
