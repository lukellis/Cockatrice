#ifndef COCKATRICE_STATIC_ABILITIES_H
#define COCKATRICE_STATIC_ABILITIES_H

#include <QList>
#include <QSet>
#include <QString>

class CardInfo;

/**
 * @brief The first slice of static/continuous card abilities -- effects a permanent applies to
 * *other* permanents on the battlefield, as opposed to every ability shape recognized so far
 * (ActivatedAbilities, TriggeredAbilities), which only ever affects the activating/triggering
 * card's own controller or a chosen target. See COMMANDER_IMPLEMENTATION_STATUS.md and
 * doc/commander-status/phase8-static-abilities.md for the staged roadmap this is part of.
 *
 * Deliberately narrow, matching this fork's "exact shape or skip" conservatism (see
 * CardKeywords::parse()): only two independent, whole-line phrasings are recognized, both scoped
 * to "creatures you control" / "other creatures you control" -- no creature-type/tribal
 * restriction, no combined single-line phrasing ("get +1/+1 and have vigilance"), no effects on
 * an opponent's creatures, no conditional/until-end-of-turn text.
 */
namespace StaticAbilities
{

/**
 * @brief Whether a StaticAbility's line said "Creatures you control" (AllYours -- the source
 * itself is included, if it's a creature) or "Other creatures you control" (OthersYours -- the
 * source excludes itself).
 */
enum class StaticScope
{
    AllYours,
    OthersYours
};

struct StaticAbility
{
    StaticScope scope = StaticScope::OthersYours;
    int powerBonus = 0;
    int toughnessBonus = 0;
    QSet<QString> grantedKeywords;

    bool operator==(const StaticAbility &other) const
    {
        return scope == other.scope && powerBonus == other.powerBonus && toughnessBonus == other.toughnessBonus &&
               grantedKeywords == other.grantedKeywords;
    }
};

/**
 * @brief Parses @p card's rules text for standalone static-ability lines. Recognizes two
 * independent whole-line shapes (a line matching neither is simply not recognized, never
 * partially guessed at):
 *   - "(Other creatures you control|Creatures you control) get +X/+Y." -- a P/T-boost ability.
 *   - "(Other creatures you control|Creatures you control) have <keyword list>." -- a
 *     keyword-granting ability, tokenized/canonicalized the same conservative way as
 *     CardKeywords::parse() (every comma/"and"-separated token must be a recognized evergreen
 *     keyword, or the whole line is skipped).
 * A card with multiple qualifying lines produces one StaticAbility per line.
 */
QList<StaticAbility> parse(const CardInfo &card);

/**
 * @brief Serializes @p abilities for the wire -- see AttrStaticAbilities in
 * card_attributes.proto. One ability per ';'-joined entry, fields '|'-joined:
 * "<a|o>|<powerBonus>|<toughnessBonus>|<kw1>,<kw2>".
 */
QString serialize(const QList<StaticAbility> &abilities);

/** @brief Inverse of serialize(). Malformed/empty entries are silently skipped. */
QList<StaticAbility> deserialize(const QString &s);

} // namespace StaticAbilities

#endif // COCKATRICE_STATIC_ABILITIES_H
