// This file was developed by Simon Kallweit and Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include "tevclient.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Ws2tcpip.h>
#include <winsock2.h>
#undef NOMINMAX
using socket_t = SOCKET;
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (-1)
#endif

#define RETURN_IF_FAILED(call)                                                                                         \
    {                                                                                                                  \
        tevclient::Error error = call;                                                                                 \
        if (error != tevclient::Error::Ok)                                                                             \
            return error;                                                                                              \
    }

namespace tevclient
{

inline std::string errorString(int error)
{
#ifdef _WIN32
    char *str = NULL;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                  error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&str, 0, NULL);

    std::string result{str};
    LocalFree(str);
#else
    std::string result{strerror(error)};
#endif
    auto pos = result.find_last_not_of("/r/n");
    if (pos != std::string::npos)
        result = result.substr(0, pos + 1);

    result += " (" + std::to_string(error) + ")";
    return result;
}

enum SocketError : int
{
#ifdef _WIN32
    Again = EAGAIN,
    ConnRefused = WSAECONNREFUSED,
    WouldBlock = WSAEWOULDBLOCK,
#else
    Again = EAGAIN,
    ConnRefused = ECONNREFUSED,
    WouldBlock = EWOULDBLOCK,
#endif
};

inline int closeSocket(socket_t socket)
{
#ifdef _WIN32
    return closesocket(socket);
#else
    return close(socket);
#endif
}

inline int lastSocketError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

enum EPacketType : char
{
    OpenImage = 0,
    ReloadImage = 1,
    CloseImage = 2,
    UpdateImage = 3,
    CreateImage = 4,
    UpdateImageV2 = 5, // Adds multi-channel support
    UpdateImageV3 = 6, // Adds custom striding/offset support
    OpenImageV2 = 7,   // Explicit separation of image name and channel selector
    VectorGraphics = 8,
};

class OStream
{
public:
    template <typename T> OStream &operator<<(const std::vector<T> &var)
    {
        for (auto &&elem : var)
        {
            *this << elem;
        }
        return *this;
    }

    OStream &operator<<(const std::string &str)
    {
        for (auto c : str)
        {
            *this << c;
        }
        *this << '\0';
        return *this;
    }

    OStream &operator<<(const char *str)
    {
        for (const char *c = str; *c != '\0'; ++c)
        {
            *this << *c;
        }
        *this << '\0';
        return *this;
    }

    OStream &operator<<(bool var)
    {
        *this << char(var ? 0 : 1);
        return *this;
    }

    template <typename T> OStream &operator<<(T var)
    {
        size_t pos = mData.size();
        mData.resize(mData.size() + sizeof(T));
        *(T *)&mData[pos] = var;
        return *this;
    }

    const void *data() const
    {
        return mData.data();
    }
    size_t size() const
    {
        return mData.size();
    }

private:
    std::vector<uint8_t> mData;
};

static std::atomic<uint32_t> sInstanceCount{0};
static std::string sInitError;
#ifdef _WIN32
static std::mutex sInstanceMutex;
#endif

static bool internalInitialize(const char **error = nullptr)
{
    if (sInstanceCount++ == 0)
    {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(sInstanceMutex);
        WSADATA wsaData;
        int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (wsaStartupResult != NO_ERROR)
        {
            sInitError = errorString(wsaStartupResult);
            if (error)
                *error = sInitError.c_str();
            return false;
        }
#else
        // We don't care about getting a SIGPIPE if the display server goes away...
        signal(SIGPIPE, SIG_IGN);
#endif
    }
    return true;
}

static void internalShutdown()
{
    if (sInstanceCount-- == 1)
    {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(sInstanceMutex);
        WSACleanup();
#endif
    }
}

class Client::Impl
{
public:
    Impl(const char *hostname, uint16_t port) : mHostname{hostname}, mPort{port}
    {
        const char *error;
        if (!internalInitialize(&error))
        {
            setLastError(Error::SocketError, std::string("Failed to initialize: ") + error);
        }
    }

    ~Impl()
    {
        disconnect();
        internalShutdown();
    }

    const std::string &getHostname() const
    {
        return mHostname;
    }

    uint16_t getPort() const
    {
        return mPort;
    }

    bool isConnected() const
    {
        return mSocketFd != INVALID_SOCKET;
    }

