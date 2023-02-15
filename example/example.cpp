#include <iostream>
#include <memory>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cstdlib>

#include "tevclient.h"

struct Image
{
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    std::vector<float> data;

    static Image checkerboard(uint32_t width, uint32_t height)
    {
        Image img;
        img.width = width;
        img.height = height;
        img.channels = 1;
        img.data.resize(width * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                float c = (((x >> 4) ^ (y >> 4)) & 1) ? 1.f : 0.f;
                img.data[y * width + x] = c;
            }
        }
        return img;
    }
};

void write_pfm(const Image &img, const std::filesystem::path &path)
{
    if (img.channels != 1 && img.channels != 3)
        return;

    FILE *f = fopen(path.string().c_str(), "wb");
    double scale = -1.0;
    fprintf(f, "P%c\n%d %d\n%lf\n", img.channels == 1 ? 'f' : 'F', img.width, img.height, scale);
    fwrite(img.data.data(), sizeof(float), img.data.size(), f);
    fclose(f);   
}

int main()
{
    std::cout << "tevclient example" << std::endl;

    std::filesystem::path cwd = std::filesystem::current_path();
    std::filesystem::path test1 = cwd / "test1.pfm";
    std::filesystem::path test2 = cwd / "test2.pfm";

    write_pfm(Image::checkerboard(128, 128), test1);
    write_pfm(Image::checkerboard(256, 256), test2);
    
    tevclient::Client client;

    auto check = [&client] (tevclient::Error error) {
        if (error != tevclient::Error::Ok)
            std::cout << "Failed: " << client.lastErrorString() << std::endl;
    };

    auto wait = [] () {
        std::this_thread::sleep_for(std::chrono::seconds{1});
    };

    std::cout << "Open image from " << test1 << std::endl;
    check(client.openImage(test1.string()));
    wait();

    std::cout << "Open image from " << test2 << std::endl;
    check(client.openImage(test2.string()));
    wait();

    write_pfm(Image::checkerboard(512, 128), test1);

    std::cout << "Reload image " << test1 << std::endl;
    check(client.reloadImage(test1.string()));
    wait();

    std::cout << "Close image " << test1 << std::endl;
    check(client.closeImage(test1.string()));
    wait();

    uint32_t width = 1024 * 2;
    uint32_t height = 1024;

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

    if (client.createImage("tes32", width, height, 3, pixels.get(), width * height * 3) != tevclient::Error::Ok)
    {
        std::cout << client.lastErrorString() << std::endl;
    }

    return 0;
}
