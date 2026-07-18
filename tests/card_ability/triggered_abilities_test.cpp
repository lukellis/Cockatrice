#include "gtest/gtest.h"
#include <libcockatrice/card/ability/triggered_abilities.h>
#include <libcockatrice/card/card_info.h>

namespace
{

CardInfoPtr makeCard(const QString &name, const QString &text)
{
    QVariantHash props;
    props.insert("type", "Creature — Bear");
    props.insert("colors", "G");
    props.insert("manacost", "{1}{G}");
    return CardInfo::newInstance(name, text, false, props, {}, {}, {}, {});
}

} // namespace

TEST(TriggeredAbilitiesTest, EntersBattlefieldDrawIsRecognized)
{
    auto card = makeCard("Test Card", "When Test Card enters the battlefield, draw a card.");
    const QList<TriggeredAbility> expected = {{TriggerKind::EntersBattlefield, {EffectKind::DrawCards, 1}}};
    EXPECT_EQ(TriggeredAbilities::parse(*card), expected);
}

TEST(TriggeredAbilitiesTest, BalefulStrixRealPrintedTextIsRecognized)
{
    // The real printed text of Baleful Strix (also used live in .uitest/sample_cards.xml) --
    // confirms the parser works against real card data, not just a synthetic fixture.
    auto card = makeCard("Baleful Strix", "Flying, deathtouch\nWhen Baleful Strix enters the battlefield, draw a card.");
    const QList<TriggeredAbility> expected = {{TriggerKind::EntersBattlefield, {EffectKind::DrawCards, 1}}};
    EXPECT_EQ(TriggeredAbilities::parse(*card), expected);
}

TEST(TriggeredAbilitiesTest, WheneverPhrasingIsRecognized)
{
    auto card = makeCard("Test Card", "Whenever Test Card enters the battlefield, you gain 3 life.");
    const QList<TriggeredAbility> expected = {{TriggerKind::EntersBattlefield, {EffectKind::GainLife, 3}}};
    EXPECT_EQ(TriggeredAbilities::parse(*card), expected);
}

TEST(TriggeredAbilitiesTest, DiesLoseLifeIsRecognized)
{
    auto card = makeCard("Test Card", "When Test Card dies, you lose 2 life.");
    const QList<TriggeredAbility> expected = {{TriggerKind::Dies, {EffectKind::LoseLife, 2}}};
    EXPECT_EQ(TriggeredAbilities::parse(*card), expected);
}

TEST(TriggeredAbilitiesTest, DiesGainLifeIsRecognized)
{
    auto card = makeCard("Test Card", "When Test Card dies, you gain 1 life.");
    const QList<TriggeredAbility> expected = {{TriggerKind::Dies, {EffectKind::GainLife, 1}}};
    EXPECT_EQ(TriggeredAbilities::parse(*card), expected);
}

TEST(TriggeredAbilitiesTest, WrongCardNameLineIsNotMatched)
{
    // Self-referential text naming a *different* card shouldn't false-positive.
    auto card = makeCard("Test Card", "When Some Other Card enters the battlefield, draw a card.");
    EXPECT_TRUE(TriggeredAbilities::parse(*card).isEmpty());
}

TEST(TriggeredAbilitiesTest, DealDamageEffectShapeIsNotMatched)
{
    // Stage 5 deliberately excludes targeted triggers (no automatic AbilityTargetPicker) -- the
    // trigger condition matches, but the effect clause doesn't, so the whole line is skipped.
    auto card = makeCard("Test Card", "When Test Card enters the battlefield, deal 2 damage to any target.");
    EXPECT_TRUE(TriggeredAbilities::parse(*card).isEmpty());
}

TEST(TriggeredAbilitiesTest, NoQualifyingLinesReturnsEmptyList)
{
    auto card = makeCard("Test Card", "Flying");
    EXPECT_TRUE(TriggeredAbilities::parse(*card).isEmpty());
}

TEST(TriggeredAbilitiesTest, ReminderTextIsStripped)
{
    auto card = makeCard("Test Card", "When Test Card enters the battlefield, draw a card. (This is reminder text.)");
    const QList<TriggeredAbility> expected = {{TriggerKind::EntersBattlefield, {EffectKind::DrawCards, 1}}};
    EXPECT_EQ(TriggeredAbilities::parse(*card), expected);
}

TEST(TriggeredAbilitiesTest, CaseInsensitiveNameAndPhrasingAreMatched)
{
    auto card = makeCard("Test Card", "when test card ENTERS THE BATTLEFIELD, draw a card.");
    const QList<TriggeredAbility> expected = {{TriggerKind::EntersBattlefield, {EffectKind::DrawCards, 1}}};
    EXPECT_EQ(TriggeredAbilities::parse(*card), expected);
}

TEST(TriggeredAbilitiesTest, MultipleQualifyingLinesBothReturned)
{
    auto card = makeCard("Test Card", "When Test Card enters the battlefield, draw a card.\n"
                                      "When Test Card dies, you gain 1 life.");
    const QList<TriggeredAbility> expected = {{TriggerKind::EntersBattlefield, {EffectKind::DrawCards, 1}},
                                              {TriggerKind::Dies, {EffectKind::GainLife, 1}}};
    EXPECT_EQ(TriggeredAbilities::parse(*card), expected);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
