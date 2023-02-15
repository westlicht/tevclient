#include "tevclient.h"

#include <string>
#include <algorithm>
#include <atomic>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <Ws2tcpip.h>
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

#define RETURN_IF_FAILED(call) \
    if (tevclient::Error error = call; error != tevclient::Error::Ok) \
        return error;

namespace tevclient
{
    inline std::string errorString(int error)
    {
#ifdef _WIN32
        char *str = NULL;
        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&str, 0, NULL);

        std::string result{str};
        LocalFree(str);
#else
        std::string result{strerror(error)};
#endif
        auto pos = result.find_last_not_of("/r/n");
        if (pos != std::string::npos)
        {
            result = result.substr(0, pos - 1);
        }
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
        template <typename T>
        OStream &operator<<(const std::vector<T> &var)
        {
            for (auto &&elem : var)
            {
                *this << elem;
            }
            return *this;
        }

        OStream &operator<<(const std::string &var)
        {
            for (auto &&character : var)
            {
                *this << character;
            }
            *this << '\0';
            return *this;
        }

        OStream &operator<<(std::string_view var)
        {
            for (auto &&character : var)
            {
                *this << character;
            }
            *this << '\0';
            return *this;
        }

        OStream &operator<<(bool var)
        {
            *this << char(var ? 0 : 1);
            return *this;
        }

        template <typename T>
        OStream &operator<<(T var)
        {
            size_t pos = mData.size();
            mData.resize(mData.size() + sizeof(T));
            *(T *)&mData[pos] = var;
            return *this;
        }

        const void *data() const { return mData.data(); }
        size_t size() const { return mData.size(); }

