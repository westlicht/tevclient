// This file was developed by Thomas Müller <thomas94@gmx.net> and Simon Kallweit <simon.kallweit@gmail.com>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#pragma once

#include <cstdint>
#include <vector>

namespace tevclient
{

/// Vector graphics command.
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

/// Error codes.
enum class Error
{
    Ok,
    SocketError,
    ArgumentError,
};

/**
 * @brief Class for communicating to the tev image viewer.
 * 
 * Communication is unidirectional (client -> tev server).
 * All calls are blocking.
 */
class Client
{
public:
    Client(const char *hostname = "127.0.0.1", uint16_t port = 14158);
    ~Client();

    Client(const Client &) = delete;
    Client(Client &&) = delete;
    Client &operator=(const Client &) = delete;
    Client &operator=(Client &&) = delete;

    /**
     * @brief Open an image from a file.
     *
     * @param imagePath Path to image.
     * @param channelSelector Channel to select (optional).
     * @param grabFocus Bring the tev window to the front.
     * @return Error::Ok if successful.
     */
    Error openImage(const char *imagePath, const char *channelSelector = "", bool grabFocus = true);

    /**
     * @brief Reload an image.
     *
     * @param imageName Name of the image.
     * @param grabFocus Bring the tev window to the front.
     * @return Error::Ok if successful.
     */
    Error reloadImage(const char *imageName, bool grabFocus = true);

    /**
     * @brief Close an image.
     *
     * @param imageName Name of the image.
     * @return Error::Ok if successful.
     */
    Error closeImage(const char *imageName);

    /**
     * @brief Create a new empty image.
     *
     * This creates a new empty image in tev that can then be updated using updateImage().
     * If channel names are not provided they default to: R, G, B, A.
     *
     * @param imageName Name of the image.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param channelCount Number of channels.
     * @param channelNames Channel names (optional if number of channels <= 4).
     * @param grabFocus Bring the tev window to the front.
     * @return Error::Ok if successful.
     */
    Error createImage(const char *imageName, uint32_t width, uint32_t height, uint32_t channelCount,
                      const char **channelNames = nullptr, bool grabFocus = true);

    /**
     * @brief Update an existing image.
     *
     * This updates a region in a previously created image.
     * If channel names are not provided they default to: R, G, B, A.
     * If channel offsets are not provided they default to: 0, 1, 2, 3.
     * If channel strides are not provided they default to: N, N, N, N where N is the number of channels.
     *
     * Note: Channel offsets and strides are given in number of floats (NOT number of bytes).
     *
     * @param imageName Name of the image.
     * @param x X position of update region in pixels.
     * @param y Y position of update region in pixels.
     * @param width Width of update region in pixels.
     * @param height Height of update region in pixels.
     * @param channelCount Number of channels.
     * @param channelNames Channel names (optional if number of channels <= 4).
     * @param channelOffsets Channel offsets (optional if number of channels <= 4).
     * @param channelStrides Channel strides (optional if number of channels <= 4).
     * @param imageData Image data as array of floats.
     * @param imageDataLength Number of elements (floats) in image data.
     * @param grabFocus Bring the tev window to the front.
     * @return Error::Ok if successful.
     */
    Error updateImage(const char *imageName, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                      uint32_t channelCount, const char **channelNames, uint64_t *channelOffsets,
                      uint64_t *channelStrides, const float *imageData, size_t imageDataLength, bool grabFocus = true);

    /**
     * @brief Create a new image.
     *
     * This is a convenience helper to create a new image and immediately update it.
     * Image data is expected to be tightly packed.
     *
     * @param imageName Name of the image.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param channelCount Number of channels (must be <= 4).
     * @param imageData Image data as array of floats.
     * @param imageDataLength Number of elements (floats) in image data.
     * @param grabFocus Bring the tev window to the front.
     * @return Error::Ok if successful.
     */
    Error createImage(const char *imageName, uint32_t width, uint32_t height, uint32_t channelCount,
                      const float *imageData, size_t imageDataLength, bool grabFocus = true);

    /**
     * @brief Draw vector graphics on top of an image.
     * 
     * @param imageName Name of the image.
     * @param commands Array of commands.
     * @param commandCount Number of elements in array of commands.
     * @param append Append to existing vector graphics.
     * @param grabFocus 
     * @param grabFocus Bring the tev window to the front.
     * @return Error::Ok if successful.
     */
    Error vectorGraphics(const char *imageName, const VgCommand *commands, size_t commandCount, bool append = true,
                         bool grabFocus = true);

    /// Return the last error.
    Error lastError() const;

    /// Return the last error as a string.
    const char *lastErrorString() const;

private:
    class Impl;
    Impl *mImpl;
};

} // namespace tevclient
