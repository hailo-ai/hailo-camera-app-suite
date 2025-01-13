#include "resources.hpp"
#include "media_library/encoder_config.hpp"
#include <iostream>

webserver::resources::EncoderResource::bitrate_control_t webserver::resources::EncoderResource::encoder_control_t::stringToEnum(const std::string& str){
    if (str == "VBR") return webserver::resources::EncoderResource::VBR;
    if (str == "CVBR") return webserver::resources::EncoderResource::CVBR;
    throw std::invalid_argument("Invalid string for bitrate_control_t");
}

std::string webserver::resources::EncoderResource::encoder_control_t::enumToString(webserver::resources::EncoderResource::bitrate_control_t bitrate) const {
    switch (bitrate) {
        case webserver::resources::EncoderResource::VBR: return "VBR";
        case webserver::resources::EncoderResource::CVBR: return "CVBR";
        default: throw std::invalid_argument("Invalid enum value for bitrate_control_t");
    }
}

void webserver::resources::to_json(nlohmann::json &j, const webserver::resources::EncoderResource::encoder_control_t &b)
{
    j = nlohmann::json{{"rc_mode", b.enumToString(b.rc_mode)},
                       {"bitrate", b.bitrate}};
}

void webserver::resources::from_json(const nlohmann::json &j, webserver::resources::EncoderResource::encoder_control_t &b)
{
    auto bitrate_control = j.at("rc_mode").get<std::string>();
    b.rc_mode = b.stringToEnum(bitrate_control);
    j.at("bitrate").get_to(b.bitrate);
}

webserver::resources::EncoderResource::EncoderResource(std::shared_ptr<webserver::resources::ConfigResource> configs, std::shared_ptr<webserver::resources::FrontendResource> frontend_res) : Resource()
{
    m_config = configs->get_encoder_default_config();
    m_encoder_control.bitrate = m_config["hailo_encoder"]["rate_control"]["bitrate"]["target_bitrate"];
    m_encoder_control.rc_mode = m_encoder_control.stringToEnum(m_config["hailo_encoder"]["rate_control"]["rc_mode"]);
    m_encoder_control.width = m_config["input_stream"]["width"];
    m_encoder_control.height = m_config["input_stream"]["height"];
    m_encoder_control.framerate = m_config["input_stream"]["framerate"];

    frontend_res->subscribe_callback([this](ResourceStateChangeNotification notification)
                             {
        if (notification.resource_type == STREAM_CONFIG){
            auto state = std::static_pointer_cast<StreamConfigResourceState>(notification.resource_state);
            if ((state->rotate_enabled && 
                (state->rotation == StreamConfigResourceState::ROTATION_90 || state->rotation == StreamConfigResourceState::ROTATION_270)))
            {
                m_encoder_control.width = state->resolutions[0].height;
                m_encoder_control.height = state->resolutions[0].width;
                m_config["input_stream"]["width"] = m_encoder_control.width;
                m_config["input_stream"]["height"] = m_encoder_control.height;
            }
            else if (state->resolutions[0].stream_size_changed || (state->rotate_enabled && 
                (state->rotation == StreamConfigResourceState::ROTATION_0 || state->rotation == StreamConfigResourceState::ROTATION_180))
                || !state->rotate_enabled)
            {
                m_encoder_control.width = state->resolutions[0].width;
                m_encoder_control.height = state->resolutions[0].height;
                m_config["input_stream"]["width"] = m_encoder_control.width;
                m_config["input_stream"]["height"] = m_encoder_control.height;
            }
            
            if (state->resolutions[0].framerate_changed){
                m_encoder_control.framerate = state->resolutions[0].framerate;
                on_resource_change(std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->to_string())));
            }
        }
    });
}

void webserver::resources::EncoderResource::set_encoder_control(webserver::resources::EncoderResource::encoder_control_t &encoder_control)
{
    m_encoder_control.bitrate = encoder_control.bitrate;
    m_encoder_control.rc_mode = encoder_control.rc_mode;
    m_config["hailo_encoder"]["rate_control"]["bitrate"]["target_bitrate"] = encoder_control.bitrate;
    m_config["hailo_encoder"]["rate_control"]["rc_mode"] = encoder_control.rc_mode == webserver::resources::EncoderResource::VBR ? "VBR" : "CVBR";
}

void webserver::resources::EncoderResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/encoder", std::function<nlohmann::json()>([this]()
                                                                         {
        nlohmann::json j;
        to_json(j, m_encoder_control);
        return j; }));

    srv->Post("/encoder", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                                                {
        webserver::resources::EncoderResource::encoder_control_t encoder_control;
        try
        {
            encoder_control = j_body.get<webserver::resources::EncoderResource::encoder_control_t>();
        }
        catch (const std::exception &e)
        {
            WEBSERVER_LOG_ERROR("Failed to parse json body to encoder_control_t");//TODO change that 
        }
        set_encoder_control(encoder_control);
        on_resource_change(std::make_shared<webserver::resources::ResourceState>(ConfigResourceState(this->to_string())));
        nlohmann::json j_out;
        to_json(j_out, m_encoder_control);
        return j_out; }));
}

void webserver::resources::EncoderResource::apply_config(GstElement *encoder_element)
{
    gpointer value = nullptr;
    g_object_get(G_OBJECT(encoder_element), "user-config", &value, NULL);
    if (!value)
    {
        WEBSERVER_LOG_ERROR("Encoder config is null");
    }
    encoder_config_t *config = reinterpret_cast<encoder_config_t *>(value);
    if (!std::holds_alternative<hailo_encoder_config_t>(*config))
    {
        WEBSERVER_LOG_ERROR("Encoder config does not hold hailo_encoder_config_t");
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(*config);
    hailo_encoder_config.rate_control.picture_rc= true;
    hailo_encoder_config.rate_control.bitrate.target_bitrate = m_encoder_control.bitrate;
    hailo_encoder_config.rate_control.rc_mode = str_to_rc_mode.at(m_encoder_control.enumToString(m_encoder_control.rc_mode));
    hailo_encoder_config.input_stream.framerate = m_encoder_control.framerate;
    hailo_encoder_config.input_stream.width = m_encoder_control.width;
    hailo_encoder_config.input_stream.height = m_encoder_control.height;
    WEBSERVER_LOG_INFO("Encoder configuration applied with the following settings: "
                       "Target Bitrate: {}, "
                       "Rate Control Mode: {}, "
                       "Input Stream Width: {}, "
                       "Input Stream Height: {}, "
                       "Input Stream Framerate: {}",
                       hailo_encoder_config.rate_control.bitrate.target_bitrate,
                       hailo_encoder_config.rate_control.rc_mode,
                       hailo_encoder_config.input_stream.width,
                       hailo_encoder_config.input_stream.height,
                       hailo_encoder_config.input_stream.framerate);
    g_object_set(G_OBJECT(encoder_element), "user-config", config, NULL);

}