#include "dlg_choose_variable_mana_cost.h"

#include "../board/translate_counter_name.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

DlgChooseVariableManaCost::DlgChooseVariableManaCost(QWidget *parent,
                                                     const ManaCost &cost,
                                                     const Rules::RulesEngine::ManaCostChoices &choices,
                                                     const QMap<QString, int> &pool)
    : QDialog(parent), manaCostChoices(choices)
{
    setWindowTitle(tr("Choose Mana Payment"));

    auto *mainLayout = new QVBoxLayout;
    auto *form = new QFormLayout;

    if (cost.xCount > 0) {
        int poolTotal = 0;
        for (const int amount : pool.values()) {
            poolTotal += amount;
        }
        xSpinBox = new QSpinBox(this);
        xSpinBox->setRange(0, poolTotal);
        form->addRow(tr("Choose X:"), xSpinBox);
    }

    for (const auto &hybrid : choices.hybridChoices) {
        if (!hybrid.ambiguous) {
            hybridGroups.append(nullptr);
            continue;
        }
        auto *group = new QButtonGroup(this);
        auto *rowLayout = new QVBoxLayout;
        auto *colorAButton = new QRadioButton(TranslateCounterName::getDisplayName(hybrid.colorA), this);
        auto *colorBButton = new QRadioButton(TranslateCounterName::getDisplayName(hybrid.colorB), this);
        group->addButton(colorAButton, 0);
        group->addButton(colorBButton, 1);
        (hybrid.defaultColor == hybrid.colorA ? colorAButton : colorBButton)->setChecked(true);
        rowLayout->addWidget(colorAButton);
        rowLayout->addWidget(colorBButton);
        form->addRow(tr("Pay hybrid %1/%2 with:")
                         .arg(TranslateCounterName::getDisplayName(hybrid.colorA),
                              TranslateCounterName::getDisplayName(hybrid.colorB)),
                     rowLayout);
        hybridGroups.append(group);
    }

    for (const auto &phyrexian : choices.phyrexianChoices) {
        if (!phyrexian.ambiguous) {
            phyrexianGroups.append(nullptr);
            continue;
        }
        auto *group = new QButtonGroup(this);
        auto *rowLayout = new QVBoxLayout;
        auto *manaButton =
            new QRadioButton(tr("Pay 1 %1").arg(TranslateCounterName::getDisplayName(phyrexian.color)), this);
        auto *lifeButton = new QRadioButton(tr("Pay 2 life"), this);
        group->addButton(manaButton, 0);
        group->addButton(lifeButton, 1);
        manaButton->setChecked(true); // default: don't touch life unless the caster opts in
        rowLayout->addWidget(manaButton);
        rowLayout->addWidget(lifeButton);
        form->addRow(tr("Pay Phyrexian %1 with:").arg(TranslateCounterName::getDisplayName(phyrexian.color)),
                     rowLayout);
        phyrexianGroups.append(group);
    }

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(new QLabel(tr("This spell's cost includes hybrid, Phyrexian, and/or variable ({X}) mana. "
                                        "Choose how to pay it:")));
    mainLayout->addItem(form);
    mainLayout->addWidget(buttonBox);
    setLayout(mainLayout);
}

bool DlgChooseVariableManaCost::hasAnythingToAsk() const
{
    if (xSpinBox) {
        return true;
    }
    for (const auto *group : hybridGroups) {
        if (group) {
            return true;
        }
    }
    for (const auto *group : phyrexianGroups) {
        if (group) {
            return true;
        }
    }
    return false;
}

int DlgChooseVariableManaCost::chosenXValue() const
{
    return xSpinBox ? xSpinBox->value() : 0;
}

QList<QString> DlgChooseVariableManaCost::chosenHybridColors() const
{
    QList<QString> result;
    for (int i = 0; i < manaCostChoices.hybridChoices.size(); ++i) {
        const auto &choice = manaCostChoices.hybridChoices[i];
        QButtonGroup *group = hybridGroups[i];
        if (!group) {
            result.append(choice.defaultColor);
            continue;
        }
        result.append(group->checkedId() == 0 ? choice.colorA : choice.colorB);
    }
    return result;
}

QList<bool> DlgChooseVariableManaCost::chosenPhyrexianPayLife() const
{
    QList<bool> result;
    for (int i = 0; i < manaCostChoices.phyrexianChoices.size(); ++i) {
        QButtonGroup *group = phyrexianGroups[i];
        if (!group) {
            result.append(manaCostChoices.phyrexianChoices[i].defaultPayLife);
            continue;
        }
        result.append(group->checkedId() == 1);
    }
    return result;
}
