#include "tevclient.h"

#include <stdexcept>
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

    class IpcPacket
    {
    public:
        enum EType : char
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

        IpcPacket() = default;
        IpcPacket(const char *data, size_t length)
        {
            if (length <= 0)
            {
                throw std::runtime_error{"Cannot construct an IPC packet from no data."};
            }
            mPayload.assign(data, data + length);
        }

        const char *data() const
        {
            return mPayload.data();
        }

        size_t size() const
        {
            return mPayload.size();
        }

        EType type() const
        {
            // The first 4 bytes encode the message size.
            return (EType)mPayload[4];
        }

        void setOpenImage(const std::string &imagePath, const std::string &channelSelector, bool grabFocus)
        {
            OStream payload{mPayload};
            payload << EType::OpenImageV2;
            payload << grabFocus;
            payload << imagePath;
            payload << channelSelector;
        }

        void setReloadImage(const std::string &imageName, bool grabFocus)
        {
            OStream payload{mPayload};
            payload << EType::ReloadImage;
            payload << grabFocus;
            payload << imageName;
        }

        void setCloseImage(const std::string &imageName)
        {
            OStream payload{mPayload};
            payload << EType::CloseImage;
            payload << imageName;
        }

        void setUpdateImage(const std::string &imageName, bool grabFocus, const std::vector<ChannelDesc> &channelDescs, int32_t x, int32_t y, int32_t width, int32_t height, const std::vector<float> &stridedImageData)
        {
            if (channelDescs.empty())
            {
                throw std::runtime_error{"UpdateImage IPC packet must have a non-zero channel count."};
            }

            int32_t nChannels = (int32_t)channelDescs.size();
            std::vector<std::string> channelNames(nChannels);
            std::vector<int64_t> channelOffsets(nChannels);
            std::vector<int64_t> channelStrides(nChannels);

            for (int32_t i = 0; i < nChannels; ++i)
            {
                channelNames[i] = channelDescs[i].name;
                channelOffsets[i] = channelDescs[i].offset;
                channelStrides[i] = channelDescs[i].stride;
            }

            OStream payload{mPayload};
            payload << EType::UpdateImageV3;
            payload << grabFocus;
            payload << imageName;
            payload << nChannels;
            payload << channelNames;
            payload << x << y << width << height;
            payload << channelOffsets;
            payload << channelStrides;

            size_t nPixels = width * height;

            size_t stridedImageDataSize = 0;
            for (int32_t c = 0; c < nChannels; ++c)
            {
                stridedImageDataSize = std::max(stridedImageDataSize, (size_t)(channelOffsets[c] + (nPixels - 1) * channelStrides[c] + 1));
            }

            if (stridedImageData.size() != stridedImageDataSize)
            {
                throw std::runtime_error{"UpdateImage IPC packet's data size does not match specified dimensions, offset, and stride. (Expected: " + std::to_string(stridedImageDataSize) + ")"};
            }

            payload << stridedImageData;
        }

        void setUpdateImage(const std::string &imageName, bool grabFocus, const std::vector<ChannelDesc> &channelDescs, int32_t x, int32_t y, int32_t width, int32_t height, const float *imageData, size_t imageDataLength)
        {
            if (channelDescs.empty())
            {
                throw std::runtime_error{"UpdateImage IPC packet must have a non-zero channel count."};
            }

            int32_t nChannels = (int32_t)channelDescs.size();
            std::vector<std::string> channelNames(nChannels);
            std::vector<int64_t> channelOffsets(nChannels);
            std::vector<int64_t> channelStrides(nChannels);

            for (int32_t i = 0; i < nChannels; ++i)
            {
                channelNames[i] = channelDescs[i].name;
                channelOffsets[i] = channelDescs[i].offset;
                channelStrides[i] = channelDescs[i].stride;
            }

            OStream payload{mPayload};
            payload << EType::UpdateImageV3;
            payload << grabFocus;
            payload << imageName;
            payload << nChannels;
            payload << channelNames;
            payload << x << y << width << height;
            payload << channelOffsets;
            payload << channelStrides;

            size_t nPixels = width * height;

            size_t stridedImageDataSize = 0;
            for (int32_t c = 0; c < nChannels; ++c)
            {
                stridedImageDataSize = std::max(stridedImageDataSize, (size_t)(channelOffsets[c] + (nPixels - 1) * channelStrides[c] + 1));
            }

            if (imageDataLength != stridedImageDataSize)
            {
                throw std::runtime_error{"UpdateImage IPC packet's data size does not match specified dimensions, offset, and stride. (Expected: " + std::to_string(stridedImageDataSize) + ")"};
            }

            payload.write(imageData, imageDataLength * sizeof(float));
        }

        void setCreateImage(const std::string &imageName, bool grabFocus, int32_t width, int32_t height, int32_t nChannels, const std::vector<std::string> &channelNames)
        {
            if ((int32_t)channelNames.size() != nChannels)
            {
                throw std::runtime_error{"CreateImage IPC packet's channel names size does not match number of channels."};
            }

            OStream payload{mPayload};
            payload << EType::CreateImage;
            payload << grabFocus;
            payload << imageName;
            payload << width << height;
            payload << nChannels;
            payload << channelNames;
        }

        void setVectorGraphics(const std::string &imageName, bool grabFocus, bool append, const std::vector<VgCommand> &commands)
        {
            OStream payload{mPayload};
            payload << EType::VectorGraphics;
            payload << grabFocus;
            payload << imageName;
            payload << append;
            payload << (int32_t)commands.size();
            for (const auto &command : commands)
            {
                payload << command.type;
                payload << command.data;
            }
        }

    private:
        std::vector<char> mPayload;

        class OStream
        {
        public:
            OStream(std::vector<char> &data) : mData{data}
            {
                // Reserve space for an integer denoting the size of the packet.
                *this << (uint32_t)0;
            }

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

            OStream &operator<<(bool var)
            {
                if (mData.size() < mIdx + 1)
                {
                    mData.resize(mIdx + 1);
                }

                mData[mIdx] = var ? 1 : 0;
                ++mIdx;
                updateSize();
                return *this;
            }

            template <typename T>
            OStream &operator<<(T var)
            {
                if (mData.size() < mIdx + sizeof(T))
                {
                    mData.resize(mIdx + sizeof(T));
                }

                *(T *)&mData[mIdx] = var;
                mIdx += sizeof(T);
                updateSize();
                return *this;
            }

            void write(const void *data, size_t len)
            {
                mData.resize(mIdx + len);
                std::memcpy(&mData[mIdx], data, len);
                mIdx += len;
                updateSize();
            }

        private:
            void updateSize()
            {
                *((uint32_t *)mData.data()) = (uint32_t)mIdx;
            }

            std::vector<char> &mData;
            size_t mIdx = 0;
        };
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

        Error send(const IpcPacket &message)
        {
            if (!connected())
            {
                Error error = connect();
                if (error != Error::Ok)
                {
                    return error;
                }
            }

            int bytesSent = ::send(mSocketFd, message.data(), (int)message.size(), 0 /* flags */);
            if (bytesSent != int(message.size()))
            {
                return setLastError(Error::SocketError, "socket send() failed: " + errorString(lastSocketError()));
            }

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

    Error Client::openImage(const std::string &imagePath, const std::string &channelSelector, bool grabFocus)
    {
        IpcPacket packet;
        packet.setOpenImage(imagePath, channelSelector, grabFocus);
        return mImpl->send(packet);
    }

    Error Client::reloadImage(const std::string &imageName, bool grabFocus)
    {
        IpcPacket packet;
        packet.setReloadImage(imageName, grabFocus);
        return mImpl->send(packet);
    }

    Error Client::closeImage(const std::string &imageName)
    {
        IpcPacket packet;
        packet.setCloseImage(imageName);
        return mImpl->send(packet);
    }

    Error Client::createImage(const std::string &imageName, uint32_t width, uint32_t height, const std::vector<std::string> &channelNames, bool grabFocus)
    {
        if (width == 0 || height == 0)
        {
            return mImpl->setLastError(Error::ImageError, "Image width and height must be greater than 0.");
        }
        if (channelNames.empty())
        {
            return mImpl->setLastError(Error::ImageError, "Image must have at least one channel.");
        }
        IpcPacket packet;
        packet.setCreateImage(imageName, grabFocus, width, height, (int32_t)channelNames.size(), channelNames);
        return mImpl->send(packet);
    }

    Error Client::createImage(const std::string &imageName, uint32_t width, uint32_t height, uint32_t channelCount, bool grabFocus)
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
            return mImpl->setLastError(Error::ImageError, "Image must have between 1 and 4 channels.");
        }
        return createImage(imageName, width, height, channelNames, grabFocus);
    }

    Error Client::createImage(const std::string &imageName, uint32_t width, uint32_t height, uint32_t channelCount, const float *imageData, size_t imageDataLength, bool grabFocus)
    {
        Error error = createImage(imageName, width, height, channelCount, grabFocus);
        if (error != Error::Ok)
        {
            return error;
        }
        return updateImage(imageName, width, height, channelCount, imageData, imageDataLength, grabFocus);
    }

    Error Client::updateImage(const std::string &imageName, int32_t x, int32_t y, int32_t width, int32_t height, const std::vector<ChannelDesc> &channelDescs, const float *imageData, size_t imageDataLength, bool grabFocus)
    {
        IpcPacket packet;
        packet.setUpdateImage(imageName, grabFocus, channelDescs, x, y, width, height, imageData, imageDataLength);
        return mImpl->send(packet);
    }

    Error Client::updateImage(const std::string &imageName, uint32_t width, uint32_t height, uint32_t channelCount, const float *imageData, size_t imageDataLength, bool grabFocus)
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
            return mImpl->setLastError(Error::ImageError, "Image must have between 1 and 4 channels.");
        }
        return updateImage(imageName, 0, 0, width, height, channelDescs, imageData, imageDataLength);
    }

    Error Client::lastError() const { return mImpl->lastError(); }

    const std::string &Client::lastErrorString() const
    {
        return mImpl->lastErrorString();
    }

} // namespace tevclient