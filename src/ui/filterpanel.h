#ifndef CALIBSTUDIO_FILTERPANEL_H
#define CALIBSTUDIO_FILTERPANEL_H

#include "processing/framefilter.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QSlider;

namespace calib
{


class FilterPanel : public QWidget
{
    Q_OBJECT

private:
    struct SliderRow
    {
        QLabel* name = nullptr;
        QSlider* slider = nullptr;
        QLabel* value = nullptr;
    };

    QCheckBox* grayscaleBox = nullptr;
    QCheckBox* negativeBox = nullptr;
    QComboBox* colorMapCombo = nullptr;

    QCheckBox* equalizeBox = nullptr;
    SliderRow clipLimit;
    QCheckBox* blurBox = nullptr;
    SliderRow blurSize;
    QCheckBox* sharpenBox = nullptr;
    SliderRow sharpenAmount;

    QComboBox* structureCombo = nullptr;
    SliderRow threshold;
    SliderRow blockSize;
    SliderRow offset;
    SliderRow cannyLow;
    SliderRow cannyHigh;

    bool suppressPublish = false;

public:
    explicit FilterPanel(QWidget* parent = nullptr);

    FilterSettings currentSettings(void) const;
    void setSettings(const calib::FilterSettings& settings);

signals:
    void filtersChanged(const calib::FilterSettings& settings);

private:
    SliderRow addSliderRow(QGridLayout* grid, int gridRow, const QString& name, int minimum, int maximum, int value,
                           std::function<QString(int)> format);
    void updateEnabledState(void);
    void resetToDefaults(void);
    void publish(void);
};


} // namespace calib

#endif // CALIBSTUDIO_FILTERPANEL_H
