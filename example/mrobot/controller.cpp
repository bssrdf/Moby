/*****************************************************************************
 * Controller for mobile robot 
 ****************************************************************************/
#include <Moby/EventDrivenSimulator.h>
#include <Moby/RCArticulatedBody.h>
#include <Ravelin/Pose3d.h>
#include <Ravelin/Vector3d.h>
#include <Ravelin/VectorNd.h>
#include <fstream>

boost::shared_ptr<Moby::EventDrivenSimulator> sim;

using namespace Ravelin;
using namespace Moby;

// set the desired wheel speeds
const double UL = 1.0;
const double UR = 0.5;

// setup the controller callback
void controller(DynamicBodyPtr body, double t, void*)
{
  const unsigned LEFT = 0, RIGHT = 1;
  static bool first_time = true;
  static Origin3d x0;

  // get the robot
  RCArticulatedBodyPtr robot = boost::dynamic_pointer_cast<RCArticulatedBody>(body);

  // get the state and velocity
  VectorNd q, dq;
  robot->get_generalized_coordinates(DynamicBody::eEuler, q);
  robot->get_generalized_velocity(DynamicBody::eSpatial, dq);

  // see whether this is the first time this function is run
  if (first_time)
  {
    first_time = false;

    // init x(t_0), y(t_0), theta(t_0)
    x0[0] = q[2];
    x0[1] = q[3];
    Quatd quat(q[5], q[6], q[7], q[8]);
    Matrix3d R = quat;
    x0[2] = std::atan2(R(2,0), R(0,0));
  }

  // setup the PD controller
  const double KV = 1000.0;

  // set dq_des, ddq_des;
  double dq_des[2], ddq_des[2];
  dq_des[LEFT] = UL;
  dq_des[RIGHT] = UR;
  ddq_des[LEFT] = ddq_des[RIGHT] = 0.0;

  // compute inverse dynamics torques

std::cout << "L: " << dq[0] << " R: " << dq[1] << std::endl;
  // setup the feedback torques
  VectorNd fleft(1), fright(1);
  fleft[0] = KV*(dq_des[LEFT] - dq[LEFT]);
  fright[0] = KV*(dq_des[RIGHT] - dq[RIGHT]);

  // collect state data
  std::ofstream out("state.data", std::ostream::app);
  out << t;
  for (unsigned i=0; i< q.size(); i++)
    out << " " << q[i];
  for (unsigned i=0; i< dq.size(); i++)
    out << " " << dq[i];
  out << std::endl;
  out.close();

  // apply the torques
  JointPtr left = robot->get_joints()[0];
  JointPtr right = robot->get_joints()[1];
  assert(left->id == "left_wheel_hinge");
  assert(right->id == "right_wheel_hinge");
  left->add_force(fleft);
  right->add_force(fright);
}

void post_event_callback_fn(const std::vector<Moby::UnilateralConstraint>& e,
                            boost::shared_ptr<void> empty)
{
  std::cout << ">> start post_event_callback_fn(.)" << std::endl;

  // PROCESS CONTACTS
  for(unsigned i=0;i<e.size();i++){
    if (e[i].constraint_type == Moby::UnilateralConstraint::eContact)
    {
      Moby::SingleBodyPtr sb1 = e[i].contact_geom1->get_single_body();
      Moby::SingleBodyPtr sb2 = e[i].contact_geom2->get_single_body();

      std::cout << "contact: " << sb1->id << " and " << sb2->id << std::endl;
      std::cout << "i = " << e[i].contact_impulse.get_linear() << std::endl;
      std::cout << "p = " << e[i].contact_point << std::endl;
      std::cout << "n = " << e[i].contact_normal << std::endl;
//      std::cout << "s = " << e[i].contact_tan1 << std::endl;
//      std::cout << "t = " << e[i].contact_tan2 << std::endl;
//      std::cout << "muC = " << e[i].contact_mu_coulomb << std::endl;
//      std::cout << "muV = " << e[i].contact_mu_viscous << std::endl;
    }
  }
  std::cout << "<< end post_event_callback_fn(.)" << std::endl;
}

// ============================================================================
// ================================ CALLBACKS =================================

/// plugin must be "extern C"
extern "C" {

void init(void* separator, const std::map<std::string, Moby::BasePtr>& read_map, double time)
{
  Moby::RCArticulatedBodyPtr robot;

  // get a reference to the EventDrivenSimulator instance and the robot
  for (std::map<std::string, Moby::BasePtr>::const_iterator i = read_map.begin();
       i !=read_map.end(); i++)
  {
    // Find the simulator reference
    if (!sim)
      sim = boost::dynamic_pointer_cast<Moby::EventDrivenSimulator>(i->second);

    // find the robot reference
    if (!robot)
      robot = boost::dynamic_pointer_cast<Moby::RCArticulatedBody>(i->second);
  }

  // make sure the robot was found
  assert(robot);

  // set the controller
  robot->controller = &controller;

  // determine the velocity of the robot's base using initial conditions
  const double WHEEL_RAD = 0.11;
  const double AXLE_LEN = 0.34;
  const double THETA0 = 0.0;
  double xd0 = WHEEL_RAD * 0.5 * (UL + UR) * std::cos(THETA0);
  double yd0 = WHEEL_RAD * 0.5 * (UL + UR) * std::sin(THETA0);
  double thetad0 = WHEEL_RAD/AXLE_LEN * (UR - UL);

  // set the velocity of the robot's base
  const unsigned X = 0, Y = 1, THETA = 2;
  SVelocityd base_xd(GLOBAL);
  Vector3d lv(GLOBAL), av(GLOBAL);
  lv.set_zero();
  av.set_zero();
  lv[X] = xd0;
  lv[Y] = yd0;
  av[THETA] = thetad0;
  base_xd.set_angular(av);
  base_xd.set_linear(lv);
  robot->get_base_link()->set_velocity(base_xd);

  // set the velocities at the robot's wheels
  JointPtr left = robot->get_joints()[0];
  JointPtr right = robot->get_joints()[1];
  assert(left->id == "left_wheel_hinge");
  assert(right->id == "right_wheel_hinge");
  left->qd[0] = UL;
  right->qd[0] = UR;

  // update the robot's link velocities
  robot->update_link_velocities();

  // clear state data
  std::ofstream out("state.data");
  out.close();
}
} // end extern C

