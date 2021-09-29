#ifndef QUADTREE_H
#define QUADTREE_H

#include "Image.h"

#include <cstdint>
#include <map>
#include <memory>
#include <shared_mutex>
#include <tuple>
#include <variant>

struct BWParameters {
    int similarityThreshold;
};

struct ColorParameters {
    int similarityThreshold;
};

struct QuadtreeParameters {
    int minSize;
    RgbColor background;
};

class SubdivisionChecker {
  public:
    using Ptr = std::shared_ptr<SubdivisionChecker>;

    virtual ~SubdivisionChecker() = default;
    virtual std::tuple<bool, std::variant<byte, RgbColor>> Check(const Image &frame, Rect r) const = 0;
};

class Quadtree {
  public:
    Quadtree(Image leafImage, QuadtreeParameters params, SubdivisionChecker::Ptr checker);

    Image ProcessFrame(Image frame);

  private:
    void ProcessFrame(Image &dst, Rect bounds);

    const Image &GetLeaf(Rect bounds);

    std::unique_ptr<std::shared_mutex> mCacheMutex = std::make_unique<std::shared_mutex>();

    std::map<std::pair<int, int>, Image> mLeafCache;
    Image mLeafImage;
    QuadtreeParameters mParams;
    SubdivisionChecker::Ptr mSubChecker;
};

SubdivisionChecker::Ptr CreateSubdivisionChecker(const BWParameters &params);
SubdivisionChecker::Ptr CreateSubdivisionChecker(const ColorParameters &params);

#endif