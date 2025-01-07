#include "resources.hpp"
#include <iostream>
#include <httplib.h>
#include <regex>

#define IMAGE_PATH "/home/root/apps/webserver/resources/configs/"
#define FONT_PATH "/usr/share/fonts/ttf/"
#define CATEGORIES {"image", "dateTime", "text"}
#define VALID_EXTENSIONS {".png", ".jpeg", ".jpg", ".bmp"}

using namespace webserver::resources;

OsdResource::OsdResource() : Resource()
{
    m_default_config = R"(
    {
        "global_enable": true,
        "image": {
            "global_enable": true,
            "items": [
                {
                    "name": "Image",
                    "type": "image",
                    "enabled": true,
                    "params": {
                        "id": "example_image",
                        "image_path": "/home/root/apps/detection/resources/configs/osd_hailo_static_image.png",
                        "width": 0.2,
                        "height": 0.13,
                        "x": 0.78,
                        "y": 0.0,
                        "z-index": 1,
                        "angle": 0,
                        "rotation_policy": "CENTER"
                    }
                }
            ]
        },
        "dateTime": 
        { 
            "global_enable": true,
            "items": [
                {
                    "name": "Date & Time",
                    "type": "dateTime",
                    "enabled": true,
                    "params": {
                        "id": "example_dateTime",
                        "font_size": 100,
                        "text_color": [
                            255,
                            0,
                            0
                        ],
                        "font_path": "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                        "x": 0.0,
                        "y": 0.95,
                        "z-index": 3,
                        "angle": 0,
                        "rotation_policy": "CENTER"
                    }
                }
            ]
        },
        "text":{
            "global_enable": true,
            "items": [
            {
                "name": "HailoAI Label",
                "type": "text",
                "enabled": true,
                "params": {
                    "id": "example_text1",
                    "label": "HailoAI",
                    "font_size": 100,
                    "text_color": [
                        255,
                        255,
                        255
                    ],
                    "x": 0.78,
                    "y": 0.12,
                    "z-index": 2,
                    "font_path": "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                    "angle": 0,
                    "rotation_policy": "CENTER"
                }
            },
            {
                "name": "Demo Label",
                "type": "text",
                "enabled": true,
                "params": {
                    "id": "example_text2",
                    "label": "DemoApplication",
                    "font_size": 100,
                    "text_color": [
                        102,
                        0,
                        51
                    ],
                    "x": 0.0,
                    "y": 0.01,
                    "z-index": 1,
                    "font_path": "/usr/share/fonts/ttf/LiberationMono-Regular.ttf",
                    "angle": 0,
                    "rotation_policy": "CENTER"
                }
            }
        ]
        }
    })";
    m_config = nlohmann::json::parse(m_default_config);
}

OsdResource::OsdResourceState::OsdResourceState(nlohmann::json config, std::vector<std::string> overlays_ids) : overlays_to_delete(overlays_ids) {
    for (const char* category : CATEGORIES)
    {
        for (auto &entry : config[category]["items"])
        {
            auto j_config = entry["params"];
            if (j_config == nullptr)
                continue;
            
            if (static_cast<std::string>(category) == "text")
            {
                bool enabled = entry["enabled"] && config["global_enable"].get<bool>() && config[category]["global_enable"];
                text_overlays.push_back(OsdResourceConfig<osd::TextOverlay>(enabled, j_config));
            }
            else if (static_cast<std::string>(category) == "image")
            {
                bool enabled = entry["enabled"] && config["global_enable"].get<bool>() && config[category]["global_enable"];
                image_overlays.push_back(OsdResourceConfig<osd::ImageOverlay>(enabled, j_config));
            }
            else if (static_cast<std::string>(category) == "dateTime")
            {
                bool enabled = entry["enabled"] && config["global_enable"].get<bool>() && config[category]["global_enable"];
                datetime_overlays.push_back(OsdResourceConfig<osd::DateTimeOverlay>(enabled, j_config));
            }
            else
            {
                WEBSERVER_LOG_ERROR("Unknown overlay type: {}", entry["type"]);
            }
        }
    }
}


nlohmann::json OsdResource::map_paths(nlohmann::json config)
{
    for (const char* category : CATEGORIES)
    {
        if (!config.contains(category) || !config[category].contains("items"))
            continue;

        for (auto &entry : config[category]["items"])
        {
            if (entry["type"] == "image")
            {
                entry["params"]["image_path"] = std::string(IMAGE_PATH) + entry["params"]["image_path"].get<std::string>();
            }
            else if (entry["type"] == "text" || entry["type"] == "dateTime")
            {
                entry["params"]["font_path"] = std::string(FONT_PATH) + entry["params"]["font_path"].get<std::string>();
            }
        }
    }
    return config;
}

