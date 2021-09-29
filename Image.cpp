#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "Image.h"

#include "lib/stb_image.h"
#include "lib/stb_image_write.h"

#include <iostream>

namespace {
template <class T> T scale(T &val, double s) {
    T old = val;
    val = static_cast<T>(old * s);
    return old;
}

template <class T> T scale(T &val, uint8_t s) {
    T old = val;
    val = old * s / 255;
    return old;
}

template <class T> T bound(double x) {
    if (x < std::numeric_limits<T>::min())
        return std::numeric_limits<T>::min();
    if (x > std::numeric_limits<T>::max())
        return std::numeric_limits<T>::max();
    return static_cast<T>(std::round(x));
}
} // namespace

Image::Image() : mWidth(100), mHeight(100), mChannels(3), mData(mWidth * mHeight * mChannels) {}

Image::Image(const char *filename) {
    uint8_t *temp = stbi_load(filename, &mWidth, &mHeight, &mChannels, 0);
    mData.insert(mData.end(), &temp[0], &temp[mWidth * mHeight * mChannels]);
    stbi_image_free(temp);
}

Image::Image(int mWidth, int mHeight, int mChannels)
    : mWidth(mWidth), mHeight(mHeight), mChannels(mChannels), mData(mWidth * mHeight * mChannels) {}

bool Image::save(const char *filename) const {
    int success;
    success = stbi_write_png(filename, mWidth, mHeight, mChannels, mData.data(), mWidth * mChannels);
    return success != 0;
}

Image &Image::rescaleLuminance(float lo, float hi) {
    float min = std::numeric_limits<float>::max();
    float max = std::numeric_limits<float>::min();
    if (mChannels < 3) {
        return *this;
    }

    auto getLuminance = [&](uint8_t *p) { return (p[0] * 0.2126f + p[1] * 0.7152f + p[2] * 0.0722f) / 255.f; };

    for (int y = 0; y < mHeight; ++y) {
        for (int x = 0; x < mWidth; ++x) {
            float lum = getLuminance(pixel(x, y));
            min = std::min(lum, min);
            max = std::max(lum, max);
        }
    }

    if (max - min > 0.01f) {
        float ratio = (hi - lo) / (max - min);
        for (int y = 0; y < mHeight; ++y) {
            for (int x = 0; x < mWidth; ++x) {
                float l = getLuminance(pixel(x, y));
                float s = (l - min) * ratio;
                if (l < 0.01f) {
                    pixel(x, y)[0] = 0;
                    pixel(x, y)[1] = 0;
                    pixel(x, y)[2] = 0;
                } else {
                    scale(pixel(x, y)[0], s / l);
                    scale(pixel(x, y)[1], s / l);
                    scale(pixel(x, y)[2], s / l);
                }

                pixel(x, y)[0] += bound<uint8_t>(255 * lo);
                pixel(x, y)[1] += bound<uint8_t>(255 * lo);
                pixel(x, y)[2] += bound<uint8_t>(255 * lo);
            }
        }
    }

    return *this;
}

Image &Image::colorMask(float r, float g, float b) {
    assert(mChannels == 3);
    for (int i = 0; i < mData.size(); i += mChannels) {
        scale(mData[i], r);
        scale(mData[i + 1], g);
        scale(mData[i + 2], b);
    }
    return *this;
}

Image &Image::colorMask(uint8_t r, uint8_t g, uint8_t b) {
    assert(mChannels == 3);
    for (int i = 0; i < mData.size(); i += mChannels) {
        scale(mData[i], r);
        scale(mData[i + 1], g);
        scale(mData[i + 2], b);
    }
    return *this;
}

Image Image::colorMaskNew(float r, float g, float b) const {
    Image new_version = *this;
    new_version.colorMask(r, g, b);
    return new_version;
}

Image Image::colorMaskNew(uint8_t r, uint8_t g, uint8_t b) const {
    Image new_version = *this;
    new_version.colorMask(r, g, b);
    return new_version;
}

Image &Image::overlay(const Image &source, int x, int y) {
    for (int sy = std::max(0, -y); sy < source.mHeight; sy++) {
        if (sy + y >= mHeight)
            break;
        for (int sx = std::max(0, -x); sx < source.mWidth; sx++) {
            if (sx + x >= mWidth)
                break;

            const uint8_t *srcPixel = source.pixel(sx, sy);
            uint8_t *dstPixel = pixel(sx + x, sy + y);
            float srcAlpha = source.mChannels < 4 ? 1 : srcPixel[3] / 255.f;
            float dstAlpha = mChannels < 4 ? 1 : dstPixel[3] / 255.f;

            if (srcAlpha > .99 && dstAlpha > .99) {
                std::copy_n(srcPixel, mChannels, dstPixel);
            } else {
                float outAlpha = srcAlpha + dstAlpha * (1 - srcAlpha);
                if (outAlpha < .01) {
                    std::fill_n(dstPixel, mChannels, uint8_t(0));
                } else {
                    for (int channel = 0; channel < mChannels; channel++) {
                        dstPixel[channel] = bound<uint8_t>((srcPixel[channel] / 255.f * srcAlpha +
                                                            dstPixel[channel] / 255.f * dstAlpha * (1 - srcAlpha)) /
                                                           outAlpha * 255.f);
                    }
                    if (mChannels > 3)
                        dstPixel[3] = bound<uint8_t>(outAlpha * 255.f);
                }
            }
        }
    }

    return *this;
}

Image &Image::rect(Rect r, RgbColor color) {
    uint8_t colors[4] = {color.r, color.g, color.b, 255};
    for (int y = std::max(0, r.y); y < std::min(r.y + r.h, mHeight); y++) {
        for (int x = std::max(0, r.x); x < std::min(r.x + r.w, mWidth); x++) {
            std::copy_n(colors, mChannels, pixel(x, y));
        }
    }

    return *this;
}

Image Image::resizeFastNew(int rw, int rh) const {
    Image resizedImage(rw, rh, mChannels);
    double x_ratio = mWidth / (double)rw;
    double y_ratio = mHeight / (double)rh;
    for (int y = 0; y < rh; y++) {
        for (int x = 0; x < rw; x++) {
            int rx = static_cast<int>(x * x_ratio);
            int ry = static_cast<int>(y * y_ratio);
            std::copy_n(pixel(rx, ry), mChannels, resizedImage.pixel(x, y));
        }
    }
    return resizedImage;
}

Image Image::cropNew(int cx, int cy, int cw, int ch) const {

    Image croppedImage(cw, ch, mChannels);

    for (int y = 0; y < ch; y++) {
        if (y + cy >= mHeight)
            break;
        for (int x = 0; x < cw; x++) {
            if (x + cx >= mWidth)
                break;
            std::copy_n(pixel(x + cx, y + cy), mChannels, croppedImage.pixel(x, y));
        }
    }

    return croppedImage;
}
