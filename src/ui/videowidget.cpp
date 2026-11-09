#include "ui/videowidget.h"

#include <QPainter>
#include <QRegion>
#include <QResizeEvent>

#include <cmath>

#include <utility>

namespace calib
{

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
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
    if(frame.isNull())
    {
        update();
    }
}

void VideoWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);

    if(!frame.isNull())
    {
        const double ratio = frame.devicePixelRatio();
        const QSizeF imageSize(frame.width() / ratio, frame.height() / ratio);
        const QSizeF fitted = imageSize.scaled(QSizeF(size()), Qt::KeepAspectRatio);
        QRectF target(QPointF(0.0, 0.0), fitted);
        target.moveCenter(QRectF(rect()).center());

        painter.setClipRegion(QRegion(rect()).subtracted(QRegion(target.toAlignedRect())));
        painter.fillRect(rect(), Qt::black);
        painter.setClipping(false);

        if(std::abs(fitted.width() - imageSize.width()) <= 2.0 && std::abs(fitted.height() - imageSize.height()) <= 2.0)
        {
            painter.drawImage(QPointF(std::round(target.x()), std::round(target.y())), frame);
        }
        else
        {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(target, frame);
        }
        return;
    }

    painter.fillRect(rect(), Qt::black);

    if(!message.isEmpty())
    {
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, message);
    }
}

void VideoWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const double ratio = devicePixelRatioF();
    emit displaySizeChanged(QSize(static_cast<int>(std::lround(width() * ratio)),
                                  static_cast<int>(std::lround(height() * ratio))),
                            ratio);
}

} // namespace calib
