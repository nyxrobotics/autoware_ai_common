/*
 * Copyright 2015-2019 Autoware Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <amathutils_lib/amathutils.hpp>
#include <tf2_eigen/tf2_eigen.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include "ros/console.h"

#include "libwaypoint_follower/libwaypoint_follower.h"

using amathutils::deg2rad;

int WayPoints::getSize() const
{
  if (current_waypoints_.waypoints.empty())
    return 0;
  else
    return current_waypoints_.waypoints.size();
}

double WayPoints::getInterval() const
{
  if (current_waypoints_.waypoints.empty())
    return 0;

  // interval between 2 waypoints
  tf::Vector3 v1(current_waypoints_.waypoints[0].pose.pose.position.x,
                 current_waypoints_.waypoints[0].pose.pose.position.y, 0);

  tf::Vector3 v2(current_waypoints_.waypoints[1].pose.pose.position.x,
                 current_waypoints_.waypoints[1].pose.pose.position.y, 0);
  return tf::tfDistance(v1, v2);
}

geometry_msgs::Point WayPoints::getWaypointPosition(int waypoint) const
{
  geometry_msgs::Point p;
  if (waypoint > getSize() - 1 || waypoint < 0)
    return p;

  p = current_waypoints_.waypoints[waypoint].pose.pose.position;
  return p;
}

geometry_msgs::Quaternion WayPoints::getWaypointOrientation(int waypoint) const
{
  geometry_msgs::Quaternion q;
  if (waypoint > getSize() - 1 || waypoint < 0)
    return q;

  q = current_waypoints_.waypoints[waypoint].pose.pose.orientation;
  return q;
}

geometry_msgs::Pose WayPoints::getWaypointPose(int waypoint) const
{
  geometry_msgs::Pose pose;
  if (waypoint > getSize() - 1 || waypoint < 0)
    return pose;

  pose = current_waypoints_.waypoints[waypoint].pose.pose;
  return pose;
}

double WayPoints::getWaypointVelocityMPS(int waypoint) const
{
  if (waypoint > getSize() - 1 || waypoint < 0)
    return 0;

  return current_waypoints_.waypoints[waypoint].twist.twist.linear.x;
}

bool WayPoints::inDrivingDirection(int waypoint, geometry_msgs::Pose current_pose) const
{
  const LaneDirection dir = getLaneDirection(current_waypoints_);
  double x = calcRelativeCoordinate(current_waypoints_.waypoints[waypoint].pose.pose.position, current_pose).x;
  return (x < 0.0 && dir == LaneDirection::Backward) || (x >= 0.0 && dir == LaneDirection::Forward);
}

double DecelerateVelocity(double distance, double prev_velocity)
{
  double decel_ms = 1.0;  // m/s
  double decel_velocity_ms = std::sqrt(2 * decel_ms * distance);

  std::cout << "velocity/prev_velocity :" << decel_velocity_ms << "/" << prev_velocity << std::endl;
  if (decel_velocity_ms < prev_velocity)
  {
    return decel_velocity_ms;
  }
  else
  {
    return prev_velocity;
  }
}

// calculation relative coordinate of point from current_pose frame
geometry_msgs::Point calcRelativeCoordinate(geometry_msgs::Point point_msg, geometry_msgs::Pose current_pose)
{
  tf::Transform inverse;
  tf::poseMsgToTF(current_pose, inverse);
  tf::Transform transform = inverse.inverse();

  tf::Point p;
  pointMsgToTF(point_msg, p);
  tf::Point tf_p = transform * p;
  geometry_msgs::Point tf_point_msg;
  pointTFToMsg(tf_p, tf_point_msg);

  return tf_point_msg;
}

// calculation absolute coordinate of point on current_pose frame
geometry_msgs::Point calcAbsoluteCoordinate(geometry_msgs::Point point_msg, geometry_msgs::Pose current_pose)
{
  tf::Transform inverse;
  tf::poseMsgToTF(current_pose, inverse);

  tf::Point p;
  pointMsgToTF(point_msg, p);
  tf::Point tf_p = inverse * p;
  geometry_msgs::Point tf_point_msg;
  pointTFToMsg(tf_p, tf_point_msg);
  return tf_point_msg;
}

// distance between target 1 and target2 in 2-D
double getPlaneDistance(geometry_msgs::Point target1, geometry_msgs::Point target2)
{
  tf::Vector3 v1 = point2vector(target1);
  v1.setZ(0);
  tf::Vector3 v2 = point2vector(target2);
  v2.setZ(0);
  return tf::tfDistance(v1, v2);
}

double getRelativeAngle(geometry_msgs::Pose waypoint_pose, geometry_msgs::Pose vehicle_pose)
{
  geometry_msgs::Point relative_p1 = calcRelativeCoordinate(waypoint_pose.position, vehicle_pose);
  geometry_msgs::Point p2;
  p2.x = 1.0;
  geometry_msgs::Point relative_p2 = calcRelativeCoordinate(calcAbsoluteCoordinate(p2, waypoint_pose), vehicle_pose);
  tf::Vector3 relative_waypoint_v(relative_p2.x - relative_p1.x, relative_p2.y - relative_p1.y,
                                  relative_p2.z - relative_p1.z);
  relative_waypoint_v.normalize();
  tf::Vector3 relative_pose_v(1, 0, 0);
  double angle = relative_pose_v.angle(relative_waypoint_v) * 180 / M_PI;

  return angle;
}

geometry_msgs::Pose getRelativeTargetPose(const geometry_msgs::Pose& current_pose,
                                          const geometry_msgs::Pose& target_pose)
{
  geometry_msgs::Pose relative_pose;
  tf::Transform current_tf;
  tf::poseMsgToTF(current_pose, current_tf);
  tf::Transform target_tf;
  tf::poseMsgToTF(target_pose, target_tf);
  tf::Transform relative_tf = current_tf.inverse() * target_tf;
  tf::poseTFToMsg(relative_tf, relative_pose);
  return relative_pose;
}

double getWaypointYaw(const autoware_msgs::Lane& current_path, int current_index)
{
  double current_yaw = tf::getYaw(current_path.waypoints[current_index].pose.pose.orientation);
  if (current_index > 0 && current_index < static_cast<int>(current_path.waypoints.size()) - 1)
  {
    // Obtain target point orientation angle from the behind and the front points
    // get the direction vector of the waypoint
    tf::Vector3 behind_to_current(current_path.waypoints[current_index].pose.pose.position.x -
                                      current_path.waypoints[current_index - 1].pose.pose.position.x,
                                  current_path.waypoints[current_index].pose.pose.position.y -
                                      current_path.waypoints[current_index - 1].pose.pose.position.y,
                                  0);
    tf::Vector3 current_to_front(current_path.waypoints[current_index + 1].pose.pose.position.x -
                                     current_path.waypoints[current_index].pose.pose.position.x,
                                 current_path.waypoints[current_index + 1].pose.pose.position.y -
                                     current_path.waypoints[current_index].pose.pose.position.y,
                                 0);
    double behind_to_current_yaw = atan2(behind_to_current.y(), behind_to_current.x());
    double current_to_front_yaw = atan2(current_to_front.y(), current_to_front.x());
    // If the velocity is negative, the direction of the waypoint is reversed
    if (current_path.waypoints[current_index].twist.twist.linear.x < 0)
    {
      behind_to_current_yaw = normalizeAngle(behind_to_current_yaw + M_PI);
    }
    if (current_path.waypoints[current_index + 1].twist.twist.linear.x < 0)
    {
      current_to_front_yaw = normalizeAngle(current_to_front_yaw + M_PI);
    }
    double angle_diff = normalizeAngle(current_to_front_yaw - behind_to_current_yaw);
    if (fabs(angle_diff) < M_PI)
    {
      current_yaw = normalizeAngle(behind_to_current_yaw + angle_diff / 2);
    }
    else
    {
      current_yaw = current_to_front_yaw;
    }
  }
  else if (current_index > 0)
  {
    tf::Vector3 behind_to_current(current_path.waypoints[current_index].pose.pose.position.x -
                                      current_path.waypoints[current_index - 1].pose.pose.position.x,
                                  current_path.waypoints[current_index].pose.pose.position.y -
                                      current_path.waypoints[current_index - 1].pose.pose.position.y,
                                  0);
    double behind_to_current_yaw = atan2(behind_to_current.y(), behind_to_current.x());
    if (current_path.waypoints[current_index].twist.twist.linear.x < 0)
    {
      behind_to_current_yaw = normalizeAngle(behind_to_current_yaw + M_PI);
    }
    current_yaw = behind_to_current_yaw;
  }
  else if (current_index < static_cast<int>(current_path.waypoints.size()) - 1)
  {
    tf::Vector3 current_to_front(current_path.waypoints[current_index + 1].pose.pose.position.x -
                                     current_path.waypoints[current_index].pose.pose.position.x,
                                 current_path.waypoints[current_index + 1].pose.pose.position.y -
                                     current_path.waypoints[current_index].pose.pose.position.y,
                                 0);
    double current_to_front_yaw = atan2(current_to_front.y(), current_to_front.x());
    if (current_path.waypoints[current_index + 1].twist.twist.linear.x < 0)
    {
      current_to_front_yaw = normalizeAngle(current_to_front_yaw + M_PI);
    }
    current_yaw = current_to_front_yaw;
  }
  return current_yaw;
}

LaneDirection getLaneDirection(const autoware_msgs::Lane& current_path)
{
  const LaneDirection pos_ret = getLaneDirectionByPosition(current_path);
  const LaneDirection vel_ret = getLaneDirectionByVelocity(current_path);
  const bool is_conflict =
      (pos_ret != vel_ret) && (pos_ret != LaneDirection::Error) && (vel_ret != LaneDirection::Error);
  return is_conflict ? LaneDirection::Error : (pos_ret != LaneDirection::Error) ? pos_ret : vel_ret;
}

LaneDirection getLaneDirectionByPosition(const autoware_msgs::Lane& current_path)
{
  if (current_path.waypoints.size() < 2)
  {
    return LaneDirection::Error;
  }
  LaneDirection positional_direction = LaneDirection::Error;
  for (size_t i = 1; i < current_path.waypoints.size(); i++)
  {
    const geometry_msgs::Pose& prev_pose = current_path.waypoints[i - 1].pose.pose;
    const geometry_msgs::Pose& next_pose = current_path.waypoints[i].pose.pose;
    const double rlt_x = calcRelativeCoordinate(next_pose.position, prev_pose).x;
    if (std::fabs(rlt_x) < 1e-3)
    {
      continue;
    }
    positional_direction = (rlt_x < 0) ? LaneDirection::Backward : LaneDirection::Forward;
    break;
  }
  return positional_direction;
}

LaneDirection getLaneDirectionByVelocity(const autoware_msgs::Lane& current_path)
{
  LaneDirection velocity_direction = LaneDirection::Error;
  for (const auto waypoint : current_path.waypoints)
  {
    const double& vel = waypoint.twist.twist.linear.x;
    if (std::fabs(vel) < 0.01)
    {
      continue;
    }
    velocity_direction = (vel < 0) ? LaneDirection::Backward : LaneDirection::Forward;
    break;
  }
  return velocity_direction;
}

class MinIDSearch
{
private:
  double val_min_;
  int idx_min_;

public:
  MinIDSearch() : val_min_(DBL_MAX), idx_min_(-1)
  {
  }
  void update(int index, double v)
  {
    if (v < val_min_)
    {
      idx_min_ = index;
      val_min_ = v;
    }
  }
  const int result() const
  {
    return idx_min_;
  }
  const bool isOK() const
  {
    return (idx_min_ != -1);
  }
};

int getClosestIndex(const autoware_msgs::Lane& current_path, geometry_msgs::Pose current_pose)
{
  if (current_path.waypoints.size() < 2)
  {
    ROS_WARN("waypoints size is too small (size = %lu)", current_path.waypoints.size());
    return -1;
  }

  // Initialize current_waypoint_index_
  int closest_index = -1;
  static const double valid_distance = 5.0;
  static const double valid_angle = M_PI / 2;
  double min_distance = valid_distance;
  double robot_yaw = tf::getYaw(current_pose.orientation);
  for (size_t i = 0; i < current_path.waypoints.size(); i++)
  {
    double distance = getPlaneDistance(current_path.waypoints[i].pose.pose.position, current_pose.position);
    double waypoint_yaw = getWaypointYaw(current_path, i);
    double angle_diff = normalizeAngle(waypoint_yaw - robot_yaw);

    if (distance < min_distance && fabs(angle_diff) < valid_angle)
    {
      min_distance = distance;
      closest_index = i;
    }
    else if (closest_index == -1)
    {
      ROS_WARN("Failed to find closest waypoint. distance: %f, waypoint_yaw: %f, robot_yaw: %f", distance, waypoint_yaw,
               robot_yaw);
    }
  }
  if (closest_index != -1)
  {
    return closest_index;
  }
  // If there is no waypoint in the forward direction, find the closest waypoint
  min_distance = std::numeric_limits<double>::max();
  for (size_t i = 0; i < current_path.waypoints.size(); i++)
  {
    double distance = getPlaneDistance(current_path.waypoints[i].pose.pose.position, current_pose.position);
    if (distance < min_distance)
    {
      min_distance = distance;
      closest_index = i;
    }
  }
  return closest_index;
}

int updateCurrentIndex(const autoware_msgs::Lane& current_path, geometry_msgs::Pose current_pose, int current_index)
{
  if (current_path.waypoints.size() < 2 || current_index > static_cast<int>(current_path.waypoints.size()) - 1)
  {
    ROS_WARN("Failed to update current index. size: %lu, index: %d", current_path.waypoints.size(), current_index);
    return -1;
  }

  int next_index = current_index;
  if (current_index < 0)
  {
    // Initialize current_waypoint_index_
    next_index = getClosestIndex(current_path, current_pose);
    return next_index;
  }
  else
  {
    // Update current_waypoint_index_
    const int path_size = static_cast<int>(current_path.waypoints.size());
    // If no waypoints are given, do nothing.
    if (path_size < 1)
    {
      return -1;
    }
    int start_index = current_index;
    if (start_index < path_size - 1 && start_index > 0)
    {
      int start_index_offset = 0;
      for (int i = start_index; i < path_size - 1; i++)
      {
        double prev_velocity = current_path.waypoints.at(i - 1).twist.twist.linear.x;
        double current_velocity = current_path.waypoints.at(i).twist.twist.linear.x;
        double next_velocity = current_path.waypoints.at(i + 1).twist.twist.linear.x;

        double prev_distance =
            getPlaneDistance(current_pose.position, current_path.waypoints.at(i - 1).pose.pose.position);
        double current_distance =
            getPlaneDistance(current_pose.position, current_path.waypoints.at(i).pose.pose.position);
        double next_distance =
            getPlaneDistance(current_pose.position, current_path.waypoints.at(i + 1).pose.pose.position);
        if (current_velocity * next_velocity < 0 && start_index_offset >= 0)
        {
          // If the velocity changes its sign, the current waypoint is the next waypoint
          // This is to avoid the case where the vehicle is at the switchback point
          start_index_offset += 1;
        }
        else if (current_velocity * next_velocity > 0 && next_distance < current_distance && start_index_offset >= 0)
        {
          start_index_offset += 1;
        }
        else if (prev_velocity * current_velocity > 0 && prev_distance < current_distance && start_index_offset <= 0)
        {
          start_index_offset -= 1;
        }
        else
        {
          break;
        }
      }
      start_index += start_index_offset;
    }
    start_index = std::min(std::max(0, start_index), path_size - 1);

    double prev_distance = std::numeric_limits<double>::max();
    // Loop to find the index where the distance first starts to increase
    for (int i = start_index; i < path_size; i++)
    {
      double current_distance =
          getPlaneDistance(current_path.waypoints.at(i).pose.pose.position, current_pose.position);
      // If the distance increases, set the previous index as the current waypoint
      if (current_distance > prev_distance)
      {
        next_index = i - 1;
        break;
      }
      else
      {
        // Update the previous distance for the next comparison
        prev_distance = current_distance;
      }
    }
    next_index = std::min(std::max(0, next_index), path_size - 1);
    return next_index;
  }
  ROS_WARN("Failed to update current index. Unknown error.");
  return -1;
}

// let the linear equation be "ax + by + c = 0"
// if there are two points (x1,y1) , (x2,y2), a = "y2-y1, b = "(-1) * x2 - x1" ,c = "(-1) * (y2-y1)x1 + (x2-x1)y1"
bool getLinearEquation(geometry_msgs::Point start, geometry_msgs::Point end, double* a, double* b, double* c)
{
  // (x1, y1) = (start.x, star.y), (x2, y2) = (end.x, end.y)
  double sub_x = std::fabs(start.x - end.x);
  double sub_y = std::fabs(start.y - end.y);
  double error = std::pow(10, -5);  // 0.00001

  if (sub_x < error && sub_y < error)
  {
    return false;
  }

  *a = end.y - start.y;
  *b = (-1) * (end.x - start.x);
  *c = (-1) * (end.y - start.y) * start.x + (end.x - start.x) * start.y;

  return true;
}
double getDistanceBetweenLineAndPoint(geometry_msgs::Point point, double a, double b, double c)
{
  double d = std::fabs(a * point.x + b * point.y + c) / std::sqrt(std::pow(a, 2) + std::pow(b, 2));

  return d;
}

tf::Vector3 point2vector(geometry_msgs::Point point)
{
  tf::Vector3 vector(point.x, point.y, point.z);
  return vector;
}

geometry_msgs::Point vector2point(tf::Vector3 vector)
{
  geometry_msgs::Point point;
  point.x = vector.getX();
  point.y = vector.getY();
  point.z = vector.getZ();
  return point;
}

tf::Vector3 rotateUnitVector(tf::Vector3 unit_vector, double degree)
{
  tf::Vector3 w1(cos(deg2rad(degree)) * unit_vector.getX() - sin(deg2rad(degree)) * unit_vector.getY(),
                 sin(deg2rad(degree)) * unit_vector.getX() + cos(deg2rad(degree)) * unit_vector.getY(), 0);
  tf::Vector3 unit_w1 = w1.normalize();

  return unit_w1;
}

geometry_msgs::Point rotatePoint(geometry_msgs::Point point, double degree)
{
  geometry_msgs::Point rotate;
  rotate.x = cos(deg2rad(degree)) * point.x - sin(deg2rad(degree)) * point.y;
  rotate.y = sin(deg2rad(degree)) * point.x + cos(deg2rad(degree)) * point.y;

  return rotate;
}

double calcCurvature(const geometry_msgs::Point& target, const geometry_msgs::Pose& curr_pose)
{
  constexpr double KAPPA_MAX = 1e9;
  const double radius = calcRadius(target, curr_pose);

  if (std::fabs(radius) > 0)
  {
    return 1.0 / radius;
  }
  else
  {
    return KAPPA_MAX;
  }
}

double calcDistSquared2D(const geometry_msgs::Point& p, const geometry_msgs::Point& q)
{
  const double dx = p.x - q.x;
  const double dy = p.y - q.y;
  return (dx * dx + dy * dy);
}

double calcLateralError2D(const geometry_msgs::Point& line_s, const geometry_msgs::Point& line_e,
                          const geometry_msgs::Point& point)
{
  tf2::Vector3 a_vec((line_e.x - line_s.x), (line_e.y - line_s.y), 0.0);
  tf2::Vector3 b_vec((point.x - line_s.x), (point.y - line_s.y), 0.0);

  double lat_err = (a_vec.length() > 0) ? a_vec.cross(b_vec).z() / a_vec.length() : 0.0;
  return lat_err;
}

double calcRadius(const geometry_msgs::Point& target, const geometry_msgs::Pose& current_pose)
{
  constexpr double RADIUS_MAX = 1e9;
  const double denominator = 2.0 * transformToRelativeCoordinate2D(target, current_pose).y;
  const double numerator = calcDistSquared2D(target, current_pose.position);

  if (std::fabs(denominator) > 0)
    return numerator / denominator;
  else
    return RADIUS_MAX;
}

std::vector<geometry_msgs::Pose> extractPoses(const autoware_msgs::Lane& lane)
{
  std::vector<geometry_msgs::Pose> poses;

  for (const auto& el : lane.waypoints)
    poses.push_back(el.pose.pose);

  return poses;
}

std::vector<geometry_msgs::Pose> extractPoses(const std::vector<autoware_msgs::Waypoint>& wps)
{
  std::vector<geometry_msgs::Pose> poses;

  for (const auto& el : wps)
    poses.push_back(el.pose.pose);

  return poses;
}

std::pair<bool, int32_t> findClosestIdxWithDistAngThr(const std::vector<geometry_msgs::Pose>& curr_ps,
                                                      const geometry_msgs::Pose& curr_pose, double dist_thr,
                                                      double angle_thr)
{
  double dist_squared_min = std::numeric_limits<double>::max();
  int32_t idx_min = -1;

  for (int32_t i = 0; i < static_cast<int32_t>(curr_ps.size()); ++i)
  {
    const double ds = calcDistSquared2D(curr_ps.at(i).position, curr_pose.position);
    if (ds > dist_thr * dist_thr)
      continue;

    double yaw_pose = tf2::getYaw(curr_pose.orientation);
    double yaw_ps = tf2::getYaw(curr_ps.at(i).orientation);
    double yaw_diff = normalizeAngle(yaw_pose - yaw_ps);
    if (std::fabs(yaw_diff) > angle_thr)
      continue;

    if (ds < dist_squared_min)
    {
      dist_squared_min = ds;
      idx_min = i;
    }
  }

  return (idx_min >= 0) ? std::make_pair(true, idx_min) : std::make_pair(false, idx_min);
}

geometry_msgs::Quaternion getQuaternionFromYaw(const double& _yaw)
{
  tf2::Quaternion q;
  q.setRPY(0, 0, _yaw);
  return tf2::toMsg(q);
}

bool isDirectionForward(const std::vector<geometry_msgs::Pose>& poses)
{
  geometry_msgs::Point rel_p = transformToRelativeCoordinate2D(poses.at(2).position, poses.at(1));
  bool is_forward = (rel_p.x > 0.0) ? true : false;
  return is_forward;
}

double normalizeAngle(double euler)
{
  double res = euler;
  while (res > M_PI)
  {
    res -= (2.0 * M_PI);
  }
  while (res < -M_PI)
  {
    res += 2.0 * M_PI;
  }

  return res;
}

geometry_msgs::Point transformToAbsoluteCoordinate2D(const geometry_msgs::Point& point,
                                                     const geometry_msgs::Pose& origin)
{
  // rotation
  geometry_msgs::Point rot_p;
  double yaw = tf2::getYaw(origin.orientation);
  rot_p.x = (cos(yaw) * point.x) + ((-1.0) * sin(yaw) * point.y);
  rot_p.y = (sin(yaw) * point.x) + (cos(yaw) * point.y);

  // translation
  geometry_msgs::Point res;
  res.x = rot_p.x + origin.position.x;
  res.y = rot_p.y + origin.position.y;
  res.z = origin.position.z;

  return res;
}

geometry_msgs::Point transformToAbsoluteCoordinate3D(const geometry_msgs::Point& point,
                                                     const geometry_msgs::Pose& origin)
{
  Eigen::Translation3d trans(origin.position.x, origin.position.y, origin.position.z);
  Eigen::Quaterniond rot(origin.orientation.w, origin.orientation.x, origin.orientation.y, origin.orientation.z);

  Eigen::Vector3d v(point.x, point.y, point.z);
  Eigen::Vector3d transformed_v;
  transformed_v = trans * rot.inverse() * v;

  geometry_msgs::Point transformed_p = tf2::toMsg(transformed_v);
  return transformed_p;
}

geometry_msgs::Point transformToRelativeCoordinate2D(const geometry_msgs::Point& point,
                                                     const geometry_msgs::Pose& origin)
{
  // translation
  geometry_msgs::Point trans_p;
  trans_p.x = point.x - origin.position.x;
  trans_p.y = point.y - origin.position.y;

  // rotation (use inverse matrix of rotation)
  double yaw = tf2::getYaw(origin.orientation);

  geometry_msgs::Point res;
  res.x = (cos(yaw) * trans_p.x) + (sin(yaw) * trans_p.y);
  res.y = ((-1.0) * sin(yaw) * trans_p.x) + (cos(yaw) * trans_p.y);
  res.z = origin.position.z;

  return res;
}

geometry_msgs::Point transformToRelativeCoordinate3D(const geometry_msgs::Point& point,
                                                     const geometry_msgs::Pose& origin)
{
  Eigen::Translation3d trans(-origin.position.x, -origin.position.y, -origin.position.z);
  Eigen::Quaterniond rot(origin.orientation.w, origin.orientation.x, origin.orientation.y, origin.orientation.z);

  Eigen::Vector3d v(point.x, point.y, point.z);
  Eigen::Vector3d transformed_v;
  transformed_v = trans * rot * v;

  geometry_msgs::Point transformed_p = tf2::toMsg(transformed_v);
  return transformed_p;
}
