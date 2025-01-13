#include <iostream>
#include <thread>
#include <sstream>
#include <tl/expected.hpp>
#include <signal.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <fstream>
#include <cxxopts/cxxopts.hpp>
#include <filesystem> 
#include <vector>
#include <utility> 
#include "apps_common.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "utils/vision_config_changes.hpp"
#include "utils/common.hpp"
#include "utils/scenarios.hpp"

// AI Pipeline Params
#define AI_VISION_SINK "sink0" // The streamid from frontend to 4K stream that shows vision results 
#define AI_SINK "sink3" // The streamid from frontend to AI

// ai includes
#include "pipeline_infra/pipeline.hpp"
#include "pipeline_infra/ai_stage.hpp"
#include "pipeline_infra/dsp_stages.hpp"
#include "pipeline_infra/postprocess_stage.hpp"
#include "pipeline_infra/overlay_stage.hpp"
#include "pipeline_infra/udp_stage.hpp"
#include "pipeline_infra/tracker_stage.hpp"
#include "pipeline_infra/aggregator_stage.hpp"

#define OVERLAY_STAGE "OverLay"
#define TRACKER_STAGE "Tracker"
// Detection AI Params
#define YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/yolov5s_personface_nv12.hef"
#define DETECTION_AI_STAGE "yolo_detection"
// Detection Postprocess Params
#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
#define YOLO_FUNC_NAME "yolov5s_personface"
// Aggregator Params
#define AGGREGATOR_STAGE "aggregator"
#define AGGREGATOR_STAGE_2 "aggregator2"
// Callback Params
#define AI_CALLBACK_STAGE "ai_to_encoder"

// Tilling Params
#define TILLING_STAGE "tilling"
#define TILLING_INPUT_WIDTH 1920
#define TILLING_INPUT_HEIGHT 1080
#define TILLING_OUTPUT_WIDTH 640
#define TILLING_OUTPUT_HEIGHT 640
std::vector<HailoBBox> TILES = {{0.0,0.0,0.6,0.6},  {0.4,0,0.6,0.6},  
                                {0, 0.4, 0.6, 0.6},  {0.4, 0.4, 0.6, 0.6}, 
                                {0.0, 0.0, 1.0, 1.0}};

// Bbox crop Parms
#define BBOX_CROP_STAGE "bbox_crops"
#define BBOX_CROP_LABEL "face"
#define BBOX_CROP_INPUT_WIDTH 3840
#define BBOX_CROP_INPUT_HEIGHT 2160
#define BBOX_CROP_OUTPUT_WIDTH 120
#define BBOX_CROP_OUTPUT_HEIGHT 120

// Landmarks AI Params
#define LANDMARKS_HEF_FILE "/home/root/apps/ai_example_app/resources/tddfa_mobilenet_v1_nv12.hef"
#define LANDMARKS_AI_STAGE "face_landmarks"
// Landmarks Postprocess Params
#define LANDMARKS_POST_STAGE "landmarks_post"
#define LANDMARKS_POST_SO "/usr/lib/hailo-post-processes/libfacial_landmarks_post.so"
#define LANDMARKS_FUNC_NAME "facial_landmarks_nv12"

#define STRESS_CONFIGS_PATH "/home/root/apps/internals/validation_apps_configs/resources/configs/"
#define FRONTEND_CONFIG_FILE STRESS_CONFIGS_PATH "stress_frontend_config.json"
#define BACKUP_FRONTEND_CONFIG_FILE "/tmp/frontend_config_example.json"

std::string g_encoder_config_file_path = std::string(STRESS_CONFIGS_PATH) + "stress_encoder_sink";
std::string g_output_file_path = "/var/volatile/tmp/chaos_out_video";
std::streambuf* originalBuffer = std::cout.rdbuf();

static bool g_pipeline_is_running = false;
static bool g_hdr_enabled = false;

std::vector<std::pair<int, int>> output_resolution;

struct MediaLibrary
{
    MediaLibraryFrontendPtr frontend;
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders;
    std::map<output_stream_id_t, bool> encoder_is_running;
    std::map<output_stream_id_t, std::ofstream> output_files;
    std::map<output_stream_id_t, std::string> encoder_file_path;
    std::map<output_stream_id_t, int> frame_counter_per_stream;
    PipelinePtr pipeline;
};
std::shared_ptr<MediaLibrary> m_media_lib;

