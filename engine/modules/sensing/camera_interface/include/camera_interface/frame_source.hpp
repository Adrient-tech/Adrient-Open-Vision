#ifndef OPENVISION_FRAME_SOURCE_HPP
#define OPENVISION_FRAME_SOURCE_HPP

#include <camera_interface/camera_interface.hpp>
#include <config/openvision_config.hpp>
#include <memory>

namespace camera_interface {

// Opens the configured frame source (video file, V4L2, or ROS2).
std::unique_ptr<CameraInterface> open_frame_source(const SourceConfig& cfg);

}  // namespace camera_interface

#endif  // OPENVISION_FRAME_SOURCE_HPP
