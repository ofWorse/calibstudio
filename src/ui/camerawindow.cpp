#include "ui/camerawindow.h"

#include "ui/theme.h"
#include "ui/videowidget.h"
#include "ui/windowchrome.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMdiArea>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace calib
{

namespace
{

constexpr int kBorder = 4;
constexpr int kGrip = 7;
constexpr int kRadius = 8;
constexpr int kTitleHeight = 30;
constexpr int kMinVisible = 60;

const QColor& kWindowColor = theme::kWindow;
const QColor& kTitleColor = theme::kTitle;
const QColor& kTitleActiveColor = theme::kTitleActive;
const QColor& kBorderColor = theme::kBorder;
const QColor& kAccentColor = theme::kAccent;
const QColor& kTextColor = theme::kText;
const QColor& kDimTextColor = theme::kDimText;

QColor stateColor(CameraLinkState state)
{
    switch(state)
    {
    case CameraLinkState::Connecting: return theme::kConnecting;
    case CameraLinkState::Streaming:  return theme::kStreaming;
    case CameraLinkState::Lost:       return theme::kLost;
    }
    return Qt::gray;
}

QString stateText(CameraLinkState state)
{
    switch(state)
    {
    case CameraLinkState::Connecting: return QObject::tr("Connecting");
    case CameraLinkState::Streaming:  return QObject::tr("Streaming");
    case CameraLinkState::Lost:       return QObject::tr("Not connected");
    }
    return QString();
}

} // namespace


CameraWindow::CameraWindow(const std::string& cameraId, const QString& cameraName, QWidget* parent)
    : QMdiSubWindow(parent, Qt::FramelessWindowHint),
      cameraId(cameraId),
      cameraName(cameraName)
{
    setAutoFillBackground(false);
    setMouseTracking(true);

    content = new QWidget(this);
    content->setObjectName(QStringLiteral("cameraWindowContent"));
    content->setMouseTracking(true);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(kBorder, kBorder, kBorder, kBorder);
    layout->setSpacing(0);

    titleBar = new QWidget(content);
    titleBar->setObjectName(QStringLiteral("cameraWindowTitleBar"));
    titleBar->setFixedHeight(kTitleHeight);
    titleBar->setMouseTracking(true);
    auto* bar = new QHBoxLayout(titleBar);
    bar->setContentsMargins(12, 0, 3, 0);
    bar->setSpacing(8);

    stateDot = new QLabel(titleBar);
    stateDot->setFixedSize(9, 9);
    nameLabel = new ElidedLabel(cameraName, titleBar);
    nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nameLabel->setToolTip(cameraName);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    QPalette namePalette = nameLabel->palette();
    namePalette.setColor(QPalette::WindowText, kTextColor);
    nameLabel->setPalette(namePalette);
    infoLabel = new QLabel(titleBar);
    QPalette infoPalette = infoLabel->palette();
    infoPalette.setColor(QPalette::WindowText, kDimTextColor);
    infoLabel->setPalette(infoPalette);

    collapseButton = new TitleButton(TitleGlyph::Collapse, titleBar);
    maximizeButton = new TitleButton(TitleGlyph::Maximize, titleBar);
    closeButton = new TitleButton(TitleGlyph::Close, titleBar);
    closeButton->setToolTip(tr("Switch the camera off"));

    bar->addWidget(stateDot);
    bar->addWidget(nameLabel, 1);
    bar->addWidget(infoLabel);
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(1);
    buttons->addWidget(collapseButton);
    buttons->addWidget(maximizeButton);
    buttons->addWidget(closeButton);
    bar->addLayout(buttons);

    video = new VideoWidget(content);
    connect(video, &VideoWidget::displaySizeChanged, this, &CameraWindow::displaySizeChanged);
    video->setMessage(tr("Connecting..."));
    layout->addWidget(titleBar);
    layout->addWidget(video, 1);
    setWidget(content);

    content->installEventFilter(this);
    titleBar->installEventFilter(this);

    connect(collapseButton, &QAbstractButton::clicked, this, [this]() { setCollapsed(!collapsed); });
    connect(maximizeButton, &QAbstractButton::clicked, this, &CameraWindow::toggleMaximized);
    connect(closeButton, &QAbstractButton::clicked, this, [this]() { emit closeRequested(this->cameraId); });
    connect(this, &QMdiSubWindow::windowStateChanged, this,
            [this]()
            {
                updateButtons();
                update();
            });

    setWindowTitle(cameraName);
    updateTitle();
    updateButtons();
}

void CameraWindow::setFrame(QImage frame, const QSize& sourceSize)
{
    if(sourceSize != frameSize)
    {
        frameSize = sourceSize;
        updateTitle();
    }
    video->setFrame(std::move(frame));
}

void CameraWindow::clearFrame(void)
{
    frameSize = QSize();
    video->clearFrame();
    updateTitle();
}

void CameraWindow::setMessage(const QString& message)
{
    video->setMessage(message);
}

void CameraWindow::setLinkState(CameraLinkState state)
{
    linkState = state;
    updateTitle();
}

void CameraWindow::setFrameRate(double fps)
{
    frameRate = fps;
    updateTitle();
}

void CameraWindow::setCollapsed(bool collapse)
{
    if(collapse == collapsed)
    {
        return;
    }
    if(isMaximized())
    {
        showNormal();
    }
    collapsed = collapse;

    if(collapse)
    {
        expandedHeight = height();
        video->hide();
    }
    else
    {
        video->show();
    }
    content->layout()->activate();
    if(layout())
    {
        layout()->activate();
    }
    resize(width(), collapse ? kTitleHeight + 2 * kBorder : std::max(expandedHeight, minimumSizeHint().height()));

    updateButtons();
    update();
}

void CameraWindow::toggleMaximized(void)
{
    if(collapsed)
    {
        setCollapsed(false);
    }
    if(isMaximized())
    {
        showNormal();
    }
    else
    {
        showMaximized();
    }
    updateButtons();
}

bool CameraWindow::eventFilter(QObject* watched, QEvent* event)
{
    if(watched != content && watched != titleBar)
    {
        return QMdiSubWindow::eventFilter(watched, event);
    }
    auto* widget = static_cast<QWidget*>(watched);

    switch(event->type())
    {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if(mouse->button() != Qt::LeftButton)
        {
            break;
        }
        if(QMdiArea* area = mdiArea())
        {
            area->setActiveSubWindow(this);
        }
        if(isMaximized())
        {
            return true;
        }
        const int edges = edgesAt(widget->mapTo(this, mouse->pos()));
        dragEdges = collapsed ? edges & (LeftEdge | RightEdge) : edges;
        moving = dragEdges == NoEdge && watched == titleBar;
        dragStart = mouse->globalPos();
        dragGeometry = geometry();
        return true;
    }
    case QEvent::MouseMove: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if(dragEdges != NoEdge || moving)
        {
            dragTo(mouse->globalPos());
            return true;
        }
        if(mouse->buttons() == Qt::NoButton)
        {
            updateCursor(isMaximized() ? NoEdge : edgesAt(widget->mapTo(this, mouse->pos())));
        }
        break;
    }
    case QEvent::MouseButtonRelease:
        if(dragEdges != NoEdge || moving)
        {
            const bool changed = geometry() != dragGeometry;
            dragEdges = NoEdge;
            moving = false;
            if(changed)
            {
                emit arranged();
            }
            return true;
        }
        break;
    case QEvent::MouseButtonDblClick: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if(mouse->button() == Qt::LeftButton && edgesAt(widget->mapTo(this, mouse->pos())) == NoEdge)
        {
            toggleMaximized();
            return true;
        }
        break;
    }
    case QEvent::Leave:
        if(dragEdges == NoEdge && !moving)
        {
            updateCursor(NoEdge);
        }
        break;
    default: break;
    }
    return QMdiSubWindow::eventFilter(watched, event);
}

