
#include <iostream>
#include <thread>
#include <ctime>
#include <sstream>
#include <tl/expected.hpp>
#include <signal.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cxxopts/cxxopts.hpp>
#include <filesystem> 
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "chaos_app/utils/common.hpp"
#include "chaos_app/utils/vision_config_changes.hpp"
#include "common_struct.hpp"

// infra includes
#include "pipeline_infra/pipeline.hpp"
#include "pipeline_infra/ai_stage.hpp"
#include "pipeline_infra/postprocess_stage.hpp"
#include "pipeline_infra/overlay_stage.hpp"


using json = nlohmann::json;

#define FRONTEND_CONFIG_FILE "/home/root/apps/basic_security_camera_streaming/resources/configs/frontend_config.json"
#define EIS_TEST_CONFIG_FILE "/home/root/apps/tests/eis_stress_frontend.json"
#define BACKUP_ENCODER_CONFIG_FILE "/tmp/encoder_config_example.json"
#define ENCODER_OSD_CONFIG_FILE "/home/root/apps/basic_security_camera_streaming/resources/configs/encoder_config_4k_no_osd.json"

// AI Pipeline Params
#define AI_SINK "sink3" // The streamid from frontend to AI
// Detection AI Params
#define YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/yolov5s_personface_nv12.hef"
#define DETECTION_AI_STAGE "yolo_detection"
// Detection Postprocess Params
#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
#define YOLO_FUNC_NAME "yolov5s_personface"
// Overlay stage
#define OVERLAY_STAGE "OverLay"
// Callback Params
#define AI_CALLBACK_STAGE "ai_to_encoder"

std::map<output_stream_id_t, bool> g_osd_is_set = {{"sink0", false}, {"sink1", false}, {"sink2", false}, {"sink3", false}};
std::vector<std::string> encoder_configurations
    = {"/tmp/encoder_config_example_4k.json",
       "/tmp/encoder_config_example_fhd.json",
       "/tmp/encoder_config_example_hd.json",
       "/tmp/encoder_config_example_sd.json"};
std::vector<std::string> outputs_files
    = {"/var/volatile/tmp/eis_stress_sink0.h264",
       "/var/volatile/tmp/eis_stress_sink1.h264",
       "/var/volatile/tmp/eis_stress_sink2.h264",
       "/var/volatile/tmp/eis_stress_sink3.h264"};
std::map<std::string, Resolution> resolutions_map = {
    {"4k", {3840, 2160, 15, 15, 8000000}},
    {"fhd", {1920, 1080, 15, 15, 8000000}},
    {"hd", {1280, 720, 30, 20, 8000000}},
    {"sd", {640, 640, 30, 30, 8000000}}};


std::string font_path = "/usr/share/fonts/ttf/LiberationMono-Bold.ttf";
osd::rgba_color_t red_argb = {255, 0, 0, 255};
osd::rgba_color_t blue_argb = {0, 0, 255, 255};
osd::rgba_color_t white_argb = {255, 255, 255, 255};
osd::TextOverlay text_overlay("text_overlay", 0.4, 0.4, "EIS ON!!!!", red_argb, blue_argb, 60.0f, 1, 1, font_path, 0, osd::rotation_alignment_policy_t::CENTER);

static bool g_encoder_is_running = false;
static bool g_pipeline_is_running = false;
static bool g_ai_set = false;
static bool g_denoise_enabled = false;
static bool g_hdr_enabled = false;
static bool g_osd_gyro_status = false;
static bool g_osd_eis_status = false;

static uint g_total_frame_counter = 0;
static uint g_inside_frame_counter = 0;
static uint g_disable_counter = 0;
static uint g_stabilize_counter = 0;
static std::string output_ai_stream_name;

struct MediaLibrary
{
    MediaLibraryFrontendPtr frontend;
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders;
    std::map<output_stream_id_t, std::ofstream> output_files;
    PipelinePtr pipeline;
};
std::shared_ptr<MediaLibrary> m_media_lib;


struct ParsedOptions {
    int no_change_frames;
    int test_time;
    int number_of_resets;
    bool run_over_day;
};

