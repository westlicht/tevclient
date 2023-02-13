#include <iostream>

#include "tevclient.h"

int main()
{
    std::cout << "tevclient example" << std::endl;

    std::string path{"D:/Photos/2023/Haus/DSCF7258.jpg"};
    // std::string path{"DSCF7258.jpg"};

    tevclient::Client client;
    // if (client.openImage(path) != tevclient::Error::Ok)
    // {
    //     std::cout << client.lastErrorString() << std::endl;
    // }

    // if (client.reloadImage(path) != tevclient::Error::Ok)
    // {
    //     std::cout << client.lastErrorString() << std::endl;
    // }

    // if (client.closeImage(path) != tevclient::Error::Ok)
    // {
    //     std::cout << client.lastErrorString() << std::endl;
    // }

    // if (client.createImage("test", 1024, 1024, {"R", "G", "B"}) != tevclient::Error::Ok)
    // {
    //     std::cout << client.lastErrorString() << std::endl;
    // }

    uint32_t width = 4096 * 2;
    uint32_t height = 4096;

    // if (client.createImage("test", width, height, 3) != tevclient::Error::Ok)
    // {
    //     std::cout << client.lastErrorString() << std::endl;
    // }

    std::unique_ptr<float[]> pixels;
    pixels.reset(new float[width * height * 3]);
    float *dst = pixels.get();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            *dst++ = x / float(width);
            *dst++ = y / float(height);
            *dst++ = 0.f;
        }
    }

    if (client.createImage("test2", width, height, 3, pixels.get(), width * height * 3) != tevclient::Error::Ok)
    {
        std::cout << client.lastErrorString() << std::endl;
    }

    return 0;
}
