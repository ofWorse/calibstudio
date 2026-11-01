#include "camera/cameradevice.h"

#include "camera/gige/gigedevice.h"
#include "camera/gige/gvsp.h"

#include <cctype>
#include <chrono>
#include <thread>
#include <utility>

namespace calib
{

namespace
{

constexpr double kMinLiveFps = 24.0;
constexpr auto kFirstFrameTimeout = std::chrono::seconds(3);
constexpr int kGigEReadTimeoutMs = 1000;

void setError(std::string* error, std::string message)
{
    if(error)
    {
        *error = std::move(message);
    }
}

std::string fourccToString(int fourcc)
{
    std::string code(4, ' ');
    for(int i = 0; i < 4; ++i)
    {
        const auto c = static_cast<unsigned char>((fourcc >> (8 * i)) & 0xFF);
        if(!std::isprint(c))
        {
            return {};
        }
        code[static_cast<size_t>(i)] = static_cast<char>(c);
    }
    return code;
}

} // namespace

const VideoMode* pickLiveMode(const std::vector<VideoMode>& modes)
{
    const auto isBetter = [](const VideoMode& a, const VideoMode& b)
    {
        if(a.pixels() != b.pixels())
        {
            return a.pixels() > b.pixels();
        }
        if(a.compressed != b.compressed)
        {
            return !a.compressed;
        }
        return a.maxFps > b.maxFps;
    };

    const VideoMode* best = nullptr;
    for(const VideoMode& mode : modes)
    {
        if(mode.maxFps >= kMinLiveFps - 0.5 && (!best || isBetter(mode, *best)))
        {
            best = &mode;
        }
    }
    if(best)
    {
        return best;
    }

    for(const VideoMode& mode : modes)
    {
        if(!best || mode.maxFps > best->maxFps || (mode.maxFps == best->maxFps && mode.pixels() > best->pixels()))
        {
            best = &mode;
        }
    }
    return best;
}

CameraDevice::CameraDevice(void) = default;

CameraDevice::~CameraDevice(void)
{
    close();
}

bool CameraDevice::open(const CameraInfo& info, std::string* error, int modeIndex)
{
    close();

    if(info.transport == CameraTransport::GigE)
    {
        return openGigE(info, error);
    }

    if(!info.streamable || info.captureIndex < 0)
    {
        setError(error,
                 info.notStreamableReason.empty() ? "this camera cannot be opened by CalibStudio"
                                                  : info.notStreamableReason);
        return false;
    }

    try
    {
        if(!capture.open(info.captureIndex, info.captureApi))
        {
            setError(error, "cv::VideoCapture::open() failed (camera busy or unplugged?)");
            return false;
        }

        const VideoMode* mode = nullptr;
        if(modeIndex >= 0 && modeIndex < static_cast<int>(info.modes.size()))
        {
            mode = &info.modes[static_cast<size_t>(modeIndex)];
        }
        else
        {
            mode = pickLiveMode(info.modes);
        }
        if(mode)
        {
            applyMode(*mode, info.captureApi);
        }

        cv::Mat first;
        const auto deadline = std::chrono::steady_clock::now() + kFirstFrameTimeout;
        bool gotFrame = false;
        while(!gotFrame && std::chrono::steady_clock::now() < deadline)
        {
            gotFrame = capture.read(first) && !first.empty();
            if(!gotFrame)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        if(!gotFrame)
        {
            capture.release();
            setError(error, "the camera opened but delivered no frames (is another application using it?)");
            return false;
        }

        this->info = info;
        activeModeIndex = mode ? static_cast<int>(mode - info.modes.data()) : -1;
        activeMode.width = first.cols;
        activeMode.height = first.rows;
        activeMode.maxFps = capture.get(cv::CAP_PROP_FPS);
        activeMode.fourcc = fourccToString(static_cast<int>(capture.get(cv::CAP_PROP_FOURCC)));
        activeMode.compressed = activeMode.fourcc == "MJPG";

        controls = createCameraControls(info);

        return true;
    }
    catch(const cv::Exception& e)
    {
        capture.release();
        setError(error, e.what());
        return false;
    }
}

bool CameraDevice::openGigE(const CameraInfo& info, std::string* error)
{
    if(!info.streamable)
    {
        setError(error, info.notStreamableReason);
        return false;
    }

    auto device = std::make_unique<GigEDevice>();
    if(!device->open(info, error))
    {
        return false;
    }

    cv::Mat first;
    const auto deadline = std::chrono::steady_clock::now() + kFirstFrameTimeout;
    bool gotFrame = false;
    while(!gotFrame && device->isOpened() && std::chrono::steady_clock::now() < deadline)
    {
        gotFrame = device->read(first, 500);
    }
    if(!gotFrame)
    {
        if(device->getReceivedPackets() == 0)
        {
            setError(error,
                     "the camera started streaming but no image packet arrived (" + device->getLastError()
                         + "). Allow CalibStudio in the firewall for this network (incoming UDP).");
        }
        else
        {
            const std::uint64_t received = device->getReceivedPackets();
            const std::uint64_t lost = device->getLostPackets();
            setError(error,
                     "images arrive incomplete (" + std::to_string(lost * 100 / (received + lost))
                         + "% of the packets are lost). Connect the camera to its own network card, raise "
                           "the card's receive buffers or lower the camera's frame rate.");
        }
        return false;
    }

    this->info = info;
    activeModeIndex = -1;
    activeMode.width = first.cols;
    activeMode.height = first.rows;
    activeMode.maxFps = 0.0;
    activeMode.fourcc = gvsp::pixelFormatName(device->getPixelFormat());
    activeMode.compressed = false;
    gige = std::move(device);
    return true;
}

void CameraDevice::applyMode(const VideoMode& mode, int captureApi)
{
    const bool hasFourcc = mode.fourcc.size() == 4;
    const int fourcc = hasFourcc
        ? cv::VideoWriter::fourcc(mode.fourcc[0], mode.fourcc[1], mode.fourcc[2], mode.fourcc[3])
        : 0;

    if(captureApi == cv::CAP_DSHOW)
    {
        capture.set(cv::CAP_PROP_FRAME_WIDTH, mode.width);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, mode.height);
        if(hasFourcc)
        {
            capture.set(cv::CAP_PROP_FOURCC, fourcc);
        }
    }
    else
    {
        if(hasFourcc)
        {
            capture.set(cv::CAP_PROP_FOURCC, fourcc);
        }
        capture.set(cv::CAP_PROP_FRAME_WIDTH, mode.width);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, mode.height);
        if(mode.maxFps > 0.0)
        {
            capture.set(cv::CAP_PROP_FPS, mode.maxFps);
        }
    }
}

