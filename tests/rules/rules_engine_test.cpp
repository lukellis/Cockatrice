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

// ---- RulesEngine::canDeclareBlocker (Phase 8 combat automation Stage A, pure decision logic) ----

TEST(RulesEngineTest, BlockerCanBeDeclaredInDeclareBlockersPhaseWhenUntappedAndNotAttacking)
{
    EXPECT_TRUE(RulesEngine::canDeclareBlocker(RulesEngine::DECLARE_BLOCKERS_PHASE, false, false));
}

TEST(RulesEngineTest, BlockerCannotBeDeclaredOutsideDeclareBlockersPhase)
{
    for (int phase : {0, 1, 2, 3, 4, 5, 7, 8, 9}) {
        EXPECT_FALSE(RulesEngine::canDeclareBlocker(phase, false, false)) << phase;
    }
}

TEST(RulesEngineTest, TappedCreatureCannotBeDeclaredAsBlocker)
{
    EXPECT_FALSE(RulesEngine::canDeclareBlocker(RulesEngine::DECLARE_BLOCKERS_PHASE, true, false));
}

TEST(RulesEngineTest, AttackingCreatureCannotAlsoBeDeclaredAsBlocker)
{
    EXPECT_FALSE(RulesEngine::canDeclareBlocker(RulesEngine::DECLARE_BLOCKERS_PHASE, false, true));
}

// ---- RulesEngine::canDeclareAttacker (turn-structure enforcement, pure decision logic) ----

TEST(RulesEngineTest, AttackerCanBeDeclaredInDeclareAttackersPhaseByActivePlayer)
{
    EXPECT_TRUE(RulesEngine::canDeclareAttacker(RulesEngine::DECLARE_ATTACKERS_PHASE, true));
}

TEST(RulesEngineTest, AttackerCannotBeDeclaredOutsideDeclareAttackersPhase)
{
    for (int phase : {0, 1, 2, 3, 4, 6, 7, 8, 9, 10}) {
        EXPECT_FALSE(RulesEngine::canDeclareAttacker(phase, true)) << phase;
    }
}

TEST(RulesEngineTest, NonActivePlayerCannotDeclareAnAttacker)
{
    EXPECT_FALSE(RulesEngine::canDeclareAttacker(RulesEngine::DECLARE_ATTACKERS_PHASE, false));
}

// ---- RulesEngine::canAdvanceToPhase / canEndTurn (turn-structure enforcement) ----

TEST(RulesEngineTest, CanAdvanceToPhaseAcceptsExactlyOneStepForward)
{
    for (int phase = 0; phase < RulesEngine::PHASE_COUNT - 1; ++phase) {
        EXPECT_TRUE(RulesEngine::canAdvanceToPhase(phase, phase + 1)) << phase;
    }
}

TEST(RulesEngineTest, CanAdvanceToPhaseRejectsSkippingAhead)
{
    EXPECT_FALSE(RulesEngine::canAdvanceToPhase(3, 5));
}

TEST(RulesEngineTest, CanAdvanceToPhaseRejectsGoingBackward)
{
    EXPECT_FALSE(RulesEngine::canAdvanceToPhase(5, 3));
}

TEST(RulesEngineTest, CanAdvanceToPhaseRejectsStayingPut)
{
    EXPECT_FALSE(RulesEngine::canAdvanceToPhase(5, 5));
}

TEST(RulesEngineTest, CanAdvanceToPhaseRejectsWrappingToPhaseZero)
{
    EXPECT_FALSE(RulesEngine::canAdvanceToPhase(RulesEngine::CLEANUP_PHASE, 0));
}

TEST(RulesEngineTest, CanEndTurnOnlyFromCleanupPhase)
{
    EXPECT_TRUE(RulesEngine::canEndTurn(RulesEngine::CLEANUP_PHASE));
    for (int phase = 0; phase < RulesEngine::CLEANUP_PHASE; ++phase) {
        EXPECT_FALSE(RulesEngine::canEndTurn(phase)) << phase;
    }
}

// ---- RulesEngine::canCastSorcerySpeed (turn-structure enforcement) ----

