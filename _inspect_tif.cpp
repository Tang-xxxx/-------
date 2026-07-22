#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    cv::Mat img = cv::imread("mode.tif", cv::IMREAD_UNCHANGED);
    std::cout << "Shape: " << img.size() << " x " << img.channels() << " ch" << std::endl;
    std::cout << "Dtype: " << (img.depth() == CV_8U ? "8U" : "other") << std::endl;

    if (img.channels() == 5) {
        std::vector<cv::Mat> channels(5);
        cv::split(img, channels);
        const char* names[] = {"R","G","B","W1","W2"};
        for (int c = 0; c < 5; c++) {
            double mn, mx;
            cv::minMaxLoc(channels[c], &mn, &mx);
            std::cout << names[c] << ": min=" << mn << " max=" << mx << std::endl;
            cv::imwrite(std::string("mode_ch") + names[c] + ".png", channels[c]);
        }
        // Also save combined RGB for visual reference
        cv::Mat rgb;
        cv::merge(std::vector<cv::Mat>{channels[2], channels[1], channels[0]}, rgb);
        cv::imwrite("mode_rgb.png", rgb);
    }
    std::cout << "Done." << std::endl;
    return 0;
}
