#ifndef CALIBSTUDIO_GVSP_H
#define CALIBSTUDIO_GVSP_H

#include <opencv2/core/mat.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// GigE Vision Stream Protocol (GVSP): image blocks split into a leader packet,
// numbered payload packets and a trailer packet, all UDP.
namespace calib::gvsp
{

constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kIpUdpOverhead = 28;

// Pixel formats (PFNC / GigE Vision values).
constexpr std::uint32_t kMono8 = 0x01080001;
constexpr std::uint32_t kMono10 = 0x01100003;
constexpr std::uint32_t kMono10Packed = 0x010C0004;
constexpr std::uint32_t kMono12 = 0x01100005;
constexpr std::uint32_t kMono12Packed = 0x010C0006;
constexpr std::uint32_t kMono16 = 0x01100007;
constexpr std::uint32_t kBayerGR8 = 0x01080008;
constexpr std::uint32_t kBayerRG8 = 0x01080009;
constexpr std::uint32_t kBayerGB8 = 0x0108000A;
constexpr std::uint32_t kBayerBG8 = 0x0108000B;
constexpr std::uint32_t kBayerGR10 = 0x0110000C;
constexpr std::uint32_t kBayerRG10 = 0x0110000D;
constexpr std::uint32_t kBayerGB10 = 0x0110000E;
constexpr std::uint32_t kBayerBG10 = 0x0110000F;
constexpr std::uint32_t kBayerGR12 = 0x01100010;
constexpr std::uint32_t kBayerRG12 = 0x01100011;
constexpr std::uint32_t kBayerGB12 = 0x01100012;
constexpr std::uint32_t kBayerBG12 = 0x01100013;
constexpr std::uint32_t kRgb8 = 0x02180014;
constexpr std::uint32_t kBgr8 = 0x02180015;
constexpr std::uint32_t kRgba8 = 0x02200016;
constexpr std::uint32_t kBgra8 = 0x02200017;
constexpr std::uint32_t kYuv422Uyvy = 0x0210001F;
constexpr std::uint32_t kYuv422Yuyv = 0x02100032;

const char* pixelFormatName(std::uint32_t format);


inline int bitsPerPixel(std::uint32_t format)
{
    return static_cast<int>((format >> 16) & 0xFF);
}


bool toMat(std::uint32_t format, int width, int height, const std::uint8_t* data, std::size_t size, cv::Mat* out,
           std::string* error);



class FrameAssembler
{
private:
    std::size_t payloadPerPacket = 1440;
    bool inBlock = false;
    std::uint16_t blockId = 0;
    std::uint32_t pixelFormat = 0;
    int width = 0;
    int height = 0;
    std::size_t imageSize = 0;
    std::vector<std::uint8_t> image;
    std::vector<bool> received;
    std::size_t receivedCount = 0;
    std::atomic<std::uint64_t> incompleteFrames{0};
    std::atomic<std::uint64_t> lostPackets{0};
    std::atomic<std::uint64_t> packets{0};

public:
    enum class Result
    {
        None,
        Frame,
    };

    void setPacketSize(std::size_t packetSize);
    void reset(void);
    Result push(const std::uint8_t* packet, std::size_t size);
    void swapImage(std::vector<std::uint8_t>& other);

    const std::vector<std::uint8_t>& getImage(void) const
    {
        return image;
    }
    std::size_t getImageSize(void) const
    {
        return imageSize;
    }
    std::uint32_t getPixelFormat(void) const
    {
        return pixelFormat;
    }
    int getWidth(void) const
    {
        return width;
    }
    int getHeight(void) const
    {
        return height;
    }
    std::uint64_t getIncompleteFrames(void) const
    {
        return incompleteFrames;
    }
    std::uint64_t getLostPackets(void) const
    {
        return lostPackets;
    }
    std::uint64_t getPackets(void) const
    {
        return packets;
    }

private:
    void startBlock(std::uint16_t id, const std::uint8_t* leader, std::size_t size);
    void dropBlock(void);
};


} // namespace calib::gvsp

#endif // CALIBSTUDIO_GVSP_H