TEST(RulesEngineTest, SorcerySpeedCastLegalInEitherMainPhaseForActivePlayerHoldingPriority)
{
    EXPECT_TRUE(RulesEngine::canCastSorcerySpeed(RulesEngine::FIRST_MAIN_PHASE, true, true));
    EXPECT_TRUE(RulesEngine::canCastSorcerySpeed(RulesEngine::SECOND_MAIN_PHASE, true, true));
}

TEST(RulesEngineTest, SorcerySpeedCastIllegalOutsideMainPhases)
{
    for (int phase : {0, 1, 2, 4, 5, 6, 7, 8, 10}) {
        EXPECT_FALSE(RulesEngine::canCastSorcerySpeed(phase, true, true)) << phase;
    }
}

TEST(RulesEngineTest, SorcerySpeedCastIllegalForNonActivePlayer)
{
    EXPECT_FALSE(RulesEngine::canCastSorcerySpeed(RulesEngine::FIRST_MAIN_PHASE, false, true));
}

TEST(RulesEngineTest, SorcerySpeedCastIllegalWithoutHoldingPriority)
{
    EXPECT_FALSE(RulesEngine::canCastSorcerySpeed(RulesEngine::FIRST_MAIN_PHASE, true, false));
}

// ---- RulesEngine::parseNumericPT (Phase 8 combat automation Stage B, pure decision logic) ----

TEST(RulesEngineTest, ParseNumericPTParsesPlainIntegers)
{
    auto pt = RulesEngine::parseNumericPT("3/4");
    ASSERT_TRUE(pt.has_value());
    EXPECT_EQ(pt->first, 3);
    EXPECT_EQ(pt->second, 4);
}

TEST(RulesEngineTest, ParseNumericPTRejectsCharacteristicDefiningPT)
{
    EXPECT_FALSE(RulesEngine::parseNumericPT("*/1+*").has_value());
    EXPECT_FALSE(RulesEngine::parseNumericPT("X/X").has_value());
}

TEST(RulesEngineTest, ParseNumericPTRejectsMalformedStrings)
{
    EXPECT_FALSE(RulesEngine::parseNumericPT("").has_value());
    EXPECT_FALSE(RulesEngine::parseNumericPT("3").has_value());
    EXPECT_FALSE(RulesEngine::parseNumericPT("3/4/5").has_value());
}

// ---- RulesEngine::applyStaticEffects (static/continuous ability slice, pure decision logic) ----

TEST(RulesEngineTest, NoSourcesLeavesBaseCharacteristicsUnchanged)
{
    auto result = RulesEngine::applyStaticEffects(1, 2, 2, {"Flying"}, {});
    EXPECT_EQ(result.power, 2);
    EXPECT_EQ(result.toughness, 2);
    EXPECT_EQ(result.keywords, QSet<QString>({"Flying"}));
}

TEST(RulesEngineTest, OthersYoursAnthemBoostsAnotherCreatureButNotItself)
{
    // Card 10 is the anthem source ("Other creatures you control get +1/+1."); card 20 is a
    // different creature the same controller controls.
    QList<QPair<int, QString>> battlefield = {{10, "o|1|1|"}, {20, ""}};

    auto boosted = RulesEngine::applyStaticEffects(20, 2, 2, {}, battlefield);
    EXPECT_EQ(boosted.power, 3);
    EXPECT_EQ(boosted.toughness, 3);

    auto sourceItself = RulesEngine::applyStaticEffects(10, 2, 2, {}, battlefield);
    EXPECT_EQ(sourceItself.power, 2);
    EXPECT_EQ(sourceItself.toughness, 2);
}

TEST(RulesEngineTest, AllYoursAnthemBoostsItsOwnSourceToo)
{
    QList<QPair<int, QString>> battlefield = {{10, "a|1|1|"}};
    auto result = RulesEngine::applyStaticEffects(10, 2, 2, {}, battlefield);
    EXPECT_EQ(result.power, 3);
    EXPECT_EQ(result.toughness, 3);
}

