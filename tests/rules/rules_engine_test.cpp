#include "libcockatrice/rules/rules_engine.h"

#include <gtest/gtest.h>
#include <libcockatrice/rules/commander_counter_names.h>

using Rules::PendingAbility;
using Rules::PhaseAutomation;
using Rules::PriorityPassResult;
using Rules::RulesEngine;

namespace
{

// ---- RulesEngine::phaseAutomationFor (pure decision logic) ----

TEST(RulesEngineTest, UntapPhaseAlwaysUntaps)
{
    EXPECT_EQ(RulesEngine::phaseAutomationFor(0, 1, 2), PhaseAutomation::UntapActivePlayer);
    EXPECT_EQ(RulesEngine::phaseAutomationFor(0, 5, 4), PhaseAutomation::UntapActivePlayer);
}

TEST(RulesEngineTest, TwoPlayerGameSkipsFirstDrawStep)
{
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 1, 2), PhaseAutomation::None);
}

TEST(RulesEngineTest, TwoPlayerGameDrawsNormallyAfterFirstTurn)
{
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 2, 2), PhaseAutomation::DrawForActivePlayer);
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 3, 2), PhaseAutomation::DrawForActivePlayer);
}

TEST(RulesEngineTest, MultiplayerGameNeverSkipsFirstDrawStep)
{
    // Rule 103.8c: only strict two-player games skip the first draw step. Free-for-all
    // Commander (this fork's default) always draws, even on turn 1.
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 1, 3), PhaseAutomation::DrawForActivePlayer);
    EXPECT_EQ(RulesEngine::phaseAutomationFor(2, 1, 4), PhaseAutomation::DrawForActivePlayer);
}

TEST(RulesEngineTest, OtherPhasesHaveNoAutomation)
{
    for (int phase : {1, 3, 4, 5, 6, 7, 8, 9, 10}) {
        EXPECT_EQ(RulesEngine::phaseAutomationFor(phase, 1, 4), PhaseAutomation::None) << phase;
    }
}

TEST(RulesEngineTest, ManaCounterNamesCoversTheFiveColorsPlusColorless)
{
    QStringList names = RulesEngine::manaCounterNames();
    EXPECT_EQ(names.size(), 6);
    for (const QString &color : {"w", "u", "b", "r", "g", "x"}) {
        EXPECT_TRUE(names.contains(color)) << color.toStdString();
    }
}

// ---- RulesEngine::isCombatPhase (Phase 8 Stage 6, pure decision logic) ----

TEST(RulesEngineTest, CombatPhasesAreRecognized)
{
    for (int phase : {4, 5, 6, 7, 8}) {
        EXPECT_TRUE(RulesEngine::isCombatPhase(phase)) << phase;
    }
}

TEST(RulesEngineTest, NonCombatPhasesAreNotRecognized)
{
    for (int phase : {0, 1, 2, 3, 9, 10}) {
        EXPECT_FALSE(RulesEngine::isCombatPhase(phase)) << phase;
    }
}

TEST(RulesEngineTest, OutOfRangePhasesAreNotCombatPhases)
{
    EXPECT_FALSE(RulesEngine::isCombatPhase(-1));
    EXPECT_FALSE(RulesEngine::isCombatPhase(100));
}

// ---- RulesEngine::planManaPayment (Phase 7 Stage 4, pure decision logic) ----

TEST(RulesEngineTest, PlanManaPaymentPaysExactColoredPips)
{
    ManaCost cost;
    cost.coloredPips = {{"r", 2}};
    QMap<QString, int> pool{{"r", 3}, {"w", 1}};

    auto plan = RulesEngine::planManaPayment(cost, pool);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->value("r"), 2);
    EXPECT_FALSE(plan->contains("w"));
}

TEST(RulesEngineTest, PlanManaPaymentFailsWhenColoredPipUnavailable)
{
    ManaCost cost;
    cost.coloredPips = {{"u", 1}};
    QMap<QString, int> pool{{"r", 5}};

    EXPECT_FALSE(RulesEngine::planManaPayment(cost, pool).has_value());
}

