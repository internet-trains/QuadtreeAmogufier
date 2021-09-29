#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "Image.h"

#include "lib/stb_image.h"
#include "lib/stb_image_write.h"

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

Image::Image() : w(100), h(100), channels(3) {
    size = w * h * channels;
    data = std::vector<uint8_t>(size);
}

Image::Image(const char *filename) {
    if (read(filename)) {
        size = w * h * channels;
    } else {
        std::cout << "Failed to read " << filename << std::endl;
    }
}

Image::Image(int w, int h, int channels) : w(w), h(h), channels(channels) {
    size = w * h * channels;
    data = std::vector<uint8_t>(size);
}

Image::Image(const Image &img) : w(img.w), h(img.h), channels(img.channels), data(img.data) { size = w * h * channels; }

bool Image::read(const char *filename) {
    uint8_t *temp = stbi_load(filename, &w, &h, &channels, 0);
    size = w * h * channels;
    data.insert(data.end(), &temp[0], &temp[size]);
    stbi_image_free(temp);
    return true;
}

bool Image::write(const char *filename) const {
    int success;
    success = stbi_write_png(filename, w, h, channels, data.data(), w * channels);
    return success != 0;
}

Image &Image::rescaleLuminance(float lo, float hi) {
    float min = std::numeric_limits<float>::max();
    float max = std::numeric_limits<float>::min();
    if (channels < 3) {
        return *this;
    }

    auto getLuminance = [&](uint8_t *p) { return (p[0] * 0.2126f + p[1] * 0.7152f + p[2] * 0.0722f) / 255.f; };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float lum = getLuminance(pixel(x, y));
            min = std::min(lum, min);
            max = std::max(lum, max);
        }
    }

    if (max - min > 0.01f) {
        float ratio = (hi - lo) / (max - min);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
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
    assert(channels == 3);
    for (int i = 0; i < data.size(); i += channels) {
        scale(data[i], r);
        scale(data[i + 1], g);
        scale(data[i + 2], b);
    }
    return *this;
}