ParsedOptions parseArguments(int argc, char* argv[]) {
    cxxopts::Options options("ProgramName", "Program Help String");
    options.add_options()
        ("h,help", "Print usage")
        ("test-time", "how much time to run in seconds", cxxopts::value<int>()->default_value("300"))
        ("frames-to-skip", "Number before we do before changing the eis", cxxopts::value<int>()->default_value("30"))
        ("number-of-resets", "Number of reset we expect to use.", cxxopts::value<int>()->default_value("450"))
        ("run-over-day", "If set to true will enable/disable denoise hdr depeding on the day time.", cxxopts::value<bool>()->default_value("false"));
    
    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(0);
    }

    ParsedOptions parsedOptions;
    parsedOptions.no_change_frames = result["frames-to-skip"].as<int>();
    parsedOptions.test_time = result["test-time"].as<int>();
    parsedOptions.number_of_resets = result["number-of-resets"].as<int>();
    parsedOptions.run_over_day = result["run-over-day"].as<bool>();

    return parsedOptions;
}

void write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, std::ofstream &output_file)
{
    char *data = (char *)buffer->get_plane_ptr(0);
    if (!data)
    {
        std::cerr << "Error occurred at writing time!" << std::endl;
        return;
    }
    output_file.write(data, size);
}

void on_signal_callback(int signum)
{
    std::cout << "Stopping Pipeline..." << std::endl;
    m_media_lib->frontend->stop();
    for (const auto &entry : m_media_lib->encoders)
    {
        entry.second->stop();
    }

    for (auto &entry : m_media_lib->output_files)
    {
        entry.second.close();
    }

    exit(signum);
}


int create_frontend_object(std::shared_ptr<MediaLibrary> m_media_lib){
    std::string frontend_config_string = read_string_from_file(EIS_TEST_CONFIG_FILE);
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create();
    if (!frontend_expected.has_value()) {
        std::cout << "Failed to create frontend" << std::endl;
        return 1;
    }

    m_media_lib->frontend = frontend_expected.value();
    if (m_media_lib->frontend->set_config(frontend_config_string ) != MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to configure frontend" << std::endl;
        return 1;
    }
    return 0;
}

void setup_frontend() {
    std::string reference_config_str = read_string_from_file(FRONTEND_CONFIG_FILE);
    json frontend_config = json::parse(reference_config_str);

    // Set the next configuration as defaul
    // Dewrap off, FOV 1
    frontend_config["dewarp"]["enabled"] = false;
    frontend_config["dewarp"]["sensor_calib_path"] = "/home/root/apps/resources/cam_intrinsics.txt";
    // EIS on, gyro on
    frontend_config["eis"]["enabled"] = true;
    frontend_config["gyro"]["enabled"] = true;

    // Output 4k 15fps, fhd 15fps, hd 30 fps, sd 30 fps
    frontend_config["output_video"]["resolutions"] = {{{"width", resolutions_map["4k"].width},
                                                       {"height", resolutions_map["4k"].height},
                                                       {"framerate", resolutions_map["4k"].framerate},
                                                       {"pool_max_buffers", resolutions_map["4k"].pool_max_buffers}},
                                                       {{"width", resolutions_map["fhd"].width},
                                                       {"height", resolutions_map["fhd"].height},
                                                       {"framerate", resolutions_map["fhd"].framerate},
                                                       {"pool_max_buffers", resolutions_map["fhd"].pool_max_buffers}},
                                                       {{"width", resolutions_map["hd"].width},
                                                       {"height", resolutions_map["hd"].height},
                                                       {"framerate", resolutions_map["hd"].framerate},
                                                       {"pool_max_buffers", resolutions_map["hd"].pool_max_buffers}},
                                                       {{"width", resolutions_map["sd"].width},
                                                       {"height", resolutions_map["sd"].height},
                                                       {"framerate", resolutions_map["sd"].framerate},
                                                       {"pool_max_buffers", resolutions_map["sd"].pool_max_buffers}}};
    // Rotation 180
    frontend_config["flip"]["enabled"] = true;
    
    // Denoise on
    frontend_config["denoise"]["enabled"] = true;
    
    writeFileContent(EIS_TEST_CONFIG_FILE, frontend_config.dump());
}

