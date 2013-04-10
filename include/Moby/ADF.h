/****************************************************************************
 * Copyright 2006 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _MOBY_ADF_H_
#define _MOBY_ADF_H_

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <list>
#include <vector>
#include <Moby/Types.h>
#include <Moby/Ravelin::Vector3.h>
#include <Moby/Polyhedron.h>

class SoSeparator;

namespace Moby {

/// An adaptively-sampled distance field using an octree representation 
class ADF : public boost::enable_shared_from_this<ADF>
{
  public:
    ADF();
    ADF(boost::shared_ptr<ADF> parent, const Ravelin::Vector3& lo_bounds, const Ravelin::Vector3& hi_bounds);
    ADF(boost::shared_ptr<ADF> parent, const std::vector<Ravelin::Vector3ConstPtr>& vertices);
    double calc_signed_distance(const Ravelin::Vector3& point) const;
    const std::vector<boost::shared_ptr<const Ravelin::Vector3> >& get_vertices() const { return _vertices; }
    void set_distances(const std::vector<double>& distances);
    void set_distances(double (*dfn)(const Ravelin::Vector3&, void*), void* data);
    void get_samples(std::vector<Ravelin::Vector3>& samples) const;
    void reset();
    void simplify(double epsilon);
    void get_bounds(Ravelin::Vector3& lo, Ravelin::Vector3& hi) const;
    void set_bounds(const Ravelin::Vector3& lo_bound, const Ravelin::Vector3& hi_bound);
    static boost::shared_ptr<ADF> build_ADF(Polyhedron& poly, unsigned max_recursion, double epsilon, double max_pos_dist = -1.0, double max_neg_dist = std::numeric_limits<double>::max());
    static boost::shared_ptr<ADF> build_ADF(const Ravelin::Vector3& lo, const Ravelin::Vector3& hi, double (*dfn)(const Ravelin::Vector3&, void*), unsigned max_recursion, double epsilon, double max_pos_dist = -1.0, double max_neg_dist = std::numeric_limits<double>::max(), void* data = NULL);
    const std::vector<double>& get_distances() const { return _distances; }
    static boost::shared_ptr<ADF> intersect(boost::shared_ptr<ADF> adf1, boost::shared_ptr<ADF> adf2, double epsilon, unsigned recursion_limit);
    bool contains(const Ravelin::Vector3& point) const;
    unsigned count_cells() const;
    bool generate_iso_sample(Ravelin::Vector3& sample, double epsilon) const;
    void get_all_leaf_nodes(std::list<boost::shared_ptr<ADF> >& leafs) const;
    void get_all_cells(std::list<boost::shared_ptr<ADF> >& cells) const;
    Ravelin::Vector3 determine_normal(const Ravelin::Vector3& point) const;
    bool intersect_seg_iso_surface(const LineSeg3& seg, Ravelin::Vector3& isect) const;
    void save_to_file(const std::string& filename) const;
    static boost::shared_ptr<ADF> load_from_file(const std::string& filename);
    SoSeparator* render() const;
    void subdivide(double (*dfn)(const Ravelin::Vector3&, void*), void*);
    void subdivide();
    unsigned get_recursion_level() const;

    /// Sets the parent of this octree
    void set_parent(boost::shared_ptr<ADF> parent) { _parent = parent; }

    /// Gets the parent of this octree
    boost::shared_ptr<ADF> get_parent() const { return (_parent.expired()) ? boost::shared_ptr<ADF>() : boost::shared_ptr<ADF>(_parent); }

    /// Determines whether this ADF node is a leaf
    bool is_leaf() const { return _children.empty(); }

    /// Gets the set of children of this ADF cell
    const std::vector<boost::shared_ptr<ADF> >& get_children() const { return _children; }

  private:
    static double trimesh_distance_function(const Ravelin::Vector3& pt, void* data);
    void render(SoSeparator* separator) const;
    void render2(SoSeparator* separator) const;
    static double calc_max_distance(boost::shared_ptr<ADF> adf1, boost::shared_ptr<ADF> adf2, const Ravelin::Vector3& point);
    static double tri_linear_interp(const std::vector<Ravelin::Vector3ConstPtr>& x, const std::vector<double>& q, const Ravelin::Vector3& p);
    boost::shared_ptr<ADF> is_cell_occupied(const Ravelin::Vector3& point) const;
    unsigned get_sub_volume_idx(const Ravelin::Vector3& point) const;

    static const unsigned OCT_CHILDREN = 8;
    static const unsigned BOX_VERTICES = 8;
    std::vector<boost::shared_ptr<ADF> > _children;
    boost::weak_ptr<ADF> _parent;
    std::vector<boost::shared_ptr<const Ravelin::Vector3> > _vertices;
    std::vector<double> _distances;
    Ravelin::Vector3 _lo_bounds, _hi_bounds;
}; // end class

std::ostream& operator<<(std::ostream& out, const ADF& adf);


} // end namespace

#endif

