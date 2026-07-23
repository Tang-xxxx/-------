/**
 * ============================================================
 * Badge Relief Height Field V4.1 — C++ Implementation
 * ============================================================
 *
 * 对照 test.hdev 逐行翻译为 OpenCV C++
 *
 * 核心算法：
 *   全图多尺度浮雕 Height Map
 *   不进行前景分割 / 不使用 Mask / 不使用 Distance Transform
 *   直接从整张图片生成高度场
 *
 *   Image
 *      │
 *      ▼
 *    Gray
 *      │
 *      ├───────────────┐
 *      ▼               ▼
 *   Large Blur       Small Blur
 *      │               │
 *      │               ▼
 *      │          High Detail
 *      │
 *      ▼
 *   Base Height
 *      │
 *      │
 *      └───────┬───────┐
 *              │       │
 *              ▼       ▼
 *          Mid Detail  High Detail
 *              │       │
 *              └───┬───┘
 *                  ▼
 *            Height Fusion
 *                  │
 *                  ▼
 *            Final Height
 *
 * ============================================================
 */

#include <opencv2/opencv.hpp>
#include <tiffio.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// ============================================================
// 配置参数
// ============================================================
struct Config {
    // 输入图像路径
    std::string imagePath = "badge.png";

    // ----------------------------------------------------------
    // 大尺度 — 越大越强调整体形状
    // ----------------------------------------------------------
    int largeScale = 101;

    // ----------------------------------------------------------
    // 中尺度 — 控制：雕刻结构、动物主体、面部、翅膀、大纹理
    // ----------------------------------------------------------
    int middleScale = 31;

    // ----------------------------------------------------------
    // 小尺度 — 控制：毛发、羽毛、细纹、雕刻细节
    // ----------------------------------------------------------
    int smallScale = 7;

    // ----------------------------------------------------------
    // 高度权重
    // ----------------------------------------------------------
    double baseWeight   = 1.0;
    double middleWeight = 0.8;
    double highWeight   = 0.25;

    // 输出路径
    std::string output8bit  = "Badge_Height_V4_1_8bit.tif";

    // 测试：缩小到一半加速
    bool testHalfScale = true;
};

// ============================================================
// 工具：打印图像最小/最大/范围 (对应 min_max_gray)
// ============================================================
void printRange(const cv::Mat& img, const std::string& name) {
    double minVal, maxVal;
    cv::minMaxLoc(img, &minVal, &maxVal);
    std::cout << "[" << name << "]  Min: " << minVal
              << "  Max: " << maxVal
              << "  Range: " << (maxVal - minVal) << std::endl;
}

// ============================================================
// 辅助：写入多字节值（大端 — Photoshop 内部一律 BE）
// ============================================================
static inline void writeBE16(uint8_t* p, uint16_t v) {
    *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
    *p++ = static_cast<uint8_t>(v & 0xFF);
}
static inline void writeBE32(uint8_t* p, uint32_t v) {
    *p++ = static_cast<uint8_t>((v >> 24) & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 16) & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
    *p++ = static_cast<uint8_t>(v & 0xFF);
}

// ============================================================
// 辅助：写一个 Photoshop Image Resource 块
// ============================================================
static void writePsResource(std::vector<uint8_t>& out,
                            uint16_t resId,
                            const uint8_t* data, uint32_t dataSize) {
    uint32_t pad = (dataSize % 2) ? 1 : 0;
    size_t off = out.size();
    out.resize(off + 4 + 2 + 2 + 4 + dataSize + pad);
    uint8_t* p = out.data() + off;

    memcpy(p, "8BIM", 4); p += 4;                    // 签名
    writeBE16(p, resId); p += 2;                       // ID 大端
    *p++ = 0x00; *p++ = 0x00;                          // 空资源名 (padded)
    writeBE32(p, dataSize); p += 4;                     // 数据长度 大端
    memcpy(p, data, dataSize); p += dataSize;
    if (pad) *p++ = 0x00;
}

