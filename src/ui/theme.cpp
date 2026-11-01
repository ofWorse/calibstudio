#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QTemporaryDir>

namespace calib::theme
{

namespace
{

QString rgb(const QColor& color)
{
    return color.name();
}

QString rgba(const QColor& color, int alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(alpha);
}

// Style sheets take images only from files.
QString glyphFolder(void)
{
    static QTemporaryDir folder;
    static bool painted = false;
    if(!painted && folder.isValid())
    {
        painted = true;
        const auto paint = [](const QString& path, const QColor& color, auto draw)
        {
            QPixmap pixmap(32, 32);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(color, 3.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            draw(painter);
            painter.end();
            pixmap.save(path);
        };
        const auto check = [](QPainter& p)
        {
            p.drawPolyline(QPolygonF({
                {7,    16.5},
                {13.5, 23  },
                {25,   9.5 }
            }));
        };
        const auto arrow = [](QPainter& p)
        {
            p.drawPolyline(QPolygonF({
                {8,  12},
                {16, 20},
                {24, 12}
            }));
        };
        paint(folder.filePath(QStringLiteral("check.png")), Qt::white, check);
        paint(folder.filePath(QStringLiteral("check-disabled.png")), kDisabledText, check);
        paint(folder.filePath(QStringLiteral("arrow.png")), kDimText, arrow);
    }
    return QDir::fromNativeSeparators(folder.path());
}

QString styleSheet(void)
{
    const QString glyphs = glyphFolder();
    QString sheet = QStringLiteral(R"(
QMainWindow { background: @panel; }
QMainWindow::separator { background: @workArea; width: 4px; height: 4px; }
QMainWindow::separator:hover { background: @accent; }

QToolBar { background: @title; border: none; border-bottom: 1px solid @border; padding: 4px 6px; spacing: 2px; }
QToolButton { color: @text; background: transparent; border: none; border-radius: 6px; padding: 5px 12px 5px 8px; }
QToolButton:hover { background: @hover; }
QToolButton:pressed { background: @pressed; }

QStatusBar { background: @title; border-top: 1px solid @border; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @dimText; padding: 2px 6px; }

QScrollArea { background: @panel; border: none; }
QScrollArea > QWidget > QWidget { background: @panel; }

QListWidget { background: @input; border: 1px solid @border; border-radius: 8px; padding: 4px; outline: none; }
QListWidget::item { color: @text; padding: 5px 6px; border-radius: 5px; }
QListWidget::item:hover { background: @hover; }

QGroupBox { border: 1px solid @border; border-radius: 8px; margin-top: 18px; padding: 10px 8px 8px 8px; background: @groupFill; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 4px; color: @dimText; font-weight: 600; }

QPushButton { background: @control; color: @text; border: 1px solid @border; border-radius: 6px; padding: 6px 14px; }
QPushButton:hover { background: @controlHover; border-color: @accent; }
QPushButton:pressed { background: @accent; }
QPushButton:disabled { color: @disabledText; background: @panel; border-color: @control; }

QComboBox { background: @input; color: @text; border: 1px solid @border; border-radius: 6px; padding: 4px 8px; min-height: 20px; }
QComboBox:hover { border-color: @accent; }
QComboBox:disabled { color: @disabledText; border-color: @control; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url(@glyphs/arrow.png); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: @input; color: @text; border: 1px solid @border; selection-background-color: @accent; outline: none; }

QCheckBox { color: @text; spacing: 8px; }
QCheckBox:disabled { color: @disabledText; }
QCheckBox::indicator, QListWidget::indicator { width: 16px; height: 16px; border: 1px solid @border; border-radius: 4px; background: @input; }
QCheckBox::indicator:hover, QListWidget::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked, QListWidget::indicator:checked { background: @accent; border-color: @accent; image: url(@glyphs/check.png); }
QCheckBox::indicator:disabled { background: @panel; border-color: @control; }
QCheckBox::indicator:checked:disabled { background: @control; image: url(@glyphs/check-disabled.png); }

QSlider::groove:horizontal { height: 4px; background: @control; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { width: 12px; height: 12px; margin: -6px 0; border-radius: 8px; background: @text; border: 2px solid @accent; }
QSlider::handle:horizontal:hover { background: white; }
QSlider::sub-page:horizontal:disabled { background: @border; }
QSlider::handle:horizontal:disabled { background: @control; border-color: @border; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @control; border-radius: 3px; min-height: 30px; }
QScrollBar::handle:horizontal { background: @control; border-radius: 3px; min-width: 30px; }
QScrollBar::handle:hover { background: @controlHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QTabBar::tab { background: @title; color: @dimText; padding: 6px 12px; border: none; border-top: 2px solid transparent; }
QTabBar::tab:selected { background: @panel; color: @text; border-top: 2px solid @accent; }
QTabBar::tab:hover { color: @text; }

QToolTip { background: @titleActive; color: @text; border: 1px solid @border; padding: 4px; }
QMenu { background: @title; color: @text; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 5px 18px; border-radius: 4px; }
QMenu::item:selected { background: @accent; }
)");

    const std::pair<const char*, QString> colors[] = {
        {"@titleActive", rgb(kTitleActive)},
        {"@title", rgb(kTitle)},
        {"@workArea", rgb(kWorkArea)},
        {"@panel", rgb(kPanel)},
        {"@input", rgb(kInput)},
        {"@controlHover", rgb(kControlHover)},
        {"@control", rgb(kControl)},
        {"@border", rgb(kBorder)},
        {"@accent", rgb(kAccent)},
        {"@disabledText", rgb(kDisabledText)},
        {"@dimText", rgb(kDimText)},
        {"@text", rgb(kText)},
        {"@hover", rgba(Qt::white, 22)},
        {"@pressed", rgba(Qt::white, 44)},
        {"@groupFill", rgba(Qt::white, 6)},
        {"@glyphs", glyphs},
    };
    for(const auto& [name, value] : colors)
    {
        sheet.replace(QLatin1String(name), value);
    }
    return sheet;
}

QPalette palette(void)
{
    QPalette p;
    p.setColor(QPalette::Window, kPanel);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, kInput);
    p.setColor(QPalette::AlternateBase, kTitle);
    p.setColor(QPalette::ToolTipBase, kTitleActive);
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::PlaceholderText, kDimText);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kControl);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, kAccent);
    p.setColor(QPalette::Light, kControlHover);
    p.setColor(QPalette::Midlight, kControl);
    p.setColor(QPalette::Mid, kBorder);
    p.setColor(QPalette::Dark, kWorkArea);
    p.setColor(QPalette::Shadow, Qt::black);
    for(const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
    {
        p.setColor(QPalette::Disabled, role, kDisabledText);
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, kControl);
    return p;
}

} // namespace

void apply(QApplication& application)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    application.setPalette(palette());
    application.setStyleSheet(styleSheet());
}

