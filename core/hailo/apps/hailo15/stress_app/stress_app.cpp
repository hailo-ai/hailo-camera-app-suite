#include <iostream>
#include <thread>
#include <sstream>
#include <tl/expected.hpp>
#include <signal.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <fstream>
#include <vector>
#include <utility> 
#include <cxxopts/cxxopts.hpp>
#include <filesystem> 
#include "apps_common.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "common.hpp"
#include "scenarios.hpp"

// ai includes
#include "pipeline_infra/pipeline.hpp"
#include "pipeline_infra/ai_stage.hpp"
#include "pipeline_infra/dsp_stages.hpp"
#include "pipeline_infra/postprocess_stage.hpp"
#include "pipeline_infra/tracker_stage.hpp"
#include "pipeline_infra/overlay_stage.hpp"
#include "pipeline_infra/aggregator_stage.hpp"

#define OVERLAY_STAGE "OverLay"
#define TRACKER_STAGE "Tracker"

#define STRESS_CONFIGS_PATH "/home/root/apps/internals/validation_apps_configs/resources/configs/"
#define FRONTEND_CONFIG_FILE STRESS_CONFIGS_PATH "stress_frontend_config.json"
#define BACKUP_FRONTEND_CONFIG_FILE "/tmp/frontend_config_example.json"

// AI Pipeline Params
#define AI_VISION_SINK "sink0" // The streamid from frontend to 4K stream that shows vision results 
#define AI_SINK "sink3" // The streamid from frontend to AI
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

std::string g_encoder_config_file_path = std::string(STRESS_CONFIGS_PATH) + "stress_encoder_sink";
std::string g_output_file_path = "/home/root/apps/stress_app/stress_video";
std::streambuf* originalBuffer = std::cout.rdbuf(); 

static bool g_pipeline_is_running = false;

struct MediaLibrary
{
    MediaLibraryFrontendPtr frontend;
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders;
    std::map<output_stream_id_t, std::ofstream> output_files;
    PipelinePtr pipeline;
};

std::shared_ptr<MediaLibrary> m_media_lib;

struct ParsedOptions {
    int test_time;
    bool ai_pipeline_enabled;
    bool leaky_ai_queues;
    int frontend_resets;
};

ParsedOptions parseArguments(int argc, char* argv[]) {
    cxxopts::Options options("ProgramName", "Program Help String");
    options.add_options()
        ("h,help", "Print usage")
        ("test-time", "how much time to run 1 iteration, time is in seconds", cxxopts::value<int>()->default_value("300"))
        ("ai-pipeline", "will the ai pipeline be added to the overall pipeline", cxxopts::value<std::string>()->default_value("true"))
        ("leaky-ai-queues", "turn the ai queues to leaky", cxxopts::value<bool>()->default_value("false"))
        ("num-of-resets", "number of frontend during the test", cxxopts::value<int>()->default_value("0"));
    
    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(0);
    }

    ParsedOptions parsedOptions;
    parsedOptions.test_time = result["test-time"].as<int>();
    std::string ai_pipeline_str = result["ai-pipeline"].as<std::string>();
    parsedOptions.ai_pipeline_enabled = (ai_pipeline_str == "true" || ai_pipeline_str == "1");
    parsedOptions.leaky_ai_queues = result["leaky-ai-queues"].as<bool>();
    parsedOptions.frontend_resets = result["num-of-resets"].as<int>();
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

