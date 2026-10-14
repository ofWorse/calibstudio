#include "videowidget.h"

#include <QPainter>

#include <utility>

namespace calib {

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    // paintEvent covers every pixel, so Qt can skip erasing the background.
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 180);
}

void VideoWidget::setFrame(QImage frame)
{
    this->frame = std::move(frame);
    update();
}

void VideoWidget::clearFrame(void)
{
    frame = QImage();
    update();
}

void VideoWidget::setMessage(const QString& message)
{
    this->message = message;
    if (frame.isNull())
        update();
}

void VideoWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if(!frame.isNull())
    {
        const QSize fitted = frame.size().scaled(size(), Qt::KeepAspectRatio);
        QRect target(QPoint(0, 0), fitted);
        target.moveCenter(rect().center());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, frame);

        return;
    }

    if(!message.isEmpty())
    {
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(rect().adjusted(16, 16, -16, -16),
                         Qt::AlignCenter | Qt::TextWordWrap, message);
    }
}

} // namespace calib
