#include "dlg_choose_generic_mana_payment.h"

#include "../board/translate_counter_name.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <libcockatrice/rules/rules_engine.h>
#include <utility>

DlgChooseGenericManaPayment::DlgChooseGenericManaPayment(QWidget *parent,
                                                         const ManaCost &cost,
                                                         const QMap<QString, int> &remainingPool,
                                                         const QMap<QString, int> &prefill)
    : QDialog(parent), genericAmount(cost.generic)
{
    setWindowTitle(tr("Choose Mana Payment"));

    auto *form = new QFormLayout;
    for (const QString &counterName : Rules::RulesEngine::manaCounterNames()) {
        const int available = remainingPool.value(counterName, 0);
        if (available <= 0) {
            continue;
        }
        auto *spinBox = new QSpinBox(this);
        spinBox->setRange(0, available);
        spinBox->setValue(prefill.value(counterName, 0));
        connect(spinBox, &QSpinBox::valueChanged, this, &DlgChooseGenericManaPayment::updateTotal);
        form->addRow(TranslateCounterName::getDisplayName(counterName), spinBox);
        spinBoxes.insert(counterName, spinBox);
    }

    totalLabel = new QLabel(this);

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(new QLabel(
        tr("This spell's generic cost (%1) can be paid with more than one color. Choose how to pay it:")
            .arg(genericAmount)));
    mainLayout->addItem(form);
    mainLayout->addWidget(totalLabel);
    mainLayout->addWidget(buttonBox);
    setLayout(mainLayout);

    updateTotal();
}

void DlgChooseGenericManaPayment::updateTotal()
{
    int total = 0;
    for (const QSpinBox *spinBox : std::as_const(spinBoxes)) {
        total += spinBox->value();
    }
    totalLabel->setText(tr("Total: %1 / %2").arg(total).arg(genericAmount));
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(total == genericAmount);
}

QMap<QString, int> DlgChooseGenericManaPayment::chosenGenericSplit() const
{
    QMap<QString, int> split;
    for (auto it = spinBoxes.constBegin(); it != spinBoxes.constEnd(); ++it) {
        if (it.value()->value() > 0) {
            split.insert(it.key(), it.value()->value());
        }
    }
    return split;
}
