#include "commander_deck_validator.h"
#include "deck_list_model.h"
#include "test_card_database_path_provider.h"

#include "gtest/gtest.h"
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/deck_list/deck_list.h>
#include <libcockatrice/interfaces/noop_card_preference_provider.h>
#include <libcockatrice/interfaces/noop_card_set_priority_controller.h>
#include <libcockatrice/utility/card_ref.h>

namespace
{

QSharedPointer<DeckList> makeDeckList(const QString &commanderName)
{
    QSharedPointer<DeckList> deckList(new DeckList());
    deckList->setGameFormat("commander");
    deckList->setBannerCard(CardRef{commanderName});
    return deckList;
}

// Adds `count` copies of `cardName` to the deck's main zone.
void addCopies(DeckListModel &model, const QString &cardName, int count)
{
    ExactCard card = CardDatabaseManager::query()->getCard(CardRef{cardName});
    ASSERT_TRUE(static_cast<bool>(card)) << "fixture card not found: " << cardName.toStdString();
    for (int i = 0; i < count; ++i) {
        model.addCard(card, DECK_ZONE_MAIN);
    }
}

bool errorsContain(const QStringList &errors, const QString &needle)
{
    return std::any_of(errors.cbegin(), errors.cend(), [&](const QString &e) { return e.contains(needle); });
}

class CommanderDeckValidatorTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        CardDatabaseManager::setCardPreferenceProvider(new NoopCardPreferenceProvider());
        CardDatabaseManager::setCardDatabasePathProvider(new TestCommanderCardDatabasePathProvider());
        CardDatabaseManager::setCardSetPriorityController(new NoopCardSetPriorityController());
        CardDatabaseManager::getInstance()->loadCardDatabases();
        ASSERT_EQ(Ok, CardDatabaseManager::getInstance()->getLoadStatus());
    }
};

TEST_F(CommanderDeckValidatorTest, ValidHundredCardDeckPasses)
{
    auto deckList = makeDeckList("Test Commander");
    DeckListModel model(nullptr, deckList);
    addCopies(model, "Test Plains", 99); // 99 + the commander itself = 100

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_TRUE(result.isValid) << result.errors.join("; ").toStdString();
    EXPECT_TRUE(result.errors.isEmpty());
}

TEST_F(CommanderDeckValidatorTest, NoCommanderDesignatedFails)
{
    QSharedPointer<DeckList> deckList(new DeckList());
    deckList->setGameFormat("commander");
    DeckListModel model(nullptr, deckList);

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(errorsContain(result.errors, "No commander has been designated"));
}

TEST_F(CommanderDeckValidatorTest, UnknownCommanderFails)
{
    auto deckList = makeDeckList("Nonexistent Card");
    DeckListModel model(nullptr, deckList);

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(errorsContain(result.errors, "could not be found in the card database"));
}

TEST_F(CommanderDeckValidatorTest, NonLegendaryCommanderFails)
{
    // "Off Color Card" is an ordinary creature, not legendary and has no
    // "can be your commander" text, so it is not a legal commander.
    auto deckList = makeDeckList("Off Color Card");
    DeckListModel model(nullptr, deckList);

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(errorsContain(result.errors, "is not a legal commander"));
}

TEST_F(CommanderDeckValidatorTest, TextGrantedCommanderEligibilityIsAccepted)
{
    auto deckList = makeDeckList("Can Be Your Commander Card");
    DeckListModel model(nullptr, deckList);

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(errorsContain(result.errors, "is not a legal commander"));
}

TEST_F(CommanderDeckValidatorTest, WrongCardCountFails)
{
    auto deckList = makeDeckList("Test Commander");
    DeckListModel model(nullptr, deckList);
    addCopies(model, "Test Plains", 5); // 5 + commander = 6, not 100

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(errorsContain(result.errors, "exactly 100 cards"));
}

TEST_F(CommanderDeckValidatorTest, ColorIdentityViolationFails)
{
    // "Test Commander" is GW; "Off Color Card" is blue.
    auto deckList = makeDeckList("Test Commander");
    DeckListModel model(nullptr, deckList);
    addCopies(model, "Off Color Card", 1);

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(errorsContain(result.errors, "outside the commander's color identity"));
}

TEST_F(CommanderDeckValidatorTest, InColorCardDoesNotTriggerColorIdentityError)
{
    auto deckList = makeDeckList("Test Commander");
    DeckListModel model(nullptr, deckList);
    addCopies(model, "Ally Card", 1); // green, within the GW commander's identity

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(errorsContain(result.errors, "outside the commander's color identity"));
}

TEST_F(CommanderDeckValidatorTest, BannedCardFails)
{
    auto deckList = makeDeckList("Test Commander");
    DeckListModel model(nullptr, deckList);
    addCopies(model, "Banned Card", 1);

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(errorsContain(result.errors, "is not legal in this Commander deck"));
}

TEST_F(CommanderDeckValidatorTest, SingletonViolationFails)
{
    auto deckList = makeDeckList("Test Commander");
    DeckListModel model(nullptr, deckList);
    addCopies(model, "Ally Card", 2); // max 1 copy allowed for a "legal" non-basic card

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(errorsContain(result.errors, "is not legal in this Commander deck"));
}

TEST_F(CommanderDeckValidatorTest, UnlimitedBasicLandsAreExemptFromSingletonRule)
{
    auto deckList = makeDeckList("Test Commander");
    DeckListModel model(nullptr, deckList);
    addCopies(model, "Test Plains", 10);

    auto result = CommanderDeckValidator::validate(model);
    EXPECT_FALSE(errorsContain(result.errors, "is not legal in this Commander deck"));
}

} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
