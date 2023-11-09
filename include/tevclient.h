// This file was developed by Simon Kallweit and Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#pragma once

#include <cstddef>
#include <cstdint>

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

    VgCommand() = default;

    VgCommand(EType type) : type(type)
    {
    }

    template <size_t N> VgCommand(EType type, const float (&data)[N]) : type(type), dataCount(N)
    {
        static_assert(N <= MaxPayload, "Payload too large");
        for (size_t i = 0; i < N; ++i)
            this->data[i] = data[i];
    }

    static VgCommand save()
    {
        return {EType::Save};
    }

    static VgCommand restore()
    {
        return {EType::Restore};
    }

    static VgCommand fillColor(const Color &c)
    {
        float data[4] = {c.r, c.g, c.b, c.a};
        return {EType::FillColor, data};
    }

    static VgCommand fill()
    {
        return {EType::Fill};
    }

    static VgCommand strokeColor(const Color &c)
    {
        float data[4] = {c.r, c.g, c.b, c.a};
        return {EType::StrokeColor, data};
    }

    static VgCommand stroke()
    {
        return {EType::Stroke};
    }

    static VgCommand beginPath()
    {
        return {EType::BeginPath};
    }

    static VgCommand closePath()
    {
        return {EType::ClosePath};
    }

    static VgCommand pathWinding(EWinding winding)
    {
        float data[1] = {(float)(int)winding};
        return {EType::PathWinding, data};
    }

    static VgCommand moveTo(const Pos &p)
    {
        float data[2] = {p.x, p.y};
        return {EType::MoveTo, data};
    }

    static VgCommand lineTo(const Pos &p)
    {
        float data[2] = {p.x, p.y};
        return {EType::LineTo, data};
    }

    static VgCommand arcTo(const Pos &p1, const Pos &p2, float radius)
    {
        float data[5] = {p1.x, p1.y, p2.x, p2.y, radius};
        return {EType::ArcTo, data};
    }

    static VgCommand arc(const Pos &center, float radius, float angle_begin, float angle_end, EWinding winding)
    {
        float data[6] = {center.x, center.y, radius, angle_begin, angle_end, (float)(int)winding};
        return {EType::Arc, data};
    }

    static VgCommand bezierTo(const Pos &c1, const Pos &c2, const Pos &p)
    {
        float data[6] = {c1.x, c1.y, c2.x, c2.y, p.x, p.y};
        return {EType::BezierTo, data};
    }

    static VgCommand circle(const Pos &center, float radius)
    {
        float data[3] = {center.x, center.y, radius};
        return {EType::Circle, data};
    }

    static VgCommand ellipse(const Pos &center, const Size &radius)
    {
        float data[4] = {center.x, center.y, radius.width, radius.height};
        return {EType::Ellipse, data};
    }

    static VgCommand quadTo(const Pos &c, const Pos &p)
    {
        float data[4] = {c.x, c.y, p.x, p.y};
        return {EType::QuadTo, data};
    }

    static VgCommand rect(const Pos &p, const Size &size)
    {
        float data[4] = {p.x, p.y, size.width, size.height};
        return {EType::Rect, data};
    }

    static VgCommand roundedRect(const Pos &p, const Size &size, float radius)
    {
        float data[5] = {p.x, p.y, size.width, size.height, radius};
        return {EType::RoundedRect, data};
    }

    static VgCommand roundedRectVarying(const Pos &p, const Size &size, float radiusTopLeft, float radiusTopRight,
                                        float radiusBottomRight, float radiusBottomLeft)
    {
        float data[8] = {
            p.x, p.y, size.width, size.height, radiusTopLeft, radiusTopRight, radiusBottomRight, radiusBottomLeft,
        };
        return {EType::RoundedRectVarying, data};
    }

    static constexpr size_t MaxPayload = 8;

    EType type{EType::Invalid};
    uint8_t dataCount{0};
    float data[MaxPayload];
};

/// Error codes.
enum class Error
{
    Ok,
    NotConnected,
    SocketError,
    ArgumentError,
};

/**
 * @brief Class for remotely controlling the tev image viewer.
 *
 * Communication is unidirectional (client -> tev server).
 * The API is not thread-safe and all calls are blocking.
 *
 * Note that a connection is not automatically established.
 * Before sending any commands, the connection needs to be
 * opened using connect().
 */
class Client
{
public:
    /**
     * @brief Constructor.
     *
     * Note that the connection is not established automatically.
     * You need to call connect() to open the connection.
     *
     * @param hostname Hostname
     * @param port Port
     */
    Client(const char *hostname = "127.0.0.1", uint16_t port = 14158);

    ~Client();

    Client(const Client &) = delete;
    Client(Client &&) = delete;
    Client &operator=(const Client &) = delete;
    Client &operator=(Client &&) = delete;

    /**
     * @brief Connect to tev.
     *
     * @return Error::Ok if succesful.
     */
    Error connect();

    /**
     * @brief Disconnect from tev.
     *
     * @return Error::Ok if successful.
     */
    Error disconnect();

    /// Return true if connected.
    bool isConnected() const;

    /**
     * @brief Open an image from a file.
     *
     * @param imagePath Path to image.
     * @param channelSelector Channel to select (optional).
     * @param grabFocus Select the image in tev.
     * @return Error::Ok if successful.
     */
    Error openImage(const char *imagePath, const char *channelSelector = "", bool grabFocus = true);

    /**
     * @brief Reload an image.
     *
     * @param imageName Name of the image.
     * @param grabFocus Select the image in tev.
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
     * @param grabFocus Select the image in tev.
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
     * @param imageDataCount Number of elements (floats) in image data.
     * @param grabFocus Select the image in tev.
     * @return Error::Ok if successful.
     */
    Error updateImage(const char *imageName, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                      uint32_t channelCount, const char **channelNames, uint64_t *channelOffsets,
                      uint64_t *channelStrides, const float *imageData, size_t imageDataCount, bool grabFocus = true);

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
     * @param imageDataCount Number of elements (floats) in image data.
     * @param grabFocus Select the image in tev.
     * @return Error::Ok if successful.
     */
    Error createImage(const char *imageName, uint32_t width, uint32_t height, uint32_t channelCount,
                      const float *imageData, size_t imageDataCount, bool grabFocus = true);

    /**
     * @brief Draw vector graphics on top of an image.
     *
     * @param imageName Name of the image.
     * @param commands Array of commands.
     * @param commandCount Number of elements in array of commands.
     * @param append Append to existing vector graphics.
     * @param grabFocus
     * @param grabFocus Select the image in tev.
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