// ============================================================
// 构造全部 Photoshop Image Resources (tag 34377)
// 注意：资源块内部所有多字节值均为 大端序 (BE)
// 对照 mode.tif 参考格式
// ============================================================
static std::vector<uint8_t> buildPhotoshopData(int totalChannels,
                                               const std::vector<std::string>& spotNames) {
    std::vector<uint8_t> out;
    int n = static_cast<int>(spotNames.size());

    // ----------------------------------------------------------
    // Resource 0x03EE (1006) — 通道名称 (Pascal strings)
    // ----------------------------------------------------------
    {
        uint32_t sz = 0;
        for (const auto& nm : spotNames) sz += 1 + static_cast<uint32_t>(nm.size());
        std::vector<uint8_t> d(sz);
        uint8_t* p = d.data();
        for (const auto& nm : spotNames) {
            *p++ = static_cast<uint8_t>(nm.size());
            memcpy(p, nm.data(), nm.size());
            p += nm.size();
        }
        writePsResource(out, 0x03EE, d.data(), sz);
    }

    // ----------------------------------------------------------
    // Resource 0x041D (1053) — 通道计数 (BE uint32 × 2)
    // ----------------------------------------------------------
    {
        uint8_t d[8];
        writeBE32(d + 0, static_cast<uint32_t>(totalChannels));      // 如 4
        writeBE32(d + 4, static_cast<uint32_t>(totalChannels + 1));  // 如 5
        writePsResource(out, 0x041D, d, sizeof(d));
    }

    // ----------------------------------------------------------
    // Resource 0x0435 (1077) — Display Info (关键：标记专色 02)
    //   结构: version(BE uint32) + count(BE uint16) + 每通道 12 字节
    //   每通道: color[8] + opacity(BE uint16) + kind(1byte, 2=Spot) + pad(1byte)
    //   对照 mode.tif: bytes ...0064 0200 02... → kind=0x02 在位置 16
    // ----------------------------------------------------------
    {
        std::vector<uint8_t> d(6 + n * 12);
        uint8_t* p = d.data();
        writeBE32(p, 1); p += 4;                                    // version = 1
        writeBE16(p, static_cast<uint16_t>(n)); p += 2;              // count
        for (int i = 0; i < n; i++) {
            memset(p, 0xFF, 8); p += 8;                              // 颜色 = 未指定
            writeBE16(p, 100); p += 2;                               // opacity = 100%
            *p++ = 0x02; *p++ = 0x00;                                // kind=2(Spot) + pad
        }
        writePsResource(out, 0x0435, d.data(),
                        static_cast<uint32_t>(d.size()));
    }

    return out;
}

