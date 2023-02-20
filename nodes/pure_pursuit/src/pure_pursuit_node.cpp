#include <memory> //for smart pointers
//#include <chrono> //for time 
#include <math.h>
#include <string>
#include <cstdlib> //for abs value function
#include <vector> /// CHECK: might cause errors due to double header in linker
#include <sstream> //for handling csv extraction
#include <iostream> //both for input/output and file stream and handling
#include <fstream>
#include <algorithm> //for max function

#include <Eigen/Eigen>


//ROS related headers
#include "rclcpp/rclcpp.hpp"
//message header
#include "nav_msgs/msg/odometry.hpp"
#include "ackermann_msgs/msg/ackermann_drive_stamped.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "visualization_msgs/msg/marker.hpp"
#include "visualization_msgs/msg/marker_array.hpp"
//tf2 headers
#include <tf2_ros/transform_listener.h>
#include <tf2_ros/buffer.h>
#include <geometry_msgs/msg/transform_stamped.hpp>
//#include <tf2/LinearMath/Matrix3x3.h>
//#include <tf2/LinearMath/Quaternion.h>

#define SIMULATION 1 // TO CHANGE TO 0 WHEN RUNNING THE CAR PHYSICALLY

#define _USE_MATH_DEFINES
#define USE_VARIABLE_LOOKAHEAD 0
#define MAX_LOOKAHEAD 2.0 // in meters
#define K_p 0.5
#define STEERING_LIMIT 25 //degrees

#if SIMULATION
    #define WAYPOINTS_PATH "/sim_ws/src/pure_pursuit/src/waypoints_odom.csv"
#else
    #define WAYPOINTS_PATH "/f1tenth_ws/src/pure_pursuit/src/waypoints_odom.csv"
#endif
//using namespaces 
//used for bind (uncomment below)
using std::placeholders::_1;
//using namespace Eigen;
//using namespace std; --> may need
//using namespace std::chrono_literals;


class PurePursuit : public rclcpp::Node {

public:
    PurePursuit() : Node("pure_pursuit_node") {
        // initialise parameters
        //set parameter during run time with: ros2 param set <node_name> <parameter_name> <value>
        //set parameter during launch time with: -p :=

        //initialise subscriber sharedptr obj
        subscription_odom = this->create_subscription<nav_msgs::msg::Odometry>(odom_topic, 25, std::bind(&PurePursuit::odom_callback, this, _1));

        //initialise publisher sharedptr obj
        publisher_drive = this->create_publisher<ackermann_msgs::msg::AckermannDriveStamped>(drive_topic, 25);
        //vis_path_pub = this->create_publisher<visualization_msgs::msg::MarkerArray>(rviz_waypoints_topic, 1000);
        vis_point_pub = this->create_publisher<visualization_msgs::msg::Marker>(rviz_waypointselected_topic, 10);

        //initialise tf2 shared pointers
        tf_buffer_ = std::make_unique<tf2_ros::Buffer>(this->get_clock());
        transform_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

        RCLCPP_INFO (this->get_logger(), "this node has been launched");

        download_waypoints();

        //copy all data once to data structure initialised before

    }

    ~PurePursuit() {}

    //EIGEN_MAKE_ALIGNED_OPERATOR_NEW
private:
    //global static (to be shared by all objects) and dynamic variables (each instance gets its own copy -> managed on the stack)
    struct csvFileData{
        std::vector<double> X;
        std::vector<double> Y;
        //double x_worldRef, y_worldRef, x_carRef, y_carRef;
        int index;

        Eigen::Vector3d p1_world; // Coordinate of point from world reference frame
        Eigen::Vector3d p1_car; // Coordinate of point from car reference frame (to be calculated)
    };

    Eigen::Matrix3d rotation_m;


    //topic names
    #if SIMULATION
        std::string odom_topic = "/ego_racecar/odom";
        std::string car_refFrame = "ego_racecar/base_link";
    #else
        std::string odom_topic = "/pf/pose/odom"; // from here: https://github.com/f1tenth/particle_filter/blob/foxy-devel/particle_filter/particle_filter.py
        std::string car_refFrame = "laser"; // TODO: Publish transform from base_link to laser, this is not done automatically? 
    #endif
    std::string drive_topic = "/drive";
    std::string global_refFrame = "map"; //TODO: might have to add backslashes before
    //std::string rviz_waypoints_topic = "/waypoints";
    std::string rviz_waypointselected_topic = "/waypoints_selected";

    
    //file object
    std::fstream csvFile_waypoints; 

    //struct initialisation
    csvFileData waypoints;
    int num_waypoints;

    //declare subscriber sharedpointer obj
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr subscription_odom;

    //declare publisher sharedpointer obj
    rclcpp::Publisher<ackermann_msgs::msg::AckermannDriveStamped>::SharedPtr publisher_drive; 
    //rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr vis_path_pub; 
    rclcpp::Publisher<visualization_msgs::msg::Marker>::SharedPtr vis_point_pub; 

