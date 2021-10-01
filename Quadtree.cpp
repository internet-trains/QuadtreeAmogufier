#include "Quadtree.h"

#include <algorithm>
#include <array>
#include <mutex>

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

std::tuple<int, bool> GetBestSplitCount(const Image &leafImage, Rect bounds) {
    double leafAR = static_cast<double>(leafImage.width()) / leafImage.height();
    double w = static_cast<double>(bounds.w);
    double h = bounds.h * leafAR;
    double bestAR = w > h ? w / h : h / w;
    int bestCount = static_cast<int>(std::floor(bestAR));
    if (bestAR * bestAR > bestCount * (bestCount + 1))
        ++bestCount;
    return {bestCount, w > h};
}
} // namespace

Quadtree::Quadtree(Image leafImage, QuadtreeParameters params, SubdivisionChecker::Ptr checker)
    : mLeafImage(std::move(leafImage)), mParams(std::move(params)), mSubChecker(std::move(checker)) {}

Image Quadtree::ProcessFrame(Image frame) {
    Rect bounds{0, 0, frame.width(), frame.height()};
    auto [splitCount, horizontal] = GetBestSplitCount(mLeafImage, bounds);
    int &size = horizontal ? bounds.w : bounds.h;
    int &pos = horizontal ? bounds.x : bounds.y;
    int step = std::max(bounds.w, bounds.h) / splitCount;
    int errStep = std::max(bounds.w, bounds.h) - step * splitCount;
    int err = errStep;
    size = step;

    for (int i = 0; i < splitCount; ++i) {
        auto result = ProcessFrame(frame, bounds);

        if (result) {
            RenderLeaf(frame, *result);
        }

        pos += size;
        err += errStep;
        if (err >= splitCount) {
            size = step + 1;
            err -= splitCount;
        } else {
            size = step;
        }
    }
    return frame;
}

struct ColorVisitor {
    RgbColor operator()(uint8_t gray) { return {gray, gray, gray}; }
    RgbColor operator()(RgbColor color) { return color; }
};

void Quadtree::RenderLeaf(Image &dst, const LeafData &data) {
    dst.rect(data.bounds, mParams.background)
        .overlay(GetLeaf(data.bounds).colorMaskNew(data.color), data.bounds.x, data.bounds.y);
}

Quadtree::ProcResult Quadtree::ProcessFrame(Image &frame, Rect bounds) {
    if (bounds.w <= mParams.minSize || bounds.h <= mParams.minSize) {
        return LeafData{mSubChecker->GetColor(frame, bounds), bounds};
    }

    int ulX = bounds.x;
    int ulY = bounds.y;
    int mmX = bounds.x + bounds.w / 2;
    int mmY = bounds.y + bounds.h / 2;
    int brX = bounds.x + bounds.w;
    int brY = bounds.y + bounds.h;

    std::array<ProcResult, 4> results = {ProcessFrame(frame, Rect{ulX, ulY, mmX - ulX, mmY - ulY}),
                                         ProcessFrame(frame, Rect{mmX, ulY, brX - mmX, mmY - ulY}),
                                         ProcessFrame(frame, Rect{ulX, mmY, mmX - ulX, brY - mmY}),
                                         ProcessFrame(frame, Rect{mmX, mmY, brX - mmX, brY - mmY})};

    if (std::all_of(results.begin(), results.end(), [](const ProcResult &result) { return result.has_value(); })) {
        auto [doMerge, color] =
            std::apply([&](const auto &...args) { return mSubChecker->Merge((args->color)...); }, results);
        if (doMerge) {
            return LeafData{color, bounds};
        }
    }

    for (const auto &result : results) {
        if (result) {
            RenderLeaf(frame, *result);
        }
    }
    return std::nullopt;
}