bool CameraDevice::isOpened(void) const
{
    return capture.isOpened() || (gige && gige->isOpened());
}

void CameraDevice::close(void)
{
    controls.reset();
    gige.reset();
    lastError.clear();
    if(capture.isOpened())
    {
        capture.release();
    }
    info = CameraInfo{};
    activeMode = VideoMode{};
    activeModeIndex = -1;
}

bool CameraDevice::read(cv::Mat& frame)
{
    if(gige)
    {
        if(gige->read(frame, kGigEReadTimeoutMs))
        {
            return true;
        }
        lastError = gige->getLastError();
        return false;
    }
    if(!capture.isOpened())
    {
        return false;
    }
    try
    {
        return capture.read(frame) && !frame.empty();
    }
    catch(const cv::Exception&)
    {
        return false;
    }
}

std::uint64_t CameraDevice::getIncompleteFrames(void) const
{
    return gige ? gige->getIncompleteFrames() : 0;
}

std::string CameraDevice::backendName(void) const
{
    if(gige)
    {
        return "GigE Vision (GVSP, " + std::to_string(gige->getPacketSize()) + "-byte packets)";
    }
    if(!capture.isOpened())
    {
        return {};
    }
    try
    {
        return capture.getBackendName();
    }
    catch(const cv::Exception&)
    {
        return {};
    }
}


} // namespace calib