int create_encoder_configuration_files(){
    // Copy frontend configuration file as reference
    try {
        std::filesystem::copy_file(ENCODER_OSD_CONFIG_FILE, BACKUP_ENCODER_CONFIG_FILE, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error copying file: " << e.what() << '\n';
        return 1;
    }

    std::string reference_encoder_config_str = read_string_from_file(BACKUP_ENCODER_CONFIG_FILE);
    json encoder_config = json::parse(reference_encoder_config_str);
    
    int index = 0;
    for (const auto& [key, resolution] : resolutions_map) {
        encoder_config["encoding"]["input_stream"]["width"] = resolution.width;
        encoder_config["encoding"]["input_stream"]["height"] = resolution.height;
        encoder_config["encoding"]["input_stream"]["framerate"] = resolution.framerate;

        writeFileContent(encoder_configurations[index], encoder_config.dump());
        ++index;
    }

    return 0;
}

int create_encoder_objects(std::shared_ptr<MediaLibrary> m_media_lib){
    auto streams = m_media_lib->frontend->get_outputs_streams();
    if (!streams.has_value()) {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }
    
    uint i = 0;
    for (auto s : streams.value()) {
        std::string encoder_config_string = read_string_from_file(encoder_configurations[i].c_str());
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create();
        if (!encoder_expected.has_value()) {
            std::cout << "Failed to create encoder" << std::endl;
            return 1;
        }
        m_media_lib->encoders[s.id] = encoder_expected.value();
        if (m_media_lib->encoders[s.id]->set_config(encoder_config_string) != MEDIA_LIBRARY_SUCCESS)
	{
            std::cout << "Failed to configure encoder osd" << std::endl;
            return 1;
	}

        std::string output_file_path = outputs_files[i];
        delete_output_file(output_file_path);
        m_media_lib->output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        if (!m_media_lib->output_files[s.id].good()) {
            std::cerr << "Error occurred while opening file" << std::endl;
            return 1;
        }
        std::cerr << "Created output file: " << s.id << std::endl;
        i++;
    }
    return 0;
}

void create_ai_pipelines(){
    m_media_lib->pipeline = std::make_shared<Pipeline>();
    std::shared_ptr<HailortAsyncStage> detection_stage = std::make_shared<HailortAsyncStage>(DETECTION_AI_STAGE, YOLO_HEF_FILE, 5, 50 ,"device0", 10, 20, 8, std::chrono::milliseconds(100), false);
    std::shared_ptr<PostprocessStage> detection_post_stage = std::make_shared<PostprocessStage>(POST_STAGE, YOLO_POST_SO, YOLO_FUNC_NAME, "", 5, false, false);
    std::shared_ptr<OverlayStage> overlay_stage = std::make_shared<OverlayStage>(OVERLAY_STAGE, 1, false, false);
    std::shared_ptr<CallbackStage> sink_stage = std::make_shared<CallbackStage>(AI_CALLBACK_STAGE, 1, false);
    m_media_lib->pipeline->add_stage(detection_stage);
    m_media_lib->pipeline->add_stage(detection_post_stage);
    m_media_lib->pipeline->add_stage(overlay_stage);
    m_media_lib->pipeline->add_stage(sink_stage);

    detection_stage->add_subscriber(detection_post_stage);
    detection_post_stage->add_subscriber(overlay_stage);
    overlay_stage->add_subscriber(sink_stage);
}


int setup() {
    int return_value = 0;
    // Create media lib pointer for tests
    m_media_lib = std::make_shared<MediaLibrary>();
    // Generate frontend configuration file
    setup_frontend();

    // Create frontend object
    return_value = create_frontend_object(m_media_lib);
    if (return_value != 0) {
        return return_value;
    }

    // Create hailort objects
    create_ai_pipelines();

    // Create encoder configuration files
    return_value = create_encoder_configuration_files();
    if (return_value != 0) {
        return return_value;
    }

    // Create encoder objects
    create_encoder_objects(m_media_lib);
    return 0;
}

void clean(bool g_pipeline_is_running, bool g_encoder_is_running, std::streambuf* originalBuffer) {

    if (g_pipeline_is_running) {
        std::cout << "Stopping" << std::endl;
        m_media_lib->frontend->stop();
        m_media_lib->frontend=NULL;
    }

    if (g_ai_set) {
        std::cout << "Stopping ai pipeline" << std::endl;
        m_media_lib->pipeline->stop_pipeline();
        m_media_lib->pipeline = NULL;
    }

    std::cout << "Stopping encoder" << std::endl;
    for (auto &entry : m_media_lib->encoders) {
        entry.second->stop();
    }

    std::cout << "Stopping file writing" << std::endl;
    for (auto &entry : m_media_lib->output_files) {
        entry.second.close();
    }

    std::cout.rdbuf(originalBuffer);
}

void subscribe_frontend(std::shared_ptr<MediaLibrary> m_media_lib, uint no_change_frames){
    auto streams = m_media_lib->frontend->get_outputs_streams();
    if (!streams.has_value() || streams->empty())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        return;
    }
    FrontendCallbacksMap fe_callbacks;
    for (const auto& s : *streams)
    {
        std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
        if(s.id == AI_SINK){
            m_media_lib->pipeline->get_stage_by_name(DETECTION_AI_STAGE)->add_queue(s.id);
            fe_callbacks[s.id] = [s, m_media_lib, no_change_frames](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                BufferPtr wrapped_buffer = std::make_shared<Buffer>(buffer);
                m_media_lib->pipeline->get_stage_by_name(DETECTION_AI_STAGE)->push(wrapped_buffer, s.id);
            };
        }
        else{
            fe_callbacks[s.id] = [s, m_media_lib, no_change_frames](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                g_total_frame_counter++;
                if ((g_total_frame_counter > 10) && ((g_total_frame_counter % no_change_frames) == 0) && g_pipeline_is_running)
                {
                    g_inside_frame_counter++;
                    
                    frontend_config_t config = m_media_lib->frontend->get_config().value();
                    config.ldc_config.eis_config.enabled = !config.ldc_config.eis_config.enabled;
                    
                    // Counter to update the gyro status
                    if (config.ldc_config.eis_config.enabled) {
                        g_disable_counter++;
                    }

                    // Counter to update the stabilize status
                    if (config.ldc_config.eis_config.enabled && config.ldc_config.gyro_config.enabled) {
                        g_stabilize_counter++;
                    }

                    // Change status of eis after doint the two permutations of eis on
                    if (g_disable_counter % 2 == 0){
                        config.ldc_config.eis_config.enabled = false;
                    }
                    else{
                        config.ldc_config.eis_config.enabled = true;
                    }

                    // Change status of gyro after doint the two permutations of eis on
                    if (g_disable_counter % 2 == 0){
                        config.ldc_config.gyro_config.enabled = false;
                    }
                    else{
                        config.ldc_config.gyro_config.enabled = true;
                    }

                    // Change status of gyro after doint the two permutations of eis on/off and gyro on/off
                    if (g_stabilize_counter % 2 == 0){
                        config.ldc_config.eis_config.stabilize = false;
                    }
                    else{
                        config.ldc_config.eis_config.stabilize = true;
                    }
                    if(m_media_lib->frontend->set_config(config) != MEDIA_LIBRARY_SUCCESS)
                    {
                        std::cout << "Error!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
                    }
                    std::cout << "Gyro status is: " << config.ldc_config.gyro_config.enabled << std::endl;
                    std::cout << "EIS status is: " << config.ldc_config.eis_config.enabled << std::endl;
                }
                m_media_lib->encoders[s.id]->add_buffer(buffer);
            };
        }
    }
    m_media_lib->frontend->subscribe(fe_callbacks);
}