void CameraWindow::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool active = mdiArea() && mdiArea()->activeSubWindow() == this;
    const qreal radius = isMaximized() ? 0.0 : kRadius;
    QPainterPath shape;
    shape.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter.fillPath(shape, kWindowColor);

    const int titleBottom = titleBar->mapTo(this, QPoint(0, titleBar->height())).y();
    painter.save();
    painter.setClipPath(shape);
    painter.fillRect(QRect(0, 0, width(), titleBottom), active ? kTitleActiveColor : kTitleColor);
    painter.restore();

    painter.setPen(QPen(active ? kAccentColor : kBorderColor, active ? 1.5 : 1.0));
    painter.drawPath(shape);
}

void CameraWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    emit closeRequested(cameraId);
}

void CameraWindow::changeEvent(QEvent* event)
{
    QMdiSubWindow::changeEvent(event);
    if(event->type() == QEvent::WindowStateChange)
    {
        updateButtons();
        update();
    }
}

int CameraWindow::edgesAt(const QPoint& position) const
{
    int edges = NoEdge;
    if(position.x() < kGrip)
    {
        edges |= LeftEdge;
    }
    else if(position.x() >= width() - kGrip)
    {
        edges |= RightEdge;
    }
    if(position.y() < kGrip)
    {
        edges |= TopEdge;
    }
    else if(position.y() >= height() - kGrip)
    {
        edges |= BottomEdge;
    }
    return edges;
}

