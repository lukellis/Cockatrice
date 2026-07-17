#include "gtest/gtest.h"
#include <libcockatrice/card/ability/mana_abilities.h>
#include <libcockatrice/card/card_info.h>

namespace
{

CardInfoPtr makeCard(const QString &text)
{
    QVariantHash props;
    props.insert("type", "Artifact");
    props.insert("colors", "");
    props.insert("manacost", "{1}");
    return CardInfo::newInstance("Test Card", text, false, props, {}, {}, {}, {});
}

} // namespace

TEST(ManaAbilitiesTest, SingleFixedColorLine)
{
    auto card = makeCard("{T}: Add {G}.");
    const QList<ManaAbilities::ManaAbility> expected = {{{"G"}, 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, RepeatedSymbolLine)
{
    auto card = makeCard("{T}: Add {C}{C}.");
    const QList<ManaAbilities::ManaAbility> expected = {{{"C"}, 2}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, BasicLandWholeLineReminderTextIsRecognized)
{
    // Basic lands' mana ability (rule 305.6) isn't printed -- Oracle/MTGJSON text renders it as
    // reminder text that IS the entire line, e.g. Forest's real text. This must still be
    // recognized, or the one-click action never appears on the most common mana source in the
    // game.
    auto card = makeCard("({T}: Add {G}.)");
    const QList<ManaAbilities::ManaAbility> expected = {{{"G"}, 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, MidLineReminderTextIsStillStripped)
{
    // Reminder text that merely explains an unrelated printed ability (not the basic-land
    // whole-line case above) must still be discarded, not misread as a mana ability -- the
    // stripped remainder here isn't a qualifying line at all.
    auto card = makeCard("Flying (This creature can't be blocked except by creatures with flying or reach.)");
    EXPECT_TRUE(ManaAbilities::parse(*card).isEmpty());
}

TEST(ManaAbilitiesTest, TwoSymbolChoiceLineIsRecognized)
{
    auto card = makeCard("{T}: Add {W} or {U}.");
    const QList<ManaAbilities::ManaAbility> expected = {{{"W", "U"}, 1}};
    const auto actual = ManaAbilities::parse(*card);
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(actual.first().isChoice());
}

TEST(ManaAbilitiesTest, ThreeSymbolCommaChoiceLineIsRecognized)
{
    auto card = makeCard("{T}: Add {W}, {U}, or {B}.");
    const QList<ManaAbilities::ManaAbility> expected = {{{"W", "U", "B"}, 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, AnyColorPhraseIsRecognizedAsFiveWayChoice)
{
    auto card = makeCard("{T}: Add one mana of any color.");
    const QList<ManaAbilities::ManaAbility> expected = {{{"W", "U", "B", "R", "G"}, 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, CommandTowerPhraseIsSimplifiedToFiveWayChoice)
{
    // The commander-color-identity restriction depends on game state this parser can't inspect
    // -- simplified to "any of the five colors", per this fork's advisory philosophy.
    auto card = makeCard("{T}: Add one mana of any color in your commander's color identity.");
    const QList<ManaAbilities::ManaAbility> expected = {{{"W", "U", "B", "R", "G"}, 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, ReflectingPoolPhraseIsSimplifiedToFiveWayChoice)
{
    // Same simplification as Command Tower -- "already produced by a land you control" needs
    // battlefield inspection this parser doesn't do.
    auto card = makeCard("{T}: Add a color of mana already produced by a land you control.");
    const QList<ManaAbilities::ManaAbility> expected = {{{"W", "U", "B", "R", "G"}, 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, DirectlyConcatenatedMixedSymbolsIsNotMatched)
{
    // No separator between different symbols would mean "both simultaneously", not a choice --
    // rare/nonexistent in real cards and deliberately not guessed at.
    auto card = makeCard("{T}: Add {W}{U}.");
    EXPECT_TRUE(ManaAbilities::parse(*card).isEmpty());
}

TEST(ManaAbilitiesTest, AdditionalCostLineIsNotMatched)
{
    auto card = makeCard("{T}, Sacrifice this artifact: Add {C}{C}{C}.");
    EXPECT_TRUE(ManaAbilities::parse(*card).isEmpty());
}

TEST(ManaAbilitiesTest, ConditionalLineIsNotMatched)
{
    auto card = makeCard("{T}: Add {G}. Spend this mana only to cast a creature spell.");
    EXPECT_TRUE(ManaAbilities::parse(*card).isEmpty());
}

TEST(ManaAbilitiesTest, TwoQualifyingLinesBothReturned)
{
    auto card = makeCard("{T}: Add {W}.\n{T}: Add {U}.");
    const QList<ManaAbilities::ManaAbility> expected = {{{"W"}, 1}, {{"U"}, 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, NoQualifyingLinesReturnsEmptyList)
{
    auto card = makeCard("Flying");
    EXPECT_TRUE(ManaAbilities::parse(*card).isEmpty());
}

TEST(ManaAbilitiesTest, CaseInsensitiveTokensAreMatched)
{
    auto card = makeCard("{t}: add {g}.");
    const QList<ManaAbilities::ManaAbility> expected = {{{"G"}, 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