QIcon icon(Icon kind)
{
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(kText, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch(kind)
    {
    case Icon::Refresh: {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(2.5, 2.5, 11, 11), 60);
        arc.arcTo(QRectF(2.5, 2.5, 11, 11), 60, 290);
        painter.drawPath(arc);
        painter.drawPolyline(QPolygonF({
            {10.2, 1.6},
            {11.4, 4.3},
            {8.5,  5.0}
        }));
        break;
    }
    case Icon::Tile:
        painter.drawRoundedRect(QRectF(2, 2, 5, 5), 1.2, 1.2);
        painter.drawRoundedRect(QRectF(9, 2, 5, 5), 1.2, 1.2);
        painter.drawRoundedRect(QRectF(2, 9, 5, 5), 1.2, 1.2);
        painter.drawRoundedRect(QRectF(9, 9, 5, 5), 1.2, 1.2);
        break;
    case Icon::Cascade:
        painter.drawRoundedRect(QRectF(6, 6, 8, 8), 1.2, 1.2);
        painter.drawPolyline(QPolygonF({
            {4,  12},
            {4,  4 },
            {12, 4 }
        }));
        painter.drawPolyline(QPolygonF({
            {2,   9.5},
            {2,   2  },
            {9.5, 2  }
        }));
        break;
    }
    painter.end();
    return QIcon(pixmap);
}

} // namespace calib::theme
