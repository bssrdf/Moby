/****************************************************************************
 * Copyright 2011 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#include <iomanip>
#include <boost/foreach.hpp>
#include <boost/algorithm/minmax_element.hpp>
#include <limits>
#include <set>
#include <cmath>
#include <numeric>
#include <Moby/ArticulatedBody.h>
#include <Moby/Constants.h>
#include <Moby/Event.h>
#include <Moby/CollisionGeometry.h>
#include <Moby/SingleBody.h>
#include <Moby/RigidBody.h>
#include <Moby/Log.h>
#include <Moby/XMLTree.h>
#include <Moby/ImpactToleranceException.h>
#include <Moby/NumericalException.h>
#include <Moby/ImpactEventHandler.h>

using namespace Ravelin;
using namespace Moby;
using std::list;
using boost::shared_ptr;
using std::vector;
using std::map;
using std::endl;
using std::cerr;
using std::pair;
using std::min_element;
using boost::dynamic_pointer_cast;

/// Sets up the default parameters for the impact event handler 
ImpactEventHandler::ImpactEventHandler()
{
  ip_max_iterations = 100;
  ip_eps = 1e-6;
  use_ip_solver = false;
  poisson_eps = NEAR_ZERO;
}

// Processes impacts
void ImpactEventHandler::process_events(const vector<Event>& events)
{
  FILE_LOG(LOG_EVENT) << "*************************************************************";
  FILE_LOG(LOG_EVENT) << endl;
  FILE_LOG(LOG_EVENT) << "ImpactEventHandler::process_events() entered";
  FILE_LOG(LOG_EVENT) << endl;
  FILE_LOG(LOG_EVENT) << "*************************************************************";
  FILE_LOG(LOG_EVENT) << endl;

  // apply the method to all contacts
  if (!events.empty())
  {
    vector<Event> nev;
    nev.push_back(events.front());
    apply_model(events);
  }
  else
    FILE_LOG(LOG_EVENT) << " (no events?!)" << endl;
    
  FILE_LOG(LOG_EVENT) << "*************************************************************" << endl;
  FILE_LOG(LOG_EVENT) << "ImpactEventHandler::process_events() exited" << endl;
  FILE_LOG(LOG_EVENT) << "*************************************************************" << endl;
}

/// Applies the model to a set of events 
/**
 * \param events a set of events
 */
void ImpactEventHandler::apply_model(const vector<Event>& events)
{
  list<Event*> impacting;

  // **********************************************************
  // determine sets of connected events 
  // **********************************************************
  list<list<Event*> > groups;
  Event::determine_connected_events(events, groups);
  Event::remove_nonimpacting_groups(groups);

  // **********************************************************
  // do method for each connected set 
  // **********************************************************
  for (list<list<Event*> >::iterator i = groups.begin(); i != groups.end(); i++)
  {
    // determine contact tangents
    for (list<Event*>::iterator j = i->begin(); j != i->end(); j++)
      if ((*j)->event_type == Event::eContact)
        (*j)->determine_contact_tangents();

      // copy the list of events
      list<Event*> revents = *i;

      FILE_LOG(LOG_EVENT) << " -- pre-event velocity (all events): " << std::endl;
      for (list<Event*>::iterator j = i->begin(); j != i->end(); j++)
        FILE_LOG(LOG_EVENT) << "    event: " << std::endl << **j;

      // determine a reduced set of events
      Event::determine_minimal_set(revents);

      // apply model to the reduced contacts   
      apply_model_to_connected_events(revents);

      FILE_LOG(LOG_EVENT) << " -- post-event velocity (all events): " << std::endl;
      for (list<Event*>::iterator j = i->begin(); j != i->end(); j++)
        FILE_LOG(LOG_EVENT) << "    event: " << std::endl << **j;
  }

  // determine whether there are any impacting events remaining
  for (list<list<Event*> >::const_iterator i = groups.begin(); i != groups.end(); i++)
    for (list<Event*>::const_iterator j = i->begin(); j != i->end(); j++)
      if ((*j)->is_impacting())

  // if there are any events still impacting, throw an exception 
  if (!impacting.empty())
    throw ImpactToleranceException(impacting);
}

