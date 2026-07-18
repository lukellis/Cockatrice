#include "gtest/gtest.h"
#include <libcockatrice/card/ability/activated_abilities.h>
#include <libcockatrice/card/card_info.h>

namespace
{

CardInfoPtr makeCard(const QString &text)
{
    QVariantHash props;
    props.insert("type", "Creature — Bear");
    props.insert("colors", "G");
    props.insert("manacost", "{1}{G}");
    return CardInfo::newInstance("Test Card", text, false, props, {}, {}, {}, {});
}

} // namespace

TEST(ActivatedAbilitiesTest, DrawACardLineIsRecognized)
{
    auto card = makeCard("{T}: Draw a card.");
    const QList<ActivatedAbility> expected = {{true, {EffectKind::DrawCards, 1}}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, GainLifeLineIsRecognized)
{
    auto card = makeCard("{T}: You gain 3 life.");
    const QList<ActivatedAbility> expected = {{true, {EffectKind::GainLife, 3}}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, LoseLifeLineIsRecognized)
{
    auto card = makeCard("{T}: You lose 2 life.");
    const QList<ActivatedAbility> expected = {{true, {EffectKind::LoseLife, 2}}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, NoQualifyingLinesReturnsEmptyList)
{
    auto card = makeCard("Flying");
    EXPECT_TRUE(ActivatedAbilities::parse(*card).isEmpty());
}

TEST(ActivatedAbilitiesTest, NonTapCostLineIsNotMatched)
{
    // "Sacrifice this creature: Draw a card." -- not a {T} cost, so out of Stage 1's scope.
    auto card = makeCard("Sacrifice this creature: Draw a card.");
    EXPECT_TRUE(ActivatedAbilities::parse(*card).isEmpty());
}

TEST(ActivatedAbilitiesTest, DealDamageAnyTargetLineIsRecognized)
{
    auto card = makeCard("{T}: Deal 3 damage to any target.");
    const QList<ActivatedAbility> expected = {{true, {EffectKind::DealDamage, 3, TargetKind::AnyTarget}}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, NonAnyTargetPhrasingIsNotMatched)
{
    // Stage 2 only recognizes the modern "any target" templating -- older phrasings naming a
    // specific target type are deliberately skipped, not guessed at.
    auto creatureTarget = makeCard("{T}: Deal 1 damage to target creature.");
    EXPECT_TRUE(ActivatedAbilities::parse(*creatureTarget).isEmpty());

    auto playerTarget = makeCard("{T}: Deal 1 damage to target player.");
    EXPECT_TRUE(ActivatedAbilities::parse(*playerTarget).isEmpty());
}

TEST(ActivatedAbilitiesTest, PluralDrawPhrasingIsNotMatched)
{
    // Documents the narrow whitelist boundary: only the exact singular "Draw a card." phrasing is
    // recognized in Stage 1, not "Draw two cards." or other numeric phrasings.
    auto card = makeCard("{T}: Draw two cards.");
    EXPECT_TRUE(ActivatedAbilities::parse(*card).isEmpty());
}

TEST(ActivatedAbilitiesTest, ReminderTextIsStripped)
{
    auto card = makeCard("{T}: Draw a card. (This is reminder text.)");
    const QList<ActivatedAbility> expected = {{true, {EffectKind::DrawCards, 1}}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, CaseInsensitiveTokensAreMatched)
{
    auto card = makeCard("{t}: draw a card.");
    const QList<ActivatedAbility> expected = {{true, {EffectKind::DrawCards, 1}}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, MultipleQualifyingLinesBothReturned)
{
    auto card = makeCard("{T}: Draw a card.\n{T}: You gain 1 life.");
    const QList<ActivatedAbility> expected = {{true, {EffectKind::DrawCards, 1}}, {true, {EffectKind::GainLife, 1}}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

// ---- Phase 7 Stage 4: mana-cost prefix parsing ----

TEST(ActivatedAbilitiesTest, GenericAndColoredCostLineIsRecognized)
{
    auto card = makeCard("{2}{R}, {T}: Deal 2 damage to any target.");
    ManaCost expectedCost;
    expectedCost.generic = 2;
    expectedCost.coloredPips = {{"r", 1}};
    const QList<ActivatedAbility> expected = {{true, {EffectKind::DealDamage, 2, TargetKind::AnyTarget}, expectedCost}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, PureGenericCostLineIsRecognized)
{
    auto card = makeCard("{2}, {T}: Draw a card.");
    ManaCost expectedCost;
    expectedCost.generic = 2;
    const QList<ActivatedAbility> expected = {{true, {EffectKind::DrawCards, 1}, expectedCost}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, PureColoredCostLineIsRecognized)
{
    auto card = makeCard("{W}{W}, {T}: You gain 2 life.");
    ManaCost expectedCost;
    expectedCost.coloredPips = {{"w", 2}};
    const QList<ActivatedAbility> expected = {{true, {EffectKind::GainLife, 2}, expectedCost}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, ColorlessCostSymbolMapsToXCounter)
{
    auto card = makeCard("{C}, {T}: You lose 1 life.");
    ManaCost expectedCost;
    expectedCost.coloredPips = {{"x", 1}};
    const QList<ActivatedAbility> expected = {{true, {EffectKind::LoseLife, 1}, expectedCost}};
    EXPECT_EQ(ActivatedAbilities::parse(*card), expected);
}

TEST(ActivatedAbilitiesTest, PreExistingFreeAbilitiesStillParseAsFree)
{
    auto card = makeCard("{T}: Draw a card.");
    const QList<ActivatedAbility> abilities = ActivatedAbilities::parse(*card);
    ASSERT_EQ(abilities.size(), 1);
    EXPECT_TRUE(abilities.first().cost.isFree());
}

TEST(ActivatedAbilitiesTest, VariableXCostLineIsNotMatched)
{
    // {X} doesn't fit the cost-symbol whitelist (plain digits or W/U/B/R/G/C only) -- a variable
    // cost needing a player-chosen value is out of scope, skipped rather than guessed at.
    auto card = makeCard("{X}, {T}: Deal 1 damage to any target.");
    EXPECT_TRUE(ActivatedAbilities::parse(*card).isEmpty());
}

TEST(ActivatedAbilitiesTest, HybridCostSymbolLineIsNotMatched)
{
    auto card = makeCard("{W/U}, {T}: You gain 1 life.");
    EXPECT_TRUE(ActivatedAbilities::parse(*card).isEmpty());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
