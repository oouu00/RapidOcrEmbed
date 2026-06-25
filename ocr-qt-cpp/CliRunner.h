#ifndef CLIRUNNER_H
#define CLIRUNNER_H

#include <string>
#include <vector>

// 命令行模式: 识别单个图片文件, 输出 JSON 到 stdout
int runCliFile(const std::string& imagePath, bool coords,
               bool layout = false, const std::string& layoutStrategy = "single_line",
               bool pdf = false, int tableMode = 0, const std::string& pages = "",
               bool clipboard = false, bool doAngle = true);

// 命令行模式: 识别多个图片/文件夹, 输出 NDJSON 到 stdout
int runCliFiles(const std::vector<std::string>& paths, bool coords,
                bool layout = false, const std::string& layoutStrategy = "single_line",
                bool pdf = false, int tableMode = 0, bool clipboard = false,
                bool doAngle = true);

// 命令行截图模式
int runCliShot(bool coords, bool layout = false, const std::string& layoutStrategy = "single_line",
               const std::string& region = "", bool clipboard = false, bool doAngle = true);

#endif // CLIRUNNER_H
