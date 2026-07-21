#include "gtest/gtest.h"
#include <libcockatrice/card/ability/static_abilities.h>
#include <libcockatrice/card/card_info.h>

using StaticAbilities::StaticAbility;
using StaticAbilities::StaticScope;

namespace
{

CardInfoPtr makeCard(const QString &text)
{
    QVariantHash props;
    props.insert("type", "Enchantment");
    props.insert("colors", "W");
    props.insert("manacost", "{2}{W}");
    return CardInfo::newInstance("Test Card", text, false, props, {}, {}, {}, {});
}

} // namespace

// ---- StaticAbilities::parse -- P/T boost shape ----

TEST(StaticAbilitiesTest, OtherCreaturesGetBoost)
{
    auto card = makeCard("Other creatures you control get +1/+1.");
    const QList<StaticAbility> found = StaticAbilities::parse(*card);
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found.at(0).scope, StaticScope::OthersYours);
    EXPECT_EQ(found.at(0).powerBonus, 1);
    EXPECT_EQ(found.at(0).toughnessBonus, 1);
    EXPECT_TRUE(found.at(0).grantedKeywords.isEmpty());
}

TEST(StaticAbilitiesTest, CreaturesYouControlGetBoostIncludesSelf)
{
    auto card = makeCard("Creatures you control get +2/+0.");
    const QList<StaticAbility> found = StaticAbilities::parse(*card);
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found.at(0).scope, StaticScope::AllYours);
    EXPECT_EQ(found.at(0).powerBonus, 2);
    EXPECT_EQ(found.at(0).toughnessBonus, 0);
}

TEST(StaticAbilitiesTest, NegativeBoost)
{
    auto card = makeCard("Other creatures you control get -1/-1.");
    const QList<StaticAbility> found = StaticAbilities::parse(*card);
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found.at(0).powerBonus, -1);
    EXPECT_EQ(found.at(0).toughnessBonus, -1);
}

TEST(StaticAbilitiesTest, CaseInsensitivePhrase)
{
    auto card = makeCard("other creatures you control get +1/+1.");
    const QList<StaticAbility> found = StaticAbilities::parse(*card);
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found.at(0).scope, StaticScope::OthersYours);
}

// ---- StaticAbilities::parse -- keyword-grant shape ----

TEST(StaticAbilitiesTest, OtherCreaturesHaveKeyword)
{
    auto card = makeCard("Other creatures you control have trample.");
    const QList<StaticAbility> found = StaticAbilities::parse(*card);
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found.at(0).scope, StaticScope::OthersYours);
    EXPECT_EQ(found.at(0).grantedKeywords, QSet<QString>({"Trample"}));
    EXPECT_EQ(found.at(0).powerBonus, 0);
    EXPECT_EQ(found.at(0).toughnessBonus, 0);
}

TEST(StaticAbilitiesTest, CreaturesYouControlHaveMultipleKeywords)
{
    auto card = makeCard("Creatures you control have vigilance and menace.");
    const QList<StaticAbility> found = StaticAbilities::parse(*card);
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found.at(0).scope, StaticScope::AllYours);
    EXPECT_EQ(found.at(0).grantedKeywords, QSet<QString>({"Vigilance", "Menace"}));
}

TEST(StaticAbilitiesTest, KeywordGrantWithUnrecognizedTokenSkipsWholeLine)
{
    auto card = makeCard("Other creatures you control have trample and ward {2}.");
    EXPECT_TRUE(StaticAbilities::parse(*card).isEmpty());
}

// ---- combined / wrong-shape lines: deliberately not recognized ----

TEST(StaticAbilitiesTest, CombinedBoostAndKeywordLineIsNotRecognized)
{
    // Named exclusion: this fork's parser doesn't attempt combined single-line phrasing.
    auto card = makeCard("Other creatures you control get +1/+1 and have vigilance.");
    EXPECT_TRUE(StaticAbilities::parse(*card).isEmpty());
}

TEST(StaticAbilitiesTest, TribalRestrictionIsNotRecognized)
{
    auto card = makeCard("Other Elves you control get +1/+1.");
    EXPECT_TRUE(StaticAbilities::parse(*card).isEmpty());
}

TEST(StaticAbilitiesTest, UnrelatedAbilityTextIsNotRecognized)
{
    auto card = makeCard("{T}: Add {G}.");
    EXPECT_TRUE(StaticAbilities::parse(*card).isEmpty());
}

TEST(StaticAbilitiesTest, NoAbilitiesReturnsEmptyList)
{
    auto card = makeCard("");
    EXPECT_TRUE(StaticAbilities::parse(*card).isEmpty());
}

TEST(StaticAbilitiesTest, MultipleQualifyingLinesEachProduceAnAbility)
{
    auto card = makeCard("Other creatures you control get +1/+1.\nCreatures you control have vigilance.");
    const QList<StaticAbility> found = StaticAbilities::parse(*card);
    ASSERT_EQ(found.size(), 2);
    EXPECT_EQ(found.at(0).scope, StaticScope::OthersYours);
    EXPECT_EQ(found.at(0).powerBonus, 1);
    EXPECT_EQ(found.at(1).scope, StaticScope::AllYours);
    EXPECT_EQ(found.at(1).grantedKeywords, QSet<QString>({"Vigilance"}));
}

TEST(StaticAbilitiesTest, ReminderTextIsStripped)
{
    auto card = makeCard("Other creatures you control have flying (they can only be blocked by flying/reach).");
    const QList<StaticAbility> found = StaticAbilities::parse(*card);
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found.at(0).grantedKeywords, QSet<QString>({"Flying"}));
}

// ---- serialize / deserialize round trip ----

TEST(StaticAbilitiesTest, RoundTripSinglePTBoost)
{
    QList<StaticAbility> abilities;
    abilities.append({StaticScope::OthersYours, 1, 1, {}});
    const QString serialized = StaticAbilities::serialize(abilities);
    EXPECT_EQ(StaticAbilities::deserialize(serialized), abilities);
}

TEST(StaticAbilitiesTest, RoundTripKeywordGrant)
{
    QList<StaticAbility> abilities;
    abilities.append({StaticScope::AllYours, 0, 0, QSet<QString>({"Menace", "Trample"})});
    const QString serialized = StaticAbilities::serialize(abilities);
    EXPECT_EQ(StaticAbilities::deserialize(serialized), abilities);
}

TEST(StaticAbilitiesTest, RoundTripMultipleAbilities)
{
    QList<StaticAbility> abilities;
    abilities.append({StaticScope::OthersYours, 1, 1, {}});
    abilities.append({StaticScope::AllYours, -1, 0, QSet<QString>({"Flying"})});
    const QString serialized = StaticAbilities::serialize(abilities);
    EXPECT_EQ(StaticAbilities::deserialize(serialized), abilities);
}

TEST(StaticAbilitiesTest, DeserializeEmptyStringYieldsEmptyList)
{
    EXPECT_TRUE(StaticAbilities::deserialize("").isEmpty());
}

TEST(StaticAbilitiesTest, DeserializeMalformedEntrySkipsIt)
{
    EXPECT_TRUE(StaticAbilities::deserialize("not-enough-fields").isEmpty());
    EXPECT_TRUE(StaticAbilities::deserialize("o|not-a-number|1|").isEmpty());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