const Image &Quadtree::GetLeaf(Rect bounds) {
    auto size = std::make_pair(bounds.w, bounds.h);

    {
        std::shared_lock lock(*mCacheMutex);
        auto it = mLeafCache.find(size);
        if (it != mLeafCache.end())
            return it->second;
    }

    {
        // Possible for multiple threads to get here just not simultaneously; ultimately this would be fine but would do
        // extra work by doing redundant resizing which gets discarded, so we're going to do a find again.
        // An alternative is to use call_once, but that would require an extra map of once_flag.
        std::unique_lock lock(*mCacheMutex);
        auto it = mLeafCache.find(size);
        if (it == mLeafCache.end()) {
            it = mLeafCache.emplace(size, mLeafImage.resizeFastNew(size.first, size.second)).first;
        }
        return it->second;
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

class SubdivisionBW : public SubdivisionChecker {
  public:
    SubdivisionBW(const BWParameters &params) : mParams(params) {}

    ~SubdivisionBW() override = default;

    RgbColor GetColor(const Image &frame, Rect r) const override {
        double sum = 0;
        for (int y = r.y; y < r.h + r.y; ++y) {
            for (int x = r.x; x < r.w + r.x; ++x) {
                sum += frame.pixel(x, y)[0];
            }
        }

        byte val = bound<byte>(sum / (r.h * r.w));

        return {val, val, val};
    }

    std::tuple<bool, RgbColor> Merge(const RgbColor &tl, const RgbColor &tr, const RgbColor &bl,
                                     const RgbColor &br) const override {
        auto [m, n] = std::minmax({tl.r, tr.r, bl.r, br.r});

        byte r = static_cast<byte>((tl.r + tr.r + bl.r + br.r) / 4);
        byte g = static_cast<byte>((tl.g + tr.g + bl.g + br.g) / 4);
        byte b = static_cast<byte>((tl.b + tr.b + bl.b + br.b) / 4);

        return std::make_pair(n - m < mParams.similarityThreshold, RgbColor{r, g, b});
    }

  private:
    BWParameters mParams;
};

class SubdivisionColor : public SubdivisionChecker {
  public:
    SubdivisionColor(const ColorParameters &params) : mParams(params) {}

    ~SubdivisionColor() override = default;

    RgbColor GetColor(const Image &frame, Rect r) const override {
        double sumR = 0;
        double sumG = 0;
        double sumB = 0;
        for (int y = r.y; y < r.h + r.y; ++y) {
            for (int x = r.x; x < r.w + r.x; ++x) {
                auto pix = frame.pixel(x, y);
                sumR += pix[0];
                sumG += pix[1];
                sumB += pix[2];
            }
        }

        sumR /= r.w * r.h;
        sumG /= r.w * r.h;
        sumB /= r.w * r.h;

        return {bound<byte>(sumR), bound<byte>(sumG), bound<byte>(sumB)};
    }

    std::tuple<bool, RgbColor> Merge(const RgbColor &tl, const RgbColor &tr, const RgbColor &bl,
                                     const RgbColor &br) const override {
        auto sqr = [](auto x) { return x * x; };
        auto thresh2 = 3 * sqr(mParams.similarityThreshold);

        auto diff2 = [&](const RgbColor &x, const RgbColor &y) {
            return sqr(x.r - y.r) + sqr(x.g - y.g) + sqr(x.b - y.b);
        };

        int maxDiff = 0;
        for (const auto &d :
             {diff2(tl, tr), diff2(tl, bl), diff2(tl, br), diff2(tr, bl), diff2(tr, br), diff2(bl, br)}) {
            maxDiff = std::max(d, maxDiff);
        }

        byte r = static_cast<byte>((tl.r + tr.r + bl.r + br.r) / 4);
        byte g = static_cast<byte>((tl.g + tr.g + bl.g + br.g) / 4);
        byte b = static_cast<byte>((tl.b + tr.b + bl.b + br.b) / 4);

        return std::make_pair(maxDiff < thresh2, RgbColor{r, g, b});
    }

  private:
    ColorParameters mParams;
};

} // namespace

SubdivisionChecker::Ptr CreateSubdivisionChecker(const BWParameters &params) {
    return std::make_shared<SubdivisionBW>(params);
}
SubdivisionChecker::Ptr CreateSubdivisionChecker(const ColorParameters &params) {
    return std::make_shared<SubdivisionColor>(params);
}