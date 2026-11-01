#ifndef CALIBSTUDIO_WINDOWCHROME_H
#define CALIBSTUDIO_WINDOWCHROME_H

#include <QAbstractButton>
#include <QLabel>
#include <QWidget>

class QDockWidget;

// Pieces shared by the camera windows and the side panels, so both have the
// same title bars and buttons.
namespace calib
{

enum class TitleGlyph
{
    Collapse,
    Expand,
    Maximize,
    Restore,
    Float,
    Close,
};


class TitleButton final : public QAbstractButton
{
private:
    TitleGlyph glyph;

public:
    TitleButton(TitleGlyph glyph, QWidget* parent);

    void setGlyph(TitleGlyph glyph);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
};


class ElidedLabel final : public QLabel
{
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent* event) override;
};


class DockTitleBar final : public QWidget
{
private:
    QDockWidget* dock = nullptr;
    ElidedLabel* title = nullptr;
    TitleButton* floatButton = nullptr;
    TitleButton* closeButton = nullptr;

public:
    explicit DockTitleBar(QDockWidget* dock);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateButtons(void);
};


} // namespace calib

#endif // CALIBSTUDIO_WINDOWCHROME_H
