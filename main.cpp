#include <condition_variable>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "Image.h"
#include "Quadtree.h"
#include "lib/cxxopts.hpp"
#include "lib/thread_pool.hpp"

namespace fs = std::filesystem;

void createVideoFrames(const cxxopts::ParseResult &options, SubdivisionChecker::Ptr checker);

int main(int argc, char *argv[]) {
    cxxopts::Options optParser("QuadtreeAmoguifier", "Processes a sequence of frames into a quadtree animation.");
    std::string defaultThreads = std::to_string(std::thread::hardware_concurrency());
    // clang-format off
    optParser.add_options()
        ("a,anim", "Path pattern to the animation frames", cxxopts::value<std::string>()->default_value("res/{}.png"))
        ("r,repeat", "Number of times to repeat each animation frame", cxxopts::value<int>()->default_value("2"))
        ("i,input", "Path pattern to input frames", cxxopts::value<std::string>()->default_value("in/img_{}.png"))
        ("o,output", "Path pattern to output frames", cxxopts::value<std::string>()->default_value("out/img_{}.png"))
        ("m,mode", "Must be either 'bw' or 'color'", cxxopts::value<std::string>()->default_value("color"))
        ("s,similarity", "Similarity threshold (0-255)", cxxopts::value<int>()->default_value("8"))
        ("b,background", "Background color", cxxopts::value<std::string>()->default_value("#000000"))
        ("p,out-resolution", "Output vertical resolution", cxxopts::value<int>()->implicit_value("480"))
        ("t,threads", "Number of threads to use", cxxopts::value<int>()->default_value(defaultThreads))
        ("min-size", "Minimum leaf dimension", cxxopts::value<int>()->default_value("8"))
        ("anim-start", "First frame index of animation frames", cxxopts::value<int>()->default_value("0"))
        ("input-start", "First frame index of input frames", cxxopts::value<int>()->default_value("1"))
        ("h,help", "Print usage");
    // clang-format on

    auto options = optParser.parse(argc, argv);

    if (options.count("help")) {
        std::cout << optParser.help() << std::endl;
        return 0;
    }

    auto mode = options["mode"].as<std::string>();
    std::transform(mode.begin(), mode.end(), mode.begin(), [](char c) { return (char)std::tolower(c); });
    SubdivisionChecker::Ptr checker;
    if (mode == "bw") {
        BWParameters params{options["similarity"].as<int>()};
        checker = CreateSubdivisionChecker(params);
    } else if (mode == "color") {
        ColorParameters params{options["similarity"].as<int>()};
        checker = CreateSubdivisionChecker(params);
    } else {
        std::cerr << "Unknown mode: '" << mode << "'\n";
        std::cout << optParser.help() << std::endl;
        return 0;
    }

    try {
        createVideoFrames(options, std::move(checker));
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}

int parseHexDigit(char digit) {
    char d = static_cast<char>(std::tolower(digit));
    if ('a' <= d && d <= 'f') {
        return 10 + d - 'a';
    }
    if ('0' <= d && d <= '9') {
        return d - '0';
    }
    return 0;
}

RgbColor parseColor(const std::string &str) {
    if (str.empty()) {
        return {0, 0, 0};
    }

    int o = str[0] == '#' ? 1 : 0;
    if (str.size() == 3 + o) {
        return RgbColor{byte(parseHexDigit(str[o]) * 16), byte(parseHexDigit(str[o + 1]) * 16),
                        byte(parseHexDigit(str[o + 2]) * 16)};
    }

    if (str.size() < 6 + o) {
        return {0, 0, 0};
    }

    int r = parseHexDigit(str[o]) * 16 + parseHexDigit(str[o + 1]);
    int g = parseHexDigit(str[o + 2]) * 16 + parseHexDigit(str[o + 3]);
    int b = parseHexDigit(str[o + 4]) * 16 + parseHexDigit(str[o + 5]);
    return RgbColor{byte(r), byte(g), byte(b)};
}

class ProgressBar {
  public:
    explicit ProgressBar(int total, int size) : mTotal(total), mSize(size) {}

    void UpdateProgress(std::ostream &os, int progress) {
        if (progress <= mProgress) {
            return;
        }

        if (mProgress == 0) {
            WriteProgressBar(os);
        }

        while (mPrinted * mTotal < progress * mSize) {
            os << '*';
            ++mPrinted;
        }

        mProgress = progress;
        if (mProgress >= mTotal) {
            os << " Done\n";
        }
    }

  private:
    void WriteProgressBar(std::ostream &os) const {
        constexpr int tickCount = 4;
        int tick = mSize;
        os << '|';
        for (int i = 1; i < mSize - 1; ++i) {
            if (i * tickCount >= tick) {
                os << '|';
                tick += mSize;
            } else {
                os << '-';
            }
        }
        os << "| 100%\n";
    }

    int mProgress = 0;
    int mPrinted = 0;
    int mTotal;
    int mSize;
};

class QuadtreeBuilder {
  public:
    QuadtreeBuilder(fs::path path, QuadtreeParameters params, SubdivisionChecker::Ptr checker)
        : mPath(std::move(path)), mParams(params), mChecker(std::move(checker)) {}

    void AddUse() {
        std::unique_lock lock(*mMutexPtr);
        ++mUseCount;
        ReleaseImpl();
    }

    Quadtree &GetTree() {
        std::unique_lock lock(*mMutexPtr);
        if (!mQuadtree) {
            mQuadtree = Quadtree{Image{mPath.string().c_str()}.rescaleLuminance(), mParams, mChecker};
            mUses = 0;
        }
        return *mQuadtree;
    }

    void Release() {
        std::unique_lock lock(*mMutexPtr);
        ++mUses;
        ReleaseImpl();
    }

    void AllowRelease() {
        std::unique_lock lock(*mMutexPtr);
        mAllowRelease = true;
        ReleaseImpl();
    }

  private:
    void ReleaseImpl() {
        if (mAllowRelease && mUses >= mUseCount) {
            mQuadtree = std::nullopt;
        }
    }

    std::unique_ptr<std::mutex> mMutexPtr = std::make_unique<std::mutex>();
    std::optional<Quadtree> mQuadtree;
    fs::path mPath;
    QuadtreeParameters mParams;
    SubdivisionChecker::Ptr mChecker;

    int mUseCount = 0;
    int mUses = 0;
    bool mAllowRelease = false;
};

void createVideoFrames(const cxxopts::ParseResult &options, SubdivisionChecker::Ptr checker) {
    auto animPat = options["anim"].as<std::string>();
    auto inputPat = options["input"].as<std::string>();
    auto outputPat = options["output"].as<std::string>();
    fs::path lastPath;

    std::vector<QuadtreeBuilder> frameBuilders;

    std::cout << "Searching for animation frames...\n";
    for (int frame = options["anim-start"].as<int>();; ++frame) {
        fs::path path = fs::absolute(fs::path(std::format(animPat, frame)));
        if (path == lastPath || !fs::exists(path)) {
            break;
        }
        QuadtreeParameters params;
        params.minSize = options["min-size"].as<int>();
        params.background = parseColor(options["background"].as<std::string>());
        frameBuilders.emplace_back(path, std::move(params), checker);
        lastPath = std::move(path);
    }

    if (frameBuilders.empty()) {
        std::cerr << "No animation frames found, aborting...\n";
        return;
    }

    std::cout << "Found " << frameBuilders.size() << " animation frames.\n";

    thread_pool pool(static_cast<std::uint_fast32_t>(options["threads"].as<int>()));

    auto getFrameBuilder = [&, repeat = options["repeat"].as<int>(), repeatIndex = 0, frameIndex = 0]() mutable {
        if (repeatIndex >= repeat) {
            repeatIndex = 0;
            ++frameIndex;
        }
        if (frameIndex >= frameBuilders.size()) {
            frameIndex = 0;
        }
        ++repeatIndex;
        auto &ret = frameBuilders[frameIndex];
        ret.AddUse();
        return &ret;
    };

    std::cout << "Generating frame tasks...\n";
    int taskCount = 0;

    std::atomic_int tasksDone = 0;
    std::condition_variable cv;
    std::mutex cvMutex;

    std::optional<int> outRes;
    if (options.count("out-resolution")) {
        outRes = options["out-resolution"].as<int>();
    }

    for (int frameIndex = options["input-start"].as<int>();; ++frameIndex) {
        fs::path inPath(std::format(inputPat, frameIndex));
        fs::path outPath(std::format(outputPat, frameIndex));
        if (inPath == lastPath || !fs::exists(inPath)) {
            break;
        }
        pool.submit([inPath = std::move(inPath), outPath = std::move(outPath), builder = getFrameBuilder(), outRes, &cv,
                     &cvMutex, &tasksDone] {
            if (outPath.has_parent_path()) {
                fs::create_directories(outPath.parent_path());
            }
            try {
                auto &tree = builder->GetTree();
                auto frame = tree.ProcessFrame(Image(inPath.string().c_str()));
                builder->Release();

                if (outRes) {
                    int h = *outRes;
                    if (h % 2) {
                        ++h;
                    }
                    int w = frame.width() * h / frame.height();
                    if (w % 2) {
                        ++w;
                    }
                    frame = frame.resizeFastNew(w, h);
                }
                frame.save(outPath.string().c_str());
            } catch (std::exception &e) {
                std::cerr << "Process for " << inPath << " threw an exception: " << e.what() << "\n";
            }
            {
                std::unique_lock lock(cvMutex);
                ++tasksDone;
            }
            cv.notify_one();
        });
        ++taskCount;
    }

    for (auto &builder : frameBuilders) {
        builder.AllowRelease();
    }

    std::cout << "Processing " << taskCount << " frames...\n";

    ProgressBar pb(taskCount, 80);
    while (tasksDone < taskCount) {
        pb.UpdateProgress(std::cout, tasksDone);
        {
            std::unique_lock lock(cvMutex);
            cv.wait(lock);
        }
    }
    pb.UpdateProgress(std::cout, tasksDone);

    pool.wait_for_tasks();
}