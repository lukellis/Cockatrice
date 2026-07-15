#ifndef COMMANDER_COUNTER_NAMES_H
#define COMMANDER_COUNTER_NAMES_H

#include <QString>

/**
 * @brief Naming convention for the per-commander player counters that the server
 * auto-creates for Commander games (see Server_Player::setupZones() and
 * Server_Game::doStartGameIfReady()). Shared so counter creation and lookup code stay in sync.
 */
namespace CommanderCounterNames
{

// Tracks how many times a commander has been cast from the command zone (rule 903.9).
// Displayed count * 2 = the additional generic mana cost.
inline QString tax(const QString &commanderName)
{
    return QStringLiteral("Commander Tax: %1").arg(commanderName);
}

namespace detail
{
inline const QString &damagePrefix()
{
    static const QString prefix = QStringLiteral("Commander Damage: ");
    return prefix;
}
} // namespace detail

// Tracks combat damage a player has been dealt by a specific opposing commander.
// A player loses the game if any single one of these reaches 21 (rule 704.5g).
inline QString damage(const QString &commanderName)
{
    return detail::damagePrefix() + commanderName;
}

inline bool isDamageCounter(const QString &counterName)
{
    return counterName.startsWith(detail::damagePrefix());
}

// Recovers the commander's name from a counter name created by damage(). Only meaningful
// when isDamageCounter() is true for the same string.
inline QString commanderNameFromDamageCounter(const QString &counterName)
{
    return counterName.mid(detail::damagePrefix().length());
}

// Combat damage from a single commander required to lose the game (rule 704.5g).
constexpr int LETHAL_COMMANDER_DAMAGE = 21;

} // namespace CommanderCounterNames

#endif // COMMANDER_COUNTER_NAMES_H
