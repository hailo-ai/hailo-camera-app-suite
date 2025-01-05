#include "resources.hpp"
#include <iostream>
#include <httplib.h>

webserver::resources::OsdResource::OsdResource() : Resource()
{
    m_default_config = R"(
    [
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
        },
        {
            "name": "Date & Time",
            "type": "datetime",
            "enabled": true,
            "params": {
                "id": "example_datetime",
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
        },
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
    ])";
    m_config = nlohmann::json::parse(m_default_config);
}

nlohmann::json webserver::resources::OsdResource::map_paths(nlohmann::json config)
{
    for (auto &entry : config)
    {
        if (entry["type"] == "image")
        {
            entry["params"]["image_path"] = std::string(m_image_path) + entry["params"]["image_path"].get<std::string>();
        }
        else if (entry["type"] == "text" || entry["type"] == "datetime")
        {
            entry["params"]["font_path"] = std::string(m_font_path) + entry["params"]["font_path"].get<std::string>();
        }
    }
    return config;
}

nlohmann::json webserver::resources::OsdResource::get_encoder_osd_config()
{
    std::vector<nlohmann::json> images;
    std::vector<nlohmann::json> texts;
    std::vector<nlohmann::json> dates;


    for (auto &config : m_config)
    {
        if (config["enabled"] == false)
            continue;

        auto j_config = config["params"];
 
        if (j_config == NULL)
            continue;

        if (config["type"] == "image")
        {
            images.push_back(j_config);
        }
        else if (config["type"] == "text")
        {
            texts.push_back(j_config);
        }
        else if (config["type"] == "datetime")
        {
            dates.push_back(j_config);
        }
    }

    nlohmann::json current_config = {{"image", images}, {"text", texts}, {"dateTime", dates}};
    return current_config;
}

std::vector<std::string> webserver::resources::OsdResource::get_overlays_to_delete(nlohmann::json previouse_config, nlohmann::json new_config){
    std::vector<std::string> overlays_to_delete;
    auto diff = nlohmann::json::diff(previouse_config, new_config);
    for (auto &entry : diff)
    {
        if (entry["op"] == "remove")
        {
            std::string path = entry["path"];
            size_t index = std::stoi(path.substr(1));
            overlays_to_delete.push_back(previouse_config[index]["params"]["id"]);
        }
    }
    return overlays_to_delete;
}

void webserver::resources::OsdResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/osd", std::function<nlohmann::json()>([this]()
                                                     { return this->m_config; }));

    srv->Get("/osd/formats", std::function<nlohmann::json()>([this]() {
        std::vector<std::string> formats;

        for (const auto& entry : std::filesystem::directory_iterator(m_font_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ttf") {
                std::string format = entry.path().filename().string();
                formats.push_back(format);
            }
        }
        return formats;
    }));

    srv->Get("/osd/images", std::function<nlohmann::json()>([this]() {
        std::vector<std::string> images;

        for (const auto& entry : std::filesystem::directory_iterator(m_image_path)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string extension = entry.path().extension();
            if (std::find(m_valid_extensions.begin(), m_valid_extensions.end(), extension) != m_valid_extensions.end()) {
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
        auto state = std::make_shared<webserver::resources::OsdResource::OsdResourceState>(OsdResourceState(m_config, get_overlays_to_delete(previouse_config, m_config)));
        on_resource_change(state);
        return result; });

    srv->Put("/osd", [this](const nlohmann::json &config)
             {
        nlohmann::json previouse_config = m_config;
        auto partial_config = nlohmann::json::diff(m_config, map_paths(config));
        m_config = m_config.patch(partial_config);
        auto result = this->m_config;
        auto state = std::make_shared<webserver::resources::OsdResource::OsdResourceState>(OsdResourceState(m_config, get_overlays_to_delete(previouse_config, m_config)));
        on_resource_change(state);
        return result; });

    srv->Post("/osd/upload", [](const httplib::MultipartFormData& file) {
        std::string extension = file.filename.substr(file.filename.find_last_of("."));
        if (std::find(m_valid_extensions.begin(), m_valid_extensions.end(), extension) == m_valid_extensions.end()) {
            WEBSERVER_LOG_ERROR("Invalid file extension: {}", file.filename);
            return false;
        }

        if (!std::filesystem::exists(m_image_path)) {
            WEBSERVER_LOG_ERROR("Image path does not exist: {}", m_image_path);
        }

        std::string file_path = m_image_path + file.filename;
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