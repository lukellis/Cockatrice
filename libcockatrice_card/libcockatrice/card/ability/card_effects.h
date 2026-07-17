#ifndef COCKATRICE_CARD_EFFECTS_H
#define COCKATRICE_CARD_EFFECTS_H

/**
 * @brief The tagged-effect vocabulary shared by this fork's card-ability parsers (see
 * ActivatedAbilities, and any future parser covering triggered/static abilities). Deliberately a
 * flat enum + struct, matching the plain-struct style already used by ManaAbilities::ManaAbility
 * rather than a std::variant -- consistency with this codebase's existing ability types matters
 * more here than using a fancier C++ feature.
 *
 * Each kind below is a self-contained, targetless effect (applies to the activating player or
 * their own permanent only) -- this is Stage 1 of the card-ability execution engine; targeted
 * effects (deal damage to a target, destroy target permanent, etc.) need a TargetSpec this IR
 * doesn't have yet. See COMMANDER_IMPLEMENTATION_STATUS.md's Phase 7 section for the staged
 * roadmap this is Stage 1 of.
 */
enum class EffectKind
{
    DrawCards,       // the activating player draws `amount` cards
    GainLife,        // the activating player gains `amount` life
    LoseLife,        // the activating player loses `amount` life
    AddCounterToSelf // reserved for future use -- no parser recognizes this yet
};

struct CardEffect
{
    EffectKind kind;
    int amount = 1;

    bool operator==(const CardEffect &other) const
    {
        return kind == other.kind && amount == other.amount;
    }
};

struct ActivatedAbility
{
    bool requiresTap = true; // Stage 1 only recognizes "{T}: ..." costs, same as ManaAbilities
    CardEffect effect;

    bool operator==(const ActivatedAbility &other) const
    {
        return requiresTap == other.requiresTap && effect == other.effect;
    }
};

#endif // COCKATRICE_CARD_EFFECTS_H
