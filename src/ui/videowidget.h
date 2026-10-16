#ifndef CALIBSTUDIO_VIDEOWIDGET_H
#define CALIBSTUDIO_VIDEOWIDGET_H

#include <QImage>
#include <QString>
#include <QWidget>

namespace calib
{


class VideoWidget : public QWidget
{
    Q_OBJECT

private:
    QImage frame;
    QString message;

public:
    explicit VideoWidget(QWidget* parent = nullptr);

    void setFrame(QImage frame);
    void clearFrame(void);
    void setMessage(const QString& message);

    QSize sizeHint(void) const override
    {
        return {960, 540};
    }

protected:
    void paintEvent(QPaintEvent* event) override;
};


} // namespace calib

#endif // CALIBSTUDIO_VIDEOWIDGET_H