void subscribe_elements(std::shared_ptr<MediaLibrary> m_media_lib, uint no_change_frames)
{
    subscribe_frontend(m_media_lib, no_change_frames);
    
    // Connect encoders to file writing
    for (const auto &[streamId, encoder] : m_media_lib->encoders)
    {
        std::cout << "subscribing to encoder for '" << streamId << "'" << std::endl;
        encoder->subscribe(
            [m_media_lib, streamId](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                auto config = m_media_lib->frontend->get_config().value();
                auto osd_blender = m_media_lib->encoders[streamId]->get_blender();
                if (!g_osd_is_set[streamId]){
                    osd::TextOverlay eis_overlay("eis_status", 0.7, 0.7, "EIS_ON!!!!!!!!!!!!!!!!!1", red_argb, blue_argb, 60.0f, 1, 1, font_path, 0, osd::rotation_alignment_policy_t::CENTER);
                    osd_blender->add_overlay(eis_overlay);
                    osd_blender->set_overlay_enabled("eis_status", true);
                    g_osd_is_set[streamId] = true;
                }

                if (config.ldc_config.eis_config.enabled != g_osd_eis_status || 
                    config.ldc_config.gyro_config.enabled != g_osd_gyro_status){
                    auto text_overlay_ptr = osd_blender->get_overlay("eis_status");
                    auto text_overlay = std::static_pointer_cast<osd::TextOverlay>(text_overlay_ptr.value());
                    
                    if (config.ldc_config.eis_config.enabled) {
                        text_overlay->label = "EIS_ON!!!!!!!!!!!!!!!!!1";
                    }
                    else{
                        text_overlay->label = "EIS_OFF!!!!!!!!!!!!!!!!!1";
                    }
                    if (config.ldc_config.gyro_config.enabled) {
                        text_overlay->label = text_overlay->label + "GYRO_ON!!!!!!!!!!!!!!!!!1";
                    }
                    else{
                        text_overlay->label = text_overlay->label + "GYRO_OFF!!!!!!!!!!!!!!!!!1";
                    }
                    if (config.ldc_config.eis_config.stabilize) {
                        text_overlay->label = text_overlay->label + "STABILIZE_ON!!!!!!!!!!!!!!!!!1";
                    }
                    else{
                        text_overlay->label = text_overlay->label + "STABILIZE_OFF!!!!!!!!!!!!!!!!!1";
                    }
                    osd_blender->set_overlay(*text_overlay);
                    g_osd_eis_status = config.ldc_config.eis_config.enabled;
                    g_osd_gyro_status = config.ldc_config.gyro_config.enabled;
                    }
                write_encoded_data(buffer, size, m_media_lib->output_files[streamId]);
            }
        );
    }
    
    // Subscribe ai stage to encoder
    std::cout << "subscribing ai pipeline to encoder " << std::endl;
    CallbackStagePtr ai_sink_stage = std::static_pointer_cast<CallbackStage>(m_media_lib->pipeline->get_stage_by_name(AI_CALLBACK_STAGE));
    ai_sink_stage->set_callback(
        [m_media_lib](BufferPtr data)
        {
            m_media_lib->encoders[AI_SINK]->add_buffer(data->get_buffer());
        });    
}