TEST(RulesEngineTest, PlanManaPaymentDrainsGenericFromLeftoverPoolInFixedOrder)
{
    ManaCost cost;
    cost.generic = 3;
    QMap<QString, int> pool{{"w", 0}, {"u", 1}, {"b", 0}, {"r", 5}, {"g", 0}, {"x", 0}};

    // manaCounterNames() order is w, u, b, r, g, x -- u has 1 available so it's drained first,
    // then the remaining 2 comes from r.
    auto plan = RulesEngine::planManaPayment(cost, pool);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->value("u"), 1);
    EXPECT_EQ(plan->value("r"), 2);
    EXPECT_FALSE(plan->contains("w"));
}

TEST(RulesEngineTest, PlanManaPaymentFailsWhenGenericCannotBeFullyPaid)
{
    ManaCost cost;
    cost.generic = 5;
    QMap<QString, int> pool{{"r", 2}};

    EXPECT_FALSE(RulesEngine::planManaPayment(cost, pool).has_value());
}

TEST(RulesEngineTest, PlanManaPaymentColoredThenGenericTogether)
{
    ManaCost cost;
    cost.coloredPips = {{"r", 1}};
    cost.generic = 1;
    QMap<QString, int> pool{{"r", 1}, {"w", 1}};

    // The colored pip consumes the only red, leaving white to cover the generic amount.
    auto plan = RulesEngine::planManaPayment(cost, pool);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->value("r"), 1);
    EXPECT_EQ(plan->value("w"), 1);
}

TEST(RulesEngineTest, PlanManaPaymentFreeCostAlwaysSucceedsWithEmptyPlan)
{
    ManaCost cost;
    QMap<QString, int> pool; // empty pool

    auto plan = RulesEngine::planManaPayment(cost, pool);
    ASSERT_TRUE(plan.has_value());
    EXPECT_TRUE(plan->isEmpty());
}

// ---- RulesEngine::isCommanderCard / commanderTaxCounterNameForMove (command-zone decisions) ----

TEST(RulesEngineTest, IsCommanderCardMatchesTheBannerCardByName)
{
    EXPECT_TRUE(RulesEngine::isCommanderCard("Atraxa, Praetors' Voice", "Atraxa, Praetors' Voice"));
}

TEST(RulesEngineTest, IsCommanderCardRejectsANonMatchingCard)
{
    EXPECT_FALSE(RulesEngine::isCommanderCard("Sol Ring", "Atraxa, Praetors' Voice"));
}

TEST(RulesEngineTest, IsCommanderCardRejectsEverythingWhenNoCommanderIsSet)
{
    EXPECT_FALSE(RulesEngine::isCommanderCard("Sol Ring", ""));
    EXPECT_FALSE(RulesEngine::isCommanderCard("", ""));
}

TEST(RulesEngineTest, CommanderTaxCounterNameForMoveFiresWhenLeavingTheCommandZone)
{
    auto name = RulesEngine::commanderTaxCounterNameForMove("command", "table", "Atraxa, Praetors' Voice");
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, CommanderCounterNames::tax("Atraxa, Praetors' Voice"));
}

TEST(RulesEngineTest, CommanderTaxCounterNameForMoveIsNulloptWhenNotLeavingTheCommandZone)
{
    EXPECT_FALSE(RulesEngine::commanderTaxCounterNameForMove("hand", "table", "Sol Ring").has_value());
}

TEST(RulesEngineTest, CommanderTaxCounterNameForMoveIsNulloptForAMoveBackIntoTheCommandZone)
{
    // e.g. an undo -- moving straight back to the command zone isn't a cast.
    EXPECT_FALSE(RulesEngine::commanderTaxCounterNameForMove("command", "command", "Atraxa").has_value());
}

// ---- RulesEngine::nextPriorityPlayer (pure decision logic) ----

TEST(RulesEngineTest, NextPriorityPlayerAdvancesToNextInOrder)
{
    QList<int> order{1, 2, 3, 4};
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {}, {}), 2);
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 2, {}, {}), 3);
}

TEST(RulesEngineTest, NextPriorityPlayerWrapsAroundTheTable)
{
    QList<int> order{1, 2, 3, 4};
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 4, {}, {}), 1);
}

TEST(RulesEngineTest, NextPriorityPlayerSkipsPassedPlayers)
{
    QList<int> order{1, 2, 3, 4};
    // 2 already passed this round; 1 just passed too, so priority should skip to 3.
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {2}, {}), 3);
}