    Error connect()
    {
        if (isConnected())
        {
            return Error::Ok;
        }

        struct addrinfo hints = {}, *addrinfo;
        hints.ai_family = PF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        int err = getaddrinfo(mHostname.c_str(), std::to_string(mPort).c_str(), &hints, &addrinfo);
        if (err != 0)
        {
            return setLastError(Error::SocketError, "getaddrinfo() failed: " + std::string(gai_strerror(err)));
        }

        setLastError(Error::Ok);

        mSocketFd = INVALID_SOCKET;
        for (struct addrinfo *ptr = addrinfo; ptr; ptr = ptr->ai_next)
        {
            mSocketFd = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (mSocketFd == INVALID_SOCKET)
            {
                setLastError(Error::SocketError, "socket() failed: " + errorString(lastSocketError()));
                continue;
            }

            if (::connect(mSocketFd, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR)
            {
                setLastError(Error::SocketError, "connect() failed: " + errorString(lastSocketError()));
                closeSocket(mSocketFd);
                mSocketFd = INVALID_SOCKET;
                continue;
            }

            break; // success
        }

        freeaddrinfo(addrinfo);

        return mLastError;
    }

    Error disconnect()
    {
        if (isConnected())
        {
            if (closeSocket(mSocketFd) == SOCKET_ERROR)
            {
                return setLastError(Error::SocketError, "Error closing socket: " + errorString(lastSocketError()));
            }
        }

        return Error::Ok;
    }

    Error send(const void *data, size_t len)
    {
        if (!isConnected())
        {
            return setLastError(Error::NotConnected, "Not connected");
        }

        size_t bytesSent =
            ::send(mSocketFd, reinterpret_cast<const char *>(data), static_cast<int>(len), 0 /* flags */);
        if (bytesSent != len)
        {

            return setLastError(Error::SocketError, "socket send() failed: " + errorString(lastSocketError()));
        }

        return Error::Ok;
    }

    Error sendMessage(const OStream &header, const void *extraData = nullptr, size_t extraLen = 0)
    {
        uint32_t totalLen = static_cast<uint32_t>(4 + header.size() + extraLen);
        RETURN_IF_FAILED(send(&totalLen, 4));
        RETURN_IF_FAILED(send(header.data(), header.size()));
        if (extraData)
        {
            RETURN_IF_FAILED(send(extraData, extraLen));
        }
        return Error::Ok;
    }

    Error setLastError(Error error, std::string errorString = "")
    {
        mLastError = error;
        mLastErrorString = std::move(errorString);
        return error;
    }

    Error lastError() const
    {
        return mLastError;
    }

    const std::string &lastErrorString() const
    {
        return mLastErrorString;
    }

private:
    std::string mHostname;
    uint16_t mPort;
    socket_t mSocketFd{INVALID_SOCKET};

    Error mLastError{Error::Ok};
    std::string mLastErrorString;
};

bool initialize(const char **error)
{
    return internalInitialize(error);
}

void shutdown()
{
    internalShutdown();
}

Client::Client(const char *hostname, uint16_t port)
{
    mImpl = new Client::Impl(hostname, port);
}

Client::~Client()
{
    delete mImpl;
}

const char *Client::getHostname() const
{
    return mImpl->getHostname().c_str();
}

uint16_t Client::getPort() const
{
    return mImpl->getPort();
}

Error Client::connect()
{
    return mImpl->connect();
}

Error Client::disconnect()
{
    return mImpl->disconnect();
}

bool Client::isConnected() const
{
    return mImpl->isConnected();
}

Error Client::openImage(const char *imagePath, const char *channelSelector, bool grabFocus)
{
    OStream msg;
    msg << EPacketType::OpenImageV2;
    msg << grabFocus;
    msg << imagePath;
    msg << channelSelector;
    return mImpl->sendMessage(msg);
}

Error Client::reloadImage(const char *imageName, bool grabFocus)
{
    OStream msg;
    msg << EPacketType::ReloadImage;
    msg << grabFocus;
    msg << imageName;
    return mImpl->sendMessage(msg);
}

Error Client::closeImage(const char *imageName)
{
    OStream msg;
    msg << EPacketType::CloseImage;
    msg << imageName;
    return mImpl->sendMessage(msg);
}

Error Client::createImage(const char *imageName, uint32_t width, uint32_t height, uint32_t channelCount,
                          const char **channelNames, bool grabFocus)
{
    if (width == 0 || height == 0)
    {
        return mImpl->setLastError(Error::ArgumentError, "Image width and height must be greater than 0.");
    }
    if (channelCount == 0)
    {
        return mImpl->setLastError(Error::ArgumentError, "Image must have at least one channel.");
    }
    if (channelCount > 4 && !channelNames)
    {
        return mImpl->setLastError(Error::ArgumentError,
                                   "Channel names cannot be inferred for images with more than 4 channels.");
    }

    const char *defaultNames[] = {"R", "G", "B", "A"};

    if (!channelNames)
    {
        channelNames = defaultNames;
    }

    OStream msg;
    msg << EPacketType::CreateImage;
    msg << grabFocus;
    msg << imageName;
    msg << width << height;
    msg << channelCount;
    for (uint32_t i = 0; i < channelCount; ++i)
    {
        msg << channelNames[i];
    }
    return mImpl->sendMessage(msg);
}

Error Client::updateImage(const char *imageName, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                          uint32_t channelCount, const char **channelNames, uint64_t *channelOffsets,
                          uint64_t *channelStrides, const float *imageData, size_t imageDataCount, bool grabFocus)
{
    if (channelCount == 0)
    {
        return mImpl->setLastError(Error::ArgumentError, "Image must have at least one channel.");
    }
    if (channelCount > 4 && (!channelNames || !channelOffsets || !channelStrides))
    {
        return mImpl->setLastError(
            Error::ArgumentError,
            "Channel names/offsets/strides cannot be inferred for images with more than 4 channels.");
    }

    const char *defaultNames[] = {"R", "G", "B", "A"};
    uint64_t defaultOffsets[] = {0, 1, 2, 3};
    uint64_t defaultStrides[] = {channelCount, channelCount, channelCount, channelCount};

    if (!channelNames)
    {
        channelNames = defaultNames;
    }
    if (!channelOffsets)
    {
        channelOffsets = defaultOffsets;
    }
    if (!channelStrides)
    {
        channelStrides = defaultStrides;
    }

    OStream msg;
    msg << EPacketType::UpdateImageV3;
    msg << grabFocus;
    msg << imageName;
    msg << channelCount;
    for (uint32_t i = 0; i < channelCount; ++i)
    {
        msg << channelNames[i];
    }
    msg << x << y << width << height;
    for (uint32_t i = 0; i < channelCount; ++i)
    {
        msg << channelOffsets[i];
    }
    for (uint32_t i = 0; i < channelCount; ++i)
    {
        msg << channelStrides[i];
    }

    size_t pixelCount = width * height;

    size_t stridedImageDataCount = 0;
    for (uint32_t i = 0; i < channelCount; ++i)
    {
        stridedImageDataCount =
            std::max(stridedImageDataCount, (size_t)(channelOffsets[i] + (pixelCount - 1) * channelStrides[i] + 1));
    }

    if (imageDataCount != stridedImageDataCount)
    {
        return mImpl->setLastError(
            Error::ArgumentError,
            "Image data size does not match specified dimensions, offset, and stride. (Expected: " +
                std::to_string(stridedImageDataCount) + ")");
    }

    return mImpl->sendMessage(msg, imageData, imageDataCount * sizeof(float));
}

Error Client::createImage(const char *imageName, uint32_t width, uint32_t height, uint32_t channelCount,
                          const float *imageData, size_t imageDataCount, bool grabFocus)
{
    RETURN_IF_FAILED(createImage(imageName, width, height, channelCount, nullptr, grabFocus));
    return updateImage(imageName, 0, 0, width, height, channelCount, nullptr, nullptr, nullptr, imageData,
                       imageDataCount, grabFocus);
}

Error Client::vectorGraphics(const char *imageName, const VgCommand *commands, size_t commandCount, bool append,
                             bool grabFocus)
{
    OStream msg;
    msg << EPacketType::VectorGraphics;
    msg << grabFocus;
    msg << imageName;
    msg << append;
    msg << static_cast<uint32_t>(commandCount);
    for (size_t i = 0; i < commandCount; ++i)
    {
        msg << commands[i].type;
        for (size_t j = 0; j < commands[i].dataCount; ++j)
            msg << commands[i].data[j];
    }
    return mImpl->sendMessage(msg);
}

Error Client::lastError() const
{
    return mImpl->lastError();
}

const char *Client::lastErrorString() const
{
    return mImpl->lastErrorString().c_str();
}

} // namespace tevclient
