#include "spell_mana_cost.h"

#include <QRegularExpression>
#include <libcockatrice/card/card_info.h>

namespace SpellManaCost
{

namespace
{
// Anchoring the *whole* input against zero-or-more tokens is what makes this "exact shape or
// skip": any leftover character anywhere (hybrid "/", Phyrexian "P", "X", "S", the " // "
// split-cost separator) makes the overall match fail, so parse() returns nullopt instead of
// guessing at a partial cost.
const QRegularExpression &fullCostPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(^(?:\{(?:[0-9]+|[WUBRGC])\})*$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &costTokenPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(\{([0-9]+|[WUBRGC])\})"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
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
        const QString symbol = token.toUpper();
        const QString counterName = symbol == QLatin1String("C") ? QStringLiteral("x") : symbol.toLower();
        cost.coloredPips[counterName] = cost.coloredPips.value(counterName, 0) + 1;
    }
    return cost;
}

} // namespace SpellManaCost
