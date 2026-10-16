#include "ui/camerasettingswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace calib
{

namespace
{


int snapToStep(const CameraControlInfo& control, int value)
{
    const int clamped = std::clamp(value, control.minimum, control.maximum);
    const int steps = (clamped - control.minimum + control.step / 2) / control.step;
    return std::min(control.minimum + steps * control.step, control.maximum);
}

QString valueText(CameraControlId id, int value)
{
    return QString::fromStdString(formatControlValue(id, value));
}

QString tooltipFor(CameraControlId id)
{
    switch(id)
    {
    case CameraControlId::Focus:
        return QObject::tr("Lens focus distance. Turn autofocus off for calibration: "
                           "the focal length must not change between captures.");
    case CameraControlId::Exposure:
        return QObject::tr("Shutter time. A long exposure brightens a dark scene but "
                           "limits the frame rate (1/30 s or shorter keeps 30 fps).");
    case CameraControlId::Gain:
        return QObject::tr("Amplifies the sensor signal: a brighter image at the cost of more noise.");
    case CameraControlId::WhiteBalance: return QObject::tr("Colour temperature of the light source, in Kelvin.");
    case CameraControlId::BacklightCompensation:
        return QObject::tr("Brightens the subject when a bright light is behind it.");
    default: return QString();
    }
}

struct ControlGroup
{
    QString title;
    std::vector<CameraControlId> ids;
};

std::vector<ControlGroup> controlGroups(void)
{
    return
    {
        {QObject::tr("Focus and exposure"), {CameraControlId::Focus, CameraControlId::Exposure, CameraControlId::Gain}},
        {QObject::tr("Image"),
        {CameraControlId::Brightness, CameraControlId::Contrast, CameraControlId::Saturation, CameraControlId::Hue,
         CameraControlId::Gamma, CameraControlId::Sharpness, CameraControlId::WhiteBalance,
         CameraControlId::BacklightCompensation}},
    };
}


} // namespace

CameraSettingsWidget::CameraSettingsWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* formatBox = new QGroupBox(tr("Video format"), this);
    auto* formatLayout = new QVBoxLayout(formatBox);
    modeCombo = new QComboBox(formatBox);
    modeCombo->setToolTip(tr("Resolution, pixel format and maximum frame rate offered by the driver"));
    formatLayout->addWidget(modeCombo);
    layout->addWidget(formatBox);

    auto* orientationBox = new QGroupBox(tr("Orientation"), this);
    auto* orientationLayout = new QVBoxLayout(orientationBox);
    flipXBox = new QCheckBox(tr("Mirror horizontally (flip X)"), orientationBox);
    flipYBox = new QCheckBox(tr("Flip vertically (flip Y)"), orientationBox);
    orientationLayout->addWidget(flipXBox);
    orientationLayout->addWidget(flipYBox);
    layout->addWidget(orientationBox);

    controlsHost = new QWidget(this);
    controlsLayout = new QVBoxLayout(controlsHost);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(controlsHost);

    noControlsLabel = new QLabel(this);
    noControlsLabel->setWordWrap(true);
    layout->addWidget(noControlsLabel);

    resetButton = new QPushButton(tr("Reset to defaults"), this);
    resetButton->setToolTip(tr("Factory values, with automatic mode wherever the camera supports it"));
    layout->addWidget(resetButton);
    layout->addStretch(1);

    connect(modeCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) { emit modeRequested(modeCombo->itemData(index).toInt()); });

    const auto emitFlip = [this](bool) { emit flipChanged(flipXBox->isChecked(), flipYBox->isChecked()); };
    connect(flipXBox, &QCheckBox::toggled, this, emitFlip);
    connect(flipYBox, &QCheckBox::toggled, this, emitFlip);

    connect(resetButton, &QPushButton::clicked, this, &CameraSettingsWidget::resetRequested);

    clearCamera();
}

void CameraSettingsWidget::clearCamera(void)
{
    modeCombo->clear();
    modeCombo->setEnabled(false);
    removeControlRows();
    noControlsLabel->setText(tr("No camera connected."));
    noControlsLabel->setVisible(true);
    resetButton->setEnabled(false);
}

