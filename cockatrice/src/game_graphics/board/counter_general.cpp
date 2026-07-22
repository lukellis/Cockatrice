#include "counter_general.h"

#include "../../interface/pixel_map_generator.h"
#include "abstract_graphics_item.h"
#include "translate_counter_name.h"

#include <QMap>
#include <QPainter>
#include <QSet>
#include <QSvgRenderer>
#include <libcockatrice/rules/commander_counter_names.h>

namespace
{
// Counters with their own themed icon (mana colors, storm's lightning bolt, poison's droplet --
// see cockatrice/resources/counters/) already read fine without a label; every other counter
// (per-commander tax/damage, and any future colorless-fallback counter) renders as the same
// generic circle (see CounterPixmapGenerator::generatePixmap's fallback), so those get a short
// text label underneath instead.
bool hasOwnIcon(const QString &name)
{
    static const QSet<QString> namesWithIcons = {"w", "u", "b", "r", "g", "storm", "poison"};
    return namesWithIcons.contains(name);
}

// Mana-color counter name -> a small pictograph glyph evoking that color (sun/drop/skull/tree/
// flame -- cockatrice/resources/icons/counter_glyphs/), each with its own transparent background
// and a natural, contrasting tone (not a uniform flat recolor) so it reads clearly against that
// color's own counter sphere. Colorless ("x") deliberately has no entry: no glyph for it.
const QMap<QString, QString> &manaGlyphFiles()
{
    static const QMap<QString, QString> files = {
        {"w", "sun"}, {"u", "drop"}, {"b", "skull"}, {"r", "flame"}, {"g", "tree"},
    };
    return files;
}

// Sized to fit fully inside the counter circle -- no overflow/halo past the counter's own edge.
QPixmap manaGlyphPixmap(const QString &glyphName, int size)
{
    static QMap<QString, QPixmap> cache;
    const QString key = glyphName + QStringLiteral("_") + QString::number(size);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return it.value();
    }

    QSvgRenderer renderer(QStringLiteral("theme:icons/counter_glyphs/%1.svg").arg(glyphName));
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    if (renderer.isValid()) {
        QPainter painter(&pixmap);
        renderer.render(&painter, QRectF(0, 0, size, size));
    }
    cache.insert(key, pixmap);
    return pixmap;
}

QString shortLabelFor(const QString &name)
{
    // Per-commander damage/tax counter names are the full "Commander Damage: <name>"/
    // "Commander Tax: <name>" strings (see commander_counter_names.h) -- too long to fit under a
    // 30px badge, so abbreviate; the full name is still available via the AbstractCounter tooltip.
    if (CommanderCounterNames::isDamageCounter(name)) {
        return QStringLiteral("Dmg");
    }
    if (CommanderCounterNames::isTaxCounter(name)) {
        return QStringLiteral("Tax");
    }
    return TranslateCounterName::getDisplayName(name);
}
} // namespace

GeneralCounter::GeneralCounter(CounterState *state,
                               PlayerLogic *player,
                               bool useNameForShortcut,
                               QGraphicsItem *parent,
                               bool shownInCounterArea,
                               bool interactive)
    : AbstractCounter(state, player, shownInCounterArea, useNameForShortcut, parent, interactive)
{
    setCacheMode(DeviceCoordinateCache);
}

bool GeneralCounter::needsLabel() const
{
    // Only counters that actually live in the shared, position-ambiguous counter column need a
    // label to tell them apart -- one rendered elsewhere (e.g. the Commander Tax badge on the
    // command zone, see PlayerGraphicsItem::onCounterAdded()) is already unambiguous from its
    // position alone, and that corner has no room to spare for one.
    return getShownInCounterArea() && !hasOwnIcon(name);
}

QRectF GeneralCounter::boundingRect() const
{
    qreal labelHeight = needsLabel() ? 12 : 0;
    return QRectF(0, 0, radius * 2, radius * 2 + labelHeight);
}

void GeneralCounter::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    QRectF iconRect(0, 0, radius * 2, radius * 2);
    QRectF mapRect = painter->combinedTransform().mapRect(iconRect);
    int translatedHeight = mapRect.size().height();
    qreal scaleFactor = translatedHeight / iconRect.height();
    QPixmap pixmap = CounterPixmapGenerator::generatePixmap(translatedHeight, name, hovered);

    painter->save();
    resetPainterTransform(painter);
    painter->drawPixmap(QPoint(0, 0), pixmap);

    const auto glyphIt = manaGlyphFiles().constFind(name);
    if (glyphIt != manaGlyphFiles().constEnd()) {
        // Sized to fit well inside the circle (not the full diameter), fully contained -- no
        // watermark/halo bleeding past the counter's own edge.
        const int glyphSize = static_cast<int>(translatedHeight * 0.62);
        const QPixmap glyph = manaGlyphPixmap(glyphIt.value(), glyphSize);
        const qreal xOffset = (translatedHeight - glyphSize) / 2.0;
        // Nudged up slightly from dead-center: these glyphs (pointed-top, rounded-bottom
        // silhouettes) have more visual weight in their lower half, so a geometrically centered
        // placement reads as sitting a bit low.
        const qreal yOffset = xOffset - glyphSize * 0.08;
        painter->drawPixmap(QPointF(xOffset, yOffset), glyph);
    }

    if (value) {
        QFont f; // inherits the app-wide sans-serif default (see main.cpp)
        f.setPixelSize(qMax((int)(radius * scaleFactor), 10));
        f.setWeight(QFont::Bold);
        painter->setFont(f);

        // Local (0,0)-based rect matching the pixmap/glyph coordinate frame above -- using
        // mapRect directly here (its position component, not just its size) drew the numeral
        // offset from the glyph/sphere it's supposed to sit on top of.
        const QRectF textRect(0, 0, translatedHeight, translatedHeight);
        const QString text = QString::number(value);

        // White fill with a dark outline (drawn as an 8-direction 1px-offset shadow) so the
        // numeral stays readable regardless of what's underneath -- a pale sphere (w/u/g/x) or
        // the dark-gray glyph silhouette itself, both of which a flat black or white fill alone
        // would lose contrast against on at least one of them.
        painter->setPen(Qt::black);
        for (qreal dx = -1; dx <= 1; ++dx) {
            for (qreal dy = -1; dy <= 1; ++dy) {
                if (dx != 0 || dy != 0) {
                    painter->drawText(textRect.translated(dx, dy), Qt::AlignCenter, text);
                }
            }
        }
        painter->setPen(Qt::white);
        painter->drawText(textRect, Qt::AlignCenter, text);
    }
    painter->restore();

    if (needsLabel()) {
        // Drawn in plain item-local coordinates (painter is back to its normal transform after
        // the restore() above), unlike the icon/value above which deliberately draws in
        // device-pixel space for crisp caching (see resetPainterTransform()).
        QFont labelFont; // inherits the app-wide sans-serif default (see main.cpp)
        labelFont.setPixelSize(9);
        painter->setFont(labelFont);
        painter->setPen(Qt::white);
        painter->drawText(QRectF(0, radius * 2, radius * 2, 12), Qt::AlignHCenter | Qt::AlignTop, shortLabelFor(name));
    }
}
