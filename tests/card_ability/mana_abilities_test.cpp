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
    const QList<ManaAbilities::ManaAbility> expected = {{"G", 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, RepeatedSymbolLine)
{
    auto card = makeCard("{T}: Add {C}{C}.");
    const QList<ManaAbilities::ManaAbility> expected = {{"C", 2}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

TEST(ManaAbilitiesTest, ReminderTextIsStripped)
{
    auto card = makeCard("({T}: Add {W}.)");
    EXPECT_TRUE(ManaAbilities::parse(*card).isEmpty());
}

TEST(ManaAbilitiesTest, MixedSymbolChoiceLineIsNotMatched)
{
    auto card = makeCard("{T}: Add {W} or {U}.");
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
    const QList<ManaAbilities::ManaAbility> expected = {{"W", 1}, {"U", 1}};
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
    const QList<ManaAbilities::ManaAbility> expected = {{"G", 1}};
    EXPECT_EQ(ManaAbilities::parse(*card), expected);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