TEST(RulesEngineTest, MultipleAnthemsStack)
{
    QList<QPair<int, QString>> battlefield = {{10, "o|1|1|"}, {11, "o|1|0|"}};
    auto result = RulesEngine::applyStaticEffects(20, 2, 2, {}, battlefield);
    EXPECT_EQ(result.power, 4);
    EXPECT_EQ(result.toughness, 3);
}

TEST(RulesEngineTest, KeywordGrantExtendsTheEffectiveKeywordSet)
{
    QList<QPair<int, QString>> battlefield = {{10, "o|0|0|Trample"}};
    auto result = RulesEngine::applyStaticEffects(20, 2, 2, {"Deathtouch"}, battlefield);
    EXPECT_EQ(result.keywords, QSet<QString>({"Deathtouch", "Trample"}));
}

TEST(RulesEngineTest, OthersYoursKeywordGrantDoesNotApplyToItsOwnSource)
{
    QList<QPair<int, QString>> battlefield = {{10, "o|0|0|Trample"}};
    auto result = RulesEngine::applyStaticEffects(10, 2, 2, {}, battlefield);
    EXPECT_TRUE(result.keywords.isEmpty());
}

// ---- RulesEngine::calculateCombatDamage (Phase 8 combat automation Stage B, pure decision logic) ----

using CombatAttack = RulesEngine::CombatAttack;
using CombatCreature = RulesEngine::CombatCreature;

TEST(RulesEngineTest, UnblockedAttackerWithTargetDealsDamageToPlayer)
{
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 3, 3};
    attack.targetPlayerId = 2;

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_EQ(result.playerLifeLoss.value(2), 3);
    EXPECT_TRUE(result.cardDamageMarked.isEmpty());
}

TEST(RulesEngineTest, UnblockedAttackerWithNoTargetDealsNoDamage)
{
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 3, 3};
    attack.targetPlayerId = -1;

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_TRUE(result.playerLifeLoss.isEmpty());
}

TEST(RulesEngineTest, MultipleUnblockedAttackersOnSamePlayerAccumulate)
{
    CombatAttack attackA;
    attackA.attacker = CombatCreature{1, 100, 3, 3};
    attackA.targetPlayerId = 2;
    CombatAttack attackB;
    attackB.attacker = CombatCreature{1, 101, 2, 2};
    attackB.targetPlayerId = 2;

    auto result = RulesEngine::calculateCombatDamage({attackA, attackB});
    EXPECT_EQ(result.playerLifeLoss.value(2), 5);
}

TEST(RulesEngineTest, SingleBlockerExchangesDamageWithAttacker)
{
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 3, 4};
    attack.targetPlayerId = 2;
    attack.blockers = {CombatCreature{2, 200, 2, 3}};

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_TRUE(result.playerLifeLoss.isEmpty());
    EXPECT_EQ(result.cardDamageMarked.value(2).value(200), 3); // blocker takes attacker's full power
    EXPECT_EQ(result.cardDamageMarked.value(1).value(100), 2); // attacker takes blocker's power
}

TEST(RulesEngineTest, MultipleBlockersSplitDamageInDeclarationOrderNoTrample)
{
    // A 5-power attacker blocked by two 2-toughness creatures: each blocker in declaration order
    // is assigned exactly its own toughness (2 each, rule 510.1c simplified to "assign lethal,
    // move on") -- the 1 leftover power (5 - 2 - 2) is simply wasted, not trampled through to the
    // player, since this attacker doesn't have trample (see the Trample-specific tests below for
    // the case where it does).
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 5, 4};
    attack.targetPlayerId = 2;
    attack.blockers = {CombatCreature{2, 200, 1, 2}, CombatCreature{2, 201, 1, 2}};

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_TRUE(result.playerLifeLoss.isEmpty());
    EXPECT_EQ(result.cardDamageMarked.value(2).value(200), 2);
    EXPECT_EQ(result.cardDamageMarked.value(2).value(201), 2);
    EXPECT_EQ(result.cardDamageMarked.value(1).value(100), 2); // sum of both blockers' power
}

