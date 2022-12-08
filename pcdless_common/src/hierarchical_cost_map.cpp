#include "pcdless_common/hierarchical_cost_map.hpp"

#include "pcdless_common/direct_cost_map.hpp"

#include <opencv4/opencv2/highgui.hpp>
#include <opencv4/opencv2/imgproc.hpp>
#include <pcdless_common/color.hpp>

namespace pcdless::common
{
float Area::unit_length_ = -1;

HierarchicalCostMap::HierarchicalCostMap(rclcpp::Node * node)
: max_range_(node->declare_parameter<float>("max_range", 40.0)),
  image_size_(node->declare_parameter<int>("image_size", 800)),
  max_map_count_(10),
  logger_(node->get_logger())
{
  Area::unit_length_ = max_range_;
  float gamma = node->declare_parameter<float>("gamma", 5.0);
  gamma_converter.reset(gamma);
}

cv::Point2i HierarchicalCostMap::to_cv_point(const Area & area, const Eigen::Vector2f p)
{
  Eigen::Vector2f relative = p - area.real_scale();
  float px = relative.x() / max_range_ * image_size_;
  float py = relative.y() / max_range_ * image_size_;
  return {static_cast<int>(px), static_cast<int>(py)};
}

cv::Vec3b HierarchicalCostMap::at3(const Eigen::Vector2f & position)
{
  if (!cloud_.has_value()) {
    return cv::Vec3b(128, 0, 0);
  }

  Area key(position);
  if (cost_maps_.count(key) == 0) {
    build_map(key);
  }
  map_accessed_[key] = true;

  cv::Point2i tmp = to_cv_point(key, position);
  return cost_maps_.at(key).at<cv::Vec3b>(tmp);
}

cv::Vec2b HierarchicalCostMap::at2(const Eigen::Vector2f & position)
{
  if (!cloud_.has_value()) {
    return cv::Vec2b(128, 0);
  }

  Area key(position);
  if (cost_maps_.count(key) == 0) {
    build_map(key);
  }
  map_accessed_[key] = true;

  cv::Point2i tmp = to_cv_point(key, position);
  cv::Vec3b value = cost_maps_.at(key).at<cv::Vec3b>(tmp);
  return {value[0], value[1]};
}

float HierarchicalCostMap::at(const Eigen::Vector2f & position)
{
  Area key(position);
  if (cost_maps_.count(key) == 0) {
    build_map(key);
  }
  map_accessed_[key] = true;

  cv::Point2i tmp = to_cv_point(key, position);
  return cost_maps_.at(key).at<cv::Vec3b>(tmp)[0];
}

void HierarchicalCostMap::set_unmapped_area(const pcl::PointCloud<pcl::PointXYZ> & polygon)
{
  unmapped_polygon_ = polygon;
}

void HierarchicalCostMap::set_cloud(const pcl::PointCloud<pcl::PointNormal> & cloud)
{
  cloud_ = cloud;
}

void HierarchicalCostMap::build_map(const Area & area)
{
  if (!cloud_.has_value()) return;

  cv::Mat image = 255 * cv::Mat::ones(cv::Size(image_size_, image_size_), CV_8UC1);
  cv::Mat orientation = cv::Mat::zeros(cv::Size(image_size_, image_size_), CV_8UC1);

  auto cvPoint = [this, area](const Eigen::Vector3f & p) -> cv::Point {
    return this->to_cv_point(area, p.topRows(2));
  };

  for (const auto pn : cloud_.value()) {
    cv::Point2i from = cvPoint(pn.getVector3fMap());
    cv::Point2i to = cvPoint(pn.getNormalVector3fMap());

    float radian = std::atan2(from.y - to.y, from.x - to.x);
    if (radian < 0) radian += M_PI;
    float degree = radian * 180 / M_PI;

    cv::line(image, from, to, cv::Scalar::all(0), 1);
    cv::line(orientation, from, to, cv::Scalar::all(degree), 1);
  }

  // channel-1
  cv::Mat distance;
  cv::distanceTransform(image, distance, cv::DIST_L2, 3);
  cv::threshold(distance, distance, 100, 100, cv::THRESH_TRUNC);
  distance.convertTo(distance, CV_8UC1, -2.55, 255);

  // channel-2
  cv::Mat whole_orientation = direct_cost_map(orientation, image);

  // channel-3
  cv::Mat available_area = create_available_area_image(area);

  cv::Mat directed_cost_map;
  cv::merge(
    std::vector<cv::Mat>{gamma_converter(distance), whole_orientation, available_area},
    directed_cost_map);

  cost_maps_[area] = directed_cost_map;
  generated_map_history_.push_back(area);

  RCLCPP_INFO_STREAM(
    logger_, "successed to build map " << area(area) << " " << area.real_scale().transpose());
}

HierarchicalCostMap::MarkerArray HierarchicalCostMap::show_map_range() const
{
  MarkerArray array_msg;

  auto gpoint = [](float x, float y) -> geometry_msgs::msg::Point {
    geometry_msgs::msg::Point gp;
    gp.x = x;
    gp.y = y;
    return gp;
  };

  int id = 0;
  for (const Area & area : generated_map_history_) {
    Marker marker;
    marker.header.frame_id = "map";
    marker.id = id++;
    marker.type = Marker::LINE_STRIP;
    marker.color = common::Color(0, 0, 1.0f, 1.0f);
    marker.scale.x = 0.1;
    Eigen::Vector2f xy = area.real_scale();
    marker.points.push_back(gpoint(xy.x(), xy.y()));
    marker.points.push_back(gpoint(xy.x() + area.unit_length_, xy.y()));
    marker.points.push_back(gpoint(xy.x() + area.unit_length_, xy.y() + area.unit_length_));
    marker.points.push_back(gpoint(xy.x(), xy.y() + area.unit_length_));
    marker.points.push_back(gpoint(xy.x(), xy.y()));
    array_msg.markers.push_back(marker);
  }
  return array_msg;
}

cv::Mat HierarchicalCostMap::get_map_image(const Pose & pose)
{
  if (generated_map_history_.empty())
    return cv::Mat::zeros(cv::Size(image_size_, image_size_), CV_8UC3);

  Eigen::Vector2f center;
  center << pose.position.x, pose.position.y;

  float w = pose.orientation.w;
  float z = pose.orientation.z;
  Eigen::Matrix2f R = Eigen::Rotation2Df(2.f * std::atan2(z, w) - M_PI_2).toRotationMatrix();

  auto toVector2f = [this, center, R](float h, float w) -> Eigen::Vector2f {
    Eigen::Vector2f offset;
    offset.x() = (w / this->image_size_ - 0.5f) * this->max_range_;
    offset.y() = -(h / this->image_size_ - 0.5f) * this->max_range_;
    return center + R * offset;
  };

  cv::Mat image = cv::Mat::zeros(cv::Size(image_size_, image_size_), CV_8UC3);
  for (int w = 0; w < image_size_; w++) {
    for (int h = 0; h < image_size_; h++) {
      cv::Vec3b v3 = this->at3(toVector2f(h, w));
      if (v3[2] == 0)
        image.at<cv::Vec3b>(h, w) = cv::Vec3b(v3[1], v3[0], v3[0]);
      else
        image.at<cv::Vec3b>(h, w) = cv::Vec3b(0, 0, 255);
    }
  }

  cv::Mat rgb_image;
  cv::cvtColor(image, rgb_image, cv::COLOR_HSV2BGR);
  return rgb_image;
}

void HierarchicalCostMap::erase_obsolete()
{
  if (cost_maps_.size() < max_map_count_) return;

  for (auto itr = generated_map_history_.begin(); itr != generated_map_history_.end();) {
    if (map_accessed_[*itr]) {
      ++itr;
      continue;
    }
    cost_maps_.erase(*itr);
    itr = generated_map_history_.erase(itr);
  }

  map_accessed_.clear();
}

cv::Mat HierarchicalCostMap::create_available_area_image(const Area & area)
{
  cv::Mat available_area = cv::Mat::zeros(cv::Size(image_size_, image_size_), CV_8UC1);
  if (unmapped_polygon_.empty()) return available_area;

  // TODO: Need roughly check before drawContours because it will be heavy to repeat.
  std::vector<std::vector<cv::Point2i>> contours(1);
  for (const pcl::PointXYZ & p : unmapped_polygon_) {
    contours.front().push_back(to_cv_point(area, {p.x, p.y}));
  }

  cv::drawContours(available_area, contours, 0, cv::Scalar::all(1), -1);
  return available_area;
}

}  // namespace pcdless::common