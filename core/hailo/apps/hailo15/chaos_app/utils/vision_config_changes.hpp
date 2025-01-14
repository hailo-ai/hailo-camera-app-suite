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

void rotate_90(bool to_rotate, std::map<output_stream_id_t, std::string> encoder_file_paths, const std::string& frontend_config_file_path) {
    try {
        std::string visionFileContents = readFileContent(frontend_config_file_path);
        std::string rotationStatus = to_rotate ? "true" : "false";
        std::string rotationAngle = to_rotate ? "\"ROTATION_ANGLE_90\"" : "\"ROTATION_ANGLE_0\"";
        std::cout << (to_rotate ? "Rotating 90 degrees" : "Canceling rotation of 90 degrees") << std::endl;

        // Swap width and height under "encoder" in encoderFileContents
        for (const auto& [id, encoder_config_file_path] : encoder_file_paths) {
            std::string encoderFileContents = readFileContent(encoder_config_file_path);
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
            writeFileContent(encoder_config_file_path, encoderFileContents);
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
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}


void rotate_output_resolutions(const std::string& frontend_config_file_path) {
    try {
        std::string visionFileContents = readFileContent(frontend_config_file_path);
        
        // Match the entire output_video object including all its outputs
        std::regex outputVideoRegex("\"output_video\"\\s*:\\s*\\{([^}]*)\\}");
        std::smatch outputVideoMatch;

        if (std::regex_search(visionFileContents, outputVideoMatch, outputVideoRegex)) {
            std::string outputVideoObject = outputVideoMatch[0];
            std::string outputVideoContent = outputVideoMatch[1];
            
            // Match individual output objects within output_video
            std::regex outputObjectRegex("\"([^\"]+)\"\\s*:\\s*\\{([^}]+)\\}");
            std::string::const_iterator searchStart(outputVideoContent.cbegin());
            std::smatch outputMatch;
            std::string modifiedOutputVideo = "\"output_video\": {";
            bool firstOutput = true;

            // Iterate through each output object
            while (std::regex_search(searchStart, outputVideoContent.cend(), outputMatch, outputObjectRegex)) {
                std::string outputName = outputMatch[1];
                std::string outputContent = outputMatch[2];

                // Extract width and height for this output
                std::regex widthValueRegex("\"width\"\\s*:\\s*(\\d+)");
                std::regex heightValueRegex("\"height\"\\s*:\\s*(\\d+)");
                
                std::smatch widthMatch, heightMatch;
                int width = 0, height = 0;

                // Get current values
                if (std::regex_search(outputContent, widthMatch, widthValueRegex)) {
                    width = std::stoi(widthMatch[1]);
                }
                if (std::regex_search(outputContent, heightMatch, heightValueRegex)) {
                    height = std::stoi(heightMatch[1]);
                }

                // Swap values for this output
                if (width != 0 && height != 0) {
                    outputContent = std::regex_replace(outputContent, widthValueRegex, "\"width\": " + std::to_string(height));
                    outputContent = std::regex_replace(outputContent, heightValueRegex, "\"height\": " + std::to_string(width));
                }

                // Add comma if not first output
                if (!firstOutput) {
                    modifiedOutputVideo += ",";
                }
                firstOutput = false;

                // Add modified output to result
                modifiedOutputVideo += "\"" + outputName + "\": {" + outputContent + "}";
                
                // Move iterator to continue search
                searchStart = outputMatch.suffix().first;
            }
            
            modifiedOutputVideo += "}";
            
            // Replace entire output_video object with modified version
            visionFileContents = std::regex_replace(visionFileContents, outputVideoRegex, modifiedOutputVideo);
        }
        
        writeFileContent(frontend_config_file_path, visionFileContents);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}
    