nlohmann::json OsdResource::unmap_paths(nlohmann::json config)
{
    for (const char* category : CATEGORIES)
    {
        if (!config.contains(category) || !config[category].contains("items"))
            continue;

        for (auto &entry : config[category]["items"])
        {
            if (entry["type"] == "image")
            {
                entry["params"]["image_path"] = entry["params"]["image_path"].get<std::string>().substr(std::string(IMAGE_PATH).size());
            }
            else if (entry["type"] == "text" || entry["type"] == "dateTime")
            {
                entry["params"]["font_path"] = entry["params"]["font_path"].get<std::string>().substr(std::string(FONT_PATH).size());
            }
        }
    }
    return config;
}


nlohmann::json OsdResource::get_encoder_osd_config()
{
    nlohmann::json current_config;

    for (const char* category : CATEGORIES)
    {
        if (!m_config.contains(category) ||
            !m_config[category]["global_enable"].get<bool>() ||
            !m_config["global_enable"].get<bool>())
            continue;

        for (auto &entry : m_config[category]["items"])
        {
            if (!entry["enabled"])
                continue;

            auto j_config = entry["params"];
            if (j_config == nullptr)
                continue;

            current_config[category].push_back(j_config);
        }
    }

    return current_config;
}

std::vector<std::string> OsdResource::get_overlays_to_delete(nlohmann::json previous_config, nlohmann::json new_config)
{
    std::vector<std::string> overlays_to_delete;
    std::regex whole_object_path(R"(^\/\d+$)");
    for (const char* category : CATEGORIES)
    {
        if (!previous_config.contains(category) || !new_config.contains(category))
            continue;
        auto diff = nlohmann::json::diff(previous_config[category]["items"], new_config[category]["items"]);
        for (auto &entry : diff)
        {
            std::string path = entry["path"];
            if (entry["op"] == "remove" && std::regex_match(path, whole_object_path))
            {
                size_t index = std::stoi(path.substr(1));
                overlays_to_delete.push_back(previous_config[category]["items"][index]["params"]["id"]);
            }
        }
    }
    return overlays_to_delete;
}

void OsdResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/osd", std::function<nlohmann::json()>([this]()
                                                     { return unmap_paths(m_config); }));

    srv->Get("/osd/formats", std::function<nlohmann::json()>([this]() {
        std::vector<std::string> formats;

        for (const auto& entry : std::filesystem::directory_iterator(FONT_PATH)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ttf") {
                std::string format = entry.path().filename().string();
                formats.push_back(format);
            }
        }
        return formats;
    }));

    srv->Get("/osd/images", std::function<nlohmann::json()>([this]() {
        std::vector<std::string> images;

        for (const auto& entry : std::filesystem::directory_iterator(IMAGE_PATH)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string extension = entry.path().extension();
            if (std::find(std::begin(VALID_EXTENSIONS), std::end(VALID_EXTENSIONS), extension) != std::end(VALID_EXTENSIONS)) {
                std::string image = entry.path().filename().string();
                images.push_back(image);
            }
        }
        return images;
    }));

    srv->Patch("/osd", [this](const nlohmann::json &partial_config)
               {
        nlohmann::json previouse_config = m_config;
        m_config.merge_patch(map_paths(partial_config));
        auto result = this->m_config;
        auto state = std::make_shared<OsdResource::OsdResourceState>(OsdResourceState(m_config, get_overlays_to_delete(previouse_config, m_config)));
        on_resource_change(state);
        return unmap_paths(result); });

    srv->Put("/osd", [this](const nlohmann::json &config)
             {
        nlohmann::json previouse_config = m_config;
        auto partial_config = nlohmann::json::diff(m_config, map_paths(config));
        m_config = m_config.patch(partial_config);
        auto result = this->m_config;
        auto state = std::make_shared<OsdResource::OsdResourceState>(OsdResourceState(m_config, get_overlays_to_delete(previouse_config, m_config)));
        on_resource_change(state);
        return unmap_paths(result); });

    srv->Post("/osd/upload", [](const httplib::MultipartFormData& file) {
        std::string extension = file.filename.substr(file.filename.find_last_of("."));
        if (std::find(std::begin(VALID_EXTENSIONS), std::end(VALID_EXTENSIONS), extension) == std::end(VALID_EXTENSIONS)) {
            WEBSERVER_LOG_ERROR("Invalid file extension: {}", file.filename);
            return false;
        }

        if (!std::filesystem::exists(IMAGE_PATH)) {
            WEBSERVER_LOG_ERROR("Image path does not exist: {}", IMAGE_PATH);
        }

        std::string file_path = std::string(IMAGE_PATH) + file.filename;
        try {
            std::ofstream ofs(file_path, std::ios::binary);
            ofs.write(file.content.c_str(), file.content.size());
            ofs.close();
            WEBSERVER_LOG_INFO("File saved to: {}", file_path);
            return true;
        } catch (const std::exception &e) {
            WEBSERVER_LOG_WARN("Failed to save file: {}", e.what());
            return false;
        }
    });
}