TEST(RulesEngineTest, BlockerPowerLessThanAttackerLeavesRemainderUnassigned)
{
    // A 2-power attacker blocked by a single 5-toughness creature: only 2 damage is assigned (no
    // more than the attacker's power), nothing wasted or overflowed elsewhere.
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 2, 4};
    attack.targetPlayerId = 2;
    attack.blockers = {CombatCreature{2, 200, 1, 5}};

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_EQ(result.cardDamageMarked.value(2).value(200), 2);
}

// ---- RulesEngine::calculateCombatDamage / isLethallyDamaged (Phase 8 combat automation Stage C:
// deathtouch, trample, indestructible) ----

TEST(RulesEngineTest, DeathtouchAttackerAssignsOnlyOneDamagePerBlocker)
{
    // A 6-power deathtouch attacker facing two high-toughness blockers only needs to assign 1
    // damage to each to be considered lethal (rule 702.2b) -- with no trample, the remaining 4
    // power (6 - 1 - 1) is simply wasted, same as any other non-trample leftover.
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 6, 4, /*hasDeathtouch=*/true, /*hasTrample=*/false};
    attack.targetPlayerId = 2;
    attack.blockers = {CombatCreature{2, 200, 1, 9}, CombatCreature{2, 201, 1, 9}};

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_TRUE(result.playerLifeLoss.isEmpty());
    EXPECT_EQ(result.cardDamageMarked.value(2).value(200), 1);
    EXPECT_EQ(result.cardDamageMarked.value(2).value(201), 1);
    EXPECT_TRUE(result.deathtouchDamaged.value(2).contains(200));
    EXPECT_TRUE(result.deathtouchDamaged.value(2).contains(201));
}

TEST(RulesEngineTest, DeathtouchBlockerMarksAttackerAsDeathtouchDamaged)
{
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 3, 9};
    attack.targetPlayerId = 2;
    attack.blockers = {CombatCreature{2, 200, 1, 3, /*hasDeathtouch=*/true, /*hasTrample=*/false}};

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_EQ(result.cardDamageMarked.value(1).value(100), 1);
    EXPECT_TRUE(result.deathtouchDamaged.value(1).contains(100));
}

TEST(RulesEngineTest, TrampleAttackerSendsExcessPowerToDefendingPlayer)
{
    // A 5-power trample attacker blocked by a single 2-toughness creature: 2 damage is lethal, the
    // remaining 3 tramples through to the defending player (rule 702.19b).
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 5, 4, /*hasDeathtouch=*/false, /*hasTrample=*/true};
    attack.targetPlayerId = 2;
    attack.blockers = {CombatCreature{2, 200, 1, 2}};

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_EQ(result.cardDamageMarked.value(2).value(200), 2);
    EXPECT_EQ(result.playerLifeLoss.value(2), 3);
}

TEST(RulesEngineTest, TrampleWithNoTargetDealsNoDamage)
{
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 5, 4, /*hasDeathtouch=*/false, /*hasTrample=*/true};
    attack.blockers = {CombatCreature{2, 200, 1, 2}};

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_TRUE(result.playerLifeLoss.isEmpty());
}

TEST(RulesEngineTest, DeathtouchAndTrampleCombineToMaximizeExcessDamage)
{
    // A 6-power deathtouch+trample attacker only needs to assign 1 damage (lethal via deathtouch)
    // to each of two blockers, sending the remaining 4 through to the defending player.
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 6, 4, /*hasDeathtouch=*/true, /*hasTrample=*/true};
    attack.targetPlayerId = 2;
    attack.blockers = {CombatCreature{2, 200, 1, 9}, CombatCreature{2, 201, 1, 9}};

    auto result = RulesEngine::calculateCombatDamage({attack});
    EXPECT_EQ(result.cardDamageMarked.value(2).value(200), 1);
    EXPECT_EQ(result.cardDamageMarked.value(2).value(201), 1);
    EXPECT_EQ(result.playerLifeLoss.value(2), 4);
}