struct ParsedOptions {
    int no_change_frames;
    int loop_test;
    int test_time;
    int number_of_frontend_restarts;
    std::string encoding_format;
    bool disable_90_rotate;
    bool ai_pipeline_enabled;
};

ParsedOptions parseArguments(int argc, char* argv[]) {
    cxxopts::Options options("ProgramName", "Program Help String");
    options.add_options()
        ("h,help", "Print usage")
        ("test-time", "how much time to run 1 iteration, time is in seconds", cxxopts::value<int>()->default_value("300"))
        ("loop-test", "how many iterations of the test to run", cxxopts::value<int>()->default_value("1"))
        ("frames-to-skip", "Number of frames that the pipeline will not make dynamic changes between each change", cxxopts::value<int>()->default_value("10"))
        ("number-of-resets", "Number of frontend resets and HDR flips", cxxopts::value<int>()->default_value("4"))
        ("encoding-format", "Encoding format", cxxopts::value<std::string>()->default_value("h264"))
        ("disable-rotate-90", "Disabling rotate 90, the output file will be valid this way", cxxopts::value<std::string>()->default_value("false"))
        ("ai-pipeline", "will the ai pipeline be added to the overall pipeline", cxxopts::value<std::string>()->default_value("true"));
    
    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(0);
    }

    ParsedOptions parsedOptions;
    parsedOptions.no_change_frames = result["frames-to-skip"].as<int>();
    parsedOptions.loop_test = result["loop-test"].as<int>();
    parsedOptions.test_time = result["test-time"].as<int>();
    parsedOptions.number_of_frontend_restarts = result["number-of-resets"].as<int>();
    parsedOptions.encoding_format = result["encoding-format"].as<std::string>();
    std::string disable_90_rotate = result["disable-rotate-90"].as<std::string>();
    std::string ai_pipeline_str = result["ai-pipeline"].as<std::string>();
    parsedOptions.ai_pipeline_enabled = (ai_pipeline_str == "true" || ai_pipeline_str == "1");

    return parsedOptions;
}

std::string get_output_paths(int id, ParsedOptions options)
{
    if ((options.encoding_format == "mjpeg") && (id == 1)) {
        return g_output_file_path + std::to_string(id) + ".jpeg";
      return g_output_file_path + std::to_string(id) + ".jpegenc";
    }
    else if ((options.encoding_format == "h264") or (id != 1)) {
        return g_output_file_path + std::to_string(id) + ".h264";
    }
    else {
        std::cerr << "Invalid encoding format" << std::endl;
        return "";
    }
}

