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

#define FRONTEND_CONFIG_FILE "/usr/bin/frontend_config_example.json"
#define BACKUP_FRONTEND_CONFIG_FILE "/tmp/frontend_config_example.json"
#define BACKUP_ENCODER_CONFIG_FILE "/tmp/encoder_config_example.json"

std::string g_encoder_config_file_path = "/usr/bin/frontend_encoder";
std::string g_output_file_path = "/var/volatile/tmp/chaos_out_video";
std::streambuf* originalBuffer = std::cout.rdbuf(); 
static uint g_total_frame_counter = 0;
static uint g_inside_frame_counter = 0;

static bool g_encoder_is_running = false;
static bool g_pipeline_is_running = false;
static bool g_hdr_enabled = false;

std::vector<std::pair<int, int>> output_resolution;

struct MediaLibrary
{
    MediaLibraryFrontendPtr frontend;
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders;
    std::map<output_stream_id_t, std::ofstream> output_files;
};
std::shared_ptr<MediaLibrary> m_media_lib;

struct ParsedOptions {
    int no_change_frames;
    int loop_test;
    int test_time;
    int number_of_frontend_restarts;
    std::string encoding_format;
};

ParsedOptions parseArguments(int argc, char* argv[]) {
    cxxopts::Options options("ProgramName", "Program Help String");
    options.add_options()
        ("h,help", "Print usage")
        ("test-time", "how much time to run 1 iteration, time is in seconds", cxxopts::value<int>()->default_value("300"))
        ("loop-test", "how many iterations of the test to run", cxxopts::value<int>()->default_value("1"))
        ("frames-to-skip", "Number of frames that the pipeline will not make dynamic changes between each change", cxxopts::value<int>()->default_value("10"))
        ("number-of-resets", "Number of frontend resets and HDR flips", cxxopts::value<int>()->default_value("4"))
        ("encoding-format", "Encoding format", cxxopts::value<std::string>()->default_value("h264"));
    
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

void subscribe_elements(std::shared_ptr<MediaLibrary> media_lib, uint no_change_frames)
{
    auto streams = media_lib->frontend->get_outputs_streams();
    if (!streams.has_value() || streams->empty())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        return;
    }

    const auto& s = streams->front();
    FrontendCallbacksMap fe_callbacks;
    fe_callbacks[s.id] = [s, media_lib, no_change_frames](HailoMediaLibraryBufferPtr buffer, size_t size)
    {
        g_total_frame_counter++;
        // Skip the first 100 frames so isp will more or less stabilize and checking that it's a frame that we want to make changes in
        if ((g_total_frame_counter > 100) && ((g_total_frame_counter % no_change_frames) == 0))
        {
            g_inside_frame_counter++;
            std::cout << "Inside Frame number: " << g_inside_frame_counter << std::endl;
            /// TODO: MSW-6310 - Getting errors when adding custom overlay or privacy mask
            //privacy_mask_scenario(g_inside_frame_counter, media_lib->frontend);               
            OSD_scenario(g_inside_frame_counter, media_lib->encoders[s.id]);                
            encoder_scenario(g_inside_frame_counter, media_lib->encoders.begin()->second, g_encoder_is_running);
            vision_scenario(g_inside_frame_counter, media_lib->frontend);
        }
        media_lib->encoders[s.id]->add_buffer(buffer);
    };
    media_lib->frontend->subscribe(fe_callbacks);

    if (!media_lib->encoders.empty()) {
        const auto &[streamId, encoder] = *media_lib->encoders.begin();
        std::cout << "subscribing to encoder for '" << streamId << "'" << std::endl;
        encoder->subscribe(
            [media_lib, streamId](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                write_encoded_data(buffer, size, media_lib->output_files[streamId]);
            });
    }
}

int set_output_and_config_files(ParsedOptions options)
{
    if (options.encoding_format == "h264") {
        g_encoder_config_file_path += "_sink0.json";
        g_output_file_path += ".h264";
    }
    else if (options.encoding_format == "mjpeg") {
        g_encoder_config_file_path += "_jpeg_sink1.json";
        g_output_file_path += ".jpegenc";
    }
    else {
        std::cerr << "Invalid encoding format" << std::endl;
        return 1;
    }
    return 0;
}