TEST(RulesEngineTest, IsLethallyDamagedTrueWhenMarkedDamageMeetsToughness)
{
    EXPECT_TRUE(RulesEngine::isLethallyDamaged(3, 3, /*anyDamageFromDeathtouch=*/false, /*indestructible=*/false));
    EXPECT_FALSE(RulesEngine::isLethallyDamaged(2, 3, /*anyDamageFromDeathtouch=*/false, /*indestructible=*/false));
}

TEST(RulesEngineTest, IsLethallyDamagedTrueForAnyDeathtouchDamageRegardlessOfToughness)
{
    EXPECT_TRUE(RulesEngine::isLethallyDamaged(1, 9, /*anyDamageFromDeathtouch=*/true, /*indestructible=*/false));
    EXPECT_FALSE(RulesEngine::isLethallyDamaged(0, 9, /*anyDamageFromDeathtouch=*/true, /*indestructible=*/false));
}

TEST(RulesEngineTest, IsLethallyDamagedFalseWhenIndestructibleEvenAtLethalDamage)
{
    EXPECT_FALSE(RulesEngine::isLethallyDamaged(9, 3, /*anyDamageFromDeathtouch=*/true, /*indestructible=*/true));
}

// ---- RulesEngine::participatesInStrikeStep / calculateCombatDamage step param (Phase 8 combat
// automation Stage D: first strike, double strike) ----

using CombatDamageStep = RulesEngine::CombatDamageStep;

TEST(RulesEngineTest, VanillaCreatureOnlyParticipatesInRegularStep)
{
    EXPECT_FALSE(RulesEngine::participatesInStrikeStep(false, false, CombatDamageStep::FirstStrike));
    EXPECT_TRUE(RulesEngine::participatesInStrikeStep(false, false, CombatDamageStep::Regular));
}

TEST(RulesEngineTest, FirstStrikeOnlyParticipatesInFirstStrikeStep)
{
    EXPECT_TRUE(RulesEngine::participatesInStrikeStep(true, false, CombatDamageStep::FirstStrike));
    EXPECT_FALSE(RulesEngine::participatesInStrikeStep(true, false, CombatDamageStep::Regular));
}

TEST(RulesEngineTest, DoubleStrikeParticipatesInBothSteps)
{
    EXPECT_TRUE(RulesEngine::participatesInStrikeStep(false, true, CombatDamageStep::FirstStrike));
    EXPECT_TRUE(RulesEngine::participatesInStrikeStep(false, true, CombatDamageStep::Regular));
    // hasFirstStrike + hasDoubleStrike together is redundant but should behave the same as
    // hasDoubleStrike alone -- a card can't usefully have both, but the pure function shouldn't
    // special-case it either way.
    EXPECT_TRUE(RulesEngine::participatesInStrikeStep(true, true, CombatDamageStep::FirstStrike));
    EXPECT_TRUE(RulesEngine::participatesInStrikeStep(true, true, CombatDamageStep::Regular));
}

TEST(RulesEngineTest, UnblockedFirstStrikeAttackerDealsDamageOnlyInFirstStrikeStep)
{
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 3, 3, false, false, /*hasFirstStrike=*/true, false};
    attack.targetPlayerId = 2;

    auto firstStrikeResult = RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::FirstStrike);
    EXPECT_EQ(firstStrikeResult.playerLifeLoss.value(2), 3);

    auto regularResult = RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::Regular);
    EXPECT_TRUE(regularResult.playerLifeLoss.isEmpty());
}

TEST(RulesEngineTest, UnblockedVanillaAttackerDealsDamageOnlyInRegularStep)
{
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 3, 3};
    attack.targetPlayerId = 2;

    EXPECT_TRUE(RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::FirstStrike).playerLifeLoss.isEmpty());
    EXPECT_EQ(RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::Regular).playerLifeLoss.value(2), 3);
}

TEST(RulesEngineTest, UnblockedDoubleStrikeAttackerDealsDamageInBothSteps)
{
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 3, 3, false, false, false, /*hasDoubleStrike=*/true};
    attack.targetPlayerId = 2;

    EXPECT_EQ(RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::FirstStrike).playerLifeLoss.value(2), 3);
    EXPECT_EQ(RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::Regular).playerLifeLoss.value(2), 3);
}

