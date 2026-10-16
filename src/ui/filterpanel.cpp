#include "ui/filterpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <utility>

namespace calib
{


namespace
{


double clipLimitFromSlider(int position)
{
    return position / 10.0;
}
int oddFromSlider(int position)
{
    return 2 * position + 1;
}

QString pixels(int size)
{
    return QObject::tr("%1 px").arg(size);
}


} // namespace

FilterPanel::FilterPanel(QWidget* parent)
    : QWidget(parent)
{
    const FilterSettings defaults;
    auto* layout = new QVBoxLayout(this);

    auto* colorBox = new QGroupBox(tr("Colour"), this);
    auto* colorGrid = new QGridLayout(colorBox);
    grayscaleBox = new QCheckBox(tr("Grayscale"), colorBox);
    negativeBox = new QCheckBox(tr("Negative (invert colours)"), colorBox);
    colorMapCombo = new QComboBox(colorBox);
    for(const FalseColorMap map : {FalseColorMap::None, FalseColorMap::Jet, FalseColorMap::Turbo,
                                   FalseColorMap::Viridis, FalseColorMap::Inferno, FalseColorMap::Hot})
    {
        colorMapCombo->addItem(QString::fromLatin1(toString(map)), static_cast<int>(map));
    }
    colorMapCombo->setToolTip(tr("Maps intensity to colours: small brightness differences become visible"));
    colorGrid->addWidget(grayscaleBox, 0, 0, 1, 3);
    colorGrid->addWidget(negativeBox, 1, 0, 1, 3);
    colorGrid->addWidget(new QLabel(tr("False colour"), colorBox), 2, 0);
    colorGrid->addWidget(colorMapCombo, 2, 1, 1, 2);
    colorGrid->setColumnStretch(1, 1);
    layout->addWidget(colorBox);





    auto* toneBox = new QGroupBox(tr("Contrast and sharpness"), this);
    auto* toneGrid = new QGridLayout(toneBox);
    equalizeBox = new QCheckBox(tr("Equalize contrast (CLAHE)"), toneBox);
    equalizeBox->setToolTip(tr("Adaptive histogram equalization: brings out detail in dark and bright areas"));
    toneGrid->addWidget(equalizeBox, 0, 0, 1, 3);
    clipLimit = addSliderRow(toneGrid, 1, tr("Strength"), 5, 80, static_cast<int>(defaults.claheClipLimit * 10),
                             [](int v) { return QString::number(clipLimitFromSlider(v), 'f', 1); });
    blurBox = new QCheckBox(tr("Blur (Gaussian)"), toneBox);
    toneGrid->addWidget(blurBox, 2, 0, 1, 3);
    blurSize = addSliderRow(toneGrid, 3, tr("Kernel"), 1, 15, 2, [](int v) { return pixels(oddFromSlider(v)); });
    sharpenBox = new QCheckBox(tr("Sharpen (unsharp mask)"), toneBox);
    toneGrid->addWidget(sharpenBox, 4, 0, 1, 3);
    sharpenAmount = addSliderRow(toneGrid, 5, tr("Amount"), 10, 300, 100,
                                 [](int v) { return QStringLiteral("%1 %").arg(v); });
    toneGrid->setColumnStretch(1, 1);
    layout->addWidget(toneBox);





    auto* structureBox = new QGroupBox(tr("Binarize / edges"), this);
    auto* structureGrid = new QGridLayout(structureBox);
    structureCombo = new QComboBox(structureBox);
    for(const StructureFilter filter :
        {StructureFilter::None, StructureFilter::FixedThreshold, StructureFilter::OtsuThreshold,
         StructureFilter::AdaptiveThreshold, StructureFilter::CannyEdges})
    {
        structureCombo->addItem(QString::fromLatin1(toString(filter)), static_cast<int>(filter));
    }
    structureGrid->addWidget(structureCombo, 0, 0, 1, 3);
    threshold = addSliderRow(structureGrid, 1, tr("Threshold"), 0, 255, defaults.threshold,
                             [](int v) { return QString::number(v); });
    blockSize = addSliderRow(structureGrid, 2, tr("Block"), 1, 50, (defaults.adaptiveBlockSize - 1) / 2,
                             [](int v) { return pixels(oddFromSlider(v)); });
    offset = addSliderRow(structureGrid, 3, tr("Offset"), -30, 30, defaults.adaptiveOffset,
                          [](int v) { return QString::number(v); });
    cannyLow = addSliderRow(structureGrid, 4, tr("Low"), 0, 500, defaults.cannyLow,
                            [](int v) { return QString::number(v); });
    cannyHigh = addSliderRow(structureGrid, 5, tr("High"), 0, 500, defaults.cannyHigh,
                             [](int v) { return QString::number(v); });
    blockSize.slider->setToolTip(tr("Size of the neighbourhood the local threshold is computed from"));
    offset.slider->setToolTip(tr("Constant subtracted from the local mean; higher = less white"));
    cannyLow.slider->setToolTip(tr("Weak edges are kept only if they touch a strong edge"));
    cannyHigh.slider->setToolTip(tr("Gradient above which a pixel is a strong edge"));
    structureGrid->setColumnStretch(1, 1);
    layout->addWidget(structureBox);

    auto* resetButton = new QPushButton(tr("Reset filters"), this);
    layout->addWidget(resetButton);

    auto* hint = new QLabel(tr("Filters change only the preview. Processing such as calibration "
                               "works on the original camera frames."),
                            this);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addWidget(hint);
    layout->addStretch(1);






    const auto changed = [this]()
    {
        updateEnabledState();
        publish();
    };
    for(QCheckBox* box : {grayscaleBox, negativeBox, equalizeBox, blurBox, sharpenBox})
    {
        connect(box, &QCheckBox::toggled, this, changed);
    }
    for(QComboBox* combo : {colorMapCombo, structureCombo})
    {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changed);
    }
    for(const SliderRow* row :
        {&clipLimit, &blurSize, &sharpenAmount, &threshold, &blockSize, &offset, &cannyLow, &cannyHigh})
    {
        connect(row->slider, &QSlider::valueChanged, this, changed);
    }
    connect(resetButton, &QPushButton::clicked, this, &FilterPanel::resetToDefaults);

