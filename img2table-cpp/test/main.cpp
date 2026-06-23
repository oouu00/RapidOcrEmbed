#include <iostream>
#include <fstream>
#include <string>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include "table/extractor.h"
#include "table/html_gen.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: table_test <image_path>" << std::endl;
        return 1;
    }

    std::string imgPath = argv[1];
    cv::Mat img = cv::imread(imgPath, cv::IMREAD_COLOR);
    if (img.empty()) {
        std::cerr << "Failed to load image: " << imgPath << std::endl;
        return 1;
    }

    std::cout << "Image loaded: " << img.cols << "x" << img.rows << std::endl;

    auto result = table::extractBorderedTables(img);

    std::cout << "Found " << result.tables.size() << " table(s)" << std::endl;

    for (size_t i = 0; i < result.tables.size(); i++) {
        auto& tbl = result.tables[i];
        std::cout << "Table " << i << ": " << tbl.nbRows() << " rows x "
                  << tbl.nbColumns() << " cols" << std::endl;

        std::string html = table::generateHTML(tbl);
        std::string outFile = "table_" + std::to_string(i) + ".html";
        std::ofstream ofs(outFile);
        if (ofs.is_open()) {
            ofs << "<!DOCTYPE html><html><body>\n";
            ofs << html;
            ofs << "</body></html>\n";
            ofs.close();
            std::cout << "HTML output: " << outFile << std::endl;
        }
    }

    if (!result.html.empty()) {
        std::ofstream ofs("all_tables.html");
        if (ofs.is_open()) {
            ofs << "<!DOCTYPE html><html><body>\n";
            ofs << result.html;
            ofs << "</body></html>\n";
            ofs.close();
        }
    }

    cv::Mat output = img.clone();
    for (auto& tbl : result.tables) {
        cv::Scalar color(0, 0, 255);
        int thickness = 2;
        for (auto& row : tbl.rows) {
            for (auto& cell : row.cells) {
                cv::rectangle(output,
                    cv::Point(cell.x1, cell.y1),
                    cv::Point(cell.x2, cell.y2),
                    color, thickness);
            }
        }
    }

    std::string ext = imgPath.substr(imgPath.find_last_of('.'));
    std::string base = imgPath.substr(0, imgPath.find_last_of('.'));
    std::string outPath = base + "_out" + ext;
    cv::imwrite(outPath, output);
    std::cout << "Output image: " << outPath << std::endl;

    return 0;
}