TEST(RulesEngineTest, FirstStrikeAttackerKillsNonFirstStrikeBlockerBeforeItCanHitBack)
{
    // In the first-strike step, the attacker (first strike) assigns its lethal damage to the
    // blocker, but the blocker (no first strike) doesn't act this step, so nothing is marked on
    // the attacker yet.
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 3, 3, false, false, /*hasFirstStrike=*/true, false};
    attack.targetPlayerId = 2;
    attack.blockers = {CombatCreature{2, 200, 2, 3}};

    auto result = RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::FirstStrike);
    EXPECT_EQ(result.cardDamageMarked.value(2).value(200), 3); // blocker takes attacker's full power
    EXPECT_FALSE(result.cardDamageMarked.contains(1));         // attacker takes nothing -- blocker hasn't acted yet
}

TEST(RulesEngineTest, NonFirstStrikeBlockerStillActsInRegularStepAgainstAFirstStrikeAttacker)
{
    // The regular step: the attacker (first strike only, not double strike) no longer acts, but
    // the blocker (if it survived the first-strike step) still deals its damage back.
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 3, 3, false, false, /*hasFirstStrike=*/true, false};
    attack.targetPlayerId = 2;
    attack.blockers = {CombatCreature{2, 200, 2, 9}}; // high toughness so it "survives" for this test

    auto result = RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::Regular);
    EXPECT_EQ(result.cardDamageMarked.value(1).value(100), 2);    // blocker's damage to the attacker
    EXPECT_FALSE(result.cardDamageMarked.value(2).contains(200)); // attacker doesn't act again
}

TEST(RulesEngineTest, AttackerRemainsBlockedInRegularStepEvenIfEveryBlockerHasDied)
{
    // Rule 509.1h: once declared blocked, an attacker stays blocked even if its blocker(s) have
    // since been removed from combat (e.g. killed in the first-strike step) -- CombatAttack::blocked
    // models this independent of the (now empty) blockers list. Without trample, a "blocked with no
    // blockers left" attacker deals no damage to anyone.
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 5, 4};
    attack.targetPlayerId = 2;
    attack.blocked = true; // blocker existed at declare time but isn't in this pass's live scan

    auto result = RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::Regular);
    EXPECT_TRUE(result.playerLifeLoss.isEmpty());
    EXPECT_TRUE(result.cardDamageMarked.isEmpty());
}

