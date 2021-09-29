#ifndef QUADTREE_H
#define QUADTREE_H

#include "Image.h"

#include <cstdint>
#include <memory>
#include <tuple>
#include <variant>
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

class Quadtree {
  public:
    Quadtree(const Image &leafImage, Rect bounds, std::size_t maxDepth = 5);

    virtual ~Quadtree() = default;

    Image ProcessFrame(const Image &frame) const;

    virtual std::tuple<bool, std::variant<byte, RgbColor>> CheckSubdivision(const Image &frame, Rect r) const = 0;

  private:
    void ProcessFrame(Image &dst, Rect bounds, std::size_t depth = 0) const;

    Rect mRootBounds;
    std::vector<Image> mLeafCache;
};

struct BWParameters {
    int similarityThreshold;
};

class QuadtreeBW : public Quadtree {
  public:
    QuadtreeBW(const Image &leafImage, Rect bounds, const BWParameters &params, std::size_t maxDepth = 5);

    ~QuadtreeBW() override = default;

    std::tuple<bool, std::variant<byte, RgbColor>> CheckSubdivision(const Image &frame, Rect r) const override;

  private:
    BWParameters mParams;
};

struct ColorParameters {
    int similarityThreshold;
};

class QuadtreeColor : public Quadtree {
  public:
    QuadtreeColor(const Image &leafImage, Rect bounds, const ColorParameters &params, std::size_t maxDepth = 5);

    ~QuadtreeColor() override = default;

    std::tuple<bool, std::variant<byte, RgbColor>> CheckSubdivision(const Image &frame, Rect r) const override;

  private:
    ColorParameters mParams;
};

std::unique_ptr<Quadtree> CreateQuadtree(const Image &leafImage, Rect bounds, const BWParameters &params,
                                         std::size_t maxDepth = 5);
std::unique_ptr<Quadtree> CreateQuadtree(const Image &leafImage, Rect bounds, const ColorParameters &params,
                                         std::size_t maxDepth = 5);

#endif