TEST(RulesEngineTest, NextPriorityPlayerSkipsConcededPlayers)
{
    QList<int> order{1, 2, 3, 4};
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {}, {2}), 3);
}

TEST(RulesEngineTest, NextPriorityPlayerReturnsNegativeOneWhenEveryoneElseIsIneligible)
{
    QList<int> order{1, 2, 3, 4};
    // Everyone but the passing player (1) has either passed or conceded.
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {3}, {2, 4}), -1);
}

TEST(RulesEngineTest, NextPriorityPlayerReturnsNegativeOneForSoloPlayer)
{
    QList<int> order{1};
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {}, {}), -1);
}

TEST(RulesEngineTest, NextPriorityPlayerReturnsNegativeOneForEmptyOrder)
{
    EXPECT_EQ(RulesEngine::nextPriorityPlayer({}, 1, {}, {}), -1);
}

TEST(RulesEngineTest, NextPriorityPlayerNeverReturnsThePassingPlayerItself)
{
    QList<int> order{1, 2, 3};
    // 2 and 3 both conceded; only 1 (the one passing) remains "eligible" by the naive
    // pass/concede check, but must never be returned as its own next priority holder.
    EXPECT_EQ(RulesEngine::nextPriorityPlayer(order, 1, {}, {2, 3}), -1);
}

// ---- RulesEngine priority-round state machine (newly testable end-to-end) ----

TEST(RulesEngineTest, StartsWithNoOneHoldingPriority)
{
    RulesEngine engine;
    EXPECT_EQ(engine.priorityHolder(), -1);
}

TEST(RulesEngineTest, StartPriorityRoundSetsTheHolder)
{
    RulesEngine engine;
    EXPECT_EQ(engine.startPriorityRound(2), 2);
    EXPECT_EQ(engine.priorityHolder(), 2);
}

TEST(RulesEngineTest, PassPriorityAdvancesAroundTheTable)
{
    RulesEngine engine;
    QList<int> order{1, 2, 3, 4};
    engine.startPriorityRound(1);

    PriorityPassResult r = engine.passPriority(1, order, {});
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.holder, 2);
    EXPECT_EQ(engine.priorityHolder(), 2);

    r = engine.passPriority(2, order, {});
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.holder, 3);
}

TEST(RulesEngineTest, PassPriorityStopsTheRoundWhenEveryoneHasPassed)
{
    RulesEngine engine;
    QList<int> order{1, 2};
    engine.startPriorityRound(1);

    EXPECT_EQ(engine.passPriority(1, order, {}).holder, 2);   // 1 passes -> 2
    PriorityPassResult r = engine.passPriority(2, order, {}); // 2 passes -> round exhausted
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.holder, -1);
    EXPECT_EQ(engine.priorityHolder(), -1);
}

TEST(RulesEngineTest, SoloPlayerPassingStopsTheRoundImmediately)
{
    // The solo/last-player-standing case that previously caused an auto-pass infinite loop:
    // passing hands priority to no one, not back to yourself.
    RulesEngine engine;
    QList<int> order{1};
    engine.startPriorityRound(1);

    PriorityPassResult r = engine.passPriority(1, order, {});
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.holder, -1);
}

TEST(RulesEngineTest, PassPriorityFromANonHolderIsANoOp)
{
    RulesEngine engine;
    QList<int> order{1, 2, 3};
    engine.startPriorityRound(1);

    PriorityPassResult r = engine.passPriority(2, order, {}); // 2 doesn't hold priority
    EXPECT_FALSE(r.changed);
    EXPECT_EQ(r.holder, 1);
    EXPECT_EQ(engine.priorityHolder(), 1);
}

TEST(RulesEngineTest, StartingANewRoundClearsPreviousPasses)
{
    RulesEngine engine;
    QList<int> order{1, 2, 3};
    engine.startPriorityRound(1);
    engine.passPriority(1, order, {}); // now 2 holds, 1 has passed

    // A new trigger (phase change / spell) restarts at 3; 1's earlier pass must not carry over.
    engine.startPriorityRound(3);
    EXPECT_EQ(engine.passPriority(3, order, {}).holder, 1); // 1 is eligible again
}