    private:
        std::vector<uint8_t> mData;
    };

    static std::atomic<uint32_t> sInstanceCount{0};

    class Client::Impl
    {
    public:
        Impl(const char *hostname, uint16_t port) : mHostname{hostname}, mPort{std::to_string(port)}
        {
            if (sInstanceCount++ == 0)
            {
#ifdef _WIN32
                WSADATA wsaData;
                int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
                if (wsaStartupResult != NO_ERROR)
                {
                    setLastError(Error::SocketError, "Could not initialize WSA: " + errorString(wsaStartupResult));
                }
#else
                // We don't care about getting a SIGPIPE if the display server goes away...
                signal(SIGPIPE, SIG_IGN);
#endif
            }
        }

        ~Impl()
        {
            disconnect();

            if (sInstanceCount-- == 1)
            {
#ifdef _WIN32
                WSACleanup();
#endif
            }
        }

        Error send(const void *data, size_t len)
        {
            if (!connected())
            {
                Error error = connect();
                if (error != Error::Ok)
                {
                    return error;
                }
            }

            size_t bytesSent = ::send(mSocketFd, reinterpret_cast<const char *>(data), static_cast<int>(len), 0 /* flags */);
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
                RETURN_IF_FAILED(send(extraData, extraLen));
            return Error::Ok;
        }

        Error setLastError(Error error, std::string errorString = "")
        {
            mLastError = error;
            mLastErrorString = std::move(errorString);
            return error;
        }

        Error lastError() const { return mLastError; }
        const std::string &lastErrorString() const { return mLastErrorString; }

    private:
        bool connected() const { return mSocketFd != INVALID_SOCKET; }

        Error connect()
        {
            if (connected())
            {
                return Error::Ok;
            }

            struct addrinfo hints = {}, *addrinfo;
            hints.ai_family = PF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            int err = getaddrinfo(mHostname.c_str(), mPort.c_str(), &hints, &addrinfo);
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
            if (connected())
            {
                if (closeSocket(mSocketFd) == SOCKET_ERROR)
                {
                    return setLastError(Error::SocketError, "Error closing socket: " + errorString(lastSocketError()));
                }
            }

            return Error::Ok;
        }

        std::string mHostname;
        std::string mPort;
        socket_t mSocketFd{INVALID_SOCKET};

        Error mLastError{Error::Ok};
        std::string mLastErrorString;
    };

    Client::Client(const char *hostname, uint16_t port)
    {
        mImpl = new Client::Impl(hostname, port);
    }

    Client::~Client()
    {
        delete mImpl;
    }

    Error Client::openImage(std::string_view imagePath, std::string_view channelSelector, bool grabFocus)
    {
        OStream msg;
        msg << EPacketType::OpenImageV2;
        msg << grabFocus;
        msg << imagePath;
        msg << channelSelector;
        return mImpl->sendMessage(msg);
    }

    Error Client::reloadImage(std::string_view imageName, bool grabFocus)
    {
        OStream msg;
        msg << EPacketType::ReloadImage;
        msg << grabFocus;
        msg << imageName;
        return mImpl->sendMessage(msg);
    }

    Error Client::closeImage(std::string_view imageName)
    {
        OStream msg;
        msg << EPacketType::CloseImage;
        msg << imageName;
        return mImpl->sendMessage(msg);
    }

    Error Client::createImage(std::string_view imageName, uint32_t width, uint32_t height, const std::vector<std::string> &channelNames, bool grabFocus)
    {
        if (width == 0 || height == 0)
        {
            return mImpl->setLastError(Error::ArgumentError, "Image width and height must be greater than 0.");
        }
        if (channelNames.empty())
        {
            return mImpl->setLastError(Error::ArgumentError, "Image must have at least one channel.");
        }

        OStream msg;
        msg << EPacketType::CreateImage;
        msg << grabFocus;
        msg << imageName;
        msg << width << height;
        msg << static_cast<uint32_t>(channelNames.size());
        msg << channelNames;
        return mImpl->sendMessage(msg);
    }

    Error Client::createImage(std::string_view imageName, uint32_t width, uint32_t height, uint32_t channelCount, bool grabFocus)
    {
        std::vector<std::string> channelNames;
        switch (channelCount)
        {
        case 1:
            channelNames = {"R"};
            break;
        case 2:
            channelNames = {"R", "G"};
            break;
        case 3:
            channelNames = {"R", "G", "B"};
            break;
        case 4:
            channelNames = {"R", "G", "B", "A"};
            break;
        default:
            return mImpl->setLastError(Error::ArgumentError, "Image must have between 1 and 4 channels.");
        }
        return createImage(imageName, width, height, channelNames, grabFocus);
    }

    Error Client::createImage(std::string_view imageName, uint32_t width, uint32_t height, uint32_t channelCount, const float *imageData, size_t imageDataLength, bool grabFocus)
    {
        Error error = createImage(imageName, width, height, channelCount, grabFocus);
        if (error != Error::Ok)
        {
            return error;
        }
        return updateImage(imageName, width, height, channelCount, imageData, imageDataLength, grabFocus);
    }

    Error Client::updateImage(std::string_view imageName, int32_t x, int32_t y, int32_t width, int32_t height, const std::vector<ChannelDesc> &channelDescs, const float *imageData, size_t imageDataLength, bool grabFocus)
    {
        if (channelDescs.empty())
        {
            return mImpl->setLastError(Error::ArgumentError, "Image must have at least one channel.");
        }

        uint32_t channelCount = static_cast<uint32_t>(channelDescs.size());
        std::vector<std::string> channelNames(channelCount);
        std::vector<int64_t> channelOffsets(channelCount);
        std::vector<int64_t> channelStrides(channelCount);

        for (uint32_t i = 0; i < channelCount; ++i)
        {
            channelNames[i] = channelDescs[i].name;
            channelOffsets[i] = channelDescs[i].offset;
            channelStrides[i] = channelDescs[i].stride;
        }

        OStream msg;
        msg << EPacketType::UpdateImageV3;
        msg << grabFocus;
        msg << imageName;
        msg << channelCount;
        msg << channelNames;
        msg << x << y << width << height;
        msg << channelOffsets;
        msg << channelStrides;

        size_t pixelCount = width * height;

        size_t stridedImageDataSize = 0;
        for (uint32_t c = 0; c < channelCount; ++c)
        {
            stridedImageDataSize = std::max(stridedImageDataSize, (size_t)(channelOffsets[c] + (pixelCount - 1) * channelStrides[c] + 1));
        }

        if (imageDataLength != stridedImageDataSize)
        {
            return mImpl->setLastError(Error::ArgumentError, "Image data size does not match specified dimensions, offset, and stride. (Expected: " + std::to_string(stridedImageDataSize) + ")");
        }

        return mImpl->sendMessage(msg, imageData, imageDataLength * sizeof(float));
    }

    Error Client::updateImage(std::string_view imageName, uint32_t width, uint32_t height, uint32_t channelCount, const float *imageData, size_t imageDataLength, bool grabFocus)
    {
        std::vector<ChannelDesc> channelDescs;
        switch (channelCount)
        {
        case 1:
            channelDescs = {{"R", 0, 1}};
            break;
        case 2:
            channelDescs = {{"R", 0, 2}, {"G", 1, 2}};
            break;
        case 3:
            channelDescs = {{"R", 0, 3}, {"G", 1, 3}, {"B", 2, 3}};
            break;
        case 4:
            channelDescs = {{"R", 0, 4}, {"G", 1, 4}, {"B", 2, 4}, {"A", 3, 4}};
            break;
        default:
            return mImpl->setLastError(Error::ArgumentError, "Image must have between 1 and 4 channels.");
        }
        return updateImage(imageName, 0, 0, width, height, channelDescs, imageData, imageDataLength);
    }

    Error Client::vectorGraphics(std::string_view imageName, const VgCommand *commands, size_t commandCount, bool append, bool grabFocus)
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
            msg << commands[i].data;
        }
        return mImpl->sendMessage(msg);
    }

    Error Client::vectorGraphics(std::string_view imageName, const std::vector<VgCommand>& commands, bool append, bool grabFocus)
    {
        return vectorGraphics(imageName, commands.data(), commands.size(), append, grabFocus);
    }

#ifdef TEVCLIENT_CPP20
    Error Client::vectorGraphics(std::string_view imageName, std::span<VgCommand> commands, bool append, bool grabFocus)
    {
        return vectorGraphics(imageName, commands.data(), commands.size(), append, grabFocus);
    }
#endif

    Error Client::lastError() const { return mImpl->lastError(); }

    const std::string &Client::lastErrorString() const
    {
        return mImpl->lastErrorString();
    }

} // namespace tevclient