#include "gtest/gtest.h"
#include <libcockatrice/card/card_info.h>
#include <libcockatrice/rules/commander_rules.h>

namespace
{

CardInfoPtr
makeCard(const QString &name, const QString &text, const QString &type, const QString &colors, const QString &manaCost)
{
    QVariantHash props;
    props.insert("type", type);
    props.insert("colors", colors);
    props.insert("manacost", manaCost);
    return CardInfo::newInstance(name, text, false, props, {}, {}, {}, {});
}

TEST(CommanderRulesTest, LegendaryCreatureCanBeCommander)
{
    auto card = makeCard("Test Legend", "", "Legendary Creature — Human", "W", "{1}{W}");
    EXPECT_TRUE(CommanderRules::canBeCommander(*card));
}

TEST(CommanderRulesTest, NonLegendaryCreatureCannotBeCommander)
{
    auto card = makeCard("Test Bear", "", "Creature — Bear", "G", "{1}{G}");
    EXPECT_FALSE(CommanderRules::canBeCommander(*card));
}

TEST(CommanderRulesTest, LegendaryNonCreatureCannotBeCommander)
{
    auto card = makeCard("Test Artifact", "", "Legendary Artifact", "", "{3}");
    EXPECT_FALSE(CommanderRules::canBeCommander(*card));
}

TEST(CommanderRulesTest, CardTextGrantingCommanderEligibilityIsHonored)
{
    auto card = makeCard("Test Background", "This card can be your commander.", "Legendary Sorcery", "B", "{1}{B}");
    EXPECT_TRUE(CommanderRules::canBeCommander(*card));
}

TEST(CommanderRulesTest, CanBeCommanderTextMatchIsCaseInsensitive)
{
    auto card = makeCard("Test Background", "THIS CARD CAN BE YOUR COMMANDER.", "Legendary Sorcery", "B", "{1}{B}");
    EXPECT_TRUE(CommanderRules::canBeCommander(*card));
}

TEST(CommanderRulesTest, ColorIdentityIncludesPrintedColors)
{
    auto card = makeCard("Test Legend", "", "Legendary Creature — Human", "GW", "{2}{G}{W}");
    QSet<QChar> identity = CommanderRules::colorIdentity(*card);
    EXPECT_EQ(identity, QSet<QChar>({QChar('G'), QChar('W')}));
}

TEST(CommanderRulesTest, ColorIdentityIncludesManaCostSymbols)
{
    // Colorless-in-`colors` card (e.g. an artifact creature) with a colored activation cost.
    auto card = makeCard("Test Artifact Creature", "", "Artifact Creature — Construct", "", "{1}{U}{U}");
    QSet<QChar> identity = CommanderRules::colorIdentity(*card);
    EXPECT_EQ(identity, QSet<QChar>({QChar('U')}));
}

TEST(CommanderRulesTest, ColorIdentityIncludesRulesTextManaSymbols)
{
    auto card = makeCard("Test Rider", "{T}: Add {R}.", "Creature — Human", "W", "{1}{W}");
    QSet<QChar> identity = CommanderRules::colorIdentity(*card);
    EXPECT_EQ(identity, QSet<QChar>({QChar('W'), QChar('R')}));
}

TEST(CommanderRulesTest, ColorIdentityIncludesHybridManaSymbols)
{
    auto card = makeCard("Test Hybrid", "", "Creature — Human", "", "{2/U}{2/B}");
    QSet<QChar> identity = CommanderRules::colorIdentity(*card);
    EXPECT_EQ(identity, QSet<QChar>({QChar('U'), QChar('B')}));
}

TEST(CommanderRulesTest, ColorIdentityExcludesReminderText)
{
    auto card = makeCard("Test Reminder", "Flying (Reminder text mentioning {U} should not count.)", "Creature — Bird",
                         "W", "{2}{W}");
    QSet<QChar> identity = CommanderRules::colorIdentity(*card);
    EXPECT_EQ(identity, QSet<QChar>({QChar('W')}));
}

TEST(CommanderRulesTest, ColorIdentityIgnoresGenericManaSymbols)
{
    auto card = makeCard("Test Colorless", "", "Artifact", "", "{5}");
    QSet<QChar> identity = CommanderRules::colorIdentity(*card);
    EXPECT_TRUE(identity.isEmpty());
}

TEST(CommanderRulesTest, IsWithinColorIdentityAcceptsSubset)
{
    QSet<QChar> commanderIdentity = {QChar('G'), QChar('W')};
    QSet<QChar> cardIdentity = {QChar('G')};
    EXPECT_TRUE(CommanderRules::isWithinColorIdentity(cardIdentity, commanderIdentity));
}

TEST(CommanderRulesTest, IsWithinColorIdentityAcceptsColorlessCard)
{
    QSet<QChar> commanderIdentity = {QChar('G'), QChar('W')};
    QSet<QChar> cardIdentity = {};
    EXPECT_TRUE(CommanderRules::isWithinColorIdentity(cardIdentity, commanderIdentity));
}

TEST(CommanderRulesTest, IsWithinColorIdentityRejectsOutsideColor)
{
    QSet<QChar> commanderIdentity = {QChar('G'), QChar('W')};
    QSet<QChar> cardIdentity = {QChar('U')};
    EXPECT_FALSE(CommanderRules::isWithinColorIdentity(cardIdentity, commanderIdentity));
}

TEST(CommanderRulesTest, FormatUsesColorIdentityForCommanderFamily)
{
    for (const QString &format :
         {"commander", "duel", "brawl", "standardbrawl", "oathbreaker", "paupercommander", "predh"}) {
        EXPECT_TRUE(CommanderRules::formatUsesColorIdentity(format)) << format.toStdString();
    }
}

TEST(CommanderRulesTest, FormatUsesColorIdentityIsCaseInsensitive)
{
    EXPECT_TRUE(CommanderRules::formatUsesColorIdentity("Commander"));
    EXPECT_TRUE(CommanderRules::formatUsesColorIdentity("COMMANDER"));
}

TEST(CommanderRulesTest, FormatUsesColorIdentityRejectsNonCommanderFormats)
{
    for (const QString &format : {"standard", "modern", "legacy", "pauper", ""}) {
        EXPECT_FALSE(CommanderRules::formatUsesColorIdentity(format)) << format.toStdString();
    }
}

TEST(CommanderRulesTest, GameTypeLabelIsCommanderMatchesCommanderLabels)
{
    for (const QString &label : {"Commander", "commander", "COMMANDER", "Commander (1v1)", "Duel Commander"}) {
        EXPECT_TRUE(CommanderRules::gameTypeLabelIsCommander(label)) << label.toStdString();
    }
}

TEST(CommanderRulesTest, GameTypeLabelIsCommanderRejectsOtherLabels)
{
    for (const QString &label : {"Standard", "2 Player", "Draft", ""}) {
        EXPECT_FALSE(CommanderRules::gameTypeLabelIsCommander(label)) << label.toStdString();
    }
}

} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
