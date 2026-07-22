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

#include <iostream>
#include <string>

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
    std::string output16bit = "Badge_Height_V4_1_16bit.tif";
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

    // ============================================================
    // 03. 获取图像尺寸  (对应 get_image_size)
    // ============================================================
    int imageWidth  = image.cols;
    int imageHeight = image.rows;
    std::cout << "Image size: " << imageWidth << " x " << imageHeight << std::endl;

    // ============================================================
    // 06. RGB → Gray  (对应 count_channels + rgb1_to_gray)
    // ============================================================
    // Alpha 遮罩：透明区域(alpha==0)为 255，不透区域为 0
    // 用于最后将透明区域压到最低高度
    cv::Mat transparentMask;

    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else if (image.channels() == 4) {
        // 分离 BGRA 通道
        std::vector<cv::Mat> channels(4);
        cv::split(image, channels);
        // channels[0]=B, [1]=G, [2]=R, [3]=A

        // 用 RGB 部分正常转灰度
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

        // 透明区域遮罩：alpha <= 15 → 255（扩大透明判定，消除半透明边缘毛刺）
        cv::threshold(channels[3], transparentMask, 15, 255, cv::THRESH_BINARY_INV);

        // 形态学膨胀：将遮罩向外扩 3 像素，形成缓冲区吃掉毛刺
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
        cv::dilate(transparentMask, transparentMask, kernel);

        // 用 inpaint 从边缘自然延伸填充透明区域，避免硬边界产生假细节
        cv::inpaint(gray, transparentMask, gray, 5.0, cv::INPAINT_TELEA);
    } else {
        gray = image.clone();
    }

    // 保存 Gray 以便检查透明区域影响
    cv::imwrite("gray_check.tif", gray);
    std::cout << "Save gray_check.tif OK" << std::endl;

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

    // ============================================================
    // 22. 保存 8-bit Height Map  (对应 write_image)
    // ============================================================
    bool ok8 = cv::imwrite(cfg.output8bit, finalHeightMap);
    std::cout << "Save 8-bit: " << (ok8 ? "OK" : "FAIL") << " → " << cfg.output8bit << std::endl;

    // ============================================================
    // 23. 转换并保存 16-bit  (对应 convert_image_type → 'uint2')
    // ============================================================
    cv::Mat heightMap16;
    // 将 8-bit [0,255] 映射到 16-bit [0,65535]
    finalHeightMap.convertTo(heightMap16, CV_16U, 65535.0 / 255.0);
    bool ok16 = cv::imwrite(cfg.output16bit, heightMap16);
    std::cout << "Save 16-bit: " << (ok16 ? "OK" : "FAIL") << " → " << cfg.output16bit << std::endl;

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
