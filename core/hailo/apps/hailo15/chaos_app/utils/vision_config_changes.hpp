#include <fstream>
#include <iostream>
#include <regex>
#include <vector>
#include <utility>
#include <string>

std::string readFileContent(const std::string& filePath) {
    std::ifstream inputFile(filePath);
    if (!inputFile.is_open()) {
        throw std::runtime_error("Failed to open the file for reading.");
    }
    return {std::istreambuf_iterator<char>(inputFile), std::istreambuf_iterator<char>()};
}

void writeFileContent(const std::string& filePath, const std::string& content) {
    std::ofstream outputFile(filePath);
    if (!outputFile.is_open()) {
        throw std::runtime_error("Failed to open the file for writing.");
    }
    outputFile << content;
    outputFile.close();
}

void init_vision_config_file(const std::string& frontend_config_file_path) {
    try {
        std::string fileContents = readFileContent(frontend_config_file_path);

        // Replace all occurrences of "enabled": true with "enabled": false
        size_t pos = 0;
        while ((pos = fileContents.find("\"enabled\": true", pos)) != std::string::npos) {
            fileContents.replace(pos, 15, "\"enabled\": false");
            pos += 16; // Move past the replaced part to avoid infinite loop
        }

        size_t startPos = fileContents.find("\"resolutions\": [");
        if (startPos == std::string::npos) {
            std::cout << "No resolutions array found." << std::endl;
            return;
        }
        startPos += 16;
        size_t endPos = fileContents.find("]", startPos);
        if (endPos == std::string::npos) {
            std::cerr << "Malformed JSON." << std::endl;
            return;
        }
        size_t firstObjEnd = fileContents.find("}", startPos) + 1;
        if (firstObjEnd == std::string::npos || firstObjEnd > endPos) {
            std::cerr << "Malformed JSON or empty resolutions array." << std::endl;
            return;
        }
        std::string firstObject = fileContents.substr(startPos, firstObjEnd - startPos);
        std::string newContent = fileContents.substr(0, startPos) + firstObject + "\n        ]";
        size_t afterArrayPos = fileContents.find("]", endPos) + 1;
        newContent += fileContents.substr(afterArrayPos);
        writeFileContent(frontend_config_file_path, newContent);
        std::cout << "Config file updated successfully." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

void change_hdr_status(bool& g_hdr_enabled, const std::string& frontend_config_file_path) {
    try {
        std::string fileContents = readFileContent(frontend_config_file_path);
        g_hdr_enabled = !g_hdr_enabled;
        std::cout << (g_hdr_enabled ? "Enabling HDR" : "Disabling HDR") << std::endl;
        std::regex hdrRegex(R"("enabled"\s*:\s*(true|false))");
        std::string replacement = std::string("\"enabled\": ") + (g_hdr_enabled ? "true" : "false");
        fileContents = std::regex_replace(fileContents, hdrRegex, replacement);
        writeFileContent(frontend_config_file_path, fileContents);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

void rotate_90(bool to_rotate, std::string& g_encoder_config_file_path, const std::string& frontend_config_file_path) {
    try {
        std::string encoderFileContents = readFileContent(g_encoder_config_file_path.c_str());
        std::string visionFileContents = readFileContent(frontend_config_file_path);
        std::string rotationStatus = to_rotate ? "true" : "false";
        std::string rotationAngle = to_rotate ? "\"ROTATION_ANGLE_90\"" : "\"ROTATION_ANGLE_0\"";
        std::cout << (to_rotate ? "Rotating 90 degrees" : "Canceling rotation of 90 degrees") << std::endl;

        // Swap width and height under "encoder" in encoderFileContents
        std::regex encoderRegex(R"("encoding"\s*:\s*\{[^}]*\})");
        std::smatch encoderMatch;

        if (std::regex_search(encoderFileContents, encoderMatch, encoderRegex)) {
            std::string encoderObject = encoderMatch[0];
            std::regex widthRegex(R"("width"\s*:\s*(\d+))");
            std::regex heightRegex(R"("height"\s*:\s*(\d+))");
            std::smatch widthMatch, heightMatch;

            if (std::regex_search(encoderObject, widthMatch, widthRegex) && 
                std::regex_search(encoderObject, heightMatch, heightRegex)) {
                std::string widthValue = widthMatch[1].str();
                std::string heightValue = heightMatch[1].str();
                encoderObject = std::regex_replace(encoderObject, widthRegex, "\"width\": " + heightValue);
                encoderObject = std::regex_replace(encoderObject, heightRegex, "\"height\": " + widthValue);
                encoderFileContents = std::regex_replace(encoderFileContents, encoderRegex, encoderObject);
            }
        }

        // Update rotation settings in visionFileContents
        std::regex rotationObjectRegex(R"("rotation"\s*:\s*\{[^}]*\})");
        std::smatch rotationMatch;
        if (std::regex_search(visionFileContents, rotationMatch, rotationObjectRegex) && !rotationMatch.empty()) {
            std::string rotationObject = rotationMatch[0];
            std::regex enabledRegex(R"("enabled"\s*:\s*(true|false))");
            rotationObject = std::regex_replace(rotationObject, enabledRegex, "\"enabled\": " + rotationStatus);
            if (to_rotate) {
                std::regex angleRegex(R"("angle"\s*:\s*("[^"]*"|null))");
                rotationObject = std::regex_replace(rotationObject, angleRegex, "\"angle\": " + rotationAngle);
            }
            visionFileContents = std::regex_replace(visionFileContents, rotationObjectRegex, rotationObject);
        }

        writeFileContent(frontend_config_file_path, visionFileContents);
        writeFileContent(g_encoder_config_file_path.c_str(), encoderFileContents);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}


void change_output_resolution(const std::string& frontend_config_file_path, const std::pair<int, int>& resolution) {
    try {
        std::string visionFileContents = readFileContent(frontend_config_file_path);
        std::regex outputVideoRegex(R"("output_video"\s*:\s*\{[^}]*\})");
        std::smatch outputVideoMatch;

        if (std::regex_search(visionFileContents, outputVideoMatch, outputVideoRegex)) {
            std::string outputVideoObject = outputVideoMatch[0];
            std::regex widthRegex(R"("width"\s*:\s*\d+)");
            std::regex heightRegex(R"("height"\s*:\s*\d+)");
            outputVideoObject = std::regex_replace(outputVideoObject, widthRegex, "\"width\": " + std::to_string(resolution.first));
            outputVideoObject = std::regex_replace(outputVideoObject, heightRegex, "\"height\": " + std::to_string(resolution.second));
            visionFileContents = std::regex_replace(visionFileContents, outputVideoRegex, outputVideoObject);
        }
        writeFileContent(frontend_config_file_path, visionFileContents);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}
    
