#include <memory> 
#include <iostream>
#include <fstream>
#include <cxxopts/cxxopts.hpp>
#include <filesystem> 
#include "media_library/frontend.hpp"
#include "media_library/encoder.hpp"

extern std::string g_output_file_path;
std::shared_ptr<osd::CustomOverlay> existing_custom_overlay;
std::string font_path = "/usr/share/fonts/ttf/LiberationMono-Bold.ttf";
std::string image_path = "/home/root/apps/detection/resources/configs/osd_hailo_static_image.png";

osd::rgba_color_t red_argb = {255, 0, 0, 255};
osd::rgba_color_t blue_argb = {0, 0, 255, 255};
osd::rgba_color_t white_argb = {255, 255, 255, 255};
osd::CustomOverlay custom_overlay("custom_argb1", 0.3, 0.5, 0.1, 0.1, 1, osd::custom_overlay_format::ARGB);
osd::TextOverlay text_overlay("text_overlay", 0.4, 0.4, "Chaos App", red_argb, blue_argb, 60.0f, 1, 1, font_path, 0, osd::rotation_alignment_policy_t::CENTER);
osd::ImageOverlay image_overlay("image_overlay", 0.5, 0.5, 0.1, 0.1, image_path, 1, 0, osd::rotation_alignment_policy_t::CENTER);
osd::DateTimeOverlay date_time_overlay("date_time_overlay", 0.2, 0.8, blue_argb, 60.0f, 1, 1, 0, osd::rotation_alignment_policy_t::CENTER);
int privacy_mask_build_factor = 150;

