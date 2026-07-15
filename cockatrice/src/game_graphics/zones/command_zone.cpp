#include "command_zone.h"

#include <QPainter>

namespace
{
const QColor CommandZoneAccent(230, 190, 80); // matches the Commander Tax counter's color
}

CommandZone::CommandZone(PileZoneLogic *_logic, QGraphicsItem *parent) : PileZone(_logic, parent)
{
}

void CommandZone::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    // A faint background tint, drawn first, so the zone is still identifiable even when empty
    // (e.g. the commander is out on the battlefield) and not just when it holds a visible card.
    QColor tint = CommandZoneAccent;
    tint.setAlpha(50);
    painter->save();
    painter->fillRect(boundingRect(), tint);
    painter->restore();

    // PileZone::paint() leaves the painter's transform permanently altered (it rotates without
    // restoring, to keep its count badge upright) — bracket it so our own drawing below happens
    // in the same coordinate space as boundingRect(), matching PileZone::paint()'s own first line.
    painter->save();
    PileZone::paint(painter, option, widget);
    painter->restore();

    painter->save();
    QPen goldPen(CommandZoneAccent, 4);
    painter->setPen(goldPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(boundingRect().adjusted(2, 2, -2, -2), 4, 4);

    QFont font(QStringLiteral("Serif"));
    font.setPixelSize(11);
    font.setWeight(QFont::Bold);
    painter->setFont(font);
    painter->setPen(CommandZoneAccent);
    painter->drawText(boundingRect(), Qt::AlignHCenter | Qt::AlignBottom, tr("CMD"));
    painter->restore();
}
