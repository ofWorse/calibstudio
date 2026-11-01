#ifndef CALIBSTUDIO_THEME_H
#define CALIBSTUDIO_THEME_H

#include <QColor>
#include <QIcon>

class QApplication;

namespace calib::theme
{

inline const QColor kWorkArea(18, 19, 22);
inline const QColor kPanel(30, 32, 36);
inline const QColor kWindow(24, 25, 28);
inline const QColor kInput(22, 23, 26);
inline const QColor kTitle(38, 40, 44);
inline const QColor kTitleActive(46, 50, 58);
inline const QColor kControl(46, 49, 55);
inline const QColor kControlHover(58, 62, 70);
inline const QColor kBorder(62, 65, 72);
inline const QColor kAccent(76, 141, 255);
inline const QColor kText(226, 228, 232);
inline const QColor kDimText(146, 151, 160);
inline const QColor kDisabledText(96, 100, 108);

inline const QColor kStreaming(60, 200, 110);
inline const QColor kConnecting(240, 180, 40);
inline const QColor kLost(235, 80, 70);

enum class Icon
{
    Refresh,
    Tile,
    Cascade,
};

void apply(QApplication& application);

QIcon icon(Icon kind);

} // namespace calib::theme

#endif // CALIBSTUDIO_THEME_H
