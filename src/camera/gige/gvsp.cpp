#include "camera/gige/gvsp.h"

#include "camera/gige/gvcp.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace calib::gvsp
{

namespace
{

constexpr std::uint8_t kFormatLeader = 1;
constexpr std::uint8_t kFormatTrailer = 2;
constexpr std::uint8_t kFormatPayload = 3;
constexpr std::uint8_t kExtendedIdFlag = 0x80;
constexpr std::uint16_t kPayloadTypeImage = 0x0001;
constexpr std::size_t kLeaderImageSize = 36;
constexpr int kMaxDimension = 32768;
constexpr std::size_t kTolerableLossDivisor = 50;

} // namespace

const char* pixelFormatName(std::uint32_t format)
{
    switch(format)
    {
    case kMono8:        return "Mono8";
    case kMono10:       return "Mono10";
    case kMono10Packed: return "Mono10Packed";
    case kMono12:       return "Mono12";
    case kMono12Packed: return "Mono12Packed";
    case kMono16:       return "Mono16";
    case kBayerGR8:     return "BayerGR8";
    case kBayerRG8:     return "BayerRG8";
    case kBayerGB8:     return "BayerGB8";
    case kBayerBG8:     return "BayerBG8";
    case kBayerGR10:    return "BayerGR10";
    case kBayerRG10:    return "BayerRG10";
    case kBayerGB10:    return "BayerGB10";
    case kBayerBG10:    return "BayerBG10";
    case kBayerGR12:    return "BayerGR12";
    case kBayerRG12:    return "BayerRG12";
    case kBayerGB12:    return "BayerGB12";
    case kBayerBG12:    return "BayerBG12";
    case kRgb8:         return "RGB8";
    case kBgr8:         return "BGR8";
    case kRgba8:        return "RGBa8";
    case kBgra8:        return "BGRa8";
    case kYuv422Uyvy:   return "YUV422_UYVY";
    case kYuv422Yuyv:   return "YUV422_YUYV";
    default:            return "unknown";
    }
}

bool toMat(std::uint32_t format, int width, int height, const std::uint8_t* data, std::size_t size, cv::Mat* out,
           std::string* error)
{
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t needed = pixels * static_cast<std::size_t>(bitsPerPixel(format)) / 8;
    if(width <= 0 || height <= 0 || size < needed)
    {
        if(error)
        {
            *error = "image is smaller than its header says";
        }
        return false;
    }
    auto* bytes = const_cast<std::uint8_t*>(data);

    int bayerCode = -1;
    switch(format)
    {
    case kBayerRG8:
    case kBayerRG10:
    case kBayerRG12: bayerCode = cv::COLOR_BayerBG2BGR; break;
    case kBayerBG8:
    case kBayerBG10:
    case kBayerBG12: bayerCode = cv::COLOR_BayerRG2BGR; break;
    case kBayerGR8:
    case kBayerGR10:
    case kBayerGR12: bayerCode = cv::COLOR_BayerGB2BGR; break;
    case kBayerGB8:
    case kBayerGB10:
    case kBayerGB12: bayerCode = cv::COLOR_BayerGR2BGR; break;
    default:         break;
    }

    int significantBits = 0;
    switch(format)
    {
    case kMono10:
    case kBayerGR10:
    case kBayerRG10:
    case kBayerGB10:
    case kBayerBG10: significantBits = 10; break;
    case kMono12:
    case kBayerGR12:
    case kBayerRG12:
    case kBayerGB12:
    case kBayerBG12: significantBits = 12; break;
    case kMono16:    significantBits = 16; break;
    default:         break;
    }

    if(significantBits > 0)
    {
        cv::Mat eight;
        cv::Mat(height, width, CV_16UC1, bytes).convertTo(eight, CV_8U, 1.0 / (1 << (significantBits - 8)));
        if(bayerCode >= 0)
        {
            cv::cvtColor(eight, *out, bayerCode);
        }
        else
        {
            *out = eight;
        }
        return true;
    }
    if(bayerCode >= 0)
    {
        cv::cvtColor(cv::Mat(height, width, CV_8UC1, bytes), *out, bayerCode);
        return true;
    }

    switch(format)
    {
    case kMono8:        cv::Mat(height, width, CV_8UC1, bytes).copyTo(*out); return true;
    case kMono10Packed:
    case kMono12Packed:
    {
        out->create(height, width, CV_8UC1);
        std::uint8_t* target = out->ptr<std::uint8_t>();
        for(std::size_t i = 0; i + 1 < pixels; i += 2)
        {
            const std::uint8_t* triple = data + (i / 2) * 3;
            target[i] = triple[0];
            target[i + 1] = triple[2];
        }
        return true;
    }
    case kRgb8:       cv::cvtColor(cv::Mat(height, width, CV_8UC3, bytes), *out, cv::COLOR_RGB2BGR); return true;
    case kBgr8:       cv::Mat(height, width, CV_8UC3, bytes).copyTo(*out); return true;
    case kRgba8:      cv::cvtColor(cv::Mat(height, width, CV_8UC4, bytes), *out, cv::COLOR_RGBA2BGR); return true;
    case kBgra8:      cv::cvtColor(cv::Mat(height, width, CV_8UC4, bytes), *out, cv::COLOR_BGRA2BGR); return true;
    case kYuv422Uyvy: cv::cvtColor(cv::Mat(height, width, CV_8UC2, bytes), *out, cv::COLOR_YUV2BGR_UYVY); return true;
    case kYuv422Yuyv: cv::cvtColor(cv::Mat(height, width, CV_8UC2, bytes), *out, cv::COLOR_YUV2BGR_YUYV); return true;
    default:          break;
    }

    if(error)
    {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer),
                      "pixel format 0x%08X is not supported; set PixelFormat to Mono8 or a "
                      "Bayer/RGB format",
                      format);
        *error = buffer;
    }
    return false;
}