/**
 * Applies method of Drumwright and Shell to a set of connected events
 * \param events a set of connected events 
 */
void ImpactEventHandler::apply_model_to_connected_events(const list<Event*>& events)
{
  double ke_minus = 0.0, ke_plus = 0.0;
  SAFESTATIC EventProblemData epd;

  FILE_LOG(LOG_EVENT) << "ImpactEventHandler::apply_model_to_connected_events() entered" << endl;

  // reset problem data
  epd.reset();

  // save the events
  epd.events = vector<Event*>(events.begin(), events.end());

  // determine sets of contact and limit events
  epd.partition_events();

  // compute all event cross-terms
  compute_problem_data(epd);

  // compute energy
  if (LOGGING(LOG_EVENT))
  {
    for (unsigned i=0; i< epd.super_bodies.size(); i++)
    {
      double ke = epd.super_bodies[i]->calc_kinetic_energy();
      FILE_LOG(LOG_EVENT) << "  body " << epd.super_bodies[i]->id << " pre-event handling KE: " << ke << endl;
      ke_minus += ke;
    }
  }

// NOTE: we disable this per Ruina's suggestion
/*
  // solve the (non-frictional) linear complementarity problem to determine
  // the kappa constant
  solve_lcp(epd);
*/
  epd.kappa = (double) -std::numeric_limits<float>::max();

  // determine what type of QP solver to use
  if (use_qp_solver(epd))
    solve_qp(epd, poisson_eps);
  else
    solve_nqp(epd, poisson_eps);

  // apply impulses 
  apply_impulses(epd);

  // compute energy
  if (LOGGING(LOG_EVENT))
  {
    for (unsigned i=0; i< epd.super_bodies.size(); i++)
    {
      double ke = epd.super_bodies[i]->calc_kinetic_energy();
      FILE_LOG(LOG_EVENT) << "  body " << epd.super_bodies[i]->id << " post-event handling KE: " << ke << endl;
      ke_plus += ke;
    }
    if (ke_plus > ke_minus)
      FILE_LOG(LOG_EVENT) << "warning! KE gain detected! energy before=" << ke_minus << " energy after=" << ke_plus << endl;
  }

  FILE_LOG(LOG_EVENT) << "ImpactEventHandler::apply_model_to_connected_events() exiting" << endl;
}

/// Determines whether we can use the QP solver
bool ImpactEventHandler::use_qp_solver(const EventProblemData& epd)
{
  const unsigned UINF = std::numeric_limits<unsigned>::max();

  // first, check whether any contact events use a true friction cone
  for (unsigned i=0; i< epd.N_CONTACTS; i++)
    if (epd.contact_events[i]->contact_NK == UINF)
      return false;

  // now, check whether any articulated bodies use the advanced friction
  // model
  for (unsigned i=0; i< epd.super_bodies.size(); i++)
  {
    ArticulatedBodyPtr abody = dynamic_pointer_cast<ArticulatedBody>(epd.super_bodies[i]);
    if (abody && abody->use_advanced_friction_model)
      return false;
  }

  // still here? ok to use QP solver
  return true;
}