    //declare tf shared pointers
    std::shared_ptr<tf2_ros::TransformListener> transform_listener_{nullptr};
    std::unique_ptr<tf2_ros::Buffer> tf_buffer_;

    //private functions
    double to_radians(double degrees) {
        double radians;
        return radians = degrees*M_PI/180.0;
    }

    double to_degrees(double radians) {
        double degrees;
        return degrees = radians*180.0/M_PI;
    }
    
    double p2pdist(double &x1, double &x2, double &y1, double &y2) {
        double dist = sqrt(pow((x2-x1),2)+pow((y2-y1),2));
        return dist;
    }

    void download_waypoints () { //put all data in vectors
        csvFile_waypoints.open(WAYPOINTS_PATH, std::ios::in);

        if (!csvFile_waypoints.is_open()) {
            RCLCPP_ERROR(this->get_logger(), "%s", "Cannot open CSV file!");
            return;
        } else {
            RCLCPP_INFO(this->get_logger(), "CSV File Opened");

        }

        
        //std::vector<std::string> row;
        std::string line, word, temp;

        while (!csvFile_waypoints.eof()) {
            std::getline(csvFile_waypoints, line, '\n');
            std::stringstream s(line); 

            int j = 0;
            while (getline(s, word, ',')) {
                if (!word.empty()) {
                    if (j == 0) {
                        double x = std::stod(word);
                        waypoints.X.push_back(x);
                    } else if (j == 1) {
                        waypoints.Y.push_back(std::stod(word));
                    }
                }
                j++;
            }
        }

        csvFile_waypoints.close();
        num_waypoints = waypoints.X.size();
        RCLCPP_INFO(this->get_logger(), "Finished loading %d waypoints from %d", num_waypoints, WAYPOINTS_PATH);
        
        double average_dist_between_waypoints = 0.0;
        for (int i=0;i<num_waypoints-1;i++) {
            average_dist_between_waypoints += p2pdist(waypoints.X[i], waypoints.X[i+1], waypoints.Y[i], waypoints.Y[i+1]);
        }
        average_dist_between_waypoints /= num_waypoints;
        RCLCPP_INFO(this->get_logger(), "Average distance between waypoints: %f", average_dist_between_waypoints);
    }

    void visualize_points(Eigen::Vector3d &point) {
        auto marker = visualization_msgs::msg::Marker();
        marker.header.frame_id = "map";
        marker.header.stamp = rclcpp::Clock().now();
        marker.type = visualization_msgs::msg::Marker::SPHERE;
        marker.action = visualization_msgs::msg::Marker::ADD;
        marker.scale.x = 0.15;
        marker.scale.y = 0.15;
        marker.scale.z = 0.15;
        marker.color.a = 1.0; 
        marker.color.r = 1.0;

        marker.pose.position.x = point(0);
        marker.pose.position.y = point(1);
        marker.id = 1;
        vis_point_pub->publish(marker);

    }