void CameraWindow::updateCursor(int edges)
{
    const bool horizontal = edges & (LeftEdge | RightEdge);
    const bool vertical = edges & (TopEdge | BottomEdge);
    if(horizontal && vertical)
    {
        const bool mainDiagonal = ((edges & LeftEdge) != 0) == ((edges & TopEdge) != 0);
        setCursor(mainDiagonal ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    }
    else if(horizontal)
    {
        setCursor(Qt::SizeHorCursor);
    }
    else if(vertical)
    {
        setCursor(Qt::SizeVerCursor);
    }
    else
    {
        unsetCursor();
    }
}

void CameraWindow::dragTo(const QPoint& globalPosition)
{
    const QPoint delta = globalPosition - dragStart;
    const QRect area = parentWidget() ? parentWidget()->rect() : QRect();
    QRect target = dragGeometry;

    if(moving)
    {
        target.moveTopLeft(dragGeometry.topLeft() + delta);
        if(!area.isEmpty())
        {
            target.moveTop(std::clamp(target.top(), 0, std::max(0, area.height() - kTitleHeight)));
            target.moveLeft(std::clamp(target.left(), kMinVisible - target.width(), area.width() - kMinVisible));
        }
        setGeometry(target);
        return;
    }

    const QSize minimum = minimumSizeHint().expandedTo(minimumSize());
    if(dragEdges & LeftEdge)
    {
        target.setLeft(std::min(target.left() + delta.x(), target.right() - minimum.width() + 1));
    }
    if(dragEdges & RightEdge)
    {
        target.setRight(std::max(target.right() + delta.x(), target.left() + minimum.width() - 1));
    }
    if(dragEdges & TopEdge)
    {
        target.setTop(std::max(0, std::min(target.top() + delta.y(), target.bottom() - minimum.height() + 1)));
    }
    if(dragEdges & BottomEdge)
    {
        target.setBottom(std::max(target.bottom() + delta.y(), target.top() + minimum.height() - 1));
    }
    setGeometry(target);
}

void CameraWindow::updateTitle(void)
{
    stateDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(stateColor(linkState).name()));
    stateDot->setToolTip(stateText(linkState));

    QStringList parts;
    if(frameSize.isValid())
    {
        parts << QStringLiteral("%1×%2").arg(frameSize.width()).arg(frameSize.height());
    }
    if(frameRate > 0.0)
    {
        parts << tr("%1 fps").arg(frameRate, 0, 'f', 1);
    }
    infoLabel->setText(parts.join(QStringLiteral("  ·  ")));
}

void CameraWindow::updateButtons(void)
{
    maximizeButton->setGlyph(isMaximized() ? TitleGlyph::Restore : TitleGlyph::Maximize);
    maximizeButton->setToolTip(isMaximized() ? tr("Restore") : tr("Maximize"));
    collapseButton->setGlyph(collapsed ? TitleGlyph::Expand : TitleGlyph::Collapse);
    collapseButton->setToolTip(collapsed ? tr("Show the picture") : tr("Collapse to the title bar"));
}


} // namespace calib
