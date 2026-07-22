#include "counter_text.h"

#include "translate_counter_name.h"

#include <QPainter>

TextCounter::TextCounter(CounterState *state, PlayerLogic *player, QGraphicsItem *parent)
    : AbstractCounter(state, player, true, false, parent, false)
{
}

QRectF TextCounter::boundingRect() const
{
    return {0, 0, 90, 24};
}

void TextCounter::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    QFont font("Serif");
    font.setPixelSize(12);
    font.setWeight(QFont::Bold);
    painter->setFont(font);
    painter->setPen(Qt::white);
    painter->drawText(boundingRect(), Qt::AlignVCenter | Qt::AlignLeft,
                      QStringLiteral("%1: %2").arg(TranslateCounterName::getDisplayName(name)).arg(value));
}
