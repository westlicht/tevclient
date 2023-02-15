// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#pragma once

#if __cplusplus >= 202002L
#define TEVCLIENT_CPP20
#endif

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef TEVCLIENT_CPP20
#include <span>
#endif

namespace tevclient
{

struct VgCommand
{
    enum class EType : int8_t
    {
        Invalid = 127,
        Save = 0,
        Restore = 1,
        FillColor = 2,
        Fill = 3,
        StrokeColor = 4,
        Stroke = 5,
        BeginPath = 6,
        ClosePath = 7,
        PathWinding = 8,
        DebugDumpPathCache = 9,
        MoveTo = 10,
        LineTo = 11,
        ArcTo = 12,
        Arc = 13,
        BezierTo = 14,
        Circle = 15,
        Ellipse = 16,
        QuadTo = 17,
        Rect = 18,
        RoundedRect = 19,
        RoundedRectVarying = 20,
    };

    enum EWinding : int
    {
        CounterClockwise = 1,
        Clockwise = 2,
    };

    struct Pos
    {
        float x, y;
    };

    struct Size
    {
        float width, height;
    };

    struct Color
    {
        float r, g, b, a;
    };

    VgCommand() : type{EType::Invalid}
    {
    }
    
    VgCommand(EType type, const std::vector<float> &data) : type{type}, data{data}
    {
    }

    static VgCommand save()
    {
        return {EType::Save, {}};
    }

    static VgCommand restore()
    {
        return {EType::Restore, {}};
    }

    static VgCommand fillColor(const Color &c)
    {
        return {EType::FillColor, {c.r, c.g, c.b, c.a}};
    }

    static VgCommand fill()
    {
        return {EType::Fill, {}};
    }

    static VgCommand strokeColor(const Color &c)
    {
        return {EType::StrokeColor, {c.r, c.g, c.b, c.a}};
    }

    static VgCommand stroke()
    {
        return {EType::Stroke, {}};
    }

    static VgCommand beginPath()
    {
        return {EType::BeginPath, {}};
    }

    static VgCommand closePath()
    {
        return {EType::ClosePath, {}};
    }

    static VgCommand pathWinding(EWinding winding)
    {
        return {EType::PathWinding, {(float)(int)winding}};
    }

    static VgCommand moveTo(const Pos &p)
    {
        return {EType::MoveTo, {p.x, p.y}};
    }

    static VgCommand lineTo(const Pos &p)
    {
        return {EType::LineTo, {p.x, p.y}};
    }

    static VgCommand arcTo(const Pos &p1, const Pos &p2, float radius)
    {
        return {EType::ArcTo, {p1.x, p1.y, p2.x, p2.y, radius}};
    }

    static VgCommand arc(const Pos &center, float radius, float angle_begin, float angle_end, EWinding winding)
    {
        return {EType::Arc, {center.x, center.y, radius, angle_begin, angle_end, (float)(int)winding}};
    }

    static VgCommand bezierTo(const Pos &c1, const Pos &c2, const Pos &p)
    {
        return {EType::BezierTo, {c1.x, c1.y, c2.x, c2.y, p.x, p.y}};
    }

    static VgCommand circle(const Pos &center, float radius)
    {
        return {EType::Circle, {center.x, center.y, radius}};
    }

    static VgCommand ellipse(const Pos &center, const Size &radius)
    {
        return {EType::Ellipse, {center.x, center.y, radius.width, radius.height}};
    }

    static VgCommand quadTo(const Pos &c, const Pos &p)
    {
        return {EType::QuadTo, {c.x, c.y, p.x, p.y}};
    }

    static VgCommand rect(const Pos &p, const Size &size)
    {
        return {EType::Rect, {p.x, p.y, size.width, size.height}};
    }

    static VgCommand roundedRect(const Pos &p, const Size &size, float radius)
    {
        return {EType::RoundedRect, {p.x, p.y, size.width, size.height, radius}};
    }

    static VgCommand roundedRectVarying(const Pos &p, const Size &size, float radiusTopLeft, float radiusTopRight,
                                        float radiusBottomRight, float radiusBottomLeft)
    {
        return {
            EType::RoundedRectVarying,
            {p.x, p.y, size.width, size.height, radiusTopLeft, radiusTopRight, radiusBottomRight, radiusBottomLeft}};
    }

    EType type;
    std::vector<float> data;
};

struct ChannelDesc
{
    std::string name;
    int64_t offset;
    int64_t stride;
};

enum class Error
{
    Ok,
    SocketError,
    ArgumentError,
};

class Client
{
public:
    Client(const char *hostname = "127.0.0.1", uint16_t port = 14158);
    ~Client();

    Client(const Client &) = delete;
    Client(Client &&) = delete;
    Client &operator=(const Client &) = delete;
    Client &operator=(Client &&) = delete;

    Error openImage(std::string_view imagePath, std::string_view channelSelector = "", bool grabFocus = true);
    Error reloadImage(std::string_view imageName, bool grabFocus = true);
    Error closeImage(std::string_view imageName);

    Error createImage(std::string_view imageName, uint32_t width, uint32_t height,
                      const std::vector<std::string> &channelNames, bool grabFocus = true);
    Error createImage(std::string_view imageName, uint32_t width, uint32_t height, uint32_t channelCount,
                      bool grabFocus = true);
    Error createImage(std::string_view imageName, uint32_t width, uint32_t height, uint32_t channelCount,
                      const float *imageData, size_t imageDataLength, bool grabFocus = true);

    Error updateImage(std::string_view imageName, int32_t x, int32_t y, int32_t width, int32_t height,
                      const std::vector<ChannelDesc> &channelDescs, const float *imageData, size_t imageDataLength,
                      bool grabFocus = true);
    Error updateImage(std::string_view imageName, uint32_t width, uint32_t height, uint32_t channelCount,
                      const float *imageData, size_t imageDataLength, bool grabFocus = true);

    Error vectorGraphics(std::string_view imageName, const VgCommand *commands, size_t commandCount, bool append = true,
                         bool grabFocus = true);
    Error vectorGraphics(std::string_view imageName, const std::vector<VgCommand> &commands, bool append = true,
                         bool grabFocus = true);
#ifdef TEVCLIENT_CPP20
    Error vectorGraphics(std::string_view imageName, std::span<VgCommand> commands, bool append = true,
                         bool grabFocus = true);
#endif

    Error lastError() const;
    const std::string &lastErrorString() const;

private:
    class Impl;
    Impl *mImpl;
};

} // namespace tevclient