std::string set_encoder_config_path(int id, ParsedOptions options)
{
    if ((options.encoding_format == "mjpeg") && (id == 1))
        return g_encoder_config_file_path + std::to_string(id) + "_jpeg.json";
    else if ((options.encoding_format == "h264") or (id != 1))
        return g_encoder_config_file_path + std::to_string(id) + ".json";
    else 
        std::cerr << "Invalid encoding format" << std::endl;  
    return "";
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

void create_ai_pipeline(std::shared_ptr<MediaLibrary> m_media_lib)
{
    // Create pipeline
    m_media_lib->pipeline = std::make_shared<Pipeline>();

    // Create pipeline stages
    std::shared_ptr<TillingCropStage> tilling_stage = std::make_shared<TillingCropStage>(TILLING_STAGE,40, TILLING_INPUT_WIDTH, TILLING_INPUT_HEIGHT,
                                                                                        TILLING_OUTPUT_WIDTH, TILLING_OUTPUT_HEIGHT,
                                                                                        "", DETECTION_AI_STAGE, TILES,
                                                                                        5, false, false);
    std::shared_ptr<HailortAsyncStage> detection_stage = std::make_shared<HailortAsyncStage>(DETECTION_AI_STAGE, YOLO_HEF_FILE, 5, 50 ,"device0", 5, 10, 5, 
                                                                                             std::chrono::milliseconds(100), false, StagePoolMode::BLOCKING);
    std::shared_ptr<PostprocessStage> detection_post_stage = std::make_shared<PostprocessStage>(POST_STAGE, YOLO_POST_SO, YOLO_FUNC_NAME, "", 5, false, false);
    std::shared_ptr<AggregatorStage> agg_stage = std::make_shared<AggregatorStage>(AGGREGATOR_STAGE, false, 
                                                                                   AI_VISION_SINK, 2, 
                                                                                   POST_STAGE, 10, 
                                                                                   true, 0.3, 0.1,
                                                                                   false, false);
    std::shared_ptr<TrackerStage> tracker_stage = std::make_shared<TrackerStage>(TRACKER_STAGE, 1, false, -1, false);
    std::shared_ptr<BBoxCropStage> bbox_crop_stage = std::make_shared<BBoxCropStage>(BBOX_CROP_STAGE, 100, BBOX_CROP_INPUT_WIDTH, BBOX_CROP_INPUT_HEIGHT,
                                                                                    BBOX_CROP_OUTPUT_WIDTH, BBOX_CROP_OUTPUT_HEIGHT,
                                                                                    AGGREGATOR_STAGE_2, LANDMARKS_AI_STAGE, BBOX_CROP_LABEL, 1, false, false);
    std::shared_ptr<HailortAsyncStage> landmarks_stage = std::make_shared<HailortAsyncStage>(LANDMARKS_AI_STAGE, LANDMARKS_HEF_FILE, 20, 101 ,"device0", 1, 30, 1, 
                                                                                             std::chrono::milliseconds(100), false, StagePoolMode::BLOCKING);
    std::shared_ptr<PostprocessStage> landmarks_post_stage = std::make_shared<PostprocessStage>(LANDMARKS_POST_STAGE, LANDMARKS_POST_SO, LANDMARKS_FUNC_NAME, "", 50, false, false);
    std::shared_ptr<AggregatorStage> agg_stage_2 = std::make_shared<AggregatorStage>(AGGREGATOR_STAGE_2, false, 
                                                                                     BBOX_CROP_STAGE, 2, 
                                                                                     LANDMARKS_POST_STAGE, 30,
                                                                                     false, 0.3, 0.1,
                                                                                     false, false);
    std::shared_ptr<OverlayStage> overlay_stage = std::make_shared<OverlayStage>(OVERLAY_STAGE, 1, false, false);
    std::shared_ptr<CallbackStage> sink_stage = std::make_shared<CallbackStage>(AI_CALLBACK_STAGE, 1, false);

    // Add stages to pipeline
    m_media_lib->pipeline->add_stage(tilling_stage);
    m_media_lib->pipeline->add_stage(detection_stage);
    m_media_lib->pipeline->add_stage(detection_post_stage);
    m_media_lib->pipeline->add_stage(agg_stage);
    m_media_lib->pipeline->add_stage(tracker_stage);
    m_media_lib->pipeline->add_stage(bbox_crop_stage);
    m_media_lib->pipeline->add_stage(agg_stage_2);
    m_media_lib->pipeline->add_stage(sink_stage);
    m_media_lib->pipeline->add_stage(landmarks_stage);
    m_media_lib->pipeline->add_stage(landmarks_post_stage);
    m_media_lib->pipeline->add_stage(overlay_stage);

    // Subscribe stages to each other
    tilling_stage->add_subscriber(detection_stage);
    detection_stage->add_subscriber(detection_post_stage);
    agg_stage->add_subscriber(tracker_stage);
    tracker_stage->add_subscriber(bbox_crop_stage);
    bbox_crop_stage->add_subscriber(agg_stage_2);
    bbox_crop_stage->add_subscriber(landmarks_stage);
    landmarks_stage->add_subscriber(landmarks_post_stage);
    landmarks_post_stage->add_subscriber(agg_stage_2);
    agg_stage_2->add_subscriber(overlay_stage);
    overlay_stage->add_subscriber(sink_stage);
}

void subscribe_elements(std::shared_ptr<MediaLibrary> media_lib, uint no_change_frames, bool ai_pipeline_enabled)
{
    auto streams = media_lib->frontend->get_outputs_streams();
    if (!streams.has_value() || streams->empty())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        return;
    }

    FrontendCallbacksMap fe_callbacks;

    for (auto s : streams.value())
    {
        if ((s.id == AI_SINK) && (ai_pipeline_enabled))
        {
            std::cout << "subscribing ai pipeline to frontend for '" << s.id << "'" << std::endl;
            media_lib->pipeline->get_stage_by_name(TILLING_STAGE)->add_queue(s.id);
            fe_callbacks[s.id] = [s, media_lib](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                BufferPtr wrapped_buffer = std::make_shared<Buffer>(buffer);
                media_lib->pipeline->get_stage_by_name(TILLING_STAGE)->push(wrapped_buffer, s.id);
            };
        }
        else if ((s.id == AI_VISION_SINK) && (ai_pipeline_enabled))
        {
            std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
            ConnectedStagePtr agg_stage = std::static_pointer_cast<ConnectedStage>(media_lib->pipeline->get_stage_by_name(AGGREGATOR_STAGE));
            fe_callbacks[s.id] = [s, media_lib, agg_stage](HailoMediaLibraryBufferPtr buffer, size_t size)
            {        
                BufferPtr wrapped_buffer = std::make_shared<Buffer>(buffer);
                CroppingMetadataPtr cropping_meta = std::make_shared<CroppingMetadata>(TILES.size());
                wrapped_buffer->add_metadata(cropping_meta);
                agg_stage->push(wrapped_buffer, s.id);
            };
        }
        else
        {
            fe_callbacks[s.id] = [s, media_lib, no_change_frames](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                m_media_lib->frame_counter_per_stream[s.id]++;
                // Skip the first 100 frames so isp will more or less stabilize and checking that it's a frame that we want to make changes in
                if ((m_media_lib->frame_counter_per_stream[s.id] > 100) && ((m_media_lib->frame_counter_per_stream[s.id] % no_change_frames) == 0))
                {
                    int frame_change_number = (m_media_lib->frame_counter_per_stream[s.id] - 100)/no_change_frames;
                    std::cout << "Frame number: " << frame_change_number << " of " << s.id << std::endl;
                    OSD_scenario(frame_change_number, m_media_lib->encoders[s.id], s.id);       
                    encoder_scenario(frame_change_number, media_lib->encoders[s.id], media_lib->encoder_is_running[s.id],read_string_from_file(media_lib->encoder_file_path[s.id].c_str()));
                  //  only one sink will change vision scenarios and the privacy mask for all sinks
                    if (s.id == "sink1")
                    {  
                        privacy_mask_scenario(frame_change_number, media_lib->frontend);
                        /// TODO: MSW-7367 - App hangs when running with 2+ streams and setting config
                        //vision_scenario(frame_change_number, media_lib->frontend); 
                    }
                }
                
                media_lib->encoders[s.id]->add_buffer(buffer);
            };
            media_lib->frontend->subscribe(fe_callbacks);
        }
    }

    // Subscribe to encoders
    for (const auto &entry : media_lib->encoders)
    {
        if ((entry.first == AI_SINK) && (ai_pipeline_enabled))
        {
            // AI pipeline does not get an encoder since it is merged into 4K
            continue;
        }

        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "subscribing output file to encoder for '" << streamId << "'" << std::endl;
        media_lib->encoders[streamId]->subscribe(
            [media_lib, streamId](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                write_encoded_data(buffer, size, media_lib->output_files[streamId]);
            });
    }
    if (ai_pipeline_enabled)
    {
        // Subscribe ai stage to encoder
        std::cout << "subscribing ai pipeline to encoder '" << AI_VISION_SINK << "'" << std::endl;
        CallbackStagePtr ai_sink_stage = std::static_pointer_cast<CallbackStage>(media_lib->pipeline->get_stage_by_name(AI_CALLBACK_STAGE));
        ai_sink_stage->set_callback(
            [media_lib](BufferPtr data)
            {
                media_lib->encoders[AI_VISION_SINK]->add_buffer(data->get_buffer());
            });
    }
}