TEST(RulesEngineTest, TrampleAttackerBlockedByNowDeadCreaturesDealsFullDamageToPlayer)
{
    // Same "remains blocked with zero blockers" scenario, but with trample: since there's no
    // blocker left to assign lethal damage to, the attacker's entire power tramples through.
    CombatAttack attack;
    attack.attacker = CombatCreature{1, 100, 5, 4, false, /*hasTrample=*/true};
    attack.targetPlayerId = 2;
    attack.blocked = true;

    auto result = RulesEngine::calculateCombatDamage({attack}, CombatDamageStep::Regular);
    EXPECT_EQ(result.playerLifeLoss.value(2), 5);
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

// ---- RulesEngine::isGenericPaymentAmbiguous / planManaPaymentWithGenericChoice ----

TEST(RulesEngineTest, GenericPaymentNotAmbiguousWithOnlyOneRemainingColor)
{
    ManaCost cost;
    cost.generic = 2;
    QMap<QString, int> pool{{"w", 5}};

    EXPECT_FALSE(RulesEngine::isGenericPaymentAmbiguous(cost, pool));
}

TEST(RulesEngineTest, GenericPaymentNotAmbiguousWhenEveryRemainingColorIsForced)
{
    ManaCost cost;
    cost.generic = 2;
    // Exactly two nonzero colors, but their combined total (1+1=2) exactly equals the generic
    // amount needed -- every unit must be used, so there's no real choice despite 2 colors.
    QMap<QString, int> pool{{"w", 1}, {"u", 1}};

    EXPECT_FALSE(RulesEngine::isGenericPaymentAmbiguous(cost, pool));
}

TEST(RulesEngineTest, GenericPaymentAmbiguousWithGenuineSlackAcrossColors)
{
    ManaCost cost;
    cost.generic = 2;
    // Three nonzero colors, only 2 of the 3 units are actually needed -- a real choice of which.
    QMap<QString, int> pool{{"w", 1}, {"u", 1}, {"b", 1}};

    EXPECT_TRUE(RulesEngine::isGenericPaymentAmbiguous(cost, pool));
}

TEST(RulesEngineTest, GenericPaymentNotAmbiguousWhenUnaffordable)
{
    ManaCost cost;
    cost.coloredPips = {{"u", 1}};
    cost.generic = 2;
    QMap<QString, int> pool{{"r", 5}}; // can't even pay the colored pip

    EXPECT_FALSE(RulesEngine::isGenericPaymentAmbiguous(cost, pool));
}

TEST(RulesEngineTest, GenericPaymentNotAmbiguousWhenGenericIsZero)
{
    ManaCost cost;
    cost.coloredPips = {{"r", 1}};
    QMap<QString, int> pool{{"r", 1}, {"w", 5}, {"u", 5}};

    EXPECT_FALSE(RulesEngine::isGenericPaymentAmbiguous(cost, pool));
}

TEST(RulesEngineTest, PlanManaPaymentWithGenericChoiceAcceptsAValidSplit)
{
    ManaCost cost;
    cost.coloredPips = {{"r", 1}};
    cost.generic = 2;
    QMap<QString, int> pool{{"r", 1}, {"w", 1}, {"u", 1}};

    auto plan = RulesEngine::planManaPaymentWithGenericChoice(cost, pool, {{"w", 1}, {"u", 1}});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->value("r"), 1);
    EXPECT_EQ(plan->value("w"), 1);
    EXPECT_EQ(plan->value("u"), 1);
}

TEST(RulesEngineTest, PlanManaPaymentWithGenericChoiceRejectsOverCapEntry)
{
    ManaCost cost;
    cost.generic = 2;
    QMap<QString, int> pool{{"w", 1}, {"u", 5}};

    // Claims 2 white when only 1 is actually available.
    EXPECT_FALSE(RulesEngine::planManaPaymentWithGenericChoice(cost, pool, {{"w", 2}}).has_value());
}

TEST(RulesEngineTest, PlanManaPaymentWithGenericChoiceRejectsWrongTotal)
{
    ManaCost cost;
    cost.generic = 2;
    QMap<QString, int> pool{{"w", 5}, {"u", 5}};

    // Only totals to 1, not the required 2.
    EXPECT_FALSE(RulesEngine::planManaPaymentWithGenericChoice(cost, pool, {{"w", 1}}).has_value());
}

// ---- RulesEngine::planManaCostChoices / resolveManaCost (phase6-mana.md hybrid/Phyrexian/X addendum) ----

TEST(RulesEngineTest, HybridPipNotAmbiguousWhenOnlyOneColorAvailable)
{
    ManaCost cost;
    cost.hybridPips = {{"r", "g"}};
    QMap<QString, int> pool{{"r", 1}};

    auto choices = RulesEngine::planManaCostChoices(cost, pool);
    ASSERT_EQ(choices.hybridChoices.size(), 1);
    EXPECT_FALSE(choices.hybridChoices.at(0).ambiguous);
    EXPECT_EQ(choices.hybridChoices.at(0).defaultColor, "r");
}

TEST(RulesEngineTest, HybridPipAmbiguousWhenBothColorsAvailable)
{
    ManaCost cost;
    cost.hybridPips = {{"r", "g"}};
    QMap<QString, int> pool{{"r", 1}, {"g", 1}};

    auto choices = RulesEngine::planManaCostChoices(cost, pool);
    ASSERT_EQ(choices.hybridChoices.size(), 1);
    EXPECT_TRUE(choices.hybridChoices.at(0).ambiguous);
    // "r" precedes "g" in manaCounterNames() order.
    EXPECT_EQ(choices.hybridChoices.at(0).defaultColor, "r");
}

