#include "camera/gige/gigecontrols.h"

#include "camera/gige/genicam.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

namespace calib
{

namespace
{


class GigECameraControls final : public ICameraControls
{
private:
    struct Feature
    {
        CameraControlInfo info;
        std::string valueNode;
        std::string autoNode;
        double scale = 1.0;
    };

    GenICamNodeMap& nodes;
    std::vector<Feature> features;

public:
    explicit GigECameraControls(GenICamNodeMap& nodes)
        : nodes(nodes)
    {
        addFeature(CameraControlId::Exposure, {"ExposureTime", "ExposureTimeAbs", "ExposureTimeRaw"}, "ExposureAuto");
        addFeature(CameraControlId::Gain, {"Gain", "GainAbs", "GainRaw"}, "GainAuto");
    }

    bool isEmpty(void) const
    {
        return features.empty();
    }

    std::vector<CameraControlInfo> list(void) const override
    {
        std::vector<CameraControlInfo> infos;
        for(const Feature& feature : features)
        {
            infos.push_back(feature.info);
        }
        return infos;
    }

    bool get(CameraControlId id, CameraControlState* state) const override
    {
        const Feature* feature = find(id);
        double value = 0;
        if(!feature || !state || !nodes.getNumber(feature->valueNode, &value))
        {
            return false;
        }
        state->value = static_cast<int>(std::lround(value * feature->scale));
        state->automatic = false;
        std::string mode;
        if(!feature->autoNode.empty() && nodes.getEnumeration(feature->autoNode, &mode))
        {
            state->automatic = mode != "Off";
        }
        state->mode = state->automatic ? ControlMode::Auto : ControlMode::Manual;
        return true;
    }

    bool set(CameraControlId id, const CameraControlState& state) override
    {
        const Feature* feature = find(id);
        if(!feature)
        {
            return false;
        }
        if(state.automatic && feature->info.supportsAuto)
        {
            return nodes.setEnumeration(feature->autoNode, "Continuous");
        }
        if(!feature->autoNode.empty())
        {
            nodes.setEnumeration(feature->autoNode, "Off");
        }
        const int value = std::clamp(state.value, feature->info.minimum, feature->info.maximum);
        return nodes.setNumber(feature->valueNode, value / feature->scale);
    }

private:
    const Feature* find(CameraControlId id) const
    {
        const auto it = std::find_if(features.begin(), features.end(),
                                     [id](const Feature& feature) { return feature.info.id == id; });
        return it == features.end() ? nullptr : &*it;
    }

    void addFeature(CameraControlId id, std::initializer_list<const char*> candidates, const char* autoNode)
    {
        for(const char* name : candidates)
        {
            double current = 0;
            if(!nodes.has(name) || !nodes.getNumber(name, &current))
            {
                continue;
            }

            Feature feature;
            feature.valueNode = name;
            feature.info.id = id;
            const bool raw = std::string(name).find("Raw") != std::string::npos || !nodes.isFloat(name);
            if(id == CameraControlId::Exposure)
            {
                feature.info.unit = raw ? ControlUnit::Raw : ControlUnit::Microseconds;
            }
            else
            {
                feature.info.unit = raw ? ControlUnit::Raw : ControlUnit::TenthDecibel;
                feature.scale = raw ? 1.0 : 10.0;
            }

            double minimum = 0;
            double maximum = 0;
            if(!nodes.getRange(name, &minimum, &maximum) || maximum <= minimum)
            {
                minimum = std::min(current, id == CameraControlId::Exposure ? 10.0 : 0.0);
                maximum = std::max(current, id == CameraControlId::Exposure ? 1000000.0 : 24.0);
            }
            feature.info.minimum = static_cast<int>(std::ceil(minimum * feature.scale));
            feature.info.maximum = static_cast<int>(std::floor(std::min(maximum * feature.scale, 2.0e9)));
            feature.info.step = 1;
            feature.info.defaultValue = std::clamp(static_cast<int>(std::lround(current * feature.scale)),
                                                   feature.info.minimum, feature.info.maximum);
            feature.info.supportsManual = true;

            const std::vector<std::string> modes = nodes.enumerationEntries(autoNode);
            if(std::find(modes.begin(), modes.end(), "Continuous") != modes.end())
            {
                feature.autoNode = autoNode;
                feature.info.supportsAuto = true;
            }
            features.push_back(feature);
            return;
        }
    }
};


} // namespace

std::unique_ptr<ICameraControls> createGigEControls(GenICamNodeMap& nodes)
{
    auto controls = std::make_unique<GigECameraControls>(nodes);
    if(controls->isEmpty())
    {
        return nullptr;
    }
    return controls;
}

} // namespace calib