void create_pipeline(std::shared_ptr<MediaLibrary> m_media_lib, bool leaky_ai_queues)
{
    // Create pipeline
    m_media_lib->pipeline = std::make_shared<Pipeline>();

    // Cropping the image into 5 tiles- 1 of the full image and 4 of the image divided into 4 tiles
    std::shared_ptr<TillingCropStage> tilling_stage = std::make_shared<TillingCropStage>(TILLING_STAGE,40, TILLING_INPUT_WIDTH, TILLING_INPUT_HEIGHT,
                                                                                        TILLING_OUTPUT_WIDTH, TILLING_OUTPUT_HEIGHT,
                                                                                        "", DETECTION_AI_STAGE, TILES,
                                                                                        5, leaky_ai_queues, false);
    // Detection AI stage which put squares around faces and people
    std::shared_ptr<HailortAsyncStage> detection_stage = std::make_shared<HailortAsyncStage>(DETECTION_AI_STAGE, YOLO_HEF_FILE, 5, 50 ,"device0", 10, 20, 8, std::chrono::milliseconds(100), false);
    //
    std::shared_ptr<PostprocessStage> detection_post_stage = std::make_shared<PostprocessStage>(POST_STAGE, YOLO_POST_SO, YOLO_FUNC_NAME, "", 5, leaky_ai_queues, false);
    // adds the squares found in detection stange to the 4k stream
    std::shared_ptr<AggregatorStage> agg_stage = std::make_shared<AggregatorStage>(AGGREGATOR_STAGE, false, 
                                                                                   AI_VISION_SINK, 2, 
                                                                                   POST_STAGE, 10, 
                                                                                   true, 0.3, 0.1,
                                                                                   leaky_ai_queues, false);
    // Tracker stage which tracks the squares around the people and faces in case there is movement (using ids)
    std::shared_ptr<TrackerStage> tracker_stage = std::make_shared<TrackerStage>(TRACKER_STAGE, 1, leaky_ai_queues, -1, false);
    //crops the image in the right size for the next stage input resolution
    std::shared_ptr<BBoxCropStage> bbox_crop_stage = std::make_shared<BBoxCropStage>(BBOX_CROP_STAGE, 100, BBOX_CROP_INPUT_WIDTH, BBOX_CROP_INPUT_HEIGHT,
                                                                                    BBOX_CROP_OUTPUT_WIDTH, BBOX_CROP_OUTPUT_HEIGHT,
                                                                                    AGGREGATOR_STAGE_2, LANDMARKS_AI_STAGE, BBOX_CROP_LABEL, 1, leaky_ai_queues, false);
    std::shared_ptr<HailortAsyncStage> landmarks_stage = std::make_shared<HailortAsyncStage>(LANDMARKS_AI_STAGE, LANDMARKS_HEF_FILE, 20, 101 ,"device0", 1, 30, 1, std::chrono::milliseconds(100), false);
    // filters the boxes that are not intesting. The detection stage finds many squares but some of them should be filtered out
    std::shared_ptr<PostprocessStage> landmarks_post_stage = std::make_shared<PostprocessStage>(LANDMARKS_POST_STAGE, LANDMARKS_POST_SO, LANDMARKS_FUNC_NAME, "", 50, leaky_ai_queues, false);
    // similar to agg_stage but for the 4k stream
    std::shared_ptr<AggregatorStage> agg_stage_2 = std::make_shared<AggregatorStage>(AGGREGATOR_STAGE_2, false, 
                                                                                     BBOX_CROP_STAGE, 2, 
                                                                                     LANDMARKS_POST_STAGE, 30,
                                                                                     false, 0.3, 0.1,
                                                                                     true, false);
    // adds the squares found in detection stange to the 4k stream
    std::shared_ptr<OverlayStage> overlay_stage = std::make_shared<OverlayStage>(OVERLAY_STAGE, 1, true, false);
    // will be used as a callback for the next frame
    std::shared_ptr<CallbackStage> sink_stage = std::make_shared<CallbackStage>(AI_CALLBACK_STAGE, 1, false);
    
    // Add stages to pipeline
    m_media_lib->pipeline->add_stage(tilling_stage);
    m_media_lib->pipeline->add_stage(detection_stage);
    m_media_lib->pipeline->add_stage(detection_post_stage);
    m_media_lib->pipeline->add_stage(agg_stage);
    m_media_lib->pipeline->add_stage(tracker_stage);
    m_media_lib->pipeline->add_stage(bbox_crop_stage);
    m_media_lib->pipeline->add_stage(agg_stage_2);
    m_media_lib->pipeline->add_stage(overlay_stage);
    m_media_lib->pipeline->add_stage(sink_stage);
    m_media_lib->pipeline->add_stage(landmarks_stage);
    m_media_lib->pipeline->add_stage(landmarks_post_stage);

    // Subscribe stages to each other
    tilling_stage->add_subscriber(detection_stage);
    detection_stage->add_subscriber(detection_post_stage);
    detection_post_stage->add_subscriber(agg_stage);
    agg_stage->add_subscriber(tracker_stage);
    tracker_stage->add_subscriber(bbox_crop_stage);
    bbox_crop_stage->add_subscriber(agg_stage_2);
    bbox_crop_stage->add_subscriber(landmarks_stage);
    landmarks_stage->add_subscriber(landmarks_post_stage);
    landmarks_post_stage->add_subscriber(agg_stage_2);
    agg_stage_2->add_subscriber(overlay_stage);
    overlay_stage->add_subscriber(sink_stage);
}

