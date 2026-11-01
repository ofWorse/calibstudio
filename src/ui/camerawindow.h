#ifndef CALIBSTUDIO_CAMERAWINDOW_H
#define CALIBSTUDIO_CAMERAWINDOW_H

#include <QImage>
#include <QMdiSubWindow>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <string>

class QLabel;

namespace calib
{

class TitleButton;
class VideoWidget;

enum class CameraLinkState
{
    Connecting,
    Streaming,
    Lost,
};


class CameraWindow : public QMdiSubWindow
{
    Q_OBJECT

private:
    enum Edge
    {
        NoEdge = 0,
        LeftEdge = 1,
        RightEdge = 2,
        TopEdge = 4,
        BottomEdge = 8,
    };

    QWidget* content = nullptr;
    QWidget* titleBar = nullptr;
    QLabel* stateDot = nullptr;
    QLabel* nameLabel = nullptr;
    QLabel* infoLabel = nullptr;
    TitleButton* collapseButton = nullptr;
    TitleButton* maximizeButton = nullptr;
    TitleButton* closeButton = nullptr;
    VideoWidget* video = nullptr;

    std::string cameraId;
    QString cameraName;
    CameraLinkState linkState = CameraLinkState::Connecting;
    QSize frameSize;
    double frameRate = 0.0;

    bool collapsed = false;
    int expandedHeight = 0;

    int dragEdges = NoEdge;
    bool moving = false;
    QPoint dragStart;
    QRect dragGeometry;

public:
    CameraWindow(const std::string& cameraId, const QString& cameraName, QWidget* parent = nullptr);

    const std::string& getCameraId(void) const
    {
        return cameraId;
    }
    bool isCollapsed(void) const
    {
        return collapsed;
    }

    void setFrame(QImage frame);
    void clearFrame(void);
    void setMessage(const QString& message);
    void setLinkState(calib::CameraLinkState state);
    void setFrameRate(double fps);

    void setCollapsed(bool collapse);
    void toggleMaximized(void);

signals:
    void closeRequested(const std::string& cameraId);
    void arranged(void);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    int edgesAt(const QPoint& position) const;
    void updateCursor(int edges);
    void dragTo(const QPoint& globalPosition);
    void updateTitle(void);
    void updateButtons(void);
};


} // namespace calib

#endif // CALIBSTUDIO_CAMERAWINDOW_H
