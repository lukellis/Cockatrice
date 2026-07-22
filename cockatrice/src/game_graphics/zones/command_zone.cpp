#include "command_zone.h"

#include "../../game/zones/pile_zone_logic.h"
#include "../board/card_item.h"

#include <QPainter>

namespace
{
const QColor COMMAND_ZONE_ACCENT(230, 190, 80); // matches the Commander Tax counter's color
}

CommandZone::CommandZone(PileZoneLogic *_logic, QGraphicsItem *parent) : PileZone(_logic, parent)
{
    // Unlike other piles (deck/graveyard/exile), which PileZone rotates 90° to render compactly
    // in a sideways stack, the command zone shows the commander in its natural portrait/untapped
    // orientation — overrides PileZone's persistent rotation back to identity.
    setTransform(QTransform());
}

void CommandZone::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    // A faint background tint, drawn first, so the zone is still identifiable even when empty
    // (e.g. the commander is out on the battlefield) and not just when it holds a visible card.
    QColor tint = COMMAND_ZONE_ACCENT;
    tint.setAlpha(50);
    painter->save();
    painter->fillRect(boundingRect(), tint);
    painter->restore();

    // Deliberately not PileZone::paint(): that method assumes (and compensates for) the 90°
    // item-level rotation this class removes in its constructor, so reusing it here would leave
    // the card image and count badge each rotated 90° off from the rest of this class's drawing.
    painter->drawPath(shape());

    if (!getLogic()->getCards().isEmpty()) {
        CardItem *card = getLogic()->getCards().at(0);
        card->paintPicture(painter, card->getTranslatedSize(painter), 0);
    }
    // Deliberately no card-count badge here (unlike the other piles) -- a commander is either in
    // the zone or it isn't, "CMD" plus the card art already says that, and the badge only ever
    // collided with the Tax counter badge in this corner.

    painter->save();
    QPen goldPen(COMMAND_ZONE_ACCENT, 4);
    painter->setPen(goldPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(boundingRect().adjusted(2, 2, -2, -2), 4, 4);

    QFont font; // inherits the app-wide sans-serif default (see main.cpp)
    font.setPixelSize(11);
    font.setWeight(QFont::Bold);
    painter->setFont(font);
    painter->setPen(COMMAND_ZONE_ACCENT);
    painter->drawText(boundingRect(), Qt::AlignHCenter | Qt::AlignBottom, tr("CMD"));
    painter->restore();
}
