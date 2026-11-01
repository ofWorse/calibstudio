#include "ui/windowchrome.h"

#include "ui/theme.h"

#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>

namespace calib
{

namespace
{

constexpr int kTitleHeight = 30;
const QColor kCloseHover(232, 17, 35);

} // namespace


TitleButton::TitleButton(TitleGlyph glyph, QWidget* parent)
    : QAbstractButton(parent),
      glyph(glyph)
{
    setFixedSize(34, 24);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover);
}

void TitleButton::setGlyph(TitleGlyph newGlyph)
{
    glyph = newGlyph;
    update();
}

bool TitleButton::event(QEvent* event)
{
    if(event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)
    {
        update();
    }
    return QAbstractButton::event(event);
}

void TitleButton::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool hovered = underMouse();
    const bool isClose = glyph == TitleGlyph::Close;
    if(hovered || isDown())
    {
        const QColor background = isClose ? kCloseHover : QColor(255, 255, 255, isDown() ? 60 : 30);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 5, 5);
    }

    painter.setPen(QPen(hovered && isClose ? QColor(Qt::white) : theme::kText, 1.4));
    painter.setBrush(Qt::NoBrush);
    const QPointF c = QRectF(rect()).center();
    const qreal s = 4.5;
    switch(glyph)
    {
    case TitleGlyph::Collapse: painter.drawLine(c + QPointF(-s, 0), c + QPointF(s, 0)); break;
    case TitleGlyph::Expand:
        painter.drawPolyline(QPolygonF({c + QPointF(-s, -2), c + QPointF(0, 2.5), c + QPointF(s, -2)}));
        break;
    case TitleGlyph::Maximize: painter.drawRect(QRectF(c.x() - s, c.y() - s, 2 * s, 2 * s)); break;
    case TitleGlyph::Restore:
    case TitleGlyph::Float:
        painter.drawRect(QRectF(c.x() - s, c.y() - s + 2, 2 * s - 2, 2 * s - 2));
        painter.drawPolyline(QPolygonF(
            {QPointF(c.x() - s + 2, c.y() - s), QPointF(c.x() + s, c.y() - s), QPointF(c.x() + s, c.y() + s - 2)}));
        break;
    case TitleGlyph::Close:
        painter.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
        painter.drawLine(c + QPointF(-s, s), c + QPointF(s, -s));
        break;
    }
}


void ElidedLabel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
    painter.setFont(font());
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
                     fontMetrics().elidedText(text(), Qt::ElideRight, width()));
}


DockTitleBar::DockTitleBar(QDockWidget* dock)
    : QWidget(dock),
      dock(dock)
{
    setFixedHeight(kTitleHeight);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 3, 0);
    layout->setSpacing(1);

    title = new ElidedLabel(dock->windowTitle(), this);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);

    floatButton = new TitleButton(TitleGlyph::Float, this);
    floatButton->setToolTip(tr("Detach / attach the panel"));
    closeButton = new TitleButton(TitleGlyph::Close, this);
    closeButton->setToolTip(tr("Hide the panel"));
    layout->addWidget(title, 1);
    layout->addWidget(floatButton);
    layout->addWidget(closeButton);

    connect(dock, &QDockWidget::windowTitleChanged, title,
            [this](const QString& text)
            {
                title->setText(text);
                title->update();
            });
    connect(dock, &QDockWidget::featuresChanged, this, &DockTitleBar::updateButtons);
    connect(floatButton, &QAbstractButton::clicked, this,
            [this]() { this->dock->setFloating(!this->dock->isFloating()); });
    connect(closeButton, &QAbstractButton::clicked, dock, &QDockWidget::close);
    updateButtons();
}

void DockTitleBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme::kTitle);
    painter.setPen(theme::kBorder);
    painter.drawLine(rect().bottomLeft(), rect().bottomRight());
}

void DockTitleBar::updateButtons(void)
{
    floatButton->setVisible(dock->features() & QDockWidget::DockWidgetFloatable);
    closeButton->setVisible(dock->features() & QDockWidget::DockWidgetClosable);
}


} // namespace calib
