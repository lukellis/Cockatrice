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

TEST(ActivatedAbilitiesTest, TargetedEffectLineIsNotMatched)
{
    // Stage 1 has no TargetSpec yet -- targeted effects must be skipped, not guessed at.
    auto card = makeCard("{T}: Deal 1 damage to any target.");
    EXPECT_TRUE(ActivatedAbilities::parse(*card).isEmpty());
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

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
