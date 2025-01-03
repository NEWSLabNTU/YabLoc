// Copyright 2023 TIER IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MODULARIZED_PARTICLE_FILTER__PREDICTION__PREDICTOR_HPP_
#define MODULARIZED_PARTICLE_FILTER__PREDICTION__PREDICTOR_HPP_

#include "modularized_particle_filter/common/visualize.hpp"
#include "modularized_particle_filter/prediction/experimental/suspension_adaptor.hpp"
#include "modularized_particle_filter/prediction/resampler.hpp"

#include <rclcpp/rclcpp.hpp>

#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <geometry_msgs/msg/twist_with_covariance_stamped.hpp>
#include <modularized_particle_filter_msgs/msg/particle_array.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/string.hpp>

#include <tf2_ros/transform_broadcaster.h>

namespace yabloc::modularized_particle_filter
{
class Predictor : public rclcpp::Node
{
public:
  using ParticleArray = modularized_particle_filter_msgs::msg::ParticleArray;
  using Particle = modularized_particle_filter_msgs::msg::Particle;
  using PoseStamped = geometry_msgs::msg::PoseStamped;
  using PoseCovStamped = geometry_msgs::msg::PoseWithCovarianceStamped;
  using TwistCovStamped = geometry_msgs::msg::TwistWithCovarianceStamped;
  using TwistStamped = geometry_msgs::msg::TwistStamped;
  using String = std_msgs::msg::String;

  Predictor();

private:
  // The number of particles of particle filter
  const int number_of_particles_;
  // The minimum resampling interval
  const float resampling_interval_seconds_;
  // Const value for X linear velocity covariance
  const float static_linear_covariance_;
  // Const value for Z angular velocity covariance
  const float static_angular_covariance_;

  // Subscriber
  rclcpp::Subscription<PoseCovStamped>::SharedPtr initialpose_sub_;
  rclcpp::Subscription<TwistCovStamped>::SharedPtr twist_cov_sub_;
  rclcpp::Subscription<ParticleArray>::SharedPtr particles_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr height_sub_;

  // Publisher
  rclcpp::Publisher<ParticleArray>::SharedPtr predicted_particles_pub_;
  rclcpp::Publisher<PoseStamped>::SharedPtr pose_pub_;
  rclcpp::Publisher<PoseCovStamped>::SharedPtr pose_cov_pub_;
  rclcpp::Publisher<String>::SharedPtr pub_string_;
  std::unique_ptr<tf2_ros::TransformBroadcaster> tf2_broadcaster_;

  // Timer callback
  rclcpp::TimerBase::SharedPtr timer_;

  float ground_height_{0};

  std::optional<ParticleArray> particle_array_opt_{std::nullopt};
  std::optional<TwistCovStamped> latest_twist_opt_{std::nullopt};
  std::optional<double> previous_resampling_time_opt_{std::nullopt};

  //
  std::unique_ptr<ParticleVisualizer> visualizer_ptr_{nullptr};
  std::unique_ptr<RetroactiveResampler> resampler_ptr_{nullptr};
  std::unique_ptr<SwapModeAdaptor> swap_mode_adaptor_ptr_{nullptr};

  // Callback
  void on_initial_pose(const PoseCovStamped::ConstSharedPtr initialpose);
  void on_twist_cov(const TwistCovStamped::ConstSharedPtr twist);
  void on_weighted_particles(const ParticleArray::ConstSharedPtr weighted_particles);
  void on_timer();

  //
  void initialize_particles(const PoseCovStamped & initialpose);
  //
  void update_with_dynamic_noise(
    ParticleArray & particle_array, const TwistCovStamped & twist, double dt);
  //
  void publish_mean_pose(const geometry_msgs::msg::Pose & mean_pose, const rclcpp::Time & stamp);
};

}  // namespace yabloc::modularized_particle_filter
#endif  // MODULARIZED_PARTICLE_FILTER__PREDICTION__PREDICTOR_HPP_
