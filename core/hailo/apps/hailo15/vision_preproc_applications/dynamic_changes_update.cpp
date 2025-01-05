#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include <fstream>
#include <regex>
#include <cxxopts/cxxopts.hpp>
#include "apps_common.hpp"
#include <nlohmann/json.hpp>

const char *BASIC_VISION_CONFIG = "/home/root/apps/basic_security_camera_streaming/resources/configs/vision_config1.json";
const char *UPDATE_VISION_CONFIG = "/home/root/apps/basic_security_camera_streaming/resources/configs/vision_config2.json";
const char *BASIC_OSD_CONFIG = "/usr/bin/frontend_encoder_sink0.json";
const char *UPDATE_OSD_CONFIG = "/usr/bin/test_update_osd_config.json";
const static uint change_config_frames = 250;
static uint counter = 0;
int num_outputs = 0;
int frame_to_toggle = 1;
std::string vision_config_1 = "";
std::string vision_config_2 = "";
std::string osd_config_1 = "";
std::string osd_config_2 = "";

/**
 * vision probe callback
 *
 * @param[in] pad               The sinkpad of the encoder.
 * @param[in] info              Info about the probe
 * @param[in] user_data         user specified data for the probe
 * @return GST_PAD_PROBE_OK
 */
static GstPadProbeReturn regular_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    counter++;
    GstElement *pipeline = GST_ELEMENT(user_data);
    GstElement *frontend_element = gst_bin_get_by_name(GST_BIN(pipeline), "frontend");

    std::vector<GstElement*> osd_elements(num_outputs);
    for (int i = 0; i < num_outputs; ++i)
        osd_elements[i] = gst_bin_get_by_name(GST_BIN(pipeline), ("osd" + std::to_string(i)).c_str());

    if (counter == change_config_frames)
    {
        // Changing to updated configuration
        GST_INFO("Changing to updated configurations");
        g_object_set(frontend_element, "config-string", vision_config_2.c_str(), NULL);
        for (int i = 0; i < num_outputs; ++i)
            g_object_set(osd_elements[i], "config-string", osd_config_2.c_str(), NULL);
    }

    gst_object_unref(frontend_element);
    for (int i = 0; i < num_outputs; ++i)
        gst_object_unref(osd_elements[i]);

    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn toggle_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    counter++;
    GstElement *pipeline = GST_ELEMENT(user_data);
    GstElement *frontend_element = gst_bin_get_by_name(GST_BIN(pipeline), "frontend");

    std::vector<GstElement*> osd_elements(num_outputs);
    for (int i = 0; i < num_outputs; ++i)
        osd_elements[i] = gst_bin_get_by_name(GST_BIN(pipeline), ("osd" + std::to_string(i)).c_str());

    if ((counter % frame_to_toggle) == 0)
    {
        if ((counter % (2 * frame_to_toggle)) == 0)
        {
            g_object_set(frontend_element, "config-string", vision_config_1.c_str(), NULL);
            for (int i = 0; i < num_outputs; ++i)
                g_object_set(osd_elements[i], "config-string", osd_config_1.c_str(), NULL);
        }
        else
        {
            g_object_set(frontend_element, "config-string", vision_config_2.c_str(), NULL);
            for (int i = 0; i < num_outputs; ++i)
                g_object_set(osd_elements[i], "config-string", osd_config_2.c_str(), NULL);
        }
    }

    gst_object_unref(frontend_element);
    for (int i = 0; i < num_outputs; ++i)
        gst_object_unref(osd_elements[i]);
    return GST_PAD_PROBE_OK;
}

/**
 * Appsink's new_sample callback
 *
 * @param[in] appsink               The appsink object.
 * @param[in] callback_data         user data.
 * @return GST_FLOW_OK
 * @note Example only - only mapping the buffer to a GstMapInfo, than unmapping.
 */
static GstFlowReturn appsink_new_sample(GstAppSink *appsink, gpointer callback_data)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo mapinfo;

    sample = gst_app_sink_pull_sample(appsink);
    buffer = gst_sample_get_buffer(sample);
    gst_buffer_map(buffer, &mapinfo, GST_MAP_READ);

    GST_INFO_OBJECT(appsink, "Got Buffer from appsink: %p", mapinfo.data);
    // Do Logic

    gst_buffer_unmap(buffer, &mapinfo);
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 * @note prints the return value to the stdout.
 */
