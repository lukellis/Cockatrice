#ifndef COCKATRICE_CARD_EFFECTS_H
#define COCKATRICE_CARD_EFFECTS_H

#include <QMap>
#include <QString>

/**
 * @brief The tagged-effect vocabulary shared by this fork's card-ability parsers (see
 * ActivatedAbilities, and any future parser covering triggered/static abilities). Deliberately a
 * flat enum + struct, matching the plain-struct style already used by ManaAbilities::ManaAbility
 * rather than a std::variant -- consistency with this codebase's existing ability types matters
 * more here than using a fancier C++ feature.
 *
 * Stage 1 kinds below are self-contained, targetless effects (apply to the activating player or
 * their own permanent only). Stage 2 adds DealDamage, the first kind needing a target -- see
 * TargetKind below. See COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 section for the staged
 * roadmap this is Stage 2 of.
 */
enum class EffectKind
{
    DrawCards,       // the activating player draws `amount` cards
    GainLife,        // the activating player gains `amount` life
    LoseLife,        // the activating player loses `amount` life
    DealDamage,      // `amount` damage to whichever target was chosen at activation -- see TargetKind
    AddCounterToSelf // reserved for future use -- no parser recognizes this yet
};

/**
 * @brief What kind of target, if any, an effect needs chosen at activation time. Stage 1 effects
 * are all TargetKind::None (self-only). Stage 2 introduces AnyTarget (a permanent or a player,
 * resolved via the client's board-click targeting interaction) for DealDamage -- no finer-grained
 * kinds (e.g. "target creature" only, "target player" only) are recognized yet; see
 * ActivatedAbilities::parse()'s doc comment for the exact phrasing this covers.
 */
enum class TargetKind
{
    None,
    AnyTarget
};

// Fork convention: the per-card counter id reserved for marked damage. Server_Card's counters map
// (libcockatrice_network's server_card.h) is a plain QMap<int,int> with no name field at all, unlike
// per-player Server_Counter (which has a real name, e.g. "life") -- there is no pre-existing
// "damage" counter to reuse the way Stage 1 reused "life", so this fork invents one, the same way
// Phase 9 invented a new named "poison" per-player counter. Shared by client display code and the
// server-side applyPendingAbility() resolution handler (Phase 7 Stage 3) so both agree on the id.
constexpr int DAMAGE_CARD_COUNTER_ID = 0;

struct CardEffect
{
    EffectKind kind;
    int amount = 1;
    TargetKind target = TargetKind::None; // DealDamage sets AnyTarget; every other kind stays None

    bool operator==(const CardEffect &other) const
    {
        return kind == other.kind && amount == other.amount && target == other.target;
    }
};

// Phase 7 Stage 4: a mana-cost prefix on an activated ability (e.g. "{2}{R}" in "{2}{R}, {T}: Deal
// 2 damage to any target."). coloredPips is keyed by the exact mana-pool counter name convention
// ("w"/"u"/"b"/"r"/"g"/"x" -- see Server_Player::setupZones()/RulesEngine::manaCounterNames()), not
// the printed symbol letter, so it's directly usable against a player's counter map with no further
// translation. generic is payable with any leftover mana of any of those six colors, unlike a
// colored pip (including the "x"/colorless pip) which requires an exact-color match. A
// default-constructed ManaCost is free -- every activated ability recognized before Stage 4 stays
// free by construction.
struct ManaCost
{
    QMap<QString, int> coloredPips;
    int generic = 0;

    bool operator==(const ManaCost &other) const
    {
        return coloredPips == other.coloredPips && generic == other.generic;
    }

    [[nodiscard]] bool isFree() const
    {
        return coloredPips.isEmpty() && generic == 0;
    }
};

struct ActivatedAbility
{
    bool requiresTap = true; // Stage 1 only recognizes "{T}: ..." costs, same as ManaAbilities
    CardEffect effect;
    ManaCost cost; // Stage 4: defaults free, same as every ability recognized before it

    bool operator==(const ActivatedAbility &other) const
    {
        return requiresTap == other.requiresTap && effect == other.effect && cost == other.cost;
    }
};

#endif // COCKATRICE_CARD_EFFECTS_H
