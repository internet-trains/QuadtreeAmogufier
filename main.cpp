#include <filesystem>
#include <format>
#include <map>
#include <string>
#include <vector>

#include "Image.h"
#include "Quadtree.h"
#include "lib/cxxopts.hpp"
#include "lib/thread_pool.hpp"

namespace fs = std::filesystem;

void workBW(int i, int index, std::vector<std::map<std::pair<int, int>, Image>> &preloadedResized);
void workCol(int i, int index, std::vector<std::map<std::pair<int, int>, Image>> &preloadedResized);

void createVideoFrames(const cxxopts::ParseResult &options, SubdivisionChecker::Ptr checker);
void createVideoFramesBW(int start, int end, int repeatFrames);
void createVideoFramesCol(int start, int end, int repeatFrames);

void showUsage() {
    std::cout << "Usage: [?.exe] [BW | Col] [Start] [End] (SFRC)\n"
              << "BW | Col:   Black and White or Colored Image Sequence\n"
              << "Start:      Frame to start on (int)\n"
              << "End:        Frame to end on (int)\n"
              << "SFRC:       How often to repeat Sprite frames (optional, default 2)" << std::endl;
}

int main(int argc, char *argv[]) {
    cxxopts::Options optParser("QuadtreeAmoguifier", "Processes a sequence of frames into a quadtree animation.");

    // clang-format off
    optParser.add_options()
        ("a,anim", "Path pattern to the animation frames", cxxopts::value<std::string>()->default_value("res/{}.png"))
        ("r,repeat", "Number of times to repeat each animation frame", cxxopts::value<int>()->default_value("2"))
        ("i,input", "Path pattern to input frames", cxxopts::value<std::string>()->default_value("in/img_{}.png"))
        ("o,output", "Path pattern to output frames", cxxopts::value<std::string>()->default_value("out/img_{}.png"))
        ("m,mode", "Must be either 'bw' or 'color'", cxxopts::value<std::string>()->default_value("color"))
        ("s,similarity", "Similarity threshold for colors (0-255)", cxxopts::value<int>()->default_value("16"))
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

void createVideoFrames(const cxxopts::ParseResult &options, SubdivisionChecker::Ptr checker) {
    auto animPat = options["anim"].as<std::string>();
    auto inputPat = options["input"].as<std::string>();
    auto outputPat = options["output"].as<std::string>();
    fs::path lastPath;

    std::vector<Quadtree> frameTrees;

    std::cout << "Searching for animation frames...\n";
    for (int frame = options["anim-start"].as<int>();; ++frame) {
        fs::path path = fs::absolute(fs::path(std::format(animPat, frame)));
        if (path == lastPath || !fs::exists(path)) {
            break;
        }
        frameTrees.emplace_back(Image{path.string().c_str()}.rescaleLuminance(), options["min-size"].as<int>(),
                                checker);
        lastPath = path;
    }

    if (frameTrees.empty()) {
        std::cerr << "No animation frames found, aborting...\n";
        return;
    }

    std::cout << "Found " << frameTrees.size() << " animation frames.\n";

    thread_pool pool;

    auto getFrameTree = [&, repeat = options["repeat"].as<int>(), repeatIndex = 0, frameIndex = 0]() mutable {
        if (repeatIndex >= repeat) {
            repeatIndex = 0;
            ++frameIndex;
        }
        if (frameIndex >= frameTrees.size()) {
            frameIndex = 0;
        }
        ++repeatIndex;
        return &frameTrees[frameIndex];
    };

    std::cout << "Generating frame tasks...\n";
    int taskCount = 0;
    for (int frameIndex = options["input-start"].as<int>();; ++frameIndex) {
        fs::path inPath(std::format(inputPat, frameIndex));
        fs::path outPath(std::format(outputPat, frameIndex));
        if (inPath == lastPath || !fs::exists(inPath)) {
            break;
        }
        auto tree = getFrameTree();
        pool.submit([=] {
            if (outPath.has_parent_path()) {
                fs::create_directories(outPath.parent_path());
            }
            tree->ProcessFrame(Image(inPath.string().c_str())).save(outPath.string().c_str());
        });
        ++taskCount;
    }

    std::cout << "Processing " << taskCount << " frames...\n";

    pool.wait_for_tasks();
}

void createVideoFramesBW(int start, int end, int repeatFrames) {

    std::vector<std::map<std::pair<int, int>, Image>> preloadedResized;
    int width;
    int height;
    std::string first_name("in/img_" + std::to_string(start) + ".png");
    Image first_frame(first_name.c_str());
    width = first_frame.width();
    height = first_frame.height();

    for (int i = 0; i < 6; i++) {
        std::string amogus_name("res/" + std::to_string(i) + ".png");
        Image amogus(amogus_name.c_str());

        amogus.rescaleLuminance();
        preloadedResized.push_back(amogus.preloadResized(width, height));
    }

    thread_pool pool;

    for (int i = start; i <= end; i++) {
        int index = (i % (6 * repeatFrames)) / repeatFrames;
        pool.submit(workBW, i, index, std::ref(preloadedResized));
    }

    pool.wait_for_tasks();
}

void workBW(int i, int index, std::vector<std::map<std::pair<int, int>, Image>> &preloadedResized) {
    std::string frame_name("in/img_" + std::to_string(i) + ".png");
    Image frame(frame_name.c_str());
    Image frame_done = frame.quadifyFrameBW(preloadedResized.at(index));
    std::string save_loc("out/img_" + std::to_string(i) + ".png");
    frame_done.save(save_loc.c_str());
}

void createVideoFramesCol(int start, int end, int repeatFrames) {

    std::vector<std::map<std::pair<int, int>, Image>> preloadedResized;
    int width;
    int height;
    std::string first_name("in/img_" + std::to_string(start) + ".png");
    Image first_frame(first_name.c_str());
    width = first_frame.width();
    height = first_frame.height();

    for (int i = 0; i < 6; i++) {
        std::string amogus_name("res/" + std::to_string(i) + ".png");
        Image amogus(amogus_name.c_str());

        preloadedResized.push_back(amogus.preloadResized(width, height));
    }

    thread_pool pool;

    for (int i = start; i <= end; i++) {
        int index = (i % (6 * repeatFrames)) / repeatFrames;
        pool.submit(workCol, i, index, std::ref(preloadedResized));
    }

    pool.wait_for_tasks();
}

void workCol(int i, int index, std::vector<std::map<std::pair<int, int>, Image>> &preloadedResized) {
    std::string frame_name("in/img_" + std::to_string(i) + ".png");
    Image frame(frame_name.c_str());
    Image frame_done = frame.quadifyFrameRGB(preloadedResized.at(index));
    std::string save_loc("out/img_" + std::to_string(i) + ".png");
    frame_done.save(save_loc.c_str());
}