/// Applies impulses to bodies
void ImpactEventHandler::apply_impulses(const EventProblemData& q) const
{
  map<DynamicBodyPtr, VectorNd> gj;
  map<DynamicBodyPtr, VectorNd>::iterator gj_iter;
  VectorNd workv;

  // loop over all contact events first
  for (unsigned i=0; i< q.contact_events.size(); i++)
  {
    // get the contact wrench
    const Event& e = *q.contact_events[i];
    const Wrenchd& w = e.contact_impulse;
    const Point3d& p = e.contact_point;

    // get the two single bodies of the contact
    SingleBodyPtr sb1 = e.contact_geom1->get_single_body();
    SingleBodyPtr sb2 = e.contact_geom2->get_single_body();

    // get the two super bodies
    DynamicBodyPtr b1 = sb1->get_super_body();
    DynamicBodyPtr b2 = sb2->get_super_body();

    // convert wrench on first body to generalized forces
    if ((gj_iter = gj.find(b1)) == gj.end())
      b1->convert_to_generalized_force(sb1, w, p, gj[b1]);
    else
    {
      b1->convert_to_generalized_force(sb1, w, p, workv);
      gj_iter->second += workv; 
    }

    // convert wrench on second body to generalized forces
    if ((gj_iter = gj.find(b2)) == gj.end())
      b2->convert_to_generalized_force(sb2, -w, p, gj[b2]);
    else
    {
      b2->convert_to_generalized_force(sb2, -w, p, workv);
      gj_iter->second += workv; 
    }
  }

  // TODO: figure out how to apply limit impulses
  // TODO: figure out how to apply constraint impulses
  // loop over all limit events next
  for (unsigned i=0; i< q.limit_events.size(); i++)
  {
    const Event& e = *q.limit_events[i];
  }

  // apply all generalized impacts
  for (map<DynamicBodyPtr, VectorNd>::const_iterator i = gj.begin(); i != gj.end(); i++)
    i->first->apply_generalized_impulse(i->second);
}

