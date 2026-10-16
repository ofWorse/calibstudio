#include "settings/settingsstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace calib
{

namespace
{


const QString kCameraPrefix = QStringLiteral("camera-");
const QString kFilters = QStringLiteral("filters");


QString sectionFor(const std::string& key)
{
    const QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString(key), QCryptographicHash::Sha1);
    return kCameraPrefix + QString::fromLatin1(hash.toHex().left(16));
}


struct StructureName
{
    StructureFilter filter;
    const char* name;
};

constexpr StructureName kStructureNames[] =
{
    {StructureFilter::None,              "none"              },
    {StructureFilter::FixedThreshold,    "fixed_threshold"   },
    {StructureFilter::OtsuThreshold,     "otsu_threshold"    },
    {StructureFilter::AdaptiveThreshold, "adaptive_threshold"},
    {StructureFilter::CannyEdges,        "canny_edges"       },
};

struct ColorMapName
{
    FalseColorMap map;
    const char* name;
};

constexpr ColorMapName kColorMapNames[] =
{
    {FalseColorMap::None,    "none"   },
    {FalseColorMap::Jet,     "jet"    },
    {FalseColorMap::Turbo,   "turbo"  },
    {FalseColorMap::Viridis, "viridis"},
    {FalseColorMap::Inferno, "inferno"},
    {FalseColorMap::Hot,     "hot"    },
};

QString structureName(StructureFilter filter)
{
    for(const StructureName& entry : kStructureNames)
    {
        if(entry.filter == filter)
        {
            return QString::fromLatin1(entry.name);
        }
    }
    return QStringLiteral("none");
}

StructureFilter structureFromName(const QString& name)
{
    for(const StructureName& entry : kStructureNames)
    {
        if(name == QLatin1String(entry.name))
        {
            return entry.filter;
        }
    }
    return StructureFilter::None;
}

QString colorMapName(FalseColorMap map)
{
    for(const ColorMapName& entry : kColorMapNames)
    {
        if(entry.map == map)
        {
            return QString::fromLatin1(entry.name);
        }
    }
    return QStringLiteral("none");
}

FalseColorMap colorMapFromName(const QString& name)
{
    for(const ColorMapName& entry : kColorMapNames)
    {
        if(name == QLatin1String(entry.name))
        {
            return entry.map;
        }
    }
    return FalseColorMap::None;
}


} // namespace

SettingsStore::SettingsStore(void)
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(folder);
    settings = std::make_unique<QSettings>(folder + QStringLiteral("/CalibStudio.ini"), QSettings::IniFormat);
}

SettingsStore::~SettingsStore(void)
{
    sync();
}

QString SettingsStore::filePath(void) const
{
    return settings->fileName();
}

std::map<std::string, CameraProfile> SettingsStore::loadCameraProfiles(void) const
{
    std::map<std::string, CameraProfile> profiles;
    for(const QString& section : settings->childGroups())
    {
        if(!section.startsWith(kCameraPrefix))
        {
            continue;
        }
        settings->beginGroup(section);

        const std::string key = settings->value(QStringLiteral("key")).toString().toStdString();
        CameraProfile profile;
        profile.flipHorizontal = settings->value(QStringLiteral("flip/horizontal"), false).toBool();
        profile.flipVertical = settings->value(QStringLiteral("flip/vertical"), false).toBool();

        if(settings->contains(QStringLiteral("mode/width")))
        {
            profile.hasMode = true;
            profile.mode.width = settings->value(QStringLiteral("mode/width")).toInt();
            profile.mode.height = settings->value(QStringLiteral("mode/height")).toInt();
            profile.mode.fourcc = settings->value(QStringLiteral("mode/fourcc")).toString().toStdString();
        }

        settings->beginGroup(QStringLiteral("controls"));
        for(const QString& name : settings->childGroups())
        {
            CameraControlId id;
            if(!controlIdFromKey(name.toStdString(), &id))
            {
                continue;
            }
            bool valid = false;
            CameraControlState state;
            state.value = settings->value(name + QStringLiteral("/value")).toInt(&valid);
            state.automatic = settings->value(name + QStringLiteral("/auto"), false).toBool();
            if(valid)
            {
                profile.controls[id] = state;
            }
        }
        settings->endGroup();

        settings->endGroup();
        if(!key.empty())
        {
            profiles[key] = profile;
        }
    }
    return profiles;
}