TEST(RulesEngineTest, TwoHybridPipsSharingAColorSequenceTheirAvailability)
{
    ManaCost cost;
    cost.hybridPips = {{"r", "g"}, {"r", "g"}};
    // Only enough red for one of the two pips to default to it; after the first pip tentatively
    // reserves "r", the second pip sees only "g" left and is no longer ambiguous.
    QMap<QString, int> pool{{"r", 1}, {"g", 1}};

    auto choices = RulesEngine::planManaCostChoices(cost, pool);
    ASSERT_EQ(choices.hybridChoices.size(), 2);
    EXPECT_TRUE(choices.hybridChoices.at(0).ambiguous);
    EXPECT_EQ(choices.hybridChoices.at(0).defaultColor, "r");
    EXPECT_FALSE(choices.hybridChoices.at(1).ambiguous);
    EXPECT_EQ(choices.hybridChoices.at(1).defaultColor, "g");
}

TEST(RulesEngineTest, PhyrexianPipForcedToLifeWhenColorUnavailable)
{
    ManaCost cost;
    cost.phyrexianPips = {"b"};
    QMap<QString, int> pool{{"r", 5}};

    auto choices = RulesEngine::planManaCostChoices(cost, pool);
    ASSERT_EQ(choices.phyrexianChoices.size(), 1);
    EXPECT_FALSE(choices.phyrexianChoices.at(0).ambiguous);
    EXPECT_TRUE(choices.phyrexianChoices.at(0).defaultPayLife);
}

TEST(RulesEngineTest, PhyrexianPipAmbiguousWhenColorAvailableDefaultsToMana)
{
    ManaCost cost;
    cost.phyrexianPips = {"b"};
    QMap<QString, int> pool{{"b", 1}};

    auto choices = RulesEngine::planManaCostChoices(cost, pool);
    ASSERT_EQ(choices.phyrexianChoices.size(), 1);
    EXPECT_TRUE(choices.phyrexianChoices.at(0).ambiguous);
    EXPECT_FALSE(choices.phyrexianChoices.at(0).defaultPayLife);
}

TEST(RulesEngineTest, ResolveManaCostFoldsHybridChoiceIntoColoredPips)
{
    ManaCost cost;
    cost.hybridPips = {{"r", "g"}};

    auto resolved = RulesEngine::resolveManaCost(cost, 0, {"g"}, {});
    EXPECT_EQ(resolved.cost.coloredPips.value("g"), 1);
    EXPECT_FALSE(resolved.cost.coloredPips.contains("r"));
    EXPECT_TRUE(resolved.cost.hybridPips.isEmpty());
    EXPECT_EQ(resolved.lifeCost, 0);
}

TEST(RulesEngineTest, ResolveManaCostFoldsPhyrexianManaChoiceIntoColoredPips)
{
    ManaCost cost;
    cost.phyrexianPips = {"b"};

    auto resolved = RulesEngine::resolveManaCost(cost, 0, {}, {false});
    EXPECT_EQ(resolved.cost.coloredPips.value("b"), 1);
    EXPECT_EQ(resolved.lifeCost, 0);
}

TEST(RulesEngineTest, ResolveManaCostFoldsPhyrexianLifeChoiceIntoLifeCost)
{
    ManaCost cost;
    cost.phyrexianPips = {"b", "b"};

    auto resolved = RulesEngine::resolveManaCost(cost, 0, {}, {true, false});
    EXPECT_EQ(resolved.cost.coloredPips.value("b"), 1); // only the second pip paid with mana
    EXPECT_EQ(resolved.lifeCost, 2);
}

TEST(RulesEngineTest, ResolveManaCostFoldsXValueIntoGeneric)
{
    ManaCost cost;
    cost.xCount = 2;
    cost.generic = 1;

    auto resolved = RulesEngine::resolveManaCost(cost, 3, {}, {});
    EXPECT_EQ(resolved.cost.generic, 1 + 2 * 3);
    EXPECT_EQ(resolved.cost.xCount, 0);
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
