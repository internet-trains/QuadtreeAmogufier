#ifndef IMAGE_H
#define IMAGE_H

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

using byte = uint8_t;

struct RgbColor {
    byte r;
    byte g;
    byte b;
};

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

struct Image {
  private:
    int mWidth;
    int mHeight;
    int mChannels;
    std::vector<byte> mData;

  public:
    Image();
    Image(const char* filename);
    Image(int w, int h, int channels);

    int width() const { return mWidth; }
    int height() const { return mHeight; }
    int channels() const { return mChannels; }

    uint8_t &operator()(int x, int y, int c) { return pixel(x, y)[c]; }
    const uint8_t &operator()(int x, int y, int c) const { return pixel(x, y)[c]; }

    uint8_t *pixel(int x, int y) { return mData.data() + (x + y * mWidth) * mChannels; }
    const uint8_t *pixel(int x, int y) const { return mData.data() + (x + y * mWidth) * mChannels; }

    bool save(const char *filename) const;

    Image &rescaleLuminance(float lo, float hi);
    Image &rescaleLuminance(float hi) { return rescaleLuminance(0, hi); }
    Image &rescaleLuminance() { return rescaleLuminance(0, 1); }

    Image &colorMask(float r, float g, float b);
    Image &colorMask(uint8_t r, uint8_t g, uint8_t b);
    Image colorMaskNew(float r, float g, float b) const;
    Image colorMaskNew(uint8_t r, uint8_t g, uint8_t b) const;
    Image colorMaskNew(const RgbColor &color) const { return colorMaskNew(color.r, color.g, color.b); }
    Image &overlay(const Image &source, int x, int y);
    Image resizeFastNew(int rw, int rh) const;
    Image cropNew(int cx, int cy, int cw, int ch) const;

    Image &rect(Rect r, RgbColor color);
};

#endif