/// Computes the data to the LCP / QP problems
void ImpactEventHandler::compute_problem_data(EventProblemData& q)
{
  const unsigned UINF = std::numeric_limits<unsigned>::max();
  MatrixNd updM;
  VectorNd updq;

  // determine set of "super" bodies from contact events
  q.super_bodies.clear();
  for (unsigned i=0; i< q.contact_events.size(); i++)
  {
    q.super_bodies.push_back(get_super_body(q.contact_events[i]->contact_geom1->get_single_body()));
    q.super_bodies.push_back(get_super_body(q.contact_events[i]->contact_geom2->get_single_body()));
  }

  // determine set of "super" bodies from limit events
  for (unsigned i=0; i< q.limit_events.size(); i++)
  {
    RigidBodyPtr outboard = q.limit_events[i]->limit_joint->get_outboard_link();
    q.super_bodies.push_back(get_super_body(outboard));
  }

  // make super bodies vector unique
  std::sort(q.super_bodies.begin(), q.super_bodies.end());
  q.super_bodies.erase(std::unique(q.super_bodies.begin(), q.super_bodies.end()), q.super_bodies.end());

  // initialize constants and set easy to set constants
  q.N_CONTACTS = q.contact_events.size();
  q.N_LIMITS = q.limit_events.size();

  // setup contact working set
  q.contact_working_set.clear();
  q.contact_working_set.resize(q.N_CONTACTS, true);

  // setup constants related to articulated bodies
  for (unsigned i=0; i< q.super_bodies.size(); i++)
  {
    ArticulatedBodyPtr abody = dynamic_pointer_cast<ArticulatedBody>(q.super_bodies[i]);
    if (abody) {
      q.N_CONSTRAINT_EQNS_EXP += abody->num_constraint_eqns_explicit();
      if (abody->use_advanced_friction_model)
      {
        q.N_CONSTRAINT_DOF_IMP += abody->num_joint_dof_implicit();
        q.N_CONSTRAINT_DOF_EXP += abody->num_joint_dof_explicit();
      }
    }
  }

  // compute number of friction polygon edges
  for (unsigned i=0; i< q.contact_events.size(); i++)
  {
    if (q.contact_events[i]->contact_NK < UINF)
    {
      q.N_K_TOTAL += q.contact_events[i]->contact_NK/2;
      q.N_LIN_CONE++;
    }
    else if (q.contact_events[i]->contact_NK == UINF)
      break;
  }

  // setup number of true cones
  q.N_TRUE_CONE = q.contact_events.size() - q.N_LIN_CONE; 

  // verify contact constraints that use a true friction cone are at the end 
  // of the contact vector
  #ifndef NDEBUG
  for (unsigned i=q.N_LIN_CONE; i< q.contact_events.size(); i++)
    assert(q.contact_events[i]->contact_NK == UINF);
  #endif
   
  // initialize the problem matrices / vectors
  q.Jc_iM_JcT.set_zero(q.N_CONTACTS, q.N_CONTACTS);
  q.Jc_iM_DcT.set_zero(q.N_CONTACTS, q.N_CONTACTS*2);
  q.Jc_iM_JlT.set_zero(q.N_CONTACTS, q.N_LIMITS);
  q.Jc_iM_DtT.set_zero(q.N_CONTACTS, q.N_CONSTRAINT_DOF_IMP);
  q.Jc_iM_JxT.set_zero(q.N_CONTACTS, q.N_CONSTRAINT_EQNS_EXP);
  q.Jc_iM_DxT.set_zero(q.N_CONTACTS, q.N_CONSTRAINT_DOF_EXP);
  q.Dc_iM_DcT.set_zero(q.N_CONTACTS*2, q.N_CONTACTS*2);
  q.Dc_iM_JlT.set_zero(q.N_CONTACTS*2, q.N_LIMITS);
  q.Dc_iM_DtT.set_zero(q.N_CONTACTS*2, q.N_CONSTRAINT_DOF_IMP);
  q.Dc_iM_JxT.set_zero(q.N_CONTACTS*2, q.N_CONSTRAINT_EQNS_EXP);
  q.Dc_iM_DxT.set_zero(q.N_CONTACTS*2, q.N_CONSTRAINT_DOF_EXP);
  q.Jl_iM_JlT.set_zero(q.N_LIMITS, q.N_LIMITS);
  q.Jl_iM_DtT.set_zero(q.N_LIMITS, q.N_CONSTRAINT_DOF_IMP);
  q.Jl_iM_JxT.set_zero(q.N_LIMITS, q.N_CONSTRAINT_EQNS_EXP);
  q.Jl_iM_DxT.set_zero(q.N_LIMITS, q.N_CONSTRAINT_DOF_EXP);
  q.Dt_iM_DtT.set_zero(q.N_CONSTRAINT_DOF_IMP, q.N_CONSTRAINT_DOF_IMP);
  q.Dt_iM_JxT.set_zero(q.N_CONSTRAINT_DOF_IMP, q.N_CONSTRAINT_EQNS_EXP);
  q.Dt_iM_DxT.set_zero(q.N_CONSTRAINT_DOF_IMP, q.N_CONSTRAINT_DOF_EXP);
  q.Jx_iM_JxT.set_zero(q.N_CONSTRAINT_EQNS_EXP, q.N_CONSTRAINT_EQNS_EXP);
  q.Jx_iM_DxT.set_zero(q.N_CONSTRAINT_EQNS_EXP, q.N_CONSTRAINT_DOF_EXP);
  q.Dx_iM_DxT.set_zero(q.N_CONSTRAINT_DOF_EXP, q.N_CONSTRAINT_DOF_EXP);
  q.Jc_v.set_zero(q.N_CONTACTS);
  q.Dc_v.set_zero(q.N_CONTACTS*2);
  q.Jl_v.set_zero(q.N_LIMITS);
  q.Jx_v.set_zero(q.N_CONSTRAINT_EQNS_EXP);
  q.Dx_v.set_zero(q.N_CONSTRAINT_DOF_EXP);
  q.alpha_c.set_zero(q.N_CONTACTS);
  q.beta_c.set_zero(q.N_CONTACTS*2);
  q.alpha_l.set_zero(q.N_LIMITS);
  q.beta_t.set_zero(q.N_CONSTRAINT_DOF_IMP);
  q.alpha_x.set_zero(q.N_CONSTRAINT_EQNS_EXP);
  q.beta_x.set_zero(q.N_CONSTRAINT_DOF_EXP);

  // setup indices
  q.ALPHA_C_IDX = 0;
  q.BETA_C_IDX = q.ALPHA_C_IDX + q.N_CONTACTS;
  q.NBETA_C_IDX = q.BETA_C_IDX + q.N_LIN_CONE*2;
  q.BETAU_C_IDX = q.NBETA_C_IDX + q.N_LIN_CONE*2;
  q.ALPHA_L_IDX = q.BETAU_C_IDX + q.N_TRUE_CONE;
  q.BETA_T_IDX = q.ALPHA_L_IDX + q.N_LIMITS;
  q.ALPHA_X_IDX = q.BETA_T_IDX + q.N_CONSTRAINT_DOF_IMP;
  q.BETA_X_IDX = q.ALPHA_X_IDX + q.N_CONSTRAINT_EQNS_EXP;
  q.N_VARS = q.BETA_X_IDX + q.N_CONSTRAINT_DOF_EXP;

  // possibility 1: let this function handle how to separate M into components
  // possibility 2: add a tangential flag to events
  // possibility 3: 

  // loop over all events
  for (unsigned i=0; i< q.events.size(); i++)
  {
    // get the event data
    q.events[i]->calc_event_data(updM, updq); 

    // get the index mapping for event i
    const vector<unsigned>& mapping_i = q.mappings[i];

    // update M and q
    const unsigned N = mapping_i.size();
    for (unsigned r=0; r< N; r++)
    {
      for (unsigned s=0; s< N; s++)
        q.M(mapping_i[r], mapping_i[s]) = updM(r,s);
      q.q[mapping_i[r]] = updq[r]; 
    }

    // loop over all over events
    for (unsigned j=i+1; j< q.events.size(); j++)
    {
      // get the cross event data, if any
      if (q.events[i]->calc_cross_event_data(*q.events[j], updM)) 
      {
        // get the index mapping for event j 
        const vector<unsigned>& mapping_j = q.mappings[j];

        // verify that updM is of the size we expect
        assert(updM.rows() == N && updM.columns() == M);

        // update M
        const unsigned M = mapping_j.size();
        for (unsigned r=0; r< N; r++)
          for (unsigned s=0; s< M; s++)
          {
            q.M(mapping_i[r], mapping_j[s]) = updM(r,s);
            q.M(mapping_j[s], mapping_i[r]) = updM(r,s);
          }
      }
    }
  }
}