void OSD_scenario(int& counter, MediaLibraryEncoderPtr encoder, std::string id)
{
    switch (counter)
    {  
        case 1:
        {
            encoder->get_blender()->add_overlay(custom_overlay);
            auto custom_expected = encoder->get_blender()->get_overlay("custom_argb1");
            auto existing_custom_overlay = std::static_pointer_cast<osd::CustomOverlay>(custom_expected.value());
            HailoMediaLibraryBufferPtr hailo_argb_buffer = existing_custom_overlay->get_buffer();
            void *plane0_userptr = hailo_argb_buffer->get_plane_ptr(0);
            for (size_t i = 0; i < hailo_argb_buffer->get_plane_size(0); i += 4)
            {
                // Set each pixel to blue
                memcpy(static_cast<uint8_t*>(plane0_userptr) + i, &blue_argb, sizeof(uint32_t));
            } 
            encoder->get_blender()->set_overlay_enabled("custom_argb1", true);
            std::cout << "adding custom" << std::endl;
            break;
        }
        case 2:
            encoder->get_blender()->add_overlay(text_overlay);
            encoder->get_blender()->set_overlay_enabled("text_overlay", true);
            std::cout << "adding text" << std::endl;
            break;
        case 3:
            encoder->get_blender()->add_overlay(date_time_overlay);
            encoder->get_blender()->set_overlay_enabled("date_time_overlay", true);
            std::cout << "adding date time" << std::endl;
            break;
        case 4:
            encoder->get_blender()->add_overlay(image_overlay);
            encoder->get_blender()->set_overlay_enabled("image_overlay", true);
            std::cout << "adding image" << std::endl;
            break;
        case 5:
            encoder->get_blender()->set_overlay_enabled("custom_argb1", false);
            std::cout << "removing custom" << std::endl;
            break;
        case 6:
            encoder->get_blender()->set_overlay_enabled("text_overlay", false);
            std::cout << "removing text" << std::endl;
            break;
        case 7:
            encoder->get_blender()->set_overlay_enabled("date_time_overlay", false);
            std::cout << "removing date time" << std::endl;
            break;
        case 8:
            encoder->get_blender()->set_overlay_enabled("custom_argb1", true);
            encoder->get_blender()->set_overlay_enabled("text_overlay", true);
            encoder->get_blender()->set_overlay_enabled("date_time_overlay", true);
            /// TODO: MSW-5915- When using blender to update a value in custom OSD, the overlay disappears
            //encoder->get_blender()->set_overlay_enabled("custom_argb1", false);
            std::cout << "enabling all overlays" << std::endl;
            break;
        /// TODO: MSW-5915- When using blender to update a value in custom OSD, the overlay disappears
        /*
        case 9:
            custom_overlay.width = 0.3;
            encoder->get_blender()->set_overlay(custom_overlay);
            break;
            std::cout << "changing custom width" << std::endl;
    */
        case 10:
            text_overlay.label = "Chaos App_edit" + id;
            encoder->get_blender()->set_overlay(text_overlay);
            std::cout << "changing text label" << std::endl;
            break;
        case 11:
            date_time_overlay.text_color = blue_argb;
            encoder->get_blender()->set_overlay(date_time_overlay);
            std::cout << "changing date time color" << std::endl;
            break;
        case 12:
            image_overlay.width = 0.2;
            encoder->get_blender()->set_overlay(image_overlay);
            std::cout << "changing image width" << std::endl;
            break;
        /// TODO: MSW-5915- When using blender to update a value in custom OSD, the overlay disappears
        /*
        case 13:
            custom_overlay.height = 0.3;
            encoder->get_blender()->set_overlay(custom_overlay);
            break;
            std::cout << "changing custom height" << std::endl;
        */
        case 14:
            text_overlay.text_color = red_argb;
            encoder->get_blender()->set_overlay(text_overlay);
            std::cout << "changing text color" << std::endl;
            break;
        case 15:
            date_time_overlay.font_size = 80.0f;
            encoder->get_blender()->set_overlay(date_time_overlay);
            std::cout << "changing date time font size" << std::endl;
            break;
        case 16:
            image_overlay.height = 0.2;
            encoder->get_blender()->set_overlay(image_overlay);
            std::cout << "changing image height" << std::endl;
            break;
        /// TODO: MSW-5915- When using blender to update a value in custom OSD, the overlay disappears
        /*
        case 17:
            custom_overlay.z_index = 0;
            encoder->get_blender()->set_overlay(custom_overlay);
            break;
            std::cout << "changing custom z index" << std::endl;
        */
        case 18:
            text_overlay.background_color = white_argb;
            encoder->get_blender()->set_overlay(text_overlay);
            std::cout << "changing text background color" << std::endl;
            break;
        case 19:
            date_time_overlay.line_thickness = 2;
            encoder->get_blender()->set_overlay(date_time_overlay);
            std::cout << "changing date time line thickness" << std::endl;
            break;
        case 20:
            image_overlay.z_index = 0;
            encoder->get_blender()->set_overlay(image_overlay);
            std::cout << "changing image z index" << std::endl;
            break;
        /// TODO: MSW-5915- When using blender to update a value in custom OSD, the overlay disappears
        /*
        case 21:
            auto dspbuffer = existing_custom_overlay->get_buffer();
            for (size_t i = 0; i < dspbuffer->planes[0].bytesused; i += 4)
            {
                // Set each pixel to blue
                memcpy((int8_t *)(dspbuffer->planes[0].userptr) + i, &red_argb, sizeof(uint32_t));
            };
                        encoder->get_blender()->set_overlay(text_overlay);
            break;
            std::cout << "changing custom color" << std::endl;
        */
        case 22:
            text_overlay.font_size = 80.0f;
            encoder->get_blender()->set_overlay(text_overlay);
            std::cout << "changing text font size" << std::endl;
            break;
        case 23:
            date_time_overlay.z_index = 0;
            encoder->get_blender()->set_overlay(date_time_overlay);
            std::cout << "changing date time z index" << std::endl;
            break;
        case 24:
            text_overlay.font_path = "/usr/share/fonts/ttf/LiberationMono-Regular.ttf";
            encoder->get_blender()->set_overlay(text_overlay);
            std::cout << "changing text font path" << std::endl;
            break;
        case 25:
            text_overlay.x = 0.0;
    // custom_overlay.y = 0.0;
            date_time_overlay.x = 0.99;
            image_overlay.y = 0.99;
            encoder->get_blender()->set_overlay(text_overlay);
        //    encoder->get_blender()->set_overlay(custom_overlay);
            encoder->get_blender()->set_overlay(date_time_overlay);
            encoder->get_blender()->set_overlay(image_overlay);
            std::cout << "moving all overlays out of bounds" << std::endl;
            break;
        case 426:
            std::cout << "Spinning all overlays" << std::endl;
            {
                text_overlay.x = 0.1;
        //  custom_overlay.y = 0.2;
                date_time_overlay.x = 0.6;
                image_overlay.y = 0.8;
            }
            encoder->get_blender()->set_overlay(text_overlay);
        //  encoder->get_blender()->set_overlay(custom_overlay);
            encoder->get_blender()->set_overlay(date_time_overlay);
            encoder->get_blender()->set_overlay(image_overlay);
            break;
        case 1871:
        //    encoder->get_blender()->remove_overlay("custom_argb1");
            encoder->get_blender()->remove_overlay("text_overlay");
            encoder->get_blender()->remove_overlay("date_time_overlay");
            encoder->get_blender()->remove_overlay("image_overlay");
            std::cout << "removing all overlays" << std::endl;
            break;
        default:
            // Moving text overlay to the right
            if (counter >= 26 && counter <= 125) {
                if (text_overlay.x < 0.99)
                    text_overlay.x = text_overlay.x + 0.01;
                encoder->get_blender()->set_overlay(text_overlay);
            }
            /// TODO: MSW-5915- When using blender to update a value in custom OSD, the overlay disappears
            /*
            // Moving custom overlay to the down
            if (counter >= 126 && counter <= 225) {
                if (custom_overlay.y < 0.99)
                    custom_overlay.y = custom_overlay.y + 0.01;
                encoder->get_blender()->set_overlay(custom_overlay);
            }
            */
            // Moving date time overlay to the left
            if (counter >= 226 && counter <= 324) {
                if (date_time_overlay.x > 0.01)
                    date_time_overlay.x = date_time_overlay.x - 0.01;
                encoder->get_blender()->set_overlay(date_time_overlay);
            }
            /// TODO: MSW-6108 - When moving/spinning image overlay + set vision config - the app freezes and doesn't release
            // moving image overlay up
        /* if (counter >= 326 && counter <= 425) {
                if (image_overlay.y > 0.01)
                    image_overlay.y =  image_overlay.y - 0.01;
                encoder->get_blender()->set_overlay(image_overlay);
            } */ 
            /// TODO: MSW-6121 - When rotating 180 + starting encoder + rotating text overlay- the stream freezes
            //flipping text overlay
        /* if (counter >= 427 && counter <= 787 ) {
                text_overlay.angle = text_overlay.angle + 1;
                encoder->get_blender()->set_overlay(text_overlay);
            } */
            // flipping custom overlay
            //if (counter >= 788 && counter <= 1148) {
            //    custom_overlay.angle = custom_overlay.angle + 1;
            //    encoder->get_blender()->set_overlay(custom_overlay);
            //}
            // flipping date time overlay
            //if (counter >= 1149 && counter <= 1509) {
            //    date_time_overlay.angle = date_time_overlay.angle + 1;
            //    encoder->get_blender()->set_overlay(date_time_overlay);
            //}
            /// TODO: MSW-6108 - When moving/spinning image overlay + set vision config - the app freezes and doesn't release
            // flipping image overlay
        /*  if (counter >= 1510 && counter <= 1870) {
                image_overlay.angle = image_overlay.angle + 1;
                encoder->get_blender()->set_overlay(image_overlay);
            } */
            break;
    }
}

