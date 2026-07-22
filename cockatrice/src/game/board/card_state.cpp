#include "card_state.h"

void CardState::resetState(bool keepAnnotations)
{
    attacking = false;
    attackTargetPlayerId = -1;
    blockedPlayerId = -1;
    blockedCardId = -1;
    counters.clear();
    pt.clear();
    effectivePt.clear();
    if (!keepAnnotations) {
        annotation.clear();
    }
    attachedTo = nullptr;
}

void CardState::setZone(CardZoneLogic *_zone)
{
    if (zone == _zone) {
        return;
    }

    zone = _zone;
    emit zoneChanged(this, zone);
    emit stateChanged();
}

void CardState::setAttacking(bool _attacking)
{
    if (attacking == _attacking) {
        return;
    }
    attacking = _attacking;
    emit attackingChanged(_attacking);
    emit stateChanged();
}

void CardState::setAttackTargetPlayerId(int _playerId)
{
    if (attackTargetPlayerId == _playerId) {
        return;
    }
    attackTargetPlayerId = _playerId;
    emit stateChanged();
}

void CardState::setBlocked(int _playerId, int _cardId)
{
    if (blockedPlayerId == _playerId && blockedCardId == _cardId) {
        return;
    }
    blockedPlayerId = _playerId;
    blockedCardId = _cardId;
    emit stateChanged();
}

void CardState::insertCounter(int id, int value)
{
    counters.insert(id, value);

    emit countersChanged(counters);
    emit stateChanged();
}

void CardState::setCounter(int id, int value)
{
    if (value) {
        counters[id] = value;
    } else {
        counters.remove(id);
    }

    emit countersChanged(counters);
    emit stateChanged();
}

void CardState::clearCounters()
{
    counters.clear();
    emit countersChanged(counters);
    emit stateChanged();
}

void CardState::setAnnotation(const QString &_annotation)
{
    if (annotation == _annotation) {
        return;
    }
    annotation = _annotation;
    emit annotationChanged(annotation);
    emit stateChanged();
}

void CardState::setPT(const QString &_pt)
{
    if (pt == _pt) {
        return;
    }
    pt = _pt;
    emit ptChanged(pt);
    emit stateChanged();
}

void CardState::setEffectivePT(const QString &_effectivePt)
{
    if (effectivePt == _effectivePt) {
        return;
    }
    effectivePt = _effectivePt;
    emit effectivePtChanged(effectivePt);
    emit stateChanged();
}

void CardState::setDoesntUntap(bool _doesntUntap)
{
    if (doesntUntap == _doesntUntap) {
        return;
    }
    doesntUntap = _doesntUntap;
    emit doesntUntapChanged(_doesntUntap);
    emit stateChanged();
}

void CardState::setDestroyOnZoneChange(bool _destroyOnZoneChange)
{
    if (destroyOnZoneChange == _destroyOnZoneChange) {
        return;
    }

    destroyOnZoneChange = _destroyOnZoneChange;
    emit destroyOnZoneChangeChanged(_destroyOnZoneChange);
    emit stateChanged();
}

void CardState::setAttachedTo(CardItem *_attachedTo)
{
    if (attachedTo == _attachedTo) {
        return;
    }
    attachedTo = _attachedTo;
    emit attachedToChanged(_attachedTo);
    emit stateChanged();
}