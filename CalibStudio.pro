QT       += core gui widgets network

CONFIG += c++17

win32 {
    # OpenCV from the same prefix as Qt, e.g. MSYS2 UCRT64
    # (pacman -S mingw-w64-ucrt-x86_64-opencv).
    # OpenCV 5 installs its headers into include/opencv5, OpenCV 4 into include/opencv4.
    QT_PREFIX = $$[QT_INSTALL_PREFIX]
    OPENCV_INCLUDE =
    exists($$QT_PREFIX/include/opencv5/opencv2/core.hpp): OPENCV_INCLUDE = $$QT_PREFIX/include/opencv5
    else: exists($$QT_PREFIX/include/opencv4/opencv2/core.hpp): OPENCV_INCLUDE = $$QT_PREFIX/include/opencv4

    isEmpty(OPENCV_INCLUDE) {
        error("OpenCV not found in $$QT_PREFIX. Install it with the same toolchain as Qt, e.g. pacman -S mingw-w64-ucrt-x86_64-opencv")
    }
    message("OpenCV: $$OPENCV_INCLUDE")
    INCLUDEPATH += $$OPENCV_INCLUDE

    LIBS += -L$$QT_PREFIX/lib \
            -lopencv_core \
            -lopencv_imgproc \
            -lopencv_imgcodecs \
            -lopencv_videoio

    LIBS += -lstrmiids -lole32 -loleaut32 -luuid
    DEFINES += NOMINMAX
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += opencv4
}





# --- Sources ------------------------------------------------------------------
# src/app          application entry point and main window
# src/camera       camera model, discovery interface, device and driver controls
#   directshow/    Windows cameras (DirectShow): discovery and UVC controls
#   gige/          GigE Vision discovery (GVCP)
#   generic/       fallback discovery through OpenCV
# src/capture      capture thread: camera life cycle and frame delivery
# src/processing   image processing on frames (preview filters)
# src/settings     settings saved between runs (camera profiles, filters)
# src/ui           widgets
# Includes are written relative to src/, e.g. #include "camera/cameradevice.h".
INCLUDEPATH += $$PWD/src

SOURCES += \
    src/app/main.cpp \
    src/app/mainwindow.cpp \
    src/camera/camerainfo.cpp \
    src/camera/cameracontrols.cpp \
    src/camera/cameraprovider.cpp \
    src/camera/cameradevice.cpp \
    src/camera/cameraprofile.cpp \
    src/camera/gige/gvcp.cpp \
    src/camera/gige/gigevisionprovider.cpp \
    src/capture/capturethread.cpp \
    src/processing/framefilter.cpp \
    src/settings/settingsstore.cpp \
    src/ui/camerasettingswidget.cpp \
    src/ui/filterpanel.cpp \
    src/ui/videowidget.cpp

HEADERS += \
    src/app/mainwindow.h \
    src/camera/camerainfo.h \
    src/camera/cameracontrols.h \
    src/camera/cameraprovider.h \
    src/camera/cameradevice.h \
    src/camera/cameraprofile.h \
    src/camera/gige/gvcp.h \
    src/camera/gige/gigevisionprovider.h \
    src/capture/capturethread.h \
    src/processing/framefilter.h \
    src/settings/settingsstore.h \
    src/ui/camerasettingswidget.h \
    src/ui/filterpanel.h \
    src/ui/videowidget.h

win32 {
    SOURCES += src/camera/directshow/directshowprovider.cpp \
               src/camera/directshow/directshowcontrols.cpp
    HEADERS += src/camera/directshow/directshowprovider.h \
               src/camera/directshow/directshowcontrols.h \
               src/camera/directshow/dshowcommon.h
} else {
    SOURCES += src/camera/generic/opencvprobeprovider.cpp
    HEADERS += src/camera/generic/opencvprobeprovider.h
}

TRANSLATIONS += \
    translations/CalibStudio_en_US.ts
CONFIG += lrelease
CONFIG += embed_translations



qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
