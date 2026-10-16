#include "camera/generic/opencvprobeprovider.h"

#include <opencv2/videoio.hpp>

namespace calib
{


std::vector<CameraInfo> OpenCvProbeProvider::enumerate(std::string* error)
{
    std::vector<CameraInfo> cameras;
    for(int index = 0; index <= maxIndex; ++index)
    {
        try
        {
            cv::VideoCapture capture;
            if(!capture.open(index, cv::CAP_ANY))
            {
                continue;
            }

            CameraInfo info;
            info.api = name();
            info.name = "Camera #" + std::to_string(index);
            info.id = "probe:" + std::to_string(index);
            info.details = "OpenCV backend " + capture.getBackendName();
            info.captureApi = cv::CAP_ANY;
            info.captureIndex = index;
            info.streamable = true;
            cameras.push_back(std::move(info));
        }
        catch(const cv::Exception& e)
        {
            if(error)
            {
                *error = e.what();
            }
        }
    }

    return cameras;
}


} // namespace calib