    void get_waypoint(double x_car_world, double y_car_world) {

        double longest_distance = 0;
        int final_i = -1;
        // Main logic: Search within the next 500 points
        int start = waypoints.index;
        int end = (waypoints.index + 500) % num_waypoints;
        
        // TODO: Add dynamic lookahead distance based on velocity if the flag USE_VARIABLE_LOOKAHEAD is set

        if (end < start) { // Loop around
            for (int i=start; i<num_waypoints; i++) {
                if (p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world) <= MAX_LOOKAHEAD 
                && p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world) >= longest_distance) {
                    longest_distance = p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world);
                    final_i = i;
                }
            }
            for (int i=0; i<end; i++) {
                if (p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world) <= MAX_LOOKAHEAD 
                && p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world) >= longest_distance) {
                    longest_distance = p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world);
                    final_i = i;
                }
            }
        } else {
            for (int i=start; i<end; i++) {
                if (p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world) <= MAX_LOOKAHEAD 
                && p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world) >= longest_distance) {
                    longest_distance = p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world);
                    final_i = i;
                }
            }

        }

        if (final_i == -1) { // if we haven't found anything, search from the beginning
            final_i = 0; // Temporarily assign to 0
            for (unsigned int i=0;i<waypoints.X.size(); i++) {
                if (p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world) <= MAX_LOOKAHEAD 
                && p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world) >= longest_distance) {
                    longest_distance = p2pdist(waypoints.X[i], x_car_world, waypoints.Y[i], y_car_world);
                    final_i = i;
                }
            }
        }
        waypoints.index = final_i; // If a waypoint is not found, then this temporarily sets it to 0. Allows us to look around the waypoints forever
        RCLCPP_INFO(this->get_logger(), "waypoint index: %d ... distance: %f", waypoints.index, p2pdist(waypoints.X[waypoints.index], x_car_world, waypoints.Y[waypoints.index], y_car_world));
    }

    void quat_to_rot(double q0, double q1, double q2, double q3) { //w,x,y,z -> q0,q1,q2,q3
        double r00 = (double)(2.0 * (q0 * q0 + q1 * q1) - 1.0);
        double r01 = (double)(2.0 * (q1 * q2 - q0 * q3));
        double r02 = (double)(2.0 * (q1 * q3 + q0 * q2));
     
        double r10 = (double)(2.0 * (q1 * q2 + q0 * q3));
        double r11 = (double)(2.0 * (q0 * q0 + q2 * q2) - 1.0);
        double r12 = (double)(2.0 * (q2 * q3 - q0 * q1));
     
        double r20 = (double)(2.0 * (q1 * q3 - q0 * q2));
        double r21 = (double)(2.0 * (q2 * q3 + q0 * q1));
        double r22 = (double)(2.0 * (q0 * q0 + q3 * q3) - 1.0);

        rotation_m << r00, r01, r02, r10, r11, r12, r20, r21, r22; //fill rotation matrix
    }

    void transformandinterp_waypoint() { //pass old waypoint here
        //initialise vectors
        waypoints.p1_world << waypoints.X[waypoints.index], waypoints.Y[waypoints.index], 0.0;
        //waypoints.p2_world << waypoints.X[waypoints.index+1], waypoints.Y[waypoints.index+1], 0.0;

        //rviz publish way point
        visualize_points(waypoints.p1_world);
        //look up transformation at that instant from tf_buffer_
        geometry_msgs::msg::TransformStamped transformStamped;
        
        try {
            // Get the transform from the base_link reference to world reference frame
            transformStamped = tf_buffer_->lookupTransform(car_refFrame, global_refFrame,tf2::TimePointZero);
        } 
        catch (tf2::TransformException & ex) {
            RCLCPP_INFO(this->get_logger(), "Could not transform. Error: %s", ex.what());
        }

        //transform points (rotate first and then translate)
        Eigen::Vector3d translation_v(transformStamped.transform.translation.x, transformStamped.transform.translation.y, transformStamped.transform.translation.z);
        quat_to_rot(transformStamped.transform.rotation.w, transformStamped.transform.rotation.x, transformStamped.transform.rotation.y, transformStamped.transform.rotation.z);

        waypoints.p1_car = (rotation_m*waypoints.p1_world) + translation_v;
    }

    double p_controller(const nav_msgs::msg::Odometry::ConstSharedPtr odom_submsgObj) { // pass waypoint
        /*
        To design our P controller, we define our error term to be the angle the car needs to correct itself, e = theta = y/r.

        Ideally, waypoints.p1_car(1) = 0, meaning we are aligned with our waypoint.

        */
        double r = waypoints.p1_car.norm(); // r = sqrt(x^2 + y^2)
        double y = waypoints.p1_car(1);
        double angle = K_p * asin(y / r);

        return angle;
    }

    double get_velocity(double steering_angle) {
        double velocity;
        if (abs(steering_angle) >= to_radians(0.0) && abs(steering_angle) < to_radians(10.0)) {
            velocity = 4.5; // If you want to go crazy
            // velocity = 1.5;
        } 
        else if (abs(steering_angle) >= to_radians(10.0) && abs(steering_angle) <= to_radians(20.0)) {
            velocity = 2.5;
        } 
        else {
            velocity = 2.0;
        }

        return velocity;
    }

    void publish_message (double steering_angle) {
        auto drive_msgObj = ackermann_msgs::msg::AckermannDriveStamped();
        if (steering_angle < 0.0) {
            drive_msgObj.drive.steering_angle = std::max(steering_angle, -to_radians(STEERING_LIMIT)); //ensure steering angle is dynamically capable
        } else {
            drive_msgObj.drive.steering_angle = std::min(steering_angle, to_radians(STEERING_LIMIT)); //ensure steering angle is dynamically capable
        }

        drive_msgObj.drive.speed = get_velocity(drive_msgObj.drive.steering_angle);

        publisher_drive->publish(drive_msgObj);
        RCLCPP_INFO(this->get_logger(), "waypoint x y : %f %f ", waypoints.p1_car(0), waypoints.p1_car(1));
        RCLCPP_INFO (this->get_logger(), "Speed: %f ..... Steering Angle: %f", drive_msgObj.drive.speed, to_degrees(drive_msgObj.drive.steering_angle));
    }

    void odom_callback(const nav_msgs::msg::Odometry::ConstSharedPtr odom_submsgObj) {
        double x_car_world = odom_submsgObj->pose.pose.position.x;
        double y_car_world = odom_submsgObj->pose.pose.position.y;
        // TODO: find the current waypoint to track using methods mentioned in lecture
        // interpolate between different way-points 
        get_waypoint(x_car_world, y_car_world);

        //use tf2 transform the goal point 
        transformandinterp_waypoint();

        // Calculate curvature/steering angle
        //p controller
        double steering_angle = p_controller(odom_submsgObj);

        // TODO: publish drive message, don't forget to limit the steering angle.
        //publish object and message: AckermannDriveStamped on drive topic 
        publish_message(steering_angle);
    }

};



int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node_ptr = std::make_shared<PurePursuit>(); // initialise node pointer
    rclcpp::spin(node_ptr);
    rclcpp::shutdown();
    return 0;
}