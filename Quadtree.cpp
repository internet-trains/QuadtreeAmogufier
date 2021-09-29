#include "Quadtree.h"

#include <algorithm>

namespace {
std::vector<Image> BuildLeafCache(const Image &leafImage, Rect bounds, std::size_t maxDepth) {
    std::vector<Image> ret;
    for (std::size_t i = 0; i < maxDepth + 1; ++i) {
        ret.emplace_back(leafImage.resizeFastNew(bounds.w, bounds.h));
        bounds.w /= 2;
        bounds.h /= 2;
    }
    return ret;
}

double GetAspect(const Rect &bounds) {
    auto [a, b] = std::minmax(bounds.w, bounds.h);
    return static_cast<double>(b) / a;
}

int GetBestSplitCount(Rect bounds) {
    double bestAR = GetAspect(bounds);
    int bestCount = static_cast<int>(std::floor(bestAR));
    if (bestAR * bestAR > bestCount * (bestCount + 1))
        ++bestCount;
    return bestCount;
}
} // namespace

Quadtree::Quadtree(const Image &leafImage, Rect bounds, std::size_t maxDepth)
    : mRootBounds(bounds), mLeafCache(BuildLeafCache(leafImage, bounds, maxDepth)) {}

Image Quadtree::ProcessFrame(const Image &frame) const {
    Image ret = frame;
    Rect bounds = mRootBounds;
    bool horizontal = bounds.w > bounds.h;
    int &size = horizontal ? bounds.w : bounds.h;
    int &pos = horizontal ? bounds.x : bounds.y;
    int splitCount = GetBestSplitCount(bounds);
    int step = std::max(bounds.w, bounds.h) / splitCount;
    int errStep = std::max(bounds.w, bounds.h) - step * splitCount;
    int err = errStep;
    size = step;

    for (int i = 0; i < splitCount; ++i) {
        ProcessFrame(ret, bounds);

        pos += size;
        err += errStep;
        if (err >= splitCount) {
            size = step + 1;
            err -= splitCount;
        } else {
            size = step;
        }
    }
    return ret;
}

struct ColorVisitor {
    RgbColor operator()(uint8_t gray) { return {gray, gray, gray}; }
    RgbColor operator()(RgbColor color) { return color; }
};

void Quadtree::ProcessFrame(Image &frame, Rect bounds, std::size_t depth) const {

    auto [subdivide, colorVariant] = CheckSubdivision(frame, bounds);
    auto color = std::visit(ColorVisitor{}, colorVariant);
    if (depth == mLeafCache.size() || !subdivide) {
        frame.overlay(mLeafCache.back().colorMaskNew(color.r, color.g, color.b), bounds.x, bounds.y);
    } else {
        int ulX = bounds.x;
        int ulY = bounds.y;
        int mmX = bounds.x + bounds.w / 2;
        int mmY = bounds.y + bounds.h / 2;
        int brX = bounds.x + bounds.w;
        int brY = bounds.y + bounds.h;
        ProcessFrame(frame, Rect{ulX, ulY, mmX - ulX, mmY - ulY}, depth + 1);
        ProcessFrame(frame, Rect{mmX, ulY, brX - mmX, mmY - ulY}, depth + 1);
        ProcessFrame(frame, Rect{ulX, mmY, mmX - ulX, brY - mmY}, depth + 1);
        ProcessFrame(frame, Rect{mmX, mmY, brX - mmX, brY - mmY}, depth + 1);
    }
}

namespace {
template <class T> T bound(double x) {
    if (x < std::numeric_limits<T>::min())
        return std::numeric_limits<T>::min();
    if (x > std::numeric_limits<T>::max())
        return std::numeric_limits<T>::max();
    return static_cast<T>(std::round(x));
}
} // namespace

QuadtreeBW::QuadtreeBW(const Image &leafImage, Rect bounds, const BWParameters &params, std::size_t maxDepth)
    : Quadtree(leafImage, bounds, maxDepth), mParams(params) {}

std::tuple<bool, std::variant<byte, RgbColor>> QuadtreeBW::CheckSubdivision(const Image &frame, Rect r) const {
    double sum = 0;
    uint8_t min = std::numeric_limits<uint8_t>::max();
    uint8_t max = std::numeric_limits<uint8_t>::min();

    for (int y = r.y; y < r.h + r.y; y++) {
        for (int x = r.x; x < r.w + r.x; x++) {
            uint8_t p = frame.pixel(x, y)[0];
            min = std::min(min, p);
            max = std::max(max, p);
            sum += p;
        }
    }

    return {max - min > mParams.similarityThreshold, bound<uint8_t>(sum / (r.h * r.w))};
}

QuadtreeColor::QuadtreeColor(const Image &leafImage, Rect bounds, const ColorParameters &params, std::size_t maxDepth)
    : Quadtree(leafImage, bounds, maxDepth), mParams(params) {}

std::tuple<bool, std::variant<byte, RgbColor>> QuadtreeColor::CheckSubdivision(const Image &frame, Rect r) const {
    bool subdivide = false;

    auto center = frame.pixel(r.x + r.w / 2, r.y + r.h / 2);
    uint8_t colR = center[0];
    uint8_t colG = center[1];
    uint8_t colB = center[2];
    double sumR = 0;
    double sumG = 0;
    double sumB = 0;

    auto sqr = [](auto x) { return x * x; };

    for (int y = r.y; y < r.h + r.y; y++) {
        for (int x = r.x; x < r.w + r.x; x++) {
            auto pix = frame.pixel(x, y);
            if (sqr(pix[0] - colR) + sqr(pix[1] - colG) + sqr(pix[2] - colB) > mParams.similarityThreshold) {
                subdivide = true;
            }
            sumR += pix[0];
            sumG += pix[1];
            sumB += pix[2];
        }
    }

    sumR /= r.h * r.w;
    sumG /= r.h * r.w;
    sumB /= r.h * r.w;

    return {subdivide, RgbColor{bound<uint8_t>(sumR), bound<uint8_t>(sumG), bound<uint8_t>(sumB)}};
}

std::unique_ptr<Quadtree> CreateQuadtree(const Image &leafImage, Rect bounds, const BWParameters &params,
                                         std::size_t maxDepth) {
    return std::make_unique<QuadtreeBW>(leafImage, bounds, params, maxDepth);
}
std::unique_ptr<Quadtree> CreateQuadtree(const Image &leafImage, Rect bounds, const ColorParameters &params,
                                         std::size_t maxDepth) {
    return std::make_unique<QuadtreeColor>(leafImage, bounds, params, maxDepth);
}