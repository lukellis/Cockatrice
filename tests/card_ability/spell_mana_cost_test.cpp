#include "gtest/gtest.h"
#include <libcockatrice/card/ability/spell_mana_cost.h>
#include <libcockatrice/card/card_info.h>

namespace
{

CardInfoPtr makeCard(const QString &manaCost)
{
    QVariantHash props;
    props.insert("type", "Creature");
    props.insert("manacost", manaCost);
    return CardInfo::newInstance("Test Card", "", false, props, {}, {}, {}, {});
}

} // namespace

TEST(SpellManaCostTest, PureGenericCost)
{
    auto card = makeCard("{3}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->generic, 3);
    EXPECT_TRUE(cost->coloredPips.isEmpty());
}

TEST(SpellManaCostTest, PureColoredCost)
{
    auto card = makeCard("{W}{W}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->generic, 0);
    EXPECT_EQ(cost->coloredPips.value("w"), 2);
}

TEST(SpellManaCostTest, GenericAndColoredCost)
{
    auto card = makeCard("{2}{R}{R}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->generic, 2);
    EXPECT_EQ(cost->coloredPips.value("r"), 2);
}

TEST(SpellManaCostTest, MultiColorCost)
{
    auto card = makeCard("{G}{W}{U}{B}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->generic, 0);
    EXPECT_EQ(cost->coloredPips.value("g"), 1);
    EXPECT_EQ(cost->coloredPips.value("w"), 1);
    EXPECT_EQ(cost->coloredPips.value("u"), 1);
    EXPECT_EQ(cost->coloredPips.value("b"), 1);
}

TEST(SpellManaCostTest, ColorlessSymbolMapsToXCounter)
{
    auto card = makeCard("{1}{C}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->generic, 1);
    EXPECT_EQ(cost->coloredPips.value("x"), 1);
}

TEST(SpellManaCostTest, EmptyManaCostIsFree)
{
    auto card = makeCard("");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_TRUE(cost->isFree());
}

TEST(SpellManaCostTest, HybridSymbolParsesAsHybridPip)
{
    auto card = makeCard("{2}{R/G}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->generic, 2);
    EXPECT_TRUE(cost->coloredPips.isEmpty());
    ASSERT_EQ(cost->hybridPips.size(), 1);
    EXPECT_EQ(cost->hybridPips.at(0).first, "r");
    EXPECT_EQ(cost->hybridPips.at(0).second, "g");
}

TEST(SpellManaCostTest, PhyrexianSymbolParsesAsPhyrexianPip)
{
    auto card = makeCard("{R/P}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->generic, 0);
    ASSERT_EQ(cost->phyrexianPips.size(), 1);
    EXPECT_EQ(cost->phyrexianPips.at(0), "r");
}

TEST(SpellManaCostTest, VariableXCostParsesXCount)
{
    auto card = makeCard("{X}{R}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->xCount, 1);
    EXPECT_EQ(cost->coloredPips.value("r"), 1);
}

TEST(SpellManaCostTest, MultipleXSymbolsAccumulateXCount)
{
    auto card = makeCard("{X}{X}{R}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->xCount, 2);
    EXPECT_EQ(cost->coloredPips.value("r"), 1);
}

TEST(SpellManaCostTest, CombinedHybridPhyrexianAndGenericCost)
{
    auto card = makeCard("{2}{W/U}{R/P}");
    const auto cost = SpellManaCost::parse(*card);
    ASSERT_TRUE(cost.has_value());
    EXPECT_EQ(cost->generic, 2);
    ASSERT_EQ(cost->hybridPips.size(), 1);
    EXPECT_EQ(cost->hybridPips.at(0).first, "w");
    EXPECT_EQ(cost->hybridPips.at(0).second, "u");
    ASSERT_EQ(cost->phyrexianPips.size(), 1);
    EXPECT_EQ(cost->phyrexianPips.at(0), "r");
}

TEST(SpellManaCostTest, MonocoloredHybridSymbolIsStillNotParsed)
{
    auto card = makeCard("{2/W}");
    EXPECT_FALSE(SpellManaCost::parse(*card).has_value());
}

TEST(SpellManaCostTest, SnowSymbolIsNotParsed)
{
    auto card = makeCard("{S}");
    EXPECT_FALSE(SpellManaCost::parse(*card).has_value());
}

TEST(SpellManaCostTest, SplitCostIsNotParsed)
{
    auto card = makeCard("{3}{U} // {4}{U}{U}");
    EXPECT_FALSE(SpellManaCost::parse(*card).has_value());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
