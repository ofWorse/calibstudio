#ifndef CALIBSTUDIO_SETTINGSSTORE_H
#define CALIBSTUDIO_SETTINGSSTORE_H

#include "camera/cameraprofile.h"
#include "processing/framefilter.h"

#include <QString>

#include <map>
#include <memory>
#include <string>

class QSettings;

namespace calib
{


class SettingsStore
{
private:
    std::unique_ptr<QSettings> settings;

public:
    SettingsStore(void);
    ~SettingsStore(void);
    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    QString filePath(void) const;

    std::map<std::string, CameraProfile> loadCameraProfiles(void) const;
    void saveCameraProfile(const std::string& key, const QString& cameraName, const CameraProfile& profile);

    FilterSettings loadFilterSettings(void) const;
    void saveFilterSettings(const FilterSettings& filters);

    void sync(void);
};


} // namespace calib

#endif // CALIBSTUDIO_SETTINGSSTORE_H
