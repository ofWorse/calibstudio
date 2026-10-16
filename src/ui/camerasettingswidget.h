#ifndef CALIBSTUDIO_CAMERASETTINGSWIDGET_H
#define CALIBSTUDIO_CAMERASETTINGSWIDGET_H

#include "camera/cameracontrols.h"
#include "camera/camerainfo.h"

#include <QVector>
#include <QWidget>

#include <map>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QSlider;
class QVBoxLayout;

namespace calib
{


class CameraSettingsWidget : public QWidget
{
    Q_OBJECT

private:
    struct ControlRow
    {
        CameraControlInfo info;
        QSlider* slider = nullptr;
        QLabel* valueLabel = nullptr;
        QCheckBox* autoBox = nullptr;
    };

    QComboBox* modeCombo = nullptr;
    QCheckBox* flipXBox = nullptr;
    QCheckBox* flipYBox = nullptr;
    QWidget* controlsHost = nullptr;
    QVBoxLayout* controlsLayout = nullptr;
    QLabel* noControlsLabel = nullptr;
    QPushButton* resetButton = nullptr;
    std::map<CameraControlId, ControlRow> rows;

public:
    explicit CameraSettingsWidget(QWidget* parent = nullptr);

    void setCamera(const calib::CameraInfo& camera, int modeIndex, const QVector<calib::CameraControlInfo>& controls,
                   const QVector<calib::CameraControlState>& states, bool flipHorizontal, bool flipVertical);
    void updateControl(calib::CameraControlId id, const calib::CameraControlState& state);
    void clearCamera(void);

signals:
    void modeRequested(int modeIndex);
    void flipChanged(bool horizontal, bool vertical);
    void controlChanged(calib::CameraControlId id, const calib::CameraControlState& state);
    void resetRequested(void);

private:
    void removeControlRows(void);
    void addControlRow(QGridLayout* grid, int gridRow, const CameraControlInfo& control,
                       const CameraControlState& state);
    void showState(ControlRow& row, const CameraControlState& state);
};


} // namespace calib

#endif // CALIBSTUDIO_CAMERASETTINGSWIDGET_H
