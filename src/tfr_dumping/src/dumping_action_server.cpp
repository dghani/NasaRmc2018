#include <dumping_action_server.h>

/*
 * The dumping action server, it backs up the rover into the navigational aid
 * slowly.
 *
 * Its first step is to make sure it can see the aruco board, it will abort the
 * mission if it can't. 
 *
 * I backs up at a set speed until it get's really close and loses sight of the
 * board. When it is blind, it drives straight back, goes slower. 
 *
 * It stops when the light detector get's triggered.
 *
 * It requires a service where it can get the most recent image on demand for
 * the camera of interest for backing up. 
 * */
Dumper::Dumper(ros::NodeHandle &node, const std::string &service_name,
        const DumpingConstraints &c) :
    server{node, "dump", boost::bind(&Dumper::dump, this, _1), false},
    detector{"light_detection"},
    //TODO add dumping controller here
    //TODO add aruco here
    constraints{c}
{
    ROS_INFO("dumping action server initializing");
    detector.waitForServer();

    image_client = node.serviceClient<tfr_msgs::WrappedImage>(service_name);

    velocity_publisher = node.advertise<geometry_msgs::Twist>("cmd_vel", 10);

    server.start();
    ROS_INFO("dumping action server initialized");
}

/*
 * The business logic of the action server.
 */
void Dumper::dump(const tfr_msgs::EmptyGoalConstPtr &goal) 
{  
    //check to make sure we can see the board
    auto initial_estimate = get_aruco_estimate();
    if (initial_estimate.markers_detected == 0)
    {
        server.setAborted();
        return;
    }

    //initialize
    tfr_msgs::EmptyGoal empty_goal{};
    detector.sendGoal(empty_goal);

    //loop until we see the light
    while (detector.getState() != actionlib::SimpleClientGoalState::SUCCEEDED)
    {
        //handle preemption
        if (server.isPreemptRequested() || !ros::ok())
        {
            stop_moving();
            server.setPreempted();
            return;
        }

        //get the most recent aruco reading
        tfr_msgs::ArucoIntegrateResult estimate = get_aruco_estimate();

        //send motor commands
        if (estimate.markers_detected == 0)
            move_blind();
        else
        {
            update_control_msg(estimate);
            velocity_publisher.publish(cmd);
        }
    }
    stop_moving();
    server.setSucceeded();
}

/*
 *  Back up and turn slightly to match the orientation of the aruco board
 * */
void Dumper::update_control_msg(const tfr_msgs::ArucoIntegrateResult &estimate)
{
    //back up
    cmd.linear.x = -1 * constraints.get_max_lin_vel();
    /*
     * Maintenence note:
     *
     * How do we decide if we are going left or right?
     * 
     * Well the estimate will return a pose describing displacement from our
     * rear camera, a +y displacement means the center of the board is to the
     * left(ccw), -y to the right (cw). 
     *
     * This conforms to rep 103
     * */
    int sign = (estimate.pose.position.y < 0) ? -1 : 1;
    cmd.angular.z = sign*constraints.get_max_ang_vel();
}

/*
 * back up slowwwwly we can't see
 * */
void Dumper::move_blind()
{
    ROS_INFO("backing up blind");
    cmd.linear.x = -1*constraints.get_min_lin_vel();
    cmd.angular.z = 0;
    velocity_publisher.publish(cmd);
}

/*
 *  Stop Moving 
 * */
void Dumper::stop_moving()
{
    ROS_INFO("Stopped");
    cmd.linear.x = 0;
    cmd.angular.z =0;
    velocity_publisher.publish(cmd);
}

/*
 * Gets the most recent position estimate from the aruco service
 */
tfr_msgs::ArucoIntegrateResult Dumper::get_aruco_estimate()
{
    tfr_msgs::WrappedImage image_request{};
    tfr_msgs::ArucoIntegrateGoal goal{};
    //grab the most recent image
    image_client.call(image_request);
    goal.image = image_request.response.image;
    //TODO hook up aruco here
    tfr_msgs::ArucoIntegrateResult result{};
    result.markers_detected = 1;
    result.pose.position.x= 0.5;
    result.pose.position.y= -0.1;
    result.pose.position.z= -0.02;
    result.pose.orientation.x = 0;
    result.pose.orientation.y = 0;
    result.pose.orientation.z = 1;
    result.pose.orientation.w = 0;
    return result;
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "dumping_action_server");
    ros::NodeHandle n;
    double min_lin_vel, max_lin_vel, min_ang_vel, max_ang_vel;
    ros::param::param<double>("~min_lin_vel",min_lin_vel, 0);
    ros::param::param<double>("~max_lin_vel",max_lin_vel, 0);
    ros::param::param<double>("~min_ang_vel",min_ang_vel, 0);
    ros::param::param<double>("~max_ang_vel",max_ang_vel, 0);
    std::string service_name;
    ros::param::param<std::string>("~image_service_name", service_name, "");
    Dumper::DumpingConstraints constraints(min_lin_vel, max_lin_vel,
            min_ang_vel, max_ang_vel);
    Dumper dumper(n, service_name, constraints);
    ros::spin();
    return 0;
}