// ============================================================
// 主处理流水线
// ============================================================
int main(int argc, char* argv[]) {
    Config cfg;

    // 允许命令行指定输入图像
    if (argc > 1) {
        cfg.imagePath = argv[1];
    }

    // ============================================================
    // 02. 读取图像  (对应 read_image)
    // ============================================================
    cv::Mat image = cv::imread(cfg.imagePath, cv::IMREAD_UNCHANGED);
    if (image.empty()) {
        std::cerr << "Error: Cannot read image: " << cfg.imagePath << std::endl;
        return -1;
    }

    // 测试：缩小到 2/3
    if (cfg.testHalfScale) {
        cv::Mat scaled;
        cv::resize(image, scaled, cv::Size(image.cols * 2 / 3, image.rows * 2 / 3),
                   0, 0, cv::INTER_AREA);
        image = scaled;
        std::cout << "Test mode: scaled to 67%" << std::endl;
    }

    // ============================================================
    // 03. 获取图像尺寸  (对应 get_image_size)
    // ============================================================
    int imageWidth  = image.cols;
    int imageHeight = image.rows;
    std::cout << "Image size: " << imageWidth << " x " << imageHeight << std::endl;

    // ============================================================
    // 06. RGB → Gray  (对应 count_channels + rgb1_to_gray)
    // ============================================================
    // 保存原始 BGR 三通道，用于最终合成 RGB + W1 多通道 TIFF
    cv::Mat bgrOriginal;

    // Alpha 遮罩：透明区域(alpha==0)为 255，不透区域为 0
    // 用于最后将透明区域压到最低高度
    cv::Mat transparentMask;

    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        bgrOriginal = image;  // 直接引用，不会修改
    } else if (image.channels() == 4) {
        // 分离 BGRA 通道
        std::vector<cv::Mat> channels(4);
        cv::split(image, channels);
        // channels[0]=B, [1]=G, [2]=R, [3]=A

        // 用 RGB 部分正常转灰度，同时保存为 bgrOriginal
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
        bgrOriginal = bgr;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

        // 透明区域遮罩：alpha <= 15 → 255（扩大透明判定，消除半透明边缘毛刺）
        cv::threshold(channels[3], transparentMask, 15, 255, cv::THRESH_BINARY_INV);

        // 形态学膨胀：将遮罩向外扩 3 像素，形成缓冲区吃掉毛刺
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
        cv::dilate(transparentMask, transparentMask, kernel);

        // 用 inpaint 从边缘自然延伸填充透明区域，避免硬边界产生假细节
        cv::inpaint(gray, transparentMask, gray, 5.0, cv::INPAINT_TELEA);
        // 透明区域 RGB 填 255（纯白），避免残留数据产生横竖条纹
        bgrOriginal.setTo(cv::Scalar(255, 255, 255), transparentMask);
    } else {
        gray = image.clone();
        cv::cvtColor(gray, bgrOriginal, cv::COLOR_GRAY2BGR);  // 单通道转伪 RGB
    }

    // 保存 Gray 以便检查透明区域影响
    cv::imwrite("gray_check.tif", gray);
    std::cout << "Save gray_check.tif OK" << std::endl;

    // 调试：保存 bgrOriginal 检查原始 RGB 是否已有条纹
    cv::imwrite("debug_bgr_original.tif", bgrOriginal);
    std::cout << "Save debug_bgr_original.tif OK" << std::endl;

    // 确保 Gray 是 CV_32F 以进行浮点运算（Halcon 内部默认使用浮点）
    cv::Mat grayF;
    gray.convertTo(grayF, CV_32F);
    printRange(grayF, "Gray");

    // ============================================================
    // 08. 大尺度 Blur  (对应 mean_image)
    //     作用：获取整体亮度 / 大结构
    // ============================================================
    cv::Mat largeBlur;
    cv::blur(grayF, largeBlur, cv::Size(cfg.largeScale, cfg.largeScale));
    printRange(largeBlur, "LargeBlur");

    // ============================================================
    // 09. 大结构作为 Base Height  (对应 scale_image)
    //     scale_image(Image, Scaled, Mult, Add)  →  Scaled = Image * Mult + Add
    // ============================================================
    cv::Mat baseHeight = largeBlur * cfg.baseWeight + 0.0;
    printRange(baseHeight, "BaseHeight");

    // ============================================================
    // 10. 中尺度 Blur  (对应 mean_image)
    // ============================================================
    cv::Mat middleBlur;
    cv::blur(grayF, middleBlur, cv::Size(cfg.middleScale, cfg.middleScale));
    printRange(middleBlur, "MiddleBlur");

    // ============================================================
    // 11. 中尺度 Detail  (对应 sub_image)
    //     sub_image(A, B, C, Mult, Add)  →  C = (A - B) * Mult + Add
    //     Gray - MiddleBlur，保留正负变化
    //     亮 → 正值 / 暗 → 负值
    // ============================================================
    cv::Mat middleDetail = (grayF - middleBlur) * 1.0 + 0.0;
    printRange(middleDetail, "MiddleDetail");

    // ============================================================
    // 12. 中尺度 Detail 加权  (对应 scale_image)
    // ============================================================
    cv::Mat middleDetailWeighted = middleDetail * cfg.middleWeight + 0.0;
    printRange(middleDetailWeighted, "MiddleDetailWeighted");

    // ============================================================
    // 13. 小尺度 Blur  (对应 mean_image)
    // ============================================================
    cv::Mat smallBlur;
    cv::blur(grayF, smallBlur, cv::Size(cfg.smallScale, cfg.smallScale));
    printRange(smallBlur, "SmallBlur");

    // ============================================================
    // 14. 高频 Detail  (对应 sub_image)
    //     Gray - SmallBlur
    // ============================================================
    cv::Mat highDetail = (grayF - smallBlur) * 1.0 + 0.0;
    printRange(highDetail, "HighDetail");

    // ============================================================
    // 15. 高频 Detail 加权  (对应 scale_image)
    // ============================================================
    cv::Mat highDetailWeighted = highDetail * cfg.highWeight + 0.0;
    printRange(highDetailWeighted, "HighDetailWeighted");

    // ============================================================
    // 16. Base + Middle  (对应 add_image)
    //     add_image(A, B, C, Mult, Add)  →  C = (A + B) * Mult + Add
    //     注意：不进行 normalize，保留原始相对关系
    // ============================================================
    cv::Mat baseMiddleHeight = (baseHeight + middleDetailWeighted) * 1.0 + 0.0;
    printRange(baseMiddleHeight, "BaseMiddleHeight");

    // ============================================================
    // 17. Base + Middle + High  (对应 add_image)
    // ============================================================
    cv::Mat heightRaw = (baseMiddleHeight + highDetailWeighted) * 1.0 + 0.0;

    // ============================================================
    // 18. 查看原始 Height 范围  (对应 min_max_gray)
    //     这里非常重要 — 用于确认算法是否真的产生高度变化
    // ============================================================
    double minHeight, maxHeight;
    cv::minMaxLoc(heightRaw, &minHeight, &maxHeight);
    double rangeHeight = maxHeight - minHeight;
    std::cout << "\n============================================" << std::endl;
    std::cout << "  HeightRaw Range" << std::endl;
    std::cout << "  MinHeight   = " << minHeight << std::endl;
    std::cout << "  MaxHeight   = " << maxHeight << std::endl;
    std::cout << "  RangeHeight = " << rangeHeight << std::endl;
    std::cout << "============================================\n" << std::endl;

    // ============================================================
    // 20. 最终归一化  (对应 scale_image_range)
    //     只在最后一步做一次 — 映射到 0~255
    // ============================================================
    cv::Mat finalHeightMap;
    cv::normalize(heightRaw, finalHeightMap, 0, 255, cv::NORM_MINMAX, CV_8U);
    printRange(finalHeightMap, "FinalHeightMap");

    // 将透明区域压到最低高度 (0)
    if (!transparentMask.empty()) {
        finalHeightMap.setTo(0, transparentMask);
        std::cout << "Transparent areas set to min height (0)." << std::endl;
    }

    // 反转高度值: 0↔255，亮区↔暗区
    cv::bitwise_not(finalHeightMap, finalHeightMap);
    std::cout << "Height map inverted." << std::endl;

    // ============================================================
    // 两步走：
    //   1. cv::imwrite 写基础 4ch TIFF（像素数据保证正确）
    //   2. libtiff 补丁 ExtraSamples + Photoshop 标签
    // ============================================================
    {
        // Step 1: 用 OpenCV 写，保持 BGR 顺序
        //         注意：imwrite 写 TIFF 时会自动 BGR→RGB，所以这里不手动交换
        std::vector<cv::Mat> bgrCh(3);
        cv::split(bgrOriginal, bgrCh);
        std::vector<cv::Mat> bgraCh = {bgrCh[0], bgrCh[1], bgrCh[2], finalHeightMap};
        cv::Mat outputBGRA;
        cv::merge(bgraCh, outputBGRA);

        // 写 8-bit
        std::string tmp8 = cfg.output8bit + ".tmp.tif";
        cv::imwrite(tmp8, outputBGRA);

        // Step 2: 用 libtiff 补丁标签
        auto psData = buildPhotoshopData(4, {"W1"});
        uint16_t extraSample = EXTRASAMPLE_UNSPECIFIED;

        auto patchTiff = [&](const std::string& tmpPath, const std::string& finalPath) {
            TIFF* tif = TIFFOpen(tmpPath.c_str(), "r+");
            if (!tif) {
                std::cerr << "TIFFOpen r+ failed: " << tmpPath << std::endl;
                return false;
            }
            // 修改 ExtraSamples: Alpha(2) → Unspecified(0) = 专色
            TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, 1, &extraSample);
            // 添加 Photoshop 标签
            TIFFSetField(tif, TIFFTAG_PHOTOSHOP,
                         static_cast<uint32_t>(psData.size()), psData.data());
            // 设置 300 DPI
            TIFFSetField(tif, TIFFTAG_XRESOLUTION, 300.0);
            TIFFSetField(tif, TIFFTAG_YRESOLUTION, 300.0);
            TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
            bool ok = (TIFFRewriteDirectory(tif) == 1);
            TIFFClose(tif);
            if (ok) {
                // 重命名为最终文件
                std::remove(finalPath.c_str());
                std::rename(tmpPath.c_str(), finalPath.c_str());
            } else {
                std::cerr << "TIFFRewriteDirectory failed for " << tmpPath << std::endl;
            }
            return ok;
        };

        bool ok8 = patchTiff(tmp8, cfg.output8bit);
        std::cout << "Save 4ch (RGB+W1): " << (ok8 ? "OK" : "FAIL")
                  << " → " << cfg.output8bit << std::endl;
    }

    // ============================================================
    // 可选：显示结果 (对应 dev_display)
    // ============================================================
#ifdef SHOW_RESULTS
    cv::namedWindow("Original", cv::WINDOW_NORMAL);
    cv::imshow("Original", image);

    cv::namedWindow("Gray", cv::WINDOW_NORMAL);
    cv::imshow("Gray", gray);

    cv::namedWindow("Final Height Map", cv::WINDOW_NORMAL);
    cv::imshow("Final Height Map", finalHeightMap);

    std::cout << "\nPress any key to exit..." << std::endl;
    cv::waitKey(0);
#endif

    std::cout << "\nDone." << std::endl;
    return 0;
}