void check_if_enable_extra_features(){
    uint startHour = 8;
    uint endHour = 19;
    // Get current time
    std::time_t timestamp = time(NULL);
    std::tm* localTime = std::localtime(&timestamp);

    // Extract the hour
    uint currentHour = localTime->tm_hour;
    // Check if the current hour is between startHour and endHour
    if (startHour < currentHour && currentHour < endHour && g_denoise_enabled == false) {
        g_denoise_enabled = true;
        g_hdr_enabled = false;
        // Read the config file
        std::string reference_config_str = read_string_from_file(EIS_TEST_CONFIG_FILE);
        json frontend_config = json::parse(reference_config_str);

        // Enabled denoise during next run
        frontend_config["denoise"]["enabled"] = true;
        frontend_config["hdr"]["enabled"] = false;
        
        // Write the new config file
        writeFileContent(EIS_TEST_CONFIG_FILE, frontend_config.dump());

    } else {
        g_denoise_enabled = false;
        g_hdr_enabled = true;
        // Read the config file
        std::string reference_config_str = read_string_from_file(EIS_TEST_CONFIG_FILE);
        json frontend_config = json::parse(reference_config_str);

        // Enabled denoise during next run, enable once eis and HDR are working
        frontend_config["denoise"]["enabled"] = false;
        // frontend_config["hdr"]["enabled"] = true;
        
        // Write the new config file
        writeFileContent(EIS_TEST_CONFIG_FILE, frontend_config.dump());
    }
}

int main(int argc, char *argv[]){
    ParsedOptions options = parseArguments(argc, argv);

    int result = setup();
    int time_in_between_reset = int(options.test_time / options.number_of_resets);
    if (result != 0) {
        std::cout << "Failed to initialize test" << std::endl;
        return 1;
    }
    subscribe_elements(m_media_lib, options.no_change_frames);    

    std::cout << "Starting encoder and frontend" << std::endl;
    for (const auto &[streamId, encoder] : m_media_lib->encoders)
    {
        std::cout << "starting encoder" << streamId << std::endl;
        encoder->start();
    }
    g_encoder_is_running = true;
    m_media_lib->pipeline->start_pipeline();
    m_media_lib->frontend->start();
    g_pipeline_is_running = true;

    std::cout << "Running test for " << options.test_time <<    " seconds" << std::endl;
    for(int i=0; i<options.number_of_resets; i++){
        std::cout << "Running test for " << time_in_between_reset <<    " seconds then stopping and restarting pipeline" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(time_in_between_reset));
        if (options.run_over_day) {
            check_if_enable_extra_features();
        }
        
        std::cout << "Reseting frontent iteration " << i + 1 << std::endl;
        g_pipeline_is_running = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        m_media_lib->frontend->stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        m_media_lib->frontend=NULL;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        create_frontend_object(m_media_lib);
        subscribe_frontend(m_media_lib, options.no_change_frames);
        m_media_lib->frontend->start();
        g_pipeline_is_running = true;
    }

    clean(g_pipeline_is_running, g_encoder_is_running, std::cout.rdbuf());
}