void CameraSettingsWidget::setCamera(const CameraInfo& camera, int modeIndex,
                                     const QVector<CameraControlInfo>& controls,
                                     const QVector<CameraControlState>& states, bool flipHorizontal, bool flipVertical)
{
    {
        const QSignalBlocker blockX(flipXBox);
        const QSignalBlocker blockY(flipYBox);
        flipXBox->setChecked(flipHorizontal);
        flipYBox->setChecked(flipVertical);
    }

    modeCombo->clear();
    for(size_t i = 0; i < camera.modes.size(); ++i)
    {
        modeCombo->addItem(QString::fromStdString(describe(camera.modes[i])), static_cast<int>(i));
    }
    if(camera.modes.empty())
    {
        modeCombo->addItem(tr("Driver default"), -1);
    }
    modeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    modeCombo->setEnabled(camera.modes.size() > 1);

    removeControlRows();
    for(const ControlGroup& group : controlGroups())
    {
        QGroupBox* box = nullptr;
        QGridLayout* grid = nullptr;
        int gridRow = 0;
        for(const CameraControlId id : group.ids)
        {
            for(int i = 0; i < controls.size(); ++i)
            {
                if(controls.at(i).id != id)
                {
                    continue;
                }
                if(!box)
                {
                    box = new QGroupBox(group.title, controlsHost);
                    grid = new QGridLayout(box);
                    grid->setColumnStretch(1, 1);
                    controlsLayout->addWidget(box);
                }
                const CameraControlState state = i < states.size() ? states.at(i) : CameraControlState{};
                addControlRow(grid, gridRow++, controls.at(i), state);
            }
        }
    }

    noControlsLabel->setText(tr("This camera exposes no adjustable controls."));
    noControlsLabel->setVisible(controls.isEmpty());
    resetButton->setEnabled(!controls.isEmpty());
}

void CameraSettingsWidget::updateControl(CameraControlId id, const CameraControlState& state)
{
    const auto it = rows.find(id);
    if(it != rows.end())
    {
        showState(it->second, state);
    }
}

void CameraSettingsWidget::removeControlRows(void)
{
    rows.clear();
    const QList<QWidget*> children = controlsHost->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    qDeleteAll(children);
}

void CameraSettingsWidget::addControlRow(QGridLayout* grid, int gridRow, const CameraControlInfo& control,
                                         const CameraControlState& state)
{
    QWidget* box = grid->parentWidget();
    const CameraControlId id = control.id;

    ControlRow row;
    row.info = control;

    auto* nameLabel = new QLabel(QString::fromLatin1(toString(id)), box);
    nameLabel->setToolTip(tooltipFor(id));

    row.slider = new QSlider(Qt::Horizontal, box);
    row.slider->setRange(control.minimum, control.maximum);
    row.slider->setSingleStep(control.step);
    row.slider->setPageStep(std::max(control.step, (control.maximum - control.minimum) / 10));
    row.slider->setToolTip(tooltipFor(id));

    row.valueLabel = new QLabel(box);
    row.valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.valueLabel->setMinimumWidth(row.valueLabel->fontMetrics().horizontalAdvance(QStringLiteral("1/16384 s")));

    grid->addWidget(nameLabel, gridRow, 0);
    grid->addWidget(row.slider, gridRow, 1);
    grid->addWidget(row.valueLabel, gridRow, 2);

    if(control.supportsAuto)
    {
        row.autoBox = new QCheckBox(id == CameraControlId::Focus ? tr("Autofocus") : tr("Auto"), box);
        grid->addWidget(row.autoBox, gridRow, 3);
    }

    connect(row.slider, &QSlider::valueChanged, this,
            [this, id](int value)
            {
                ControlRow& current = rows.at(id);
                const int snapped = snapToStep(current.info, value);
                current.valueLabel->setText(valueText(id, snapped));
                emit controlChanged(id, CameraControlState{snapped, false});
            });

    if(row.autoBox)
    {
        connect(row.autoBox, &QCheckBox::toggled, this,
                [this, id](bool automatic)
                {
                    ControlRow& current = rows.at(id);
                    current.slider->setEnabled(current.info.supportsManual && !automatic);
                    emit controlChanged(
                        id, CameraControlState{snapToStep(current.info, current.slider->value()), automatic});
                });
    }

    ControlRow& stored = rows[id] = row;
    showState(stored, state);
}

void CameraSettingsWidget::showState(ControlRow& row, const CameraControlState& state)
{
    const QSignalBlocker sliderBlocker(row.slider);
    row.slider->setValue(state.value);
    row.valueLabel->setText(valueText(row.info.id, state.value));
    if(row.autoBox)
    {
        const QSignalBlocker autoBlocker(row.autoBox);
        row.autoBox->setChecked(state.automatic);
    }
    row.slider->setEnabled(row.info.supportsManual && !(state.automatic && row.autoBox));
}


} // namespace calib