TEST(RulesEngineTest, ClearPriorityResetsToNoOne)
{
    RulesEngine engine;
    engine.startPriorityRound(2);
    engine.clearPriority();
    EXPECT_EQ(engine.priorityHolder(), -1);
}

// ---- Phase 7 Stage 3: pending-ability stack ----

TEST(RulesEngineTest, HasPendingAbilitiesStartsFalse)
{
    RulesEngine engine;
    EXPECT_FALSE(engine.hasPendingAbilities());
}

TEST(RulesEngineTest, PushPendingAbilityMakesHasPendingAbilitiesTrue)
{
    RulesEngine engine;
    PendingAbility ability;
    ability.controllerId = 1;
    ability.effect.kind = EffectKind::DrawCards;
    ability.effect.amount = 1;
    engine.pushPendingAbility(ability);
    EXPECT_TRUE(engine.hasPendingAbilities());
}

TEST(RulesEngineTest, ExhaustingARoundWithNoPendingAbilitiesBehavesAsBefore)
{
    // Pure regression check: the untouched (no pending abilities) path must be byte-for-byte
    // identical to before this stage -- holder -1, no resolved ability.
    RulesEngine engine;
    QList<int> order{1, 2};
    engine.startPriorityRound(1);

    engine.passPriority(1, order, {});                        // 1 passes -> 2
    PriorityPassResult r = engine.passPriority(2, order, {}); // 2 passes -> exhausted
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.holder, -1);
    EXPECT_FALSE(r.resolvedAbility.has_value());
}

TEST(RulesEngineTest, ExhaustingARoundWithOnePendingAbilityResolvesIt)
{
    RulesEngine engine;
    QList<int> order{1, 2};
    engine.startPriorityRound(1);

    PendingAbility ability;
    ability.controllerId = 1;
    ability.effect.kind = EffectKind::GainLife;
    ability.effect.amount = 3;
    engine.pushPendingAbility(ability);

    engine.passPriority(1, order, {});                        // 1 passes -> 2
    PriorityPassResult r = engine.passPriority(2, order, {}); // 2 passes -> exhausted, resolves
    EXPECT_TRUE(r.changed);
    ASSERT_TRUE(r.resolvedAbility.has_value());
    EXPECT_EQ(r.resolvedAbility->controllerId, 1);
    EXPECT_EQ(r.resolvedAbility->effect.kind, EffectKind::GainLife);
    EXPECT_EQ(r.resolvedAbility->effect.amount, 3);
    EXPECT_FALSE(engine.hasPendingAbilities());
}

TEST(RulesEngineTest, ExhaustingTwiceResolvesInLifoOrder)
{
    // Rule 608.1/117.4: the most recently activated ability resolves first. Two abilities pushed
    // before either resolves must come back out in reverse (B, then A) across two separate
    // exhaustions, not the order they were pushed.
    RulesEngine engine;
    QList<int> order{1, 2};
    engine.startPriorityRound(1);

    PendingAbility abilityA;
    abilityA.controllerId = 1;
    abilityA.effect.kind = EffectKind::DrawCards;
    abilityA.effect.amount = 1;
    engine.pushPendingAbility(abilityA);

    PendingAbility abilityB;
    abilityB.controllerId = 2;
    abilityB.effect.kind = EffectKind::LoseLife;
    abilityB.effect.amount = 2;
    engine.pushPendingAbility(abilityB);

    // First exhaustion resolves B (most recently pushed).
    engine.startPriorityRound(1);
    engine.passPriority(1, order, {});
    PriorityPassResult firstResolve = engine.passPriority(2, order, {});
    ASSERT_TRUE(firstResolve.resolvedAbility.has_value());
    EXPECT_EQ(firstResolve.resolvedAbility->effect.kind, EffectKind::LoseLife);
    EXPECT_TRUE(engine.hasPendingAbilities()); // A is still pending

    // Second exhaustion resolves A.
    engine.startPriorityRound(1);
    engine.passPriority(1, order, {});
    PriorityPassResult secondResolve = engine.passPriority(2, order, {});
    ASSERT_TRUE(secondResolve.resolvedAbility.has_value());
    EXPECT_EQ(secondResolve.resolvedAbility->effect.kind, EffectKind::DrawCards);
    EXPECT_FALSE(engine.hasPendingAbilities());
}

} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