void ImpactEventHandler::update_event_data(EventProblemData& q, unsigned i)
{
  vector<DynamicBodyPtr> ebodies;

  // get the event
  const Event& e = *q.events[i];

  // get the event bodies
  e.get_super_bodies(std::back_inserter(ebodies));

  // loop through all bodies
  for (unsigned i=0; i< ebodies.size(); i++)
  {
//    ebodies[i]->calc_event_data(e, 
  }
}

/// Solves the (frictionless) LCP
void ImpactEventHandler::solve_lcp(EventProblemData& q, VectorNd& z)
{
  SAFESTATIC MatrixNd UL, LR, MM, U, V;
  SAFESTATIC MatrixNd UR, t2, iJx_iM_JxT;
  SAFESTATIC VectorNd alpha_c, alpha_l, alpha_x, v1, v2, qq, S;

  // setup sizes
  UL.resize(q.N_CONTACTS, q.N_CONTACTS);
  UR.resize(q.N_CONTACTS, q.N_LIMITS);
  LR.resize(q.N_LIMITS, q.N_LIMITS);

  // prepare to invert Jx*inv(M)*Jx'
  iJx_iM_JxT = q.Jx_iM_JxT;
  _LA.svd(iJx_iM_JxT, U, S, V);

  // setup primary terms -- first upper left hand block of matrix
  MatrixNd::transpose(q.Jc_iM_JxT, t2);
  _LA.solve_LS_fast(U, S, V, t2);
  q.Jc_iM_JxT.mult(t2, UL); 
  
  q.Jc_iM_JxT.mult(iJx_iM_JxT, t2);
  t2.mult_transpose(q.Jc_iM_JxT, UL);

  // now do upper right hand block of matrix
  MatrixNd::transpose(q.Jl_iM_JxT, t2);
  _LA.solve_LS_fast(U, S, V, t2);
  q.Jc_iM_JxT.mult(t2, UR);
  
  // now lower right hand block of matrix
  t2.mult_transpose(q.Jl_iM_JxT, LR);

  // subtract secondary terms
  UL -= q.Jc_iM_JcT;
  UR -= q.Jc_iM_JlT;
  LR -= q.Jl_iM_JlT;

  // now negate all terms
  UL.negate();
  UR.negate();
  LR.negate();

  // setup the LCP matrix
  MM.resize(q.N_CONTACTS + q.N_LIMITS, q.N_CONTACTS + q.N_LIMITS);
  MM.set_sub_mat(0, 0, UL);
  MM.set_sub_mat(0, q.N_CONTACTS, UR);
  MM.set_sub_mat(q.N_CONTACTS, 0, UR, Ravelin::eTranspose);
  MM.set_sub_mat(q.N_CONTACTS, q.N_CONTACTS, LR);

  // setup the LCP vector
  qq.resize(MM.rows());
  _LA.solve_LS_fast(U, S, V, v2 = q.Jx_v); 
  q.Jc_iM_JxT.mult(v2, v1);
  v1 -= q.Jc_v;
  qq.set_sub_vec(0, v1);
  q.Jl_iM_JxT.mult(v2, v1);
  v1 -= q.Jl_v;
  qq.set_sub_vec(q.N_CONTACTS, v1);
  qq.negate();

  FILE_LOG(LOG_EVENT) << "ImpulseEventHandler::solve_lcp() entered" << std::endl;
  FILE_LOG(LOG_EVENT) << "  Jc * inv(M) * Jc': " << std::endl << q.Jc_iM_JcT;
  FILE_LOG(LOG_EVENT) << "  Jc * v: " << q.Jc_v << std::endl;
  FILE_LOG(LOG_EVENT) << "  Jl * v: " << q.Jl_v << std::endl;
  FILE_LOG(LOG_EVENT) << "  LCP matrix: " << std::endl << MM;
  FILE_LOG(LOG_EVENT) << "  LCP vector: " << qq << std::endl;

  // solve the LCP
  if (!_lcp.lcp_lemke_regularized(MM, qq, z))
    throw std::runtime_error("Unable to solve event LCP!");

  // determine the value of kappa
  q.kappa = (double) 0.0;
  for (unsigned i=0; i< q.N_CONTACTS; i++)
    q.kappa += z[i];

  // get alpha_c and alpha_l
  z.get_sub_vec(0, q.N_CONTACTS, alpha_c);
  z.get_sub_vec(q.N_CONTACTS, z.size(), alpha_l);

  // Mv^* - Mv = Jc'*alpha_c + Jl'*alpha_l + Jx'*alpha_x

  // Mv^* - Mv^- = Jx'*alpha_x
  // Jx*v^*     = 0
  // v^* = v^- + inv(M)*Jx'*alpha_x
  // Jx*v^- + Jx*inv(M)*Jx'*alpha_x = 0

  // Jx*inv(M)*Jx'*alpha_x = -Jx*(v + inv(M)*Jc'*alpha_c + inv(M)*Jl'*alpha_l)

  // compute alpha_x 
  q.Jc_iM_JxT.transpose_mult(alpha_c, v1);
  q.Jl_iM_JxT.transpose_mult(alpha_l, v2); 
  v1 += v2;
  v1 += q.Jx_v;
  v1.negate();
  _LA.solve_LS_fast(U, S, V, alpha_x = v1);

  // setup the homogeneous solution
  z.set_zero();
  z.set_sub_vec(q.ALPHA_C_IDX, alpha_c);
  z.set_sub_vec(q.ALPHA_L_IDX, alpha_l);
  z.set_sub_vec(q.ALPHA_X_IDX, alpha_x);

  FILE_LOG(LOG_EVENT) << "  LCP result: " << z << std::endl;
  FILE_LOG(LOG_EVENT) << "  kappa: " << q.kappa << std::endl;
  FILE_LOG(LOG_EVENT) << "ImpulseEventHandler::solve_lcp() exited" << std::endl;
}

/// Gets the super body (articulated if any)
DynamicBodyPtr ImpactEventHandler::get_super_body(SingleBodyPtr sb)
{
  ArticulatedBodyPtr ab = sb->get_articulated_body();
  if (ab)
    return ab;
  else
    return sb;
}

