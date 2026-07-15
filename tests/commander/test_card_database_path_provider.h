#ifndef COCKATRICE_COMMANDER_TEST_CARD_DATABASE_PATH_PROVIDER_H
#define COCKATRICE_COMMANDER_TEST_CARD_DATABASE_PATH_PROVIDER_H

#include <libcockatrice/interfaces/interface_card_database_path_provider.h>

// A dedicated fixture (isolated from tests/carddatabase/data) so Commander-specific
// cards/formats don't affect the exact card/set counts asserted by other tests.
class TestCommanderCardDatabasePathProvider : public ICardDatabasePathProvider
{

public:
    QString getCardDatabasePath() const override
    {
        return QString("%1/cards.xml").arg(CARDDB_DATADIR);
    }
    QString getCustomCardDatabasePath() const override
    {
        return QString();
    }
    QString getTokenDatabasePath() const override
    {
        return QString();
    }
    QString getSpoilerCardDatabasePath() const override
    {
        return QString();
    }
};

#endif // COCKATRICE_COMMANDER_TEST_CARD_DATABASE_PATH_PROVIDER_H
