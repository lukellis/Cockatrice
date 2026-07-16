#include "gtest/gtest.h"
#include <libcockatrice/card/ability/card_keywords.h>
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

TEST(CardKeywordsTest, SingleKeywordAlone)
{
    auto card = makeCard("Flying");
    EXPECT_EQ(CardKeywords::parse(*card), QSet<QString>({"Flying"}));
}

TEST(CardKeywordsTest, SingleKeywordWithTrailingPeriod)
{
    auto card = makeCard("Trample.");
    EXPECT_EQ(CardKeywords::parse(*card), QSet<QString>({"Trample"}));
}

TEST(CardKeywordsTest, CommaSeparatedKeywordList)
{
    auto card = makeCard("First strike, trample, vigilance");
    EXPECT_EQ(CardKeywords::parse(*card), QSet<QString>({"First strike", "Trample", "Vigilance"}));
}

TEST(CardKeywordsTest, AndSeparatedKeywordList)
{
    auto card = makeCard("Flying and lifelink");
    EXPECT_EQ(CardKeywords::parse(*card), QSet<QString>({"Flying", "Lifelink"}));
}

TEST(CardKeywordsTest, CaseInsensitiveMatchNormalizesToCanonicalForm)
{
    auto card = makeCard("FLYING");
    EXPECT_EQ(CardKeywords::parse(*card), QSet<QString>({"Flying"}));
}

TEST(CardKeywordsTest, MultipleKeywordLinesAllCounted)
{
    auto card = makeCard("Flying\nDeathtouch");
    EXPECT_EQ(CardKeywords::parse(*card), QSet<QString>({"Flying", "Deathtouch"}));
}

TEST(CardKeywordsTest, ReminderTextIsStripped)
{
    auto card = makeCard("Flying (This creature can't be blocked except by creatures with flying or reach.)");
    EXPECT_EQ(CardKeywords::parse(*card), QSet<QString>({"Flying"}));
}

TEST(CardKeywordsTest, GrantedKeywordIsNotCountedAsPrinted)
{
    // Rule out false positives: this creature doesn't *have* flying, it can grant it -- and the
    // line has other words on it, so it must not reduce to a bare keyword list.
    auto card = makeCard("Whenever this creature attacks, it gains flying until end of turn.");
    EXPECT_TRUE(CardKeywords::parse(*card).isEmpty());
}

TEST(CardKeywordsTest, MentionedKeywordInUnrelatedAbilityIsNotCountedAsPrinted)
{
    auto card = makeCard("Destroy target creature with flying.");
    EXPECT_TRUE(CardKeywords::parse(*card).isEmpty());
}

TEST(CardKeywordsTest, NoAbilitiesReturnsEmptySet)
{
    auto card = makeCard("");
    EXPECT_TRUE(CardKeywords::parse(*card).isEmpty());
}

TEST(CardKeywordsTest, NonKeywordAbilityLineAlongsideKeywordLineOnlyCountsKeywordLine)
{
    auto card = makeCard("Flying\n{T}: Add {G}.");
    EXPECT_EQ(CardKeywords::parse(*card), QSet<QString>({"Flying"}));
}

TEST(CardKeywordsTest, EvergreenKeywordsListIsNonEmptyAndContainsFlying)
{
    EXPECT_TRUE(CardKeywords::evergreenKeywords().contains("Flying"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