void FrameAssembler::setPacketSize(std::size_t packetSize)
{
    if(packetSize > kIpUdpOverhead + kHeaderSize)
    {
        payloadPerPacket = packetSize - kIpUdpOverhead - kHeaderSize;
    }
    reset();
}

void FrameAssembler::reset(void)
{
    inBlock = false;
    receivedCount = 0;
}

FrameAssembler::Result FrameAssembler::push(const std::uint8_t* packet, std::size_t size)
{
    if(size < kHeaderSize || (packet[4] & kExtendedIdFlag))
    {
        return Result::None;
    }
    ++packets;
    const std::uint16_t id = gvcp::readBe16(packet + 2);
    const std::uint8_t format = packet[4] & 0x0F;
    const std::uint32_t packetId = (static_cast<std::uint32_t>(packet[5]) << 16)
        | (static_cast<std::uint32_t>(packet[6]) << 8) | packet[7];
    const std::uint8_t* body = packet + kHeaderSize;
    const std::size_t bodySize = size - kHeaderSize;

    if(inBlock && id != blockId)
    {
        dropBlock();
    }

    switch(format)
    {
    case kFormatLeader:
        if(!inBlock)
        {
            startBlock(id, body, bodySize);
        }
        return Result::None;

    case kFormatPayload: {
        if(!inBlock || packetId == 0)
        {
            return Result::None;
        }
        if(bodySize > payloadPerPacket)
        {
            payloadPerPacket = bodySize;
            dropBlock();
            return Result::None;
        }
        const std::size_t index = packetId - 1;
        const std::size_t offset = index * payloadPerPacket;
        if(offset < imageSize)
        {
            std::memcpy(image.data() + offset, body, std::min(bodySize, imageSize - offset));
        }
        if(index < received.size() && !received[index])
        {
            received[index] = true;
            ++receivedCount;
        }
        return Result::None;
    }

    case kFormatTrailer: {
        if(!inBlock)
        {
            return Result::None;
        }
        inBlock = false;
        const std::size_t missing = received.size() - receivedCount;
        if(missing == 0)
        {
            return Result::Frame;
        }
        ++incompleteFrames;
        lostPackets += missing;
        return missing <= received.size() / kTolerableLossDivisor ? Result::Frame : Result::None;
    }

    default: return Result::None;
    }
}

void FrameAssembler::startBlock(std::uint16_t id, const std::uint8_t* leader, std::size_t size)
{
    if(size < kLeaderImageSize || (gvcp::readBe16(leader + 2) & 0x3FFF) != kPayloadTypeImage)
    {
        return;
    }
    const std::uint32_t format = gvcp::readBe32(leader + 12);
    const auto sizeX = static_cast<int>(gvcp::readBe32(leader + 16));
    const auto sizeY = static_cast<int>(gvcp::readBe32(leader + 20));
    if(sizeX <= 0 || sizeY <= 0 || sizeX > kMaxDimension || sizeY > kMaxDimension || bitsPerPixel(format) == 0)
    {
        return;
    }

    blockId = id;
    pixelFormat = format;
    width = sizeX;
    height = sizeY;
    imageSize = static_cast<std::size_t>(sizeX) * static_cast<std::size_t>(sizeY)
        * static_cast<std::size_t>(bitsPerPixel(format)) / 8;
    image.resize(imageSize);
    received.assign((imageSize + payloadPerPacket - 1) / payloadPerPacket, false);
    receivedCount = 0;
    inBlock = true;
}

void FrameAssembler::dropBlock(void)
{
    if(inBlock)
    {
        ++incompleteFrames;
        lostPackets += received.size() - receivedCount;
    }
    inBlock = false;
}


} // namespace calib::gvsp
