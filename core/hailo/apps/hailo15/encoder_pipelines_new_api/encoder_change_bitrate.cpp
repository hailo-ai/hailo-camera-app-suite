#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include <cxxopts/cxxopts.hpp>
#include "apps_common.hpp"
#include "media_library/encoder_config.hpp"

#define BITRATE_FOR_VBR (1000000)
#define TOL_MOVING_BITRATE_FOR_VBR (2000)
#define PICTURE_RC_OFF (0)
#define PICTURE_RC_ON (1)
#define BITRATE_FOR_CBR (25000000)
#define TOL_MOVING_BITRATE_FOR_CBR (0)

static int counter=0;

/**
 * Encoder's probe callback
 *
 * @param[in] pad               The sinkpad of the encoder.
 * @param[in] info              Info about the probe
 * @param[in] user_data         user specified data for the probe
 * @return GST_PAD_PROBE_OK
 * @note Example only - Switches between "CBR" and "VBR" every 200 frames, user_data is the pipeline.
 */
static GstPadProbeReturn encoder_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    GstElement *pipeline = GST_ELEMENT(user_data);
    GstElement *encoder_element = gst_bin_get_by_name(GST_BIN(pipeline), "enco");

    counter++;

    if (counter % 200 == 0) {
        // get properties
        gpointer value = nullptr;
        g_object_get(G_OBJECT(encoder_element), "user-config", &value, NULL);
        encoder_config_t *config = reinterpret_cast<encoder_config_t *>(value);
        hailo_encoder_config_t hailo_config = std::get<hailo_encoder_config_t>(*config);

        if (counter % 400 != 0) {
	        // Changing to VBR
            GST_INFO("Changing encoder to VBR");
            hailo_config.rate_control.picture_rc = PICTURE_RC_OFF;
            hailo_config.rate_control.ctb_rc = true;
            hailo_config.rate_control.bitrate.target_bitrate = BITRATE_FOR_VBR;
            hailo_config.rate_control.bitrate.tolerance_moving_bitrate = TOL_MOVING_BITRATE_FOR_VBR;
        }
        else
        {
            // Changing to CBR
            GST_INFO("Changing encoder to CBR");
            hailo_config.rate_control.picture_rc = PICTURE_RC_ON;
            hailo_config.rate_control.ctb_rc = true;
            hailo_config.rate_control.bitrate.target_bitrate = BITRATE_FOR_CBR;
            hailo_config.rate_control.bitrate.tolerance_moving_bitrate = TOL_MOVING_BITRATE_FOR_CBR;
        }
        g_object_set(G_OBJECT(encoder_element), "user-config", config, NULL);
    }

    gst_object_unref(encoder_element);
    return GST_PAD_PROBE_OK;
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 * @note prints the return value to the stdout.
 */
std::string create_pipeline_string(std::string codec)
{
    std::string pipeline = "";
    std::string config_file_path;
    std::string output_format;

    if (codec == "h265")
    {
        config_file_path = "/home/root/apps/encoder_pipelines_new_api/configs/encoder_sink_fhd_h265.json";
        output_format = "hevc";
    }
    else
    {
        config_file_path = "/home/root/apps/encoder_pipelines_new_api/configs/encoder_sink_fhd_h264.json";
        output_format = "h264";
    }

    pipeline = "v4l2src name=src_element device=/dev/video0 io-mode=dmabuf ! "
               "video/x-raw,format=NV12,width=1920,height=1080, framerate=30/1 ! "
               "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
               "hailoencoder config-file-path=" + config_file_path + " name=enco ! " + codec + "parse config-interval=-1 ! "
               "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
               "video/x-" + codec + ",framerate=30/1 ! "
               "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
               "fpsdisplaysink fps-update-interval=2000 name=display_sink text-overlay=false video-sink=\"filesink location=test."
               + output_format + " name=hailo_sink\""
               " sync=true signal-fps-measurements=true";

                                           
    std::cout << "Pipeline:" << std::endl;
    std::cout << "gst-launch-1.0 " << pipeline << std::endl;

    return pipeline;
}

/**
 * Set the priting fps messages callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @param[in] print_fps       To print FPS or not.
 * @note Sets the app to print fps messages from the application
 */
void set_print_fps(GstElement *pipeline, bool print_fps)
{
    if (print_fps)
    {
        GstElement *display_sink = gst_bin_get_by_name(GST_BIN(pipeline), "display_sink");
        g_signal_connect(display_sink, "fps-measurements", G_CALLBACK(fps_measurements_callback), NULL);
    }
}

/**
 * Set the pipeline's probes
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @note Sets a probe to the encoder sinkpad.
 */
void set_probes(GstElement *pipeline)
{
    // extract elements from pipeline
    GstElement *encoder = gst_bin_get_by_name(GST_BIN(pipeline), "enco");
    // extract pads from elements
    GstPad *pad_encoder = gst_element_get_static_pad(encoder, "sink");
    // set probes
    gst_pad_add_probe(pad_encoder, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)encoder_probe_callback, pipeline, NULL);
    // free resources
    gst_object_unref(encoder);
}

/**
 * Set the encoder starting configuration
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @note Sets a probe to the encoder sinkpad.
 */
void set_starting_config(GstElement *pipeline)
{
    // extract elements from pipeline
    GstElement *encoder = gst_bin_get_by_name(GST_BIN(pipeline), "enco");
    // get properties
    // Error getting properties from encoder
    encoder_config_t config;
    hailo_encoder_config_t hailo_config = std::get<hailo_encoder_config_t>(config);
    g_object_get(G_OBJECT(encoder), "user-config", &config, NULL);
    // Configuring starting config
    hailo_config.rate_control.picture_rc = PICTURE_RC_ON;
    hailo_config.rate_control.ctb_rc = true;
    hailo_config.rate_control.bitrate.target_bitrate = BITRATE_FOR_CBR;
    hailo_config.rate_control.bitrate.tolerance_moving_bitrate = TOL_MOVING_BITRATE_FOR_CBR;
    g_object_set(G_OBJECT(encoder), "user-config", config, NULL);
    // free resources
    gst_object_unref(encoder);
}

int main(int argc, char *argv[])
{
    std::string src_pipeline_string;
    GstFlowReturn ret;
    add_sigint_handler();
    std::string codec;
    bool print_fps = false;

    // Parse user arguments
    cxxopts::Options options = build_arg_parser();
    auto result = options.parse(argc, argv);
    std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options, codec);

    for (ArgumentType argument: argument_handling_results)
    {
        switch (argument) {
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

    std::string pipeline_string = create_pipeline_string(codec);
    GstElement *pipeline = gst_parse_launch(pipeline_string.c_str(), NULL);
    set_print_fps(pipeline, print_fps);
    set_probes(pipeline);
    set_starting_config(pipeline);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    ret = wait_for_end_of_pipeline(pipeline);

    // Free resources
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_deinit();

    return ret;
}