void subscribe_elements(std::shared_ptr<MediaLibrary> media_lib, bool ai_pipeline_enabled)
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
            fe_callbacks[s.id] = [s, media_lib](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                media_lib->encoders[s.id]->add_buffer(buffer);
            };
        }
    }
    media_lib->frontend->subscribe(fe_callbacks);

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

std::string get_output_paths(int id)
{
    return g_output_file_path + std::to_string(id) + ".h264";
}

std::string get_encoder_config(int id)
{
    return g_encoder_config_file_path + std::to_string(id) + ".json";
}

int setup(std::shared_ptr<MediaLibrary> media_lib, ParsedOptions options) {
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
        std::string encoder_config_path = get_encoder_config(encoder_index);
        std::string encoderosd_config_string = read_string_from_file(encoder_config_path.c_str());
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create(s.id);
        if (!encoder_expected.has_value()) {
            std::cout << "Failed to create encoder osd" << std::endl;
            return 1;
        }
        m_media_lib->encoders[s.id] = encoder_expected.value();
        if (m_media_lib->encoders[s.id]->set_config(encoderosd_config_string) != MEDIA_LIBRARY_SUCCESS)
	{
            std::cout << "Failed to configure encoder osd" << std::endl;
            return 1;
	}

        std::string output_file_path = get_output_paths(encoder_index);
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

void clean(bool g_pipeline_is_running, const std::string& backup_config_file, std::streambuf* originalBuffer,
           bool ai_pipeline_enabled) {
    std::cout << "Stopping" << std::endl;
    m_media_lib->frontend->stop();
    if (ai_pipeline_enabled)
        m_media_lib->pipeline->stop_pipeline();

        
    for (const auto &entry : m_media_lib->encoders)
    {
        entry.second->stop();
    }

    // close all file in m_media_lib->output_files
    for (auto &entry : m_media_lib->output_files)
    {
        entry.second.close();
    }
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
    
    if (options.ai_pipeline_enabled)
        create_pipeline(m_media_lib, options.leaky_ai_queues);
    subscribe_elements(m_media_lib, options.ai_pipeline_enabled);

    std::cout << "Starting encoder and frontend" << std::endl;
    for (const auto &entry : m_media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "starting encoder for " << streamId << std::endl;
        encoder->start();
    }
    if (options.ai_pipeline_enabled)
        m_media_lib->pipeline->start_pipeline();
    m_media_lib->frontend->start();
    g_pipeline_is_running = true;
    if (options.frontend_resets > 0)
    {
        int interval = options.test_time / options.frontend_resets;
        for (int i = 0; i < options.frontend_resets; i++)
        {
            std::this_thread::sleep_for(std::chrono::seconds(interval));
            m_media_lib->frontend->stop();
            m_media_lib->frontend->start();
        }
    }
    else
        std::this_thread::sleep_for(std::chrono::seconds(options.test_time));

    clean(g_pipeline_is_running, BACKUP_FRONTEND_CONFIG_FILE, originalBuffer, options.ai_pipeline_enabled);
    return 0;
}
