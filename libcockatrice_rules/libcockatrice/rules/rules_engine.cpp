#include "rules_engine.h"

#include <algorithm>
#include <libcockatrice/rules/commander_counter_names.h>
#include <libcockatrice/utility/zone_names.h>

namespace Rules
{

PhaseAutomation RulesEngine::phaseAutomationFor(int phase, int turnNumber, int playerCount)
{
    // Phase indices match the client's phase order (cockatrice/src/game/phase.cpp:
    // Phases::phases[]) -- there's no shared server/client phase enum, so this is inherently
    // coupled to that ordering.
    constexpr int UNTAP_PHASE = 0;
    constexpr int DRAW_PHASE = 2;

    if (phase == UNTAP_PHASE) {
        return PhaseAutomation::UntapActivePlayer;
    }
    if (phase == DRAW_PHASE) {
        // Rule 103.8a/103.8c: only a strict two-player game's starting player skips their
        // first draw step; multiplayer games never skip it.
        bool skipFirstDraw = turnNumber == 1 && playerCount == 2;
        return skipFirstDraw ? PhaseAutomation::None : PhaseAutomation::DrawForActivePlayer;
    }
    return PhaseAutomation::None;
}

bool RulesEngine::isCombatPhase(int phase)
{
    // Phase indices match the client's phase order (cockatrice/src/game/phase.cpp: Phases::phases[]):
    // 4 Beginning of Combat, 5 Declare Attackers, 6 Declare Blockers, 7 Combat Damage, 8 End of Combat.
    constexpr int FIRST_COMBAT_PHASE = 4;
    constexpr int LAST_COMBAT_PHASE = 8;
    return phase >= FIRST_COMBAT_PHASE && phase <= LAST_COMBAT_PHASE;
}

bool RulesEngine::canDeclareBlocker(int phase, bool blockerTapped, bool blockerAttacking)
{
    return phase == DECLARE_BLOCKERS_PHASE && !blockerTapped && !blockerAttacking;
}

bool RulesEngine::participatesInStrikeStep(bool hasFirstStrike, bool hasDoubleStrike, CombatDamageStep step)
{
    if (step == CombatDamageStep::FirstStrike) {
        return hasFirstStrike || hasDoubleStrike;
    }
    return !hasFirstStrike || hasDoubleStrike;
}

RulesEngine::CombatDamageResult RulesEngine::calculateCombatDamage(const QList<CombatAttack> &attacks,
                                                                   CombatDamageStep step)
{
    CombatDamageResult result;

    for (const CombatAttack &attack : attacks) {
        const bool attackerActs =
            participatesInStrikeStep(attack.attacker.hasFirstStrike, attack.attacker.hasDoubleStrike, step);
        const bool isBlocked = attack.blocked || !attack.blockers.isEmpty();

        if (!isBlocked) {
            if (attackerActs && attack.targetPlayerId != -1) {
                result.playerLifeLoss[attack.targetPlayerId] += attack.attacker.power;
            }
            continue;
        }

        int remainingPower = attack.attacker.power;
        int totalBlockerPower = 0;
        bool anyBlockerHasDeathtouch = false;
        for (const CombatCreature &blocker : attack.blockers) {
            const bool blockerActs = participatesInStrikeStep(blocker.hasFirstStrike, blocker.hasDoubleStrike, step);
            if (blockerActs) {
                totalBlockerPower += blocker.power;
                anyBlockerHasDeathtouch = anyBlockerHasDeathtouch || blocker.hasDeathtouch;
            }
            if (!attackerActs || remainingPower <= 0) {
                continue;
            }
            const int neededForLethal = attack.attacker.hasDeathtouch ? 1 : blocker.toughness;
            const int assigned = std::min(remainingPower, neededForLethal);
            result.cardDamageMarked[blocker.playerId][blocker.cardId] += assigned;
            if (assigned > 0 && attack.attacker.hasDeathtouch) {
                result.deathtouchDamaged[blocker.playerId].insert(blocker.cardId);
            }
            remainingPower -= assigned;
        }
        if (attackerActs && remainingPower > 0 && attack.attacker.hasTrample && attack.targetPlayerId != -1) {
            result.playerLifeLoss[attack.targetPlayerId] += remainingPower;
        }

        if (totalBlockerPower > 0) {
            result.cardDamageMarked[attack.attacker.playerId][attack.attacker.cardId] += totalBlockerPower;
            if (anyBlockerHasDeathtouch) {
                result.deathtouchDamaged[attack.attacker.playerId].insert(attack.attacker.cardId);
            }
        }
    }

    return result;
}

bool RulesEngine::isLethallyDamaged(int markedDamage, int toughness, bool anyDamageFromDeathtouch, bool indestructible)
{
    if (indestructible) {
        return false;
    }
    return markedDamage >= toughness || (anyDamageFromDeathtouch && markedDamage > 0);
}

std::optional<std::pair<int, int>> RulesEngine::parseNumericPT(const QString &pt)
{
    const QStringList parts = pt.split(QLatin1Char('/'));
    if (parts.size() != 2) {
        return std::nullopt;
    }

    bool powerOk = false;
    bool toughnessOk = false;
    const int power = parts.at(0).toInt(&powerOk);
    const int toughness = parts.at(1).toInt(&toughnessOk);
    if (!powerOk || !toughnessOk) {
        return std::nullopt;
    }

    return std::make_pair(power, toughness);
}

RulesEngine::StaticEffectResult RulesEngine::applyStaticEffects(int targetCardId,
                                                                int basePower,
                                                                int baseToughness,
                                                                const QSet<QString> &baseKeywords,
                                                                const QList<QPair<int, QString>> &controllerBattlefield)
{
    StaticEffectResult result{basePower, baseToughness, baseKeywords};

    for (const auto &source : controllerBattlefield) {
        const QList<StaticAbilities::StaticAbility> abilities = StaticAbilities::deserialize(source.second);
        for (const StaticAbilities::StaticAbility &ability : abilities) {
            if (ability.scope == StaticAbilities::StaticScope::OthersYours && source.first == targetCardId) {
                continue;
            }
            result.power += ability.powerBonus;
            result.toughness += ability.toughnessBonus;
            result.keywords += ability.grantedKeywords;
        }
    }

    return result;
}

int RulesEngine::nextPriorityPlayer(const QList<int> &playerOrder,
                                    int currentPlayerId,
                                    const QSet<int> &passedPlayers,
                                    const QSet<int> &concededPlayers)
{
    const int n = playerOrder.size();
    if (n == 0) {
        return -1;
    }

    int startIndex = playerOrder.indexOf(currentPlayerId);
    for (int step = 1; step <= n; ++step) {
        int idx = (startIndex + step + n) % n;
        int candidate = playerOrder.at(idx);
        if (candidate == currentPlayerId || concededPlayers.contains(candidate) || passedPlayers.contains(candidate)) {
            continue;
        }
        return candidate;
    }
    return -1;
}

const QStringList &RulesEngine::manaCounterNames()
{
    static const QStringList names{"w", "u", "b", "r", "g", "x"};
    return names;
}

std::optional<QMap<QString, int>> RulesEngine::planManaPayment(const ManaCost &cost, const QMap<QString, int> &pool)
{
    QMap<QString, int> remaining = pool;
    QMap<QString, int> plan;

    for (auto it = cost.coloredPips.constBegin(); it != cost.coloredPips.constEnd(); ++it) {
        const int available = remaining.value(it.key(), 0);
        if (available < it.value()) {
            return std::nullopt;
        }
        remaining[it.key()] = available - it.value();
        plan[it.key()] = plan.value(it.key(), 0) + it.value();
    }

    int genericRemaining = cost.generic;
    for (const QString &name : manaCounterNames()) {
        if (genericRemaining <= 0) {
            break;
        }
        const int available = remaining.value(name, 0);
        if (available <= 0) {
            continue;
        }
        const int used = std::min(available, genericRemaining);
        remaining[name] = available - used;
        plan[name] = plan.value(name, 0) + used;
        genericRemaining -= used;
    }

    if (genericRemaining > 0) {
        return std::nullopt;
    }
    return plan;
}

std::optional<QMap<QString, int>> RulesEngine::remainingPoolAfterColoredPips(const ManaCost &cost,
                                                                             const QMap<QString, int> &pool)
{
    QMap<QString, int> remaining = pool;
    for (auto it = cost.coloredPips.constBegin(); it != cost.coloredPips.constEnd(); ++it) {
        const int available = remaining.value(it.key(), 0);
        if (available < it.value()) {
            return std::nullopt;
        }
        remaining[it.key()] = available - it.value();
    }
    return remaining;
}

bool RulesEngine::isGenericPaymentAmbiguous(const ManaCost &cost, const QMap<QString, int> &pool)
{
    if (cost.generic <= 0) {
        return false;
    }
    const std::optional<QMap<QString, int>> remaining = remainingPoolAfterColoredPips(cost, pool);
    if (!remaining) {
        return false; // unaffordable -- planManaPayment()'s concern, not this function's
    }

    int nonzeroColors = 0;
    int total = 0;
    for (const QString &name : manaCounterNames()) {
        const int available = remaining->value(name, 0);
        if (available > 0) {
            ++nonzeroColors;
            total += available;
        }
    }
    return nonzeroColors >= 2 && total > cost.generic;
}

std::optional<QMap<QString, int>> RulesEngine::planManaPaymentWithGenericChoice(const ManaCost &cost,
                                                                                const QMap<QString, int> &pool,
                                                                                const QMap<QString, int> &genericChoice)
{
    const std::optional<QMap<QString, int>> remaining = remainingPoolAfterColoredPips(cost, pool);
    if (!remaining) {
        return std::nullopt;
    }

    QMap<QString, int> plan;
    for (auto it = cost.coloredPips.constBegin(); it != cost.coloredPips.constEnd(); ++it) {
        plan[it.key()] = plan.value(it.key(), 0) + it.value();
    }

    int total = 0;
    for (auto it = genericChoice.constBegin(); it != genericChoice.constEnd(); ++it) {
        const int amount = it.value();
        if (amount < 0 || amount > remaining->value(it.key(), 0)) {
            return std::nullopt;
        }
        total += amount;
        if (amount > 0) {
            plan[it.key()] = plan.value(it.key(), 0) + amount;
        }
    }

    if (total != cost.generic) {
        return std::nullopt;
    }
    return plan;
}

bool RulesEngine::isCommanderCard(const QString &cardName, const QString &commanderName)
{
    return !commanderName.isEmpty() && cardName == commanderName;
}

std::optional<QString> RulesEngine::commanderTaxCounterNameForMove(const QString &startZoneName,
                                                                   const QString &targetZoneName,
                                                                   const QString &cardName)
{
    if (startZoneName == ZoneNames::COMMAND && targetZoneName != ZoneNames::COMMAND) {
        return CommanderCounterNames::tax(cardName);
    }
    return std::nullopt;
}

int RulesEngine::startPriorityRound(int playerId)
{
    passedBy.clear();
    holder = playerId;
    return holder;
}

PriorityPassResult
RulesEngine::passPriority(int playerId, const QList<int> &playerOrder, const QSet<int> &concededPlayers)
{
    if (playerId != holder) {
        return {false, holder}; // not the current holder -- no-op (server also validates)
    }

    passedBy.insert(playerId);

    int next = nextPriorityPlayer(playerOrder, playerId, passedBy, concededPlayers);
    if (next != -1) {
        holder = next;
        return {true, holder, std::nullopt};
    }

    // Everyone eligible has passed in succession -- the round is exhausted. If something is
    // pending, rule 117.4's stack resolution: pop the top (most recently activated) ability and
    // hand it back to the caller to apply and re-open a fresh round. Otherwise priority simply
    // stops (holder becomes -1) until the next triggering event (a phase change, or a
    // spell/ability going on the stack). Deliberately does NOT auto-advance the phase/turn itself
    // either way.
    passedBy.clear();
    holder = -1;
    if (!pendingAbilities.isEmpty()) {
        PendingAbility resolved = pendingAbilities.takeLast();
        return {true, holder, resolved};
    }
    return {true, holder, std::nullopt};
}

void RulesEngine::clearPriority()
{
    passedBy.clear();
    holder = -1;
}

void RulesEngine::pushPendingAbility(const PendingAbility &ability)
{
    pendingAbilities.append(ability);
}

} // namespace Rules