Image &Image::colorMask(uint8_t r, uint8_t g, uint8_t b) {
    assert(channels == 3);
    for (int i = 0; i < data.size(); i += channels) {
        scale(data[i], r);
        scale(data[i + 1], g);
        scale(data[i + 2], b);
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

    for (int sy = std::max(0, -y); sy < source.h; sy++) {
        if (sy + y >= h)
            break;
        for (int sx = std::max(0, -x); sx < source.w; sx++) {
            if (sx + x >= w)
                break;

            const uint8_t *srcPixel = source.pixel(sx, sy);
            uint8_t *dstPixel = pixel(sx + x, sy + y);
            float srcAlpha = source.channels < 4 ? 1 : srcPixel[3] / 255.f;
            float dstAlpha = channels < 4 ? 1 : dstPixel[3] / 255.f;

            if (srcAlpha > .99 && dstAlpha > .99) {
                std::copy_n(srcPixel, channels, dstPixel);
            } else {
                float outAlpha = srcAlpha + dstAlpha * (1 - srcAlpha);
                if (outAlpha < .01) {
                    std::fill_n(dstPixel, channels, uint8_t(0));
                } else {
                    for (int channel = 0; channel < channels; channel++) {
                        dstPixel[channel] = bound<uint8_t>((srcPixel[channel] / 255.f * srcAlpha +
                                                            dstPixel[channel] / 255.f * dstAlpha * (1 - srcAlpha)) /
                                                           outAlpha * 255.f);
                    }
                    if (channels > 3)
                        dstPixel[3] = bound<uint8_t>(outAlpha * 255.f);
                }
            }
        }
    }

    return *this;
}

Image Image::resizeFastNew(int rw, int rh) const {
    Image resizedImage(rw, rh, channels);
    double x_ratio = w / (double)rw;
    double y_ratio = h / (double)rh;
    for (int y = 0; y < rh; y++) {
        for (int x = 0; x < rw; x++) {
            int rx = static_cast<int>(x * x_ratio);
            int ry = static_cast<int>(y * y_ratio);
            std::copy_n(pixel(rx, ry), channels, resizedImage.pixel(x, y));
        }
    }
    return resizedImage;
}

Image Image::cropNew(int cx, int cy, int cw, int ch) const {

    Image croppedImage(cw, ch, channels);

    for (int y = 0; y < ch; y++) {
        if (y + cy >= h)
            break;
        for (int x = 0; x < cw; x++) {
            if (x + cx >= w)
                break;
            std::copy_n(pixel(x + cx, y + cy), channels, croppedImage.pixel(x, y));
        }
    }

    return croppedImage;
}

Image Image::quadifyFrameBW(std::map<std::pair<int, int>, Image> &resizedAmogi) const {
    Image frame(w, h, 3);

    subdivideBW(0, 0, w, h, frame, resizedAmogi);

    return frame;
}

// sw: subdivided x | sy subdivided y
// sw: subdivided width | sh subdivided height
void Image::subdivideBW(int sx, int sy, int sw, int sh, Image &frame,
                        std::map<std::pair<int, int>, Image> &resizedAmogi) const {

    auto [subdivide, val] = subdivideCheckBW(sx, sy, sw, sh);

    if (subdivide && sw > 4 && sh > 4) {
        int sw_l = sw / 2;
        int sw_r = (sw + 1) / 2;
        int sh_t = sh / 2;
        int sh_b = (sh + 1) / 2;
        subdivideBW(sx, sy, sw_l, sh_t, frame, resizedAmogi);
        subdivideBW(sx + sw_r, sy, sw_l, sh_t, frame, resizedAmogi);
        subdivideBW(sx, sy + sh_b, sw_l, sh_t, frame, resizedAmogi);
        subdivideBW(sx + sw_r, sy + sh_b, sw_l, sh_t, frame, resizedAmogi);
    } else {
        if (val <= 20)
            return;
        frame.overlay(resizedAmogi[std::make_pair(sw, sh)].colorMaskNew(val / 255.f, val / 255.f, val / 255.f), sx, sy);
    }
}

std::tuple<bool, uint8_t> Image::subdivideCheckBW(int sx, int sy, int sw, int sh) const {
    double sum = 0;
    uint8_t min = std::numeric_limits<uint8_t>::max();
    uint8_t max = std::numeric_limits<uint8_t>::min();

    for (int y = sy; y < sh + sy; y++) {
        for (int x = sx; x < sw + sx; x++) {
            uint8_t p = pixel(x, y)[0];
            min = std::min(min, p);
            max = std::max(max, p);
            sum += p;
        }
    }

    return {max - min > 4, bound<uint8_t>(sum / (sh * sw))};
}

Image Image::quadifyFrameRGB(std::map<std::pair<int, int>, Image> &resizedAmogi) const {
    Image frameRGB(w, h, 3);

    subdivideRGB(0, 0, w, h, frameRGB, resizedAmogi);

    return frameRGB;
}

void Image::subdivideRGB(int sx, int sy, int sw, int sh, Image &frameRGB,
                         std::map<std::pair<int, int>, Image> &resizedAmogi) const {

    auto [subdivide, valR, valG, valB] = subdivideCheckRGB(sx, sy, sw, sh);

    if (subdivide && sw > 4 && sh > 4) {
        int sw_l = sw / 2;
        int sw_r = (sw + 1) / 2;
        int sh_t = sh / 2;
        int sh_b = (sh + 1) / 2;
        subdivideRGB(sx, sy, sw_l, sh_t, frameRGB, resizedAmogi);
        subdivideRGB(sx + sw_r, sy, sw_l, sh_t, frameRGB, resizedAmogi);
        subdivideRGB(sx, sy + sh_b, sw_l, sh_t, frameRGB, resizedAmogi);
        subdivideRGB(sx + sw_r, sy + sh_b, sw_l, sh_t, frameRGB, resizedAmogi);
    } else {
        frameRGB.overlay(resizedAmogi[std::make_pair(sw, sh)].colorMaskNew(valR / 255.f, valG / 255.f, valB / 255.f),
                         sx, sy);
    }
}

std::tuple<bool, uint8_t, uint8_t, uint8_t> Image::subdivideCheckRGB(int sx, int sy, int sw, int sh) const {
    bool subdivide = false;

    auto center = pixel(sx + sw / 2, sy + sh / 2);
    uint8_t colR = center[0];
    uint8_t colG = center[1];
    uint8_t colB = center[2];
    double sumR = 0;
    double sumG = 0;
    double sumB = 0;

    auto sqr = [](auto x) { return x * x; };

    for (int y = sy; y < sh + sy; y++) {
        for (int x = sx; x < sw + sx; x++) {
            auto pix = pixel(x, y);
            if (sqr(pix[0] - colR) + sqr(pix[1] - colG) + sqr(pix[2] - colB) > 64) {
                subdivide = true;
            }
            sumR += pix[0];
            sumG += pix[1];
            sumB += pix[2];
        }
    }

    sumR /= sh * sw;
    sumG /= sh * sw;
    sumB /= sh * sw;

    return {subdivide, bound<uint8_t>(sumR), bound<uint8_t>(sumG), bound<uint8_t>(sumB)};
}

void Image::subdivideValues(int sx, int sy, int sw, int sh, std::map<std::pair<int, int>, Image> &image_map) const {
    if (sw > 4 && sh > 4) {
        subdivideValues(sx, sy, sw / 2, sh / 2, image_map);
    }

    if (image_map.count(std::make_pair(sw, sh)) == 0) {
        image_map[std::make_pair(sw, sh)] = resizeFastNew(sw, sh);
    }
}

std::map<std::pair<int, int>, Image> Image::preloadResized(int sw, int sh) const {
    std::map<std::pair<int, int>, Image> image_map;

    subdivideValues(0, 0, sw, sh, image_map);

    return image_map;
}