int setup(std::shared_ptr<MediaLibrary> media_lib, ParsedOptions options) {

    if (set_output_and_config_files(options) != 0)
        return 1;

    try {
        std::filesystem::copy_file(FRONTEND_CONFIG_FILE, BACKUP_FRONTEND_CONFIG_FILE, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(g_encoder_config_file_path, BACKUP_ENCODER_CONFIG_FILE, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error copying file: " << e.what() << '\n';
        return 1;
    }

    init_vision_config_file(FRONTEND_CONFIG_FILE);
    std::string frontend_config_string = read_string_from_file(FRONTEND_CONFIG_FILE);
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create(FRONTEND_SRC_ELEMENT_V4L2SRC, frontend_config_string);
    if (!frontend_expected.has_value()) {
        std::cout << "Failed to create frontend" << std::endl;
        return 1;
    }
    m_media_lib->frontend = frontend_expected.value();

    auto streams = m_media_lib->frontend->get_outputs_streams();
    if (!streams.has_value()) {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    for (auto s : streams.value()) {
        std::cout << "Creating encoder enc_" << s.id << std::endl;
        std::string encoderosd_config_string = read_string_from_file(g_encoder_config_file_path.c_str());
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create(encoderosd_config_string, s.id);
        if (!encoder_expected.has_value()) {
            std::cout << "Failed to create encoder osd" << std::endl;
            return 1;
        }
        m_media_lib->encoders[s.id] = encoder_expected.value();

        std::string output_file_path = g_output_file_path;
        delete_output_file(output_file_path);
        m_media_lib->output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        if (!m_media_lib->output_files[s.id].good()) {
            std::cerr << "Error occurred at writing time!" << std::endl;
            return 1;
        }
    }
    return 0;
}

void clean(bool g_pipeline_is_running, bool g_encoder_is_running, const std::string& backup_config_file, std::streambuf* originalBuffer) {
    if (g_pipeline_is_running) {
        std::cout << "Stopping" << std::endl;
        m_media_lib->frontend->stop();
    }
        
    if (!m_media_lib->encoders.empty() && g_encoder_is_running) {
        m_media_lib->encoders.begin()->second->stop();
    }

    for (auto &entry : m_media_lib->output_files) {
        entry.second.close();
    }

    try {
        std::filesystem::copy_file(backup_config_file, FRONTEND_CONFIG_FILE, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(BACKUP_ENCODER_CONFIG_FILE, g_encoder_config_file_path, std::filesystem::copy_options::overwrite_existing);
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
    for (const auto& entry : resolutionMap) {
        output_resolution.push_back(entry.second);
    }
    subscribe_elements(m_media_lib, options.no_change_frames);

    std::cout << "Starting encoder and frontend" << std::endl;
    if (!m_media_lib->encoders.empty()) {
        auto &encoder = m_media_lib->encoders.begin()->second;
        std::cout << "starting encoder" << std::endl;
        encoder->start();
        g_encoder_is_running = true;
    }
    m_media_lib->frontend->start();
    g_pipeline_is_running = true;

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

                /// TODO: uncomment after MSW-6042 bug is solved     
                /*             
                if (j % 3 == 1) {
                    rotate_90(true, g_encoder_config_file_path, FRONTEND_CONFIG_FILE);
                }
                else if (j % 3 == 2) {
                    rotate_90(false, g_encoder_config_file_path, FRONTEND_CONFIG_FILE);
                }
                if (g_encoder_is_running) {
                    m_media_lib->encoders.begin()->second->stop();
                    std::cout << "Stopping encoder" << std::endl;
                    g_encoder_is_running = false;   
                }
                if (!g_encoder_is_running) {
                    m_media_lib->encoders.begin()->second->start();
                    std::cout << "Starting encoder" << std::endl;
                    g_encoder_is_running = true;
                }
                */
                change_output_resolution(FRONTEND_CONFIG_FILE, output_resolution[j % output_resolution.size()]);
                change_hdr_status(g_hdr_enabled, FRONTEND_CONFIG_FILE);
                std::cout << "Starting frontend" << std::endl;
                g_pipeline_is_running = true;
                m_media_lib->frontend->start();
                std::this_thread::sleep_for(std::chrono::seconds(options.test_time/options.number_of_frontend_restarts));
            }
        }
    }

    clean(g_pipeline_is_running, g_encoder_is_running, BACKUP_FRONTEND_CONFIG_FILE, originalBuffer);
    return 0;
}