int setup(std::shared_ptr<MediaLibrary> media_lib, ParsedOptions options) {
    try {
        std::filesystem::copy_file(FRONTEND_CONFIG_FILE, BACKUP_FRONTEND_CONFIG_FILE, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error copying file: " << e.what() << '\n';
        return 1;
    }

    //init_vision_config_file(FRONTEND_CONFIG_FILE);
    std::string frontend_config_string = read_string_from_file(FRONTEND_CONFIG_FILE);
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

    auto streams = m_media_lib->frontend->get_outputs_streams();
    if (!streams.has_value()) {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    int encoder_index = 0;
    for (auto s : streams.value()) {
        if (options.ai_pipeline_enabled && (s.id == AI_SINK))
        {
            // AI pipeline does not get an encoder since it is merged into 4K
            continue;
        }
        std::cout << "Creating encoder enc_" << s.id << std::endl;
        media_lib->encoder_file_path[s.id] = set_encoder_config_path(encoder_index, options);
        std::string encoderosd_config_string = read_string_from_file(media_lib->encoder_file_path[s.id].c_str());
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create();
        if (!encoder_expected.has_value()) {
            std::cout << "Failed to create encoder osd" << std::endl;
            return 1;
        }
        m_media_lib->encoders[s.id] = encoder_expected.value();
        std::string output_file_path = get_output_paths(encoder_index, options);
        delete_output_file(output_file_path);
        m_media_lib->output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        if (!m_media_lib->output_files[s.id].good()) {
            std::cerr << "Error occurred at writing time!" << std::endl;
            return 1;
        }
        encoder_index++;
    }
    return 0;
}

void start_or_stop_all_encoders(std::shared_ptr<MediaLibrary> media_lib, bool start = true)
{
    for (const auto &entry : m_media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        const MediaLibraryEncoderPtr &encoder = entry.second;
        if ((start) and (!m_media_lib->encoder_is_running[streamId]))
        {
            std::cout << "starting encoder for " << streamId << std::endl;
            encoder->set_config(read_string_from_file(media_lib->encoder_file_path[streamId].c_str()));
            encoder->start();
        }
        else if ((!start) and (m_media_lib->encoder_is_running[streamId]))
        {
            std::cout << "stopping encoder for " << streamId << std::endl;
            encoder->stop();
        }
        m_media_lib->encoder_is_running[streamId] = true;
    }
}

void clean(bool g_pipeline_is_running, const std::string& backup_config_file, std::streambuf* originalBuffer) {
    if (g_pipeline_is_running) {
        std::cout << "Stopping" << std::endl;
        m_media_lib->frontend->stop();
    }
    start_or_stop_all_encoders(m_media_lib, false);

    for (auto &entry : m_media_lib->output_files) {
        entry.second.close();
    }

    try {
        std::filesystem::copy_file(backup_config_file, FRONTEND_CONFIG_FILE, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error restoring file: " << e.what() << '\n';
    }

    std::cout.rdbuf(originalBuffer);
}

int main(int argc, char *argv[])
{
    ParsedOptions options = parseArguments(argc, argv);
    m_media_lib = std::make_shared<MediaLibrary>();
    int result = setup(m_media_lib, options);
    if (result != 0) {
        std::cout << "Failed to initialize test" << std::endl;
        return 1;
    }

    //output resolutions to switch every frontend restart
    //for (const auto& entry : resolutionMap) {
    //    output_resolution.push_back(entry.second);
   // }
    if (options.ai_pipeline_enabled)
        create_ai_pipeline(m_media_lib);
    subscribe_elements(m_media_lib, options.no_change_frames, options.ai_pipeline_enabled);

    std::cout << "Starting encoders and frontend" << std::endl;
    start_or_stop_all_encoders(m_media_lib);
    m_media_lib->frontend->start();
    g_pipeline_is_running = true;
    if (options.ai_pipeline_enabled)
        m_media_lib->pipeline->start_pipeline();
    for(int i = 0; i < options.loop_test; i++)
    {
        std::cout << "Running test iteration " << i + 1 << std::endl;
        if (options.number_of_frontend_restarts == 0)
            std::this_thread::sleep_for(std::chrono::seconds(options.test_time));
        else {
            for (int j = 0; j < options.number_of_frontend_restarts; j++) {
                std::cout << "Stopping frontend for 1 second" << std::endl;
                g_pipeline_is_running = false;
                m_media_lib->frontend->stop();
                std::this_thread::sleep_for(std::chrono::seconds(1));
                start_or_stop_all_encoders(m_media_lib, false);
                if (options.disable_90_rotate) {
                    if (j % 3 == 1) {
                        rotate_90(true, m_media_lib->encoder_file_path , FRONTEND_CONFIG_FILE);
                        rotate_output_resolutions(FRONTEND_CONFIG_FILE);
                    }
                    else if (j % 3 == 2) {
                        rotate_90(false, m_media_lib->encoder_file_path, FRONTEND_CONFIG_FILE);
                        rotate_output_resolutions(FRONTEND_CONFIG_FILE);
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
                start_or_stop_all_encoders(m_media_lib);
                change_hdr_status(g_hdr_enabled, FRONTEND_CONFIG_FILE);
                std::cout << "Starting frontend" << std::endl;
                g_pipeline_is_running = true;
                m_media_lib->frontend->start();
                std::this_thread::sleep_for(std::chrono::seconds(options.test_time/options.number_of_frontend_restarts));
            }
        }
    }

    clean(g_pipeline_is_running, BACKUP_FRONTEND_CONFIG_FILE, originalBuffer);
    return 0;
}