void update_encoder_param(MediaLibraryEncoderPtr encoder, std::string param_to_change, uint32_t new_value)
{
    EncoderType encoding_format = encoder->get_type();
    encoder_config_t encoder_config = encoder->get_user_config();
    if (encoding_format == EncoderType::Hailo) {
        hailo_encoder_config_t &h264_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
        if (param_to_change == "qp_max") {
            std::cout << " current" << param_to_change << h264_encoder_config.rate_control.quantization.qp_max.value() << " Setting to "  << new_value << std::endl;
            h264_encoder_config.rate_control.quantization.qp_max = new_value;
        } else if (param_to_change == "bitrate") {
            std::cout << " current" << param_to_change << h264_encoder_config.rate_control.bitrate.target_bitrate << " Setting to "  << new_value << std::endl;
            h264_encoder_config.rate_control.bitrate.target_bitrate = new_value;
        } else {
            std::cout << "Invalid parameter" << std::endl;
        }
    } else if (encoding_format == EncoderType::Jpeg) {
        std::cout << "MJPEG" << std::endl;
        jpeg_encoder_config_t &mjpeg_encoder_config = std::get<jpeg_encoder_config_t>(encoder_config);
        if (param_to_change == "quality") {
            std::cout << " current" << param_to_change << mjpeg_encoder_config.quality << " Setting to "  << new_value << std::endl;
            mjpeg_encoder_config.quality = new_value;
        } else {
            std::cout << "Invalid parameter" << std::endl;
        }
    }
    else
    {
        std::cout << "Invalid encoding format" << std::endl;
    }
    if (encoder->set_config(encoder_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to configure Encoder " << std::endl;
    }
}

void add_privacy_masks(PrivacyMaskBlenderPtr privacy_mask_blender)
{
    for (int i = 0; i < 8; ++i) {
        polygon example_polygon;
        example_polygon.id = "privacy_mask" + std::to_string(i + 1);
        example_polygon.vertices.push_back(vertex(i * privacy_mask_build_factor + i * privacy_mask_build_factor, i * privacy_mask_build_factor + i * privacy_mask_build_factor));
        example_polygon.vertices.push_back(vertex(i * privacy_mask_build_factor + 60 + i * privacy_mask_build_factor, i * privacy_mask_build_factor + 20 + i * privacy_mask_build_factor + i*10));
        example_polygon.vertices.push_back(vertex(i * privacy_mask_build_factor + 120 + i * privacy_mask_build_factor + i*10, i * privacy_mask_build_factor + 20 + i * privacy_mask_build_factor));
        example_polygon.vertices.push_back(vertex(i * privacy_mask_build_factor + 180 + i * privacy_mask_build_factor, i * privacy_mask_build_factor + 60 + i * privacy_mask_build_factor + i*10));
        example_polygon.vertices.push_back(vertex(i * privacy_mask_build_factor + 180 + i * privacy_mask_build_factor, i * privacy_mask_build_factor + 120 + i * privacy_mask_build_factor));
        example_polygon.vertices.push_back(vertex(i * privacy_mask_build_factor + 120 + i * privacy_mask_build_factor, i * privacy_mask_build_factor + 180 + i * privacy_mask_build_factor - i*10));
        example_polygon.vertices.push_back(vertex(i * privacy_mask_build_factor + 60 + i * privacy_mask_build_factor - i*10, i * privacy_mask_build_factor + 180 + i * privacy_mask_build_factor));
        example_polygon.vertices.push_back(vertex(i * privacy_mask_build_factor + i * privacy_mask_build_factor, i * privacy_mask_build_factor + 120 + i * privacy_mask_build_factor));
        std::cout << "Adding privacy mask " << example_polygon.id << std::endl;
        privacy_mask_blender->add_privacy_mask(example_polygon);
    }
}

void remove_all_polygons(PrivacyMaskBlenderPtr privacy_mask_blender)
{
    privacy_mask_blender->remove_privacy_mask("privacy_mask1");
    privacy_mask_blender->remove_privacy_mask("privacy_mask2");
    privacy_mask_blender->remove_privacy_mask("privacy_mask3");
    privacy_mask_blender->remove_privacy_mask("privacy_mask4");
    privacy_mask_blender->remove_privacy_mask("privacy_mask5");
    privacy_mask_blender->remove_privacy_mask("privacy_mask6");
    privacy_mask_blender->remove_privacy_mask("privacy_mask7");
    privacy_mask_blender->remove_privacy_mask("privacy_mask8");
}

void privacy_mask_scenario(int& counter, MediaLibraryFrontendPtr frontend)
{
    PrivacyMaskBlenderPtr privacy_mask_blender = frontend->get_privacy_mask_blender();
    tl::expected<privacy_mask_types::polygon, media_library_return> polygon_exp1;
    polygon polygon1;
    tl::expected<privacy_mask_types::polygon, media_library_return> polygon_exp2;
    polygon polygon2;
    tl::expected<privacy_mask_types::polygon, media_library_return> polygon_exp3;
    polygon polygon3;
    tl::expected<privacy_mask_types::polygon, media_library_return> polygon_exp4;
    polygon polygon4;
    switch (counter)
    {
        case 1:
            add_privacy_masks(privacy_mask_blender);
            std::cout << "Adding privacy masks" << std::endl;
            break;
        case 2:
            remove_all_polygons(privacy_mask_blender);
            std::cout << "Removing privacy masks" << std::endl;
            break;
        case 3:
            add_privacy_masks(privacy_mask_blender);
            std::cout << "Adding privacy masks" << std::endl;
            break;
        case 4:
            polygon_exp2 = privacy_mask_blender->get_privacy_mask("privacy_mask2");
            polygon2 = polygon_exp2.value();
            polygon2.vertices.erase(polygon2.vertices.begin());
            privacy_mask_blender->set_privacy_mask(polygon2);
            std::cout << "Removing vertex from privacy mask" << std::endl;
            break;
        case 5:
            polygon_exp2 = privacy_mask_blender->get_privacy_mask("privacy_mask2");
            polygon2 = polygon_exp2.value();
            polygon2.vertices.push_back(vertex(250, 250));
            privacy_mask_blender->set_privacy_mask(polygon2);
            std::cout << "Adding vertex to privacy mask" << std::endl;
            break;
        case 6:
            polygon_exp1 = privacy_mask_blender->get_privacy_mask("privacy_mask1");
            polygon1 = polygon_exp1.value();
            polygon1.vertices[0].x = polygon1.vertices[0].x + 100;
            polygon1.vertices[0].y = polygon1.vertices[0].y + 100;
            privacy_mask_blender->set_privacy_mask(polygon1);
            std::cout << "Changing vertex location in privacy mask" << std::endl;
            break;
        /// TODO: MSW-5914 - set_color in privacy mask doesn't change the color
        /*
        case 7:
            privacy_mask_types::rgb_color_t color1_privacy_mask;
            color1_privacy_mask.r = 0xFF;
            color1_privacy_mask.g = 0xFF;
            color1_privacy_mask.b = 0x00;
            privacy_mask_blender->set_color(color1_privacy_mask);
            std::cout << "Setting color to privacy mask" << std::endl;
            break;
        */
        case 8:
            std::cout << "Moving masks out of bounds" << std::endl;
            break;
        case 1201:
            remove_all_polygons(privacy_mask_blender);
            std::cout << "Removing privacy masks" << std::endl;
            break;
        default:
            if (counter >= 9 && counter <= 500) {
                polygon_exp1 = privacy_mask_blender->get_privacy_mask("privacy_mask1");
                polygon1 = polygon_exp1.value();
                for(int i = 0; i < 8 ; i++)
                {
                    polygon1.vertices[i].x = polygon1.vertices[i].x + 10;
                    privacy_mask_blender->set_privacy_mask(polygon1);
                }
            }
            if (counter >= 501 && counter <= 600) {
                polygon_exp4 = privacy_mask_blender->get_privacy_mask("privacy_mask4");
                polygon4 = polygon_exp4.value();
                for(int i = 0; i < 8 ; i++)
                {
                    polygon4.vertices[i].x = polygon4.vertices[i].x - 10;
                    privacy_mask_blender->set_privacy_mask(polygon4);
                }
            }
            if (counter >= 500 && counter <= 1000) {
                polygon_exp3 = privacy_mask_blender->get_privacy_mask("privacy_mask3");
                polygon3 = polygon_exp3.value();
                for(int i = 0; i < 8 ; i++)
                {
                    polygon3.vertices[i].y = polygon3.vertices[i].y + 10;
                    privacy_mask_blender->set_privacy_mask(polygon3);
                }
            }
            if (counter >= 1000 && counter <= 1200) {
                polygon_exp2 = privacy_mask_blender->get_privacy_mask("privacy_mask2");
                polygon2 = polygon_exp2.value();
                for(int i = 0; i < 8 ; i++)
                {
                    polygon2.vertices[i].y = polygon2.vertices[i].y - 10;
                    privacy_mask_blender->set_privacy_mask(polygon2);
                }
            }
            break;
    }

}

void encoder_scenario(int& counter, MediaLibraryEncoderPtr encoder, bool& encoder_is_running, std::string config_string)
{
    EncoderType encoding_format = encoder->get_type();
    if (encoding_format == EncoderType::Hailo) {
        if (counter % 100 == 0) {
            if (counter % 200 == 0)
            {
                update_encoder_param(encoder, "bitrate", 5000000);
            }
            else
            {
                update_encoder_param(encoder, "bitrate", 10000000) ;
            }
        }
        /// TODO: MSW-6372 - When dynamically setting qp_max to 10 and doing 3 times set_config (without any change) the app freezes
        /*
        if (counter % 75 == 0) {
            if (counter % 150 == 0)
            {
                update_encoder_param(encoder, "qp_max", 10);
            }
            else
            {
                update_encoder_param(encoder, "qp_max", 50);
            }
        }
    */
    }
    if (encoding_format == EncoderType::Jpeg) {
        if (counter % 100 == 0) {
            if (counter % 200 == 0)
            {
                update_encoder_param(encoder, "quality", 60);
            }
            else
            {
                update_encoder_param(encoder, "quality", 85);
            }
        }
    }
    /// TODO: MSW-7390 - When running a few streams and toggling start stop of their encoders- errors received and app crashes
    /*
    if ((counter % 60 == 0) || (counter % 60 == 5)) {
        if ((counter % 60 == 0) && (encoder_is_running))
        {
            std::cout << "stopping encoder" << std::endl;
            encoder->stop();
            encoder_is_running = false;
        }
        else if ((counter % 60 == 5) && (!encoder_is_running))
        {
            std::cout << "starting encoder" << std::endl;
            encoder->set_config(config_string);
            encoder->start();
            encoder_is_running = true;
        }
    } */
    

}

void vision_scenario(int& counter, MediaLibraryFrontendPtr frontend)
{
    auto config = frontend->get_config().value();
    bool change = false;
    if (counter % 7 == 0)
    {
        std::cout << "Flipping vertical enabled = " << !config.ldc_config.flip_config.enabled << std::endl;
        config.ldc_config.flip_config.direction = FLIP_DIRECTION_VERTICAL;
        config.ldc_config.flip_config.enabled = !config.ldc_config.flip_config.enabled;
        change = true;
    }
    
    if (counter % 11 == 0)
    {
        std::cout << "Rotating 180 enabled = " << !config.ldc_config.rotation_config.enabled << std::endl;
        config.ldc_config.rotation_config.angle = ROTATION_ANGLE_180;
        config.ldc_config.rotation_config.enabled = !config.ldc_config.rotation_config.enabled;
        change = true;
    }
    if (counter % 13 == 0)
    {
        std::cout << "Flipping horizontal enabled = !config.ldc_config.flip_config.enabled" << std::endl;
        config.ldc_config.flip_config.direction = FLIP_DIRECTION_HORIZONTAL;
        config.ldc_config.flip_config.enabled = !config.ldc_config.flip_config.enabled;
        change = true;
    }
    ///// TODO: denoise change nets - not supported at the moment
    if (counter % 17 == 0)
    {
        std::cout << "denoise enabled = " << !config.denoise_config.enabled << std::endl;
        config.denoise_config.enabled = !config.denoise_config.enabled;
        change = true;
    }
    if (counter % 23 == 0)
    {
        std::cout << "digital zoom enabled = " << !config.multi_resize_config.digital_zoom_config.enabled << std::endl;
        config.multi_resize_config.digital_zoom_config.enabled = !config.multi_resize_config.digital_zoom_config.enabled;
        change = true;
    }
    if (counter % 29 == 0)
    {
        std::cout << "grayscale enabled = " << !config.multi_resize_config.output_video_config.grayscale << std::endl;
        config.multi_resize_config.output_video_config.grayscale = !config.multi_resize_config.output_video_config.grayscale;
        change = true;
    }
    if (counter % 31 == 0)
    {
        std::cout << "dewarp enabled = " << !config.ldc_config.dewarp_config.enabled << std::endl;
        config.ldc_config.dewarp_config.enabled = !config.ldc_config.dewarp_config.enabled;
        change = true;
    }
    if (counter % 37 == 0)
    {
        if (counter % 370 == 0)
        {
            std::cout << "video freeze enabled " << std::endl;
            frontend->set_freeze(true);
        }
        else
        {
            std::cout << "video freeze disabled " << std::endl;
            frontend->set_freeze(false);
        }
    }

    if (change)
        frontend->set_config(config);
}