std::string create_dynamic_pipeline_string(std::string codec, std::string vision_config_string,
                                           std::string osd_config_string, int num_outputs)
{
    std::ostringstream pipeline_builder;
    pipeline_builder << "v4l2src name=src_element device=/dev/video0 io-mode=dmabuf ! "
                     << "video/x-raw, width=3840, height=2160, framerate=30/1, format=NV12 ! "
                     << "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                     << "hailofrontend config-string='" << vision_config_string << "' name=frontend ";
    for (int i = 0; i < num_outputs; ++i) {
        pipeline_builder << "frontend. ! "
                         << "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                         << " hailoosd config-string='" << osd_config_string << "' name=osd" << i << " "
                         << "osd" << i << ". ! "
                         << "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                         << "hailoh264enc bitrate=10000000 hrd=false ! video/x-h264 ! "
                         << "tee name=stream" << (i + 1) << "_tee "
                         << "stream" << (i + 1) << "_tee. ! "
                         << "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                         << "rtph264pay ! application/x-rtp, media=(string)video, encoding-name=(string)H264 ! "
                         << "udpsink host=10.0.0.2 sync=false port=" << (5000 + i * 2) << " "
                         << "stream" << (i + 1) << "_tee. ! "
                         << "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                         << "fpsdisplaysink fps-update-interval=2000 name=display_sink" << (i + 1) << " text-overlay=false "
                         << "video-sink=\"appsink max-buffers=1 name=hailo_sink" << (i + 1) << "\" sync=true signal-fps-measurements=true ";
    }

    return pipeline_builder.str();
}

/**
 * Set the callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @param[in] print_fps       To print FPS or not.
 * @note Sets the new_sample and propose_allocation callbacks, without callback user data (NULL).
 */
void set_callbacks(GstElement *pipeline, bool print_fps, int num_outputs) {
    GstAppSinkCallbacks callbacks = {NULL};
    callbacks.new_sample = appsink_new_sample;

    for (int i = 1; i <= num_outputs; ++i) {
        std::string sink_name = "hailo_sink" + std::to_string(i);
        GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), sink_name.c_str());
        gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, NULL, NULL);
        gst_object_unref(appsink);
    }
}

/**
 * Set the pipeline's probes
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @note Sets a probe to the encoder sinkpad.
 */
void set_probes(GstElement *pipeline, std::string mode, bool print_fps)
{
    // extract elements from pipeline
    GstElement *frontend = gst_bin_get_by_name(GST_BIN(pipeline), "frontend");
    // extract pads from elements
    GstPad *pad_frontend = gst_element_get_static_pad(frontend, "sink");
    // set probes
    if (mode == "regular") {
        gst_pad_add_probe(pad_frontend, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)regular_probe_callback, pipeline, NULL);
    }
    else {
        gst_pad_add_probe(pad_frontend, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)toggle_probe_callback, pipeline, NULL);
    }
    if (print_fps) {
        for (int i = 1; i <= num_outputs; ++i) {
            std::string display_sink_name = "display_sink" + std::to_string(i);
            GstElement *display_sink = gst_bin_get_by_name(GST_BIN(pipeline), display_sink_name.c_str());
            g_signal_connect(display_sink, "fps-measurements", G_CALLBACK(fps_measurements_callback), NULL);
            gst_object_unref(display_sink);
        }
    }
    // free resources
    gst_object_unref(frontend);
}

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error("config path is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
}

int main(int argc, char *argv[])
{
    std::string src_pipeline_string;
    GstFlowReturn ret;
    add_sigint_handler();
    std::string codec = "h264";
    bool print_fps = false;

    // Parse user arguments
    cxxopts::Options options = build_arg_parser();
    options.add_options()
        ("mode", "Mode of operation. options: regular, toggle", cxxopts::value<std::string>()->default_value("regular"));
    options.add_options()
        ("frames", "Number of frames for toggle", cxxopts::value<int>()->default_value("1"));
    auto result = options.parse(argc, argv);
    std::string mode = result["mode"].as<std::string>();
    frame_to_toggle = result["frames"].as<int>();
    std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options, codec);

    for (ArgumentType argument : argument_handling_results)
    {
        switch (argument)
        {
        case ArgumentType::Help:
            return 0;
        case ArgumentType::Codec:
            break;
        case ArgumentType::PrintFPS:
            print_fps = true;
            break;
        case ArgumentType::Error:
            return 1;
        }
    }

    gst_init(&argc, &argv);
    vision_config_1 = read_string_from_file(BASIC_VISION_CONFIG);
    vision_config_2 = read_string_from_file(UPDATE_VISION_CONFIG);
    osd_config_1 = read_string_from_file(BASIC_OSD_CONFIG);
    osd_config_2 = read_string_from_file(UPDATE_OSD_CONFIG);
    nlohmann::json config_1 = nlohmann::json::parse(vision_config_1);
    num_outputs = config_1["output_video"]["resolutions"].size();

    std::cout << "Number of output streams: " << num_outputs << std::endl;
    std::string pipeline_string = create_dynamic_pipeline_string(codec, vision_config_1, osd_config_1, num_outputs);
    std::cout << "Created pipeline string." << std::endl;
    GstElement *pipeline = gst_parse_launch(pipeline_string.c_str(), NULL);
    std::cout << "Parsed pipeline." << std::endl;
    std::cout << "Set probes and callbacks." << std::endl;
    set_callbacks(pipeline, print_fps, num_outputs);
    set_probes(pipeline, mode, print_fps);
    std::cout << "Setting state to playing." << std::endl;
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    ret = wait_for_end_of_pipeline(pipeline);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_deinit();
    return ret;
}