    updateEnabledState();
}

FilterPanel::SliderRow FilterPanel::addSliderRow(QGridLayout* grid, int gridRow, const QString& name, int minimum,
                                                 int maximum, int value, std::function<QString(int)> format)
{
    QWidget* box = grid->parentWidget();
    SliderRow row;
    row.name = new QLabel(name, box);
    row.slider = new QSlider(Qt::Horizontal, box);
    row.slider->setRange(minimum, maximum);
    row.slider->setValue(value);
    row.value = new QLabel(format(value), box);
    row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.value->setMinimumWidth(row.value->fontMetrics().horizontalAdvance(QStringLiteral("101 px")));

    grid->addWidget(row.name, gridRow, 0, Qt::AlignRight);
    grid->addWidget(row.slider, gridRow, 1);
    grid->addWidget(row.value, gridRow, 2);

    QLabel* valueLabel = row.value;
    connect(row.slider, &QSlider::valueChanged, valueLabel,
            [valueLabel, format = std::move(format)](int v) { valueLabel->setText(format(v)); });
    return row;
}

FilterSettings FilterPanel::currentSettings(void) const
{
    FilterSettings settings;
    settings.grayscale = grayscaleBox->isChecked();
    settings.negative = negativeBox->isChecked();
    settings.colorMap = static_cast<FalseColorMap>(colorMapCombo->currentData().toInt());

    settings.equalize = equalizeBox->isChecked();
    settings.claheClipLimit = clipLimitFromSlider(clipLimit.slider->value());
    settings.blurKernel = blurBox->isChecked() ? oddFromSlider(blurSize.slider->value()) : 0;
    settings.sharpenPercent = sharpenBox->isChecked() ? sharpenAmount.slider->value() : 0;

    settings.structure = static_cast<StructureFilter>(structureCombo->currentData().toInt());
    settings.threshold = threshold.slider->value();
    settings.adaptiveBlockSize = oddFromSlider(blockSize.slider->value());
    settings.adaptiveOffset = offset.slider->value();
    settings.cannyLow = cannyLow.slider->value();
    settings.cannyHigh = cannyHigh.slider->value();
    return settings;
}

void FilterPanel::updateEnabledState(void)
{
    const auto enable = [](const SliderRow& row, bool on)
    {
        row.name->setEnabled(on);
        row.slider->setEnabled(on);
        row.value->setEnabled(on);
    };
    const auto show = [](const SliderRow& row, bool on)
    {
        row.name->setVisible(on);
        row.slider->setVisible(on);
        row.value->setVisible(on);
    };

    enable(clipLimit, equalizeBox->isChecked());
    enable(blurSize, blurBox->isChecked());
    enable(sharpenAmount, sharpenBox->isChecked());

    const auto structure = static_cast<StructureFilter>(structureCombo->currentData().toInt());
    show(threshold, structure == StructureFilter::FixedThreshold);
    show(blockSize, structure == StructureFilter::AdaptiveThreshold);
    show(offset, structure == StructureFilter::AdaptiveThreshold);
    show(cannyLow, structure == StructureFilter::CannyEdges);
    show(cannyHigh, structure == StructureFilter::CannyEdges);

    grayscaleBox->setEnabled(structure == StructureFilter::None);
}

void FilterPanel::setSettings(const FilterSettings& settings)
{
    constexpr int kDefaultBlurSlider = 2;
    constexpr int kDefaultSharpenPercent = 100;

    const auto selectData = [](QComboBox* combo, int value)
    {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };

    suppressPublish = true;
    grayscaleBox->setChecked(settings.grayscale);
    negativeBox->setChecked(settings.negative);
    selectData(colorMapCombo, static_cast<int>(settings.colorMap));
    equalizeBox->setChecked(settings.equalize);
    clipLimit.slider->setValue(static_cast<int>(settings.claheClipLimit * 10 + 0.5));
    blurBox->setChecked(settings.blurKernel >= 3);
    blurSize.slider->setValue(settings.blurKernel >= 3 ? (settings.blurKernel - 1) / 2 : kDefaultBlurSlider);
    sharpenBox->setChecked(settings.sharpenPercent > 0);
    sharpenAmount.slider->setValue(settings.sharpenPercent > 0 ? settings.sharpenPercent : kDefaultSharpenPercent);
    selectData(structureCombo, static_cast<int>(settings.structure));
    threshold.slider->setValue(settings.threshold);
    blockSize.slider->setValue((settings.adaptiveBlockSize - 1) / 2);
    offset.slider->setValue(settings.adaptiveOffset);
    cannyLow.slider->setValue(settings.cannyLow);
    cannyHigh.slider->setValue(settings.cannyHigh);
    suppressPublish = false;

    updateEnabledState();
    publish();
}

void FilterPanel::resetToDefaults(void)
{
    setSettings(FilterSettings{});
}

void FilterPanel::publish(void)
{
    if(!suppressPublish)
    {
        emit filtersChanged(currentSettings());
    }
}


} // namespace calib