void SettingsStore::saveCameraProfile(const std::string& key, const QString& cameraName, const CameraProfile& profile)
{
    settings->beginGroup(sectionFor(key));
    settings->setValue(QStringLiteral("key"), QString::fromStdString(key));
    settings->setValue(QStringLiteral("name"), cameraName);
    settings->setValue(QStringLiteral("flip/horizontal"), profile.flipHorizontal);
    settings->setValue(QStringLiteral("flip/vertical"), profile.flipVertical);

    settings->remove(QStringLiteral("mode"));
    if(profile.hasMode)
    {
        settings->setValue(QStringLiteral("mode/width"), profile.mode.width);
        settings->setValue(QStringLiteral("mode/height"), profile.mode.height);
        settings->setValue(QStringLiteral("mode/fourcc"), QString::fromStdString(profile.mode.fourcc));
    }

    settings->remove(QStringLiteral("controls"));
    for(const auto& [id, state] : profile.controls)
    {
        const QString prefix = QStringLiteral("controls/") + QString::fromLatin1(toKey(id));
        settings->setValue(prefix + QStringLiteral("/value"), state.value);
        settings->setValue(prefix + QStringLiteral("/auto"), state.automatic);
    }
    settings->endGroup();
}

FilterSettings SettingsStore::loadFilterSettings(void) const
{
    const FilterSettings defaults;
    FilterSettings filters;
    settings->beginGroup(kFilters);
    filters.grayscale = settings->value(QStringLiteral("grayscale"), defaults.grayscale).toBool();
    filters.negative = settings->value(QStringLiteral("negative"), defaults.negative).toBool();
    filters.colorMap = colorMapFromName(settings->value(QStringLiteral("color_map")).toString());
    filters.equalize = settings->value(QStringLiteral("equalize"), defaults.equalize).toBool();
    filters.claheClipLimit = settings->value(QStringLiteral("clahe_clip_limit"), defaults.claheClipLimit).toDouble();
    filters.blurKernel = settings->value(QStringLiteral("blur_kernel"), defaults.blurKernel).toInt();
    filters.sharpenPercent = settings->value(QStringLiteral("sharpen_percent"), defaults.sharpenPercent).toInt();
    filters.structure = structureFromName(settings->value(QStringLiteral("structure")).toString());
    filters.threshold = settings->value(QStringLiteral("threshold"), defaults.threshold).toInt();
    filters.adaptiveBlockSize =
        settings->value(QStringLiteral("adaptive_block_size"), defaults.adaptiveBlockSize).toInt();
    filters.adaptiveOffset = settings->value(QStringLiteral("adaptive_offset"), defaults.adaptiveOffset).toInt();
    filters.cannyLow = settings->value(QStringLiteral("canny_low"), defaults.cannyLow).toInt();
    filters.cannyHigh = settings->value(QStringLiteral("canny_high"), defaults.cannyHigh).toInt();
    settings->endGroup();
    return filters;
}

void SettingsStore::saveFilterSettings(const FilterSettings& filters)
{
    settings->beginGroup(kFilters);
    settings->setValue(QStringLiteral("grayscale"), filters.grayscale);
    settings->setValue(QStringLiteral("negative"), filters.negative);
    settings->setValue(QStringLiteral("color_map"), colorMapName(filters.colorMap));
    settings->setValue(QStringLiteral("equalize"), filters.equalize);
    settings->setValue(QStringLiteral("clahe_clip_limit"), filters.claheClipLimit);
    settings->setValue(QStringLiteral("blur_kernel"), filters.blurKernel);
    settings->setValue(QStringLiteral("sharpen_percent"), filters.sharpenPercent);
    settings->setValue(QStringLiteral("structure"), structureName(filters.structure));
    settings->setValue(QStringLiteral("threshold"), filters.threshold);
    settings->setValue(QStringLiteral("adaptive_block_size"), filters.adaptiveBlockSize);
    settings->setValue(QStringLiteral("adaptive_offset"), filters.adaptiveOffset);
    settings->setValue(QStringLiteral("canny_low"), filters.cannyLow);
    settings->setValue(QStringLiteral("canny_high"), filters.cannyHigh);
    settings->endGroup();
}

void SettingsStore::sync(void)
{
    settings->sync();
}


} // namespace calib
