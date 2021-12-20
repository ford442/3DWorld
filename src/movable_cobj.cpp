// 3D World - OpenGL CS184 Computer Graphics Project
// by Frank Gennari
// 5/1/02

#include "3DWorld.h"
#include "physics_objects.h"
#include "mesh.h"
#include "player_state.h"
#include "csg.h"
#include "openal_wrap.h"

set<unsigned> moving_cobjs;

extern unsigned scene_smap_vbo_invalid;
extern int num_groups, frame_counter;
extern float base_gravity, tstep, temperature;
extern double camera_zh;
extern coll_obj_group coll_objects;
extern cobj_groups_t cobj_groups;
extern player_state *sstates;
extern platform_cont platforms;
extern obj_type object_types[NUM_TOT_OBJS];
extern obj_group obj_groups[NUM_TOT_OBJS];


bool push_cobj(unsigned index, vector3d &delta, set<unsigned> &seen, point const &pushed_from);

bool coll_obj::is_moving() const {return (is_movable() && moving_cobjs.find(id) != moving_cobjs.end());}

void mark_movable_cobj_smap_update() {
	scene_smap_vbo_invalid |= 1; // mark for rebuild, but don't force full update
}


int cube_polygon_intersect(coll_obj const &c, coll_obj const &p) {

	for (int i = 0; i < p.npoints; ++i) { // check points (fast)
		if (c.contains_pt(p.points[i])) return 1; // definite intersection
	}
	for (int i = 0; i < p.npoints; ++i) { // check edges
		if (check_line_clip(p.points[i], p.points[(i+1)%p.npoints], c.d)) return 1; // definite intersection
	}
	static vector<point> clipped_pts;
	clip_polygon_to_cube(c, p.points, p.npoints, p, clipped_pts); // clip the polygon to the cube
	if (!clipped_pts.empty()) return 1;

	if (p.thickness > MIN_POLY_THICK) { // test extruded (3D) polygon
		point pts[2][4];
		gen_poly_planes(p.points, p.npoints, p.norm, p.thickness, pts);
				
		for (unsigned j = 0; j < 2; ++j) {
			for (int i = 0; i < p.npoints; ++i) {
				if (check_line_clip(pts[j][i], pts[j][(i+1)%p.npoints], c.d)) return 1; // definite intersection
			}
		}
		// need to handle cube completely insde of a thick polygon
		if (sphere_ext_poly_intersect(p.points, p.npoints, p.norm, c.get_cube_center(), 0.0, p.thickness, MIN_POLY_THICK)) return 1;
		static vector<tquad_t> side_pts;
		thick_poly_to_sides(p.points, p.npoints, p.norm, p.thickness, side_pts);

		for (auto i = side_pts.begin(); i != side_pts.end(); ++i) { // clip each face to the cube
			clip_polygon_to_cube(c, i->pts, i->npts, p, clipped_pts);
			if (!clipped_pts.empty()) return 1;
		}
		return 0;
	}
	return 0;
}

int cylin_cube_int_aa_via_circle_rect(coll_obj const &cube, cylinder_3dw const &cylin) {

	bool deq[3];
	UNROLL_3X(deq[i_] = (cylin.p1[i_] == cylin.p2[i_]);)

	for (unsigned d = 0; d < 3; ++d) { // approximate projected circle test for x/y/z oriented cylinders
		if (!deq[(d+1)%3] || !deq[(d+2)%3]) continue; // not oriented in direction d
		if ( circle_rect_intersect(cylin.p1, min(cylin.r1, cylin.r2), cube, d)) return 1;
		if (!circle_rect_intersect(cylin.p1, max(cylin.r1, cylin.r2), cube, d)) return 0; // no intersection
	}
	if (cylin.p1.x == cylin.p2.x && cylin.p1.y == cylin.p2.y && cylin.r1 != cylin.r2) { // vertical truncated cone
		point const closest_cp(cube.closest_pt(cylin.p1));
		float const cline_dist((closest_cp - cylin.p1).xy_mag()); // min cube xy distance to cylinder center line
		float const z1(max(cube.d[2][0], min(cylin.p1.z, cylin.p2.z))); // bottom of shared range
		float const z2(min(cube.d[2][1], max(cylin.p1.z, cylin.p2.z))); // top    of shared range
		if (z2 <= z1) return 0; // no z-intersection (should be handled earlier?)
		float const rz_val((cylin.r2 - cylin.r1)/(cylin.p2.z - cylin.p1.z));
		float const rz1(cylin.r1 + rz_val*(z1 - cylin.p1.z)); // radius at bottom of shared range
		float const rz2(cylin.r1 + rz_val*(z2 - cylin.p1.z)); // radius at bottom of shared range
		return (cline_dist < max(rz1, rz2)); // min cube xy distance to cylinder center line is less than max radius of shared range 
	}
	return 2; // FIXME: finish
}
int cylin_cube_int_aa_via_circle_rect(coll_obj const &cube, coll_obj const &cylin) {
	return cylin_cube_int_aa_via_circle_rect(cube, cylin.get_bounding_cylinder());
}

int vert_cylin_cylin_SAT_test(coll_obj const &vc, coll_obj const &nvc) {
	if (!vc.is_cylin_vertical()) return 2;
	return cylin_proj_circle_z_SAT_test(vc.points[0], max(vc.radius, vc.radius2), nvc.points[0], nvc.points[1], nvc.radius, nvc.radius2);
}

int cylin_cylin_int(coll_obj const &c1, coll_obj const &c2) {

	float const line_dist(line_line_dist(c2.points[0], c2.points[1], c1.points[0], c1.points[1]));
	if (line_dist > (max(c2.radius, c2.radius2) + max(c1.radius, c1.radius2))) return 0;
	if (c1.line_intersect(c2.points[0], c2.points[1])) return 1;
	if (c2.line_intersect(c1.points[0], c1.points[1])) return 1;
	if (!cylin_cube_int_aa_via_circle_rect(c1, c2))    return 0;
	if (!cylin_cube_int_aa_via_circle_rect(c2, c1))    return 0;
	if (!vert_cylin_cylin_SAT_test(c1, c2) || !vert_cylin_cylin_SAT_test(c2, c1)) return 0;
	// see http://www.geometrictools.com/Documentation/IntersectionOfCylinders.pdf
	return 2; // FIXME: finish
}

int poly_cylin_int(coll_obj const &p, coll_obj const &c) {

	cylinder_3dw const cylin(c.get_bounding_cylinder());
	if (p.line_intersect(cylin.p1, cylin.p2)) return 1;

	for (int i = 0; i < p.npoints; ++i) {
		if (c.line_intersect(p.points[i], p.points[(i+1)%p.npoints])) return 1; // definite intersection
	}
	if (!cube_polygon_intersect(c, p)) return 0; // use c for bounding cube
	if (!cylin_cube_int_aa_via_circle_rect(p, cylin)) return 0;

	if (p.thickness > MIN_POLY_THICK) { // test extruded (3D) polygon
		// WRITE
	}
	return 2; // FIXME: finish
}

int poly_poly_int_test(coll_obj const &p1, coll_obj const &p2) {

	for (int i = 0; i < p1.npoints; ++i) {
		if (p2.line_intersect(p1.points[i], p1.points[(i+1)%p1.npoints])) return 1;
	}
	if (p1.thickness <= MIN_POLY_THICK) return 0;
	if (p1.was_a_cube() && p2.was_a_cube()) {} // special case for OBB/OBB? Can transform one into the other's coordinate system
	// Note: this is inefficient since all edges belong to two faces and are checked twice, but probably doesn't matter
	// maybe we should call gen_poly_planes() instead and check top/bot faces, but this will miss 4 of the edges parallel to the normal
	static vector<tquad_t> side_pts;
	thick_poly_to_sides(p1.points, p1.npoints, p1.norm, p1.thickness, side_pts);

	for (auto i = side_pts.begin(); i != side_pts.end(); ++i) { // intersect the edges from each face
		for (unsigned j = 0; j < i->npts; ++j) {
			if (p2.line_intersect(i->pts[j], i->pts[(j+1)%i->npts])) return 1;
		}
	}
	// need to handle one polygon completely insde of a thick polygon (Note: calls thick_poly_to_sides() again)
	if (sphere_ext_poly_intersect(p1.points, p1.npoints, p1.norm, p2.points[0], 0.0, p1.thickness, MIN_POLY_THICK)) return 1;
	return 0;
}

bool coll_sphere_cylin_int(point const &sc, float sr, coll_obj const &c) {
	if (!sphere_cube_intersect(sc, sr, c)) return 0; // test bcube
	return sphere_intersect_cylinder(sc, sr, c.points[0], c.points[1], c.radius, c.radius2);
}

bool sphere_def_coll_vert_cylin(point const &sc, float sr, point const &cp1, point const &cp2, float cr) {
	return (sc.z >= min(cp1.z, cp2.z) && sc.z <= max(cp1.z, cp2.z) && dist_xy_less_than(sc, cp1, (sr + cr)));
}

void copy_torus_bounding_cylin(coll_obj const &torus, coll_obj &cylin) { // Note: only copies data needed for intersection tests

	cylin.copy_from(torus); // copy bcube
	cylinder_3dw const c(torus.get_bounding_cylinder());
	cylin.points[0] = c.p1;
	cylin.points[1] = c.p2;
	cylin.radius    = c.r1;
	cylin.radius2   = c.r2;
	cylin.type      = COLL_CYLINDER_ROT;
}

int torus_cylinder_int(coll_obj const &t, coll_obj const &c) {

	if (c.is_cylin_vertical() && c.radius == c.radius2 && t.has_z_normal()) { // Note: +z torus vs. +z capsule
		return (dist_xy_less_than(c.points[0], t.points[0], (t.radius+t.radius2+c.radius)) && !dist_xy_less_than(c.points[0], t.points[0], (t.radius-t.radius2-c.radius)));
	}
	coll_obj cylin;
	copy_torus_bounding_cylin(t, cylin);
	if (!cylin_cylin_int(cylin, c)) return 0;
	return 2; // FIXME: unclear how to accurately handle this case
}


// 0: no intersection, 1: intersection, 2: maybe intersection (incomplete)
// 21 total: 15 complete, 5 partial (all cylinder cases), 1 incomplete (capsule-capsule)
// Note: pos toler => adjacency doesn't count; neg toler => adjacency counts
int coll_obj::intersects_cobj(coll_obj const &c, float toler) const {

	if (!intersects(c, toler)) return 0; // cube-cube intersection
	if (c.type < type) {return c.intersects_cobj(*this, toler);} // swap arguments
	float const r1(radius-toler), r2(radius2-toler), cr1(c.radius-toler), cr2(c.radius2-toler);

	if (c.type == COLL_TORUS && c.norm.x == 0.0 && c.norm.y == 0.0) { // check cobj inside torus center case
		point sc;
		float sr;
		bounding_sphere(sc, sr);
		if (!sphere_torus_intersect(sc, sr, c.points[0], c.radius2, c.radius)) return 0;
	}

	// c.type >= type
	switch (type) {
	case COLL_CUBE:
		switch (c.type) {
		case COLL_CUBE:     return 1; // as simple as that
		case COLL_CYLINDER: return circle_rect_intersect(c.points[0], cr1, *this, 2); // in z
		case COLL_SPHERE:   return sphere_cube_intersect(c.points[0], cr1, *this);
		case COLL_CAPSULE:
			if (sphere_cube_intersect(c.points[0], cr1, *this) || sphere_cube_intersect(c.points[1], cr2, *this)) return 1;
			// fallthrough
		case COLL_CYLINDER_ROT:
		case COLL_TORUS: {
			cylinder_3dw const cylin(c.get_bounding_cylinder());
			if (check_line_clip(cylin.p1, cylin.p2, d)) return 1; // definite intersection
			return cylin_cube_int_aa_via_circle_rect(*this, cylin);
		}
		case COLL_POLYGON: return cube_polygon_intersect(*this, c);
		default: assert(0);
		} // end switch

	case COLL_CYLINDER:
	case COLL_CYLINDER_ROT:
		switch (c.type) {
		case COLL_CYLINDER:
			assert(type == COLL_CYLINDER);
			return dist_xy_less_than(points[0], c.points[0], (cr1+radius)); // bcube test guarantees overlap in z
		case COLL_SPHERE:
			if (type == COLL_CYLINDER && sphere_def_coll_vert_cylin(c.points[0], cr1, points[0], points[1], r1)) return 1;
			return coll_sphere_cylin_int(c.points[0], cr1, *this);
		case COLL_CAPSULE:
			if (type == COLL_CYLINDER && (sphere_def_coll_vert_cylin(c.points[0], cr1, points[0], points[1], radius) ||
				                          sphere_def_coll_vert_cylin(c.points[1], cr2, points[0], points[1], radius))) return 1;
			if (coll_sphere_cylin_int(c.points[0], cr1, *this) || coll_sphere_cylin_int(c.points[1], cr2, *this)) return 1;
			// fallthrough
		case COLL_CYLINDER_ROT: return cylin_cylin_int(c, *this);
		case COLL_TORUS:        return torus_cylinder_int(c, *this);
		case COLL_POLYGON:      return poly_cylin_int(c, *this);
		default: assert(0);
		} // end switch

	case COLL_SPHERE:
		switch (c.type) {
		case COLL_SPHERE: return dist_less_than(points[0], c.points[0], (cr1+radius));
		case COLL_CAPSULE: if (dist_less_than(points[0], c.points[0], (cr1+radius)) || dist_less_than(points[0], c.points[1], (cr2+radius))) return 1;
			// fallthrough
		case COLL_CYLINDER_ROT: return coll_sphere_cylin_int(points[0], r1, c);
		case COLL_TORUS: {
				if (!sphere_cube_intersect(points[0], radius, c)) return 0; // test bcube
				if (c.norm.x == 0.0 && c.norm.y == 0.0) {return sphere_torus_intersect(points[0], radius, c.points[0], c.radius2, c.radius);} // Note: duplicate check made above
				coll_obj cylin;
				copy_torus_bounding_cylin(c, cylin);
				if (!coll_sphere_cylin_int(points[0], r1, cylin)) return 0;
				return 2;
			}
		case COLL_POLYGON: return sphere_ext_poly_intersect(c.points, c.npoints, c.norm, points[0], r1, c.thickness, MIN_POLY_THICK);
		default: assert(0);
		} // end switch

	case COLL_POLYGON:
		switch (c.type) {
		case COLL_TORUS: return poly_cylin_int(*this, c);
		case COLL_CAPSULE:
			if (sphere_ext_poly_intersect(points, npoints, norm, c.points[0], cr1, thickness, MIN_POLY_THICK)) return 1;
			if (sphere_ext_poly_intersect(points, npoints, norm, c.points[1], cr2, thickness, MIN_POLY_THICK)) return 1;
			return poly_cylin_int(*this, c);
		case COLL_POLYGON: {
			if (thickness   > MIN_POLY_THICK && !cube_polygon_intersect(c, *this)) return 0;
			if (c.thickness > MIN_POLY_THICK && !cube_polygon_intersect(*this, c)) return 0;
			//float const poly_toler(max(-toler, (thickness + c.thickness)*(1.0f - fabs(dot_product(norm, c.norm)))));
			float const poly_toler(-toler*(1.0f - fabs(dot_product(norm, c.norm)))); // Note: not taking thickness into account, which is more correct

			if (poly_toler > 0.0) { // use toler for edge adjacency tests (for adjacent roof polygons, sponza polygons, etc.)
				for (int i = 0; i < c.npoints; ++i) { // test point adjacency
					for (int j = 0; j < npoints; ++j) {
						if (dist_less_than(points[j], c.points[i], poly_toler)) return 1;
					}
				}
				for (int i = 0; i < c.npoints; ++i) { // test edges
					for (int j = 0; j < npoints; ++j) {
						if (pt_line_seg_dist_less_than(c.points[i],   points[j],   points[(j+1)%  npoints], poly_toler)) return 1;
						if (pt_line_seg_dist_less_than(  points[j], c.points[i], c.points[(i+1)%c.npoints], poly_toler)) return 1;
					}
				}
			}
			int const ret1(poly_poly_int_test(c, *this)), ret2(poly_poly_int_test(*this, c));
			if (ret1 || ret2) {return ((ret1 == 1 || ret2 == 1) ? 1 : 2);}
		} // end COLL_POLYGON
		default: assert(0);
		} // end switch

	case COLL_CAPSULE:
		switch (c.type) {
		case COLL_CAPSULE: {
			sphere_t const sa[2] = {sphere_t(  points[0],  r1), sphere_t(  points[1],  r2)};
			sphere_t const sb[2] = {sphere_t(c.points[0], cr1), sphere_t(c.points[1], cr2)};

			for (unsigned i = 0; i < 4; ++i) { // 4 sphere-sphere intersections
				if (dist_less_than(sa[i&1].pos, sb[i>>1].pos, (sa[i&1].radius + sb[i>>1].radius))) return 1;
			}
			for (unsigned i = 0; i < 2; ++i) { // 4 sphere-cylinder intersections
				if (coll_sphere_cylin_int(sa[i].pos, sa[i].radius, c    )) return 1;
				if (coll_sphere_cylin_int(sb[i].pos, sb[i].radius, *this)) return 1;
			}
			return cylin_cylin_int(c, *this); // cylinder-cylinder intersection
		} // end COLL_CAPSULE
		case COLL_TORUS: return torus_cylinder_int(c, *this); // use capsule bounding cylinder (but ignore ends)
		default: assert(0);
		} // end switch
	case COLL_TORUS: {
			assert(c.type == COLL_TORUS);
			coll_obj cylin1, cylin2;
			copy_torus_bounding_cylin(*this, cylin1);
			copy_torus_bounding_cylin(c,     cylin2);
			// torus_cylinder_int()?
			if (!cylin_cylin_int(cylin1, cylin2)) return 0; // definitely no intersection
			
			if (norm.x == 0.0 && norm.y == 0.0 && c.norm.x == 0.0 && c.norm.y == 0.0) { // both are vertical
				float const cdist_xy(p2p_dist_xy(points[0], c.points[0]) - radius - c.radius); // distance between circles through torus centers in XY plane

				if (cdist_xy > 0.0) {
					float const dz(points[0].z - c.points[0].z), cdist(sqrt(cdist_xy*cdist_xy + dz*dz)); // distance between circles through torus centers
					if (cdist > (radius2 + c.radius2)) return 0; // no intersection
				}
			}
			return 2; // FIXME: unclear how to accurately handle this case (torus vs. torus)
		}
	default: assert(0);
	} // end switch
	return 0;
}

// Note: only cubes and polygons are supported, since they have hard polygon sides
// top_bot_only: 0 = all sides, 1 = top (+z) only, 2 = bottom (-z) only
void coll_obj::get_side_polygons(vector<tquad_t> &sides, int top_bot_only) const {

	float const norm_z_thresh = 0.01;
	assert(has_hard_edges());

	if (type == COLL_CUBE) {
		for (unsigned dim = 0; dim < 3; ++dim) { // {x, y, z}
			if (top_bot_only && dim != 2) continue; // not z edge
			unsigned const dim2((dim+1)%3), dim3((dim+2)%3);

			for (unsigned dir = 0; dir < 2; ++dir) { // {low, high} edges
				if (top_bot_only && (int)dir == (top_bot_only-1)) continue; // not z edge
				tquad_t tq(4);
				
				for (unsigned i = 0; i < 4; ++i) {
					tq.pts[i][dim ] = d[dim][dir];
					tq.pts[i][dim2] = d[dim2][i>>1]; // 0011
					tq.pts[i][dim3] = d[dim3][(i&1)^(i>>1)]; // 0110
				}
				sides.push_back(tq);
			}
		}
	}
	else if (type == COLL_POLYGON) {
		if (thickness > MIN_POLY_THICK) { // thick polygon
			vector<tquad_t> pts;
			thick_poly_to_sides(points, npoints, norm, thickness, pts);

			for (auto i = pts.begin(); i != pts.end(); ++i) {
				if (top_bot_only == 1 && i->get_norm().z <  norm_z_thresh) continue; // top only
				if (top_bot_only == 2 && i->get_norm().z > -norm_z_thresh) continue; // bot only
				sides.push_back(*i); // keep this side
			}
		}
		else { // thin polygon
			if (!top_bot_only || fabs(norm.z) > norm_z_thresh) { // maybe skip vert edges
				tquad_t tq(npoints);
				for (int i = 0; i < npoints; ++i) {tq.pts[i] = points[i];}
				sides.push_back(tq);
			}
		}
	}
}

void add_cube_cube_touching_face_contact_pts(cube_t const &a, cube_t const &b, unsigned dim, vector<point> &contact_pts, float toler) {

	unsigned const d1((dim+1)%3), d2((dim+2)%3);

	for (unsigned dir = 0; dir < 2; ++dir) { // for both combinations of the cubes
		float const val(a.d[dim][dir]);
		if (fabs(val - b.d[dim][!dir]) > toler) continue; // not intersecting c (we already know that orthogonal ranges overlap)
		float const u1(max(a.d[d1][0], b.d[d1][0])), v1(max(a.d[d2][0], b.d[d2][0])), u2(min(a.d[d1][1], b.d[d1][1])), v2(min(a.d[d2][1], b.d[d2][1]));
		point pt;
		pt[dim] = val;
		pt[d1] = u1;
		pt[d2] = v1;
		contact_pts.push_back(pt);
		pt[d2] = v2;
		contact_pts.push_back(pt);
		pt[d1] = u2;
		contact_pts.push_back(pt);
		pt[d2] = v1;
		contact_pts.push_back(pt);
	} // for dir
}

// a glorified version of intersects_cobj that works on even fewer cases
// rather than returning the full (possibly infinite for flat surfaces) set of contact points,
// we're free to return the convex hull of the contact area, which will be merged with the convex hulls of other interacting cobjs
// Note: generally assumes that the two cobjs are adjacent/interacting
// Note: contact_pts are relative to the contacting plane/line/point of *this
// Note: for vert_only, *this is assumed to be resting on c
void coll_obj::get_contact_points(coll_obj const &c, vector<point> &contact_pts, bool vert_only, float toler) const {

	if (!intersects(c, toler)) return; // cube-cube intersection

	if (type == COLL_SPHERE && c.type == COLL_SPHERE) {
		point const p[2] = {points[0], c.points[0]};
		if (dist_less_than(p[0], p[1], (radius + c.radius - toler))) {contact_pts.push_back(p[0] + radius*(p[1] - p[0]).get_norm());}
		return;
	}
	if (type == COLL_CUBE && c.type == COLL_CUBE) { // vertical cube optimization
		for (unsigned dim = (vert_only ? 2 : 0); dim < 3; ++dim) { // either {x,y,z} or just {z}
			add_cube_cube_touching_face_contact_pts(*this, c, dim, contact_pts, -toler);
		}
		return;
	}
	if (has_hard_edges() && c.has_hard_edges()) { // cubes and polygons
		vector<tquad_t> sides[2];
		get_side_polygons  (sides[0], (vert_only ? 2 : 0));
		c.get_side_polygons(sides[1], (vert_only ? 1 : 0));
		
		for (auto i = sides[0].begin(); i != sides[0].end(); ++i) {
			vector3d const norm_i(i->get_norm());

			for (auto j = sides[1].begin(); j != sides[1].end(); ++j) {
				vector3d const norm_j(j->get_norm());

				if (dot_product(norm_i, norm_j) > 0.99) {
					// FIXME: WRITE plane case
				}
				else {
					// FIXME: WRITE point/edge case
				}
			}
		}
	}
	else {
		// FIXME: WRITE
	}
}

// Note: generally the returned normal should point up in +z (down in -z if bot_surf=1), but could have z == 0
// Note: support_pos.z is ignored
vector3d coll_obj::get_cobj_supporting_normal(point const &support_pos, bool bot_surf) const {

	if (type == COLL_CUBE || type == COLL_CYLINDER) return (bot_surf ? -plus_z : plus_z); // exact since cobj is axis aligned in x
	float const cobj_height(dz());
	point line_pts[2] = {support_pos, support_pos};
	line_pts[0].z = d[2][0] - cobj_height; // make sure the line completely crosses the z range of the cobj
	line_pts[1].z = d[2][1] + cobj_height;
	float t; // unused
	vector3d normal;
	if (line_int_exact(line_pts[!bot_surf], line_pts[bot_surf], t, normal)) return normal;
	if (type != COLL_POLYGON) return zero_vector;
	float const norm_z_thresh = 0.1;

	// unclear if the code below can ever return nonzero if the line_int_exact() test fails
	if (thickness > MIN_POLY_THICK) { // thick polygon
		vector<tquad_t> pts;
		thick_poly_to_sides(points, npoints, norm, thickness, pts);
			
		for (auto i = pts.begin(); i != pts.end(); ++i) {
			vector3d const normal(i->get_norm()); // thick_poly_to_sides() should guarantee that the sign is correct (normal facing out)
			if (bot_surf ? (normal.z > -norm_z_thresh) : (normal.z < norm_z_thresh)) continue; // facing down or vertical
			if (point_in_polygon_2d(support_pos.x, support_pos.y, i->pts, i->npts)) return normal;
		}
	}
	else if (fabs(norm.z) > norm_z_thresh) { // thin polygon, use the normal (facing up)
		if (point_in_polygon_2d(support_pos.x, support_pos.y, points, npoints)) {return (((norm.z < 0.0) ^ bot_surf) ? -norm : norm);}
	}
	return zero_vector; // not supported at this point
}
vector3d coll_obj::get_cobj_resting_normal() const {
	return get_cobj_supporting_normal(get_center_of_mass(), 1); // bottom normal, generally points in -z
}

vector3d get_mesh_normal_at(point const &pt) {

	int xpos(max(0, min(MESH_X_SIZE-1, get_xpos(pt.x)))), ypos(max(0, min(MESH_Y_SIZE-1, get_ypos(pt.y))));
	assert(!point_outside_mesh(xpos, ypos));
	if (is_in_ice(xpos, ypos) && pt.z > water_matrix[ypos][xpos]) return wat_vert_normals[ypos][xpos]; // on ice (no interpolation)
	float const xp((pt.x + X_SCENE_SIZE)*DX_VAL_INV), yp((pt.y + Y_SCENE_SIZE)*DY_VAL_INV);
	int const x0((int)xp), y0((int)yp);
	if (x0 < 0 || y0 < 0 || x0 >= MESH_X_SIZE-1 || y0 >= MESH_Y_SIZE-1) return surface_normals[ypos][xpos]; // Note: okay to just always return this?
	float const xpi(fabs(xp - (float)x0)), ypi(fabs(yp - (float)y0));
	return (1.0 - xpi)*((1.0 - ypi)*vertex_normals[y0][x0] + ypi*vertex_normals[y0+1][x0]) + xpi*((1.0 - ypi)*vertex_normals[y0][x0+1] + ypi*vertex_normals[y0+1][x0+1]);
}

void adjust_cobj_resting_normal(coll_obj &c, vector3d const &supp_norm) {
	//cout << "supp_norm: " << supp_norm.str() << endl;
	if (supp_norm == zero_vector) return; // invalid (can this happen?)
	vector3d const rest_norm(-c.get_cobj_resting_normal()); // negate so that it points up
	//cout << "rest_norm: " << rest_norm.str() << endl;
	if (rest_norm == zero_vector) return; // invalid (can this happen?)
	if (dot_product(supp_norm, rest_norm) > 0.999) return; // normals already align, no rotation needed
	c.rotate_about(c.get_center_of_mass(), cross_product(supp_norm, rest_norm).get_norm(), get_norm_angle(rest_norm, supp_norm));
	mark_movable_cobj_smap_update();
}
void rotate_to_align_with_supporting_cobj(coll_obj &rc, coll_obj const &sc) {
	//if (sc.is_movable()) return;
	adjust_cobj_resting_normal(rc, sc.get_cobj_supporting_normal(rc.get_center_of_mass(), 0));
}
void rotate_to_align_with_mesh(coll_obj &c) {
	adjust_cobj_resting_normal(c, get_mesh_normal_at(c.get_center_of_mass()));
}

point coll_obj::get_center_of_mass(bool ignore_group) const {

	if (!ignore_group && cgroup_id >= 0) {
		cgroup_props_t const &props(cobj_groups.get_props(cgroup_id));
		if (props.mass > 0.0) {return props.center_of_mass;} // use precomputed group center of mass if valid
	}
	if ((type == COLL_CYLINDER_ROT || type == COLL_CAPSULE) && radius != radius2) {
		float const r1s(radius*radius), r2s(radius2*radius2), r1r2(radius*radius2), t((r1s + 2*r1r2 + 3*r2s)/(4*(r1s + r1r2 + r2s)));
		return t*points[1] + (1.0 - t)*points[0]; // correct for cylinder, approximate for capsule
	}
	else if (type == COLL_POLYGON) { // polygon centroid (approximate), should be correct for both thick and thin polygons
		if (was_a_cube()) {return get_center_pt();} // cube optimization
		assert(npoints == 3 || npoints == 4);
		point const ca(triangle_centroid(points[0], points[1], points[2]));
		
		if (npoints == 4) {
			point const cb(triangle_centroid(points[2], points[3], points[0]));
			float const aa(triangle_area(points[0], points[1], points[2])), ab(triangle_area(points[2], points[3], points[0]));
			return (ca*aa + cb*ab)/(aa + ab); // centroid points weighted by triangle area (extruded cobj volume)
		}
	}
	return get_center_pt(); // correct for cube, sphere, and torus
}

float coll_obj::get_group_mass() const {
	return ((cgroup_id >= 0) ? cobj_groups.get_props(cgroup_id).mass : get_mass());
}

void rotate_point(point &pt, point const &rot_pt, vector3d const &axis, float angle) {
	pt -= rot_pt; // translate to rotation point
	rotate_vector3d(axis, angle, pt); // rotate the point
	pt += rot_pt; // translate back
}

void coll_obj::rotate_about(point const &pt, vector3d const &axis, float angle, bool do_re_add) { // angle is in radians

	if (angle == 0.0) return;
	assert(axis != zero_vector);
	remove_coll_object(id, 0);
	//point const prev_pts0(points[0]);
	//cout << "pt: " << pt.str() << ", axis: " << axis.str() << ", angle: " << angle << endl;

	switch (type) {
	case COLL_TORUS:
		rotate_vector3d(axis, angle, norm);
		norm.normalize(); // keep it normalized
		// fallthrough to sphere case to rotate center point
	case COLL_SPHERE:
		rotate_point(points[0], pt, axis, angle);
		break;
	case COLL_CYLINDER: // axis-aligned, not normally rotated
		coll_type = COLL_CYLINDER_ROT; // convert to rotated cylinder, then fallthrough
		radius2   = radius; // ???
	case COLL_CAPSULE:
	case COLL_CYLINDER_ROT:
		for (int i = 0; i < 2; ++i) {rotate_point(points[i], pt, axis, angle);}
		break;
	case COLL_CUBE: // axis-aligned, not normally rotated
		convert_cube_to_ext_polygon(); // convert to extruded polygon, then fallthrough
	case COLL_POLYGON: {
		for (int i = 0; i < npoints; ++i) {rotate_point(points[i], pt, axis, angle);}
		//vector3d const prev_norm(norm);
		norm = get_poly_norm(points);
		//if (dot_product(norm, prev_norm) < 0.0) {norm = -norm;} // keep the correct sign
		//if (get_min_dim(norm) != get_min_dim(prev_norm)) {cp.swap_tcs ^= SWAP_TCS_XY;} // if tex coord dim changed, swap XY (doesn't work in all cases)
		break;
	}
	default: assert(0);
	}
	//if (cp.tscale != 0.0) {texture_offset -= (points[0] - prev_pts0);}
	calc_bcube(); // may not always be needed
	if (do_re_add) {re_add_coll_cobj(id, 0);}
}


float cross_mag(point const &O, point const &A, point const &B, vector3d const &normal) {
	return dot_product(normal, cross_product(A-O, B-O));
}

struct pt_less { // override standard strange point operator<()
	bool operator()(point const &a, point const &b) const {
		if (a.z < b.z) return 1;
		if (a.z > b.z) return 0;
		if (a.y < b.y) return 1;
		if (a.y > b.y) return 0;
		return (a.x < b.x);
	}
};

void convex_hull(vector<point> const &pts, point const &normal, vector<point> &hull) {

	assert(!pts.empty());
	if (pts.size() <= 3) {hull = pts; return;} // <= 3 points must be convex
	// Andrew's monotone chain convex hull algorithm
	// https://en.wikibooks.org/wiki/Algorithm_Implementation/Geometry/Convex_hull/Monotone_chain
	int const n(pts.size());
	int k(0);
	hull.resize(2*n);
	vector<point> sorted(pts);
	sort(sorted.begin(), sorted.end(), pt_less()); // sort points lexicographically

	for (int i = 0; i < n; ++i) { // build lower hull
		while (k >= 2 && cross_mag(hull[k-2], hull[k-1], sorted[i], normal) <= 0) {k--;}
		hull[k++] = sorted[i];
	}
	for (int i = n-2, t = k+1; i >= 0; i--) { // build upper hull
		while (k >= t && cross_mag(hull[k-2], hull[k-1], sorted[i], normal) <= 0) {k--;}
		hull[k++] = sorted[i];
	}
	assert(k > 0);
	hull.resize(k-1); // remove the last point (duplicate)
}

vector3d get_lever_rot_axis(point const &support_pt, point const &center_of_mass, vector3d const &gravity=plus_z) {
	return cross_product((center_of_mass - support_pt), gravity).get_norm();
}

point get_hull_closest_pt(vector<point> const &hull, point const &pt) {

	assert(hull.size() >= 3);
	float dmin(0.0);
	point min_pt(hull.front());
	point prev(hull.back());

	for (auto i = hull.begin(); i != hull.end(); ++i) {
		float const dist(pt_line_dist(pt, prev, *i));
		if (dmin == 0.0 || dist < dmin) {min_pt = get_closest_pt_on_line(pt, prev, *i); dmin = dist;}
		prev = *i;
	}
	return min_pt;
}

struct rot_val_t {
	point pt;
	vector3d axis;
	rot_val_t() : pt(all_zeros), axis(zero_vector) {}
	rot_val_t(point const &pt_, vector3d const &axis_) : pt(pt_), axis(axis_) {}
};

// could calculate normal = get_poly_norm(&support_pts.front(), 1);
rot_val_t get_cobj_rot_axis(vector<point> const &support_pts, point const &normal, point const &center_of_mass, vector3d const &gravity=plus_z) {

	point closest_pt;
	if      (support_pts.size() == 1) {closest_pt = support_pts[0];} // supported by a point
	else if (support_pts.size() == 2) {closest_pt = get_closest_pt_on_line(center_of_mass, support_pts[0], support_pts[1]);} // supported by a line
	else {
		vector<point> hull;
		convex_hull(support_pts, normal, hull);
		if (point_in_convex_planar_polygon(hull, normal, center_of_mass)) return rot_val_t();
		// Note: if closest point is on an edge, we could use the edge dir for the rot axis; however, that doesn't work if the closest point is a corner on the convex hull
		closest_pt = get_hull_closest_pt(hull, center_of_mass);
		//cout << "support pts: " << endl; for (auto i = support_pts.begin(); i != support_pts.end(); ++i) {cout << i->str() << endl;}
		//cout << "hull: " << endl; for (auto i = hull.begin(); i != hull.end(); ++i) {cout << i->str() << endl;}
		//cout << "closest_pt: " << closest_pt.str() << endl << "center_of_mass: " << center_of_mass.str() << endl;
	}
	if (dist_less_than(closest_pt, center_of_mass, TOLERANCE)) return rot_val_t(); // perfect balance (avoid div-by-zero)
	return rot_val_t(closest_pt, get_lever_rot_axis(closest_pt, center_of_mass, gravity)); // zero_vector means point is supported
}


bool coll_obj::is_point_supported(point const &pos) const {

	float const norm_z_thresh = 0.9; // for polygon sides; if normal.z > this value, the surface is mostly horizontal

	switch (type) {
	case COLL_CUBE:     return contains_pt_xy(pos);
	case COLL_CYLINDER: return dist_xy_less_than(pos, points[0], radius);
	case COLL_SPHERE:   return 0; // not flat
	case COLL_CYLINDER_ROT:
		if (points[0].x != points[1].x || points[0].y != points[1].y) return 0; // non-vertical/not flat
		return dist_xy_less_than(pos, points[0], ((points[0].z < points[1].z) ? radius2 : radius)); // use radius at the top
	case COLL_TORUS:    return 0; // not flat
	case COLL_CAPSULE:  return 0; // not flat
	case COLL_POLYGON:
		if (fabs(norm.z) > norm_z_thresh) {return point_in_polygon_2d(pos.x, pos.y, points, npoints);}
		
		if (thickness > MIN_POLY_THICK) { // non-horizontal thick polygon
			vector<tquad_t> pts;
			thick_poly_to_sides(points, npoints, norm, thickness, pts);
			
			for (auto i = pts.begin(); i != pts.end(); ++i) {
				if (fabs(i->get_norm().z) > norm_z_thresh) {return point_in_polygon_2d(pos.x, pos.y, i->pts, i->npts);}
			}
		}
		return 0; // not flat
	default: assert(0);
	} // end switch
	return 0; // never gets here
}

float get_max_cobj_move_delta(coll_obj const &c1, coll_obj const &c2, vector3d const &delta, float step_thresh, float tolerance=0.0) {

	assert(step_thresh > 0.0);
	float valid_t(0.0), prev_t(0.0);
	unsigned num_iters(0);
	coll_obj test_cobj(c1); // deep copy
	test_cobj.cgroup_id = -1; // clear cgroup_id flag to avoid unnecessary updates

	// since there is no cobj-cobj closest distance function, binary split the delta range until cobj and c no longer intersect
	for (float t = 0.5, step = 0.25; step > step_thresh; step *= 0.5) { // initial guess is the midpoint
		test_cobj.shift_by((t - prev_t)*delta);
		prev_t = t;
		if (test_cobj.intersects_cobj(c2, tolerance)) {t -= step;} else {valid_t = t; t += step;}
		if (++num_iters > 1000) break; // reached max iteration count (to avoid perf problems)
	}
	return valid_t;
}

bool binary_step_moving_cobj_delta(coll_obj const &cobj, vector<unsigned> const &cobjs, vector3d &delta, float tolerance=0.0) {

	float const int_toler(1.5*tolerance); // larger tolerance to allow for slight movement
	float step_thresh(0.001);

	for (auto i = cobjs.begin(); i != cobjs.end(); ++i) {
		coll_obj const &c(coll_objects.get_cobj(*i));
		// moving object resting (stacked) on cobj, ignore it
		if (cobj.has_flat_top_bot() && c.has_flat_top_bot() && c.is_movable() && c.get_cube_center().z > cobj.d[2][1]) continue;
		
		if (cobj.intersects_cobj(c, int_toler)) { // intersects at the starting location, don't allow it to move (stuck)
			coll_obj cobj2(cobj); // deep copy so we can modify it
			cobj2.translate_pts_and_bcube(delta); // shift by to see if it gets unstuck - handle the touching case (floating-point error)
			if (cobj2.intersects_cobj(c, int_toler)) return 0; // it's actually intersecting, fail
		}
		float const valid_t(get_max_cobj_move_delta(cobj, c, delta, step_thresh, tolerance));
		if (valid_t < TOLERANCE) return 0; // can't move (avoid div-by-zero and negative t)
		step_thresh /= valid_t; // adjust thresh to avoid tiny steps for large number of cobjs
		delta       *= valid_t;
	} // for i
	return 1;
}

bool intersects_any_cobj(coll_obj const &cobj, vector<unsigned> const &cobjs, float tolerance=0.0) {

	for (auto i = cobjs.begin(); i != cobjs.end(); ++i) {
		if (cobj.intersects_cobj(coll_objects.get_cobj(*i), tolerance)) return 1;
	}
	return 0;
}

void register_moving_cobj(unsigned index) {
	moving_cobjs.insert(index); // skip insert if last_coll was set?
	coll_objects.get_cobj(index).last_coll = 8; // mark as moving/collided to prevent the physics system from immediately putting this cobj to sleep
}

void check_moving_cobj_int_with_dynamic_objs(unsigned index, vector3d const &delta) {

	coll_obj &cobj(coll_objects.get_cobj(index));
	vector<unsigned> cobjs;

	// wake up adjacent/nearby moving cobjs in case they need to move
	cube_t bcube(cobj);
	bcube.union_with_cube(bcube - delta); // expand to cover entire range of movement
	bcube.d[2][1] += 0.1*cobj.dz(); // increase height by 10% to include cobjs resting on this cobj
	get_intersecting_cobjs_tree(bcube, cobjs, index, 0.0, 0, 0, -1);

	for (auto i = cobjs.begin(); i != cobjs.end(); ++i) {
		if (coll_objects.get_cobj(*i).is_movable()) {register_moving_cobj(*i);} // wake this cobj up - may already be there
	}

	// check for collisions with dynamic objects from groups
	cobjs.clear();
	get_intersecting_cobjs_tree(cobj, cobjs, -1, 0.0, 1, 0, -1); // duplicates are okay
	if (cobjs.empty()) return;

	// some dynamic object collided, but we can't tell which one, so iterate over the groups and test them all
	for (int g = 0; g < num_groups; ++g) {
		obj_group &objg(obj_groups[g]);
		if (!objg.enabled || !objg.large_radius()) continue;
		
		for (unsigned i = 0; i < objg.end_id; ++i) {
			dwobject &obj(objg.get_obj(i));
			if (obj.status != 4) continue; // not stopped
			if (!cobj.sphere_intersects(obj.pos, obj.get_true_radius())) continue;
			obj.flags |= WAS_PUSHED;
			obj.flags &= ~ALL_COLL_STOPPED;
			obj.status = 1;
		}
	} // for g
}

bool is_rolling_cobj(coll_obj const &cobj) {

	if (cobj.type == COLL_SPHERE) return 1;
#if 0 // the other cases don't work
	if ((cobj.type == COLL_CYLINDER || cobj.type == COLL_CAPSULE) && cobj.radius == cobj.radius2) { // cylinder/capsule with uniform radius
		vector3d const dir(cobj.points[1] - cobj.points[0]);
		return (dir.z == 0.0 && (dir.x == 0.0 || dir.y == 0.0)); // oriented in either X or Y
	}
#endif
	return 0;
}

float get_cobj_step_height() {return 0.4*C_STEP_HEIGHT*CAMERA_RADIUS;} // cobj can be lifted by 40% of the player step height

bool check_top_face_agreement(vector<unsigned> const &cobjs) {

	// this flow doesn't handle cobj alignment due to multiple contact points/angles;
	// therefore, we only allow multi-cobj alignment if all supporting cobjs have the same top face height and slope,
	// which for now only includes adjacent cubes with the same z2 value, such as those that come out of coll cube splitting
	if (cobjs.size() <= 1) return 1;
	coll_obj &ref_cobj(coll_objects.get_cobj(cobjs.front()));
	if (ref_cobj.type != COLL_CUBE) return 0;

	for (auto i = cobjs.begin()+1; i != cobjs.end(); ++i) {
		coll_obj &cobj(coll_objects.get_cobj(*i));
		if (cobj.type != COLL_CUBE || fabs(cobj.d[2][1] - ref_cobj.d[2][1]) > TOLER) return 0;
	}
	return 1;
}

void check_cobj_alignment(unsigned index) {

	coll_obj &cobj(coll_objects.get_cobj(index));
	if (!cobj.has_hard_edges()) return; // not yet supported (use has_flat_top_bot()?)
	float const tolerance(1.0E-6), cobj_height(cobj.dz());
	point const center_of_mass(cobj.get_center_of_mass());
	// check other static cobjs
	// Note: since we're going to rotate the cobj, we need to use the bounding sphere, which is rotation invariant, so that we don't intersect a new cobj after rotation
	point bs_center;
	float bs_radius;
	cobj.bounding_sphere(bs_center, bs_radius);
	bs_radius += p2p_dist(bs_center, center_of_mass); // conservative, but probably okay since the delta will usually be small
	//assert(bs_radius < 4.0); // sanity check
	cube_t context_bcube(center_of_mass, center_of_mass);
	context_bcube.expand_by(bs_radius);
	vector<unsigned> &cobjs(coll_objects.get_temp_cobjs());
	get_intersecting_cobjs_tree(context_bcube, cobjs, index, -tolerance, 0, 0, -1); // include adjacencies
	auto in(cobjs.begin()), out(in);

	for (; in != cobjs.end(); ++in) {
		coll_obj const &c(coll_objects.get_cobj(*in));
		if (!c.sphere_intersects(center_of_mass, bs_radius)) continue; // no intersection with bounding sphere
		if (c.is_movable() && c.get_center_of_mass().z > center_of_mass.z) continue; // movable cobj resting on this cobj, ignore it (let it update itself later)
		*out++ = *in; // keep this cobj
	}
	cobjs.erase(out, cobjs.end());

#if 0 // Note: unfinished and disabled
	// allow rotation of cubes, which will become extruded polygons, so polygons need to work as well;
	// curved cobjs such as spheres, rotated cylinders, and capsules are generally unstable since they only contact the ground at a single point;
	// spheres are handled by is_rolling_cobj() and should be okay;
	// Z-axis cylinders can rest on a flat surface, but as soon as they start to rotate they're no longer axis aligned
	if (cobj.v_fall == 0.0 && (cobj.type == COLL_CUBE || cobj.type == COLL_POLYGON) && !cobjs.empty()) {
		float const support_toler(-max(100.0*tolerance, 0.01*cobj_height)); // negative tolerance to make intersections more likely
		vector3d normal(plus_z); // okay for cubes and Z-oriented cylinders
		vector<point> support_pts;
		bool supported(0);

		for (auto i = cobjs.begin(); i != cobjs.end(); ++i) {
			coll_obj const &c(coll_objects.get_cobj(*i));
			if (c.is_point_supported(center_of_mass)) {supported = 1; break;}
			// fill in support_pts, which generally requires determining intersection points, which is not implemented for all cobjs;
			// this works for cubes, but as soon as the cube rotates it's no longer axis aligned
			cobj.get_contact_points(c, support_pts, 1, support_toler);
		}
		if (!supported && !support_pts.empty()) { // can rotate due to gravity and maybe fall
			rot_val_t const rot_val(get_cobj_rot_axis(support_pts, normal, center_of_mass));
			//cout << TXT(supported) << TXT(support_pts.size())<< ", normal: " << normal.str() << ", rot axis: " << rot_val.axis.str() << endl;
		
			if (rot_val.axis != zero_vector) { // apply rotation if not stable at rest
				float const angle(0.1); // FIXME
				cobj.rotate_about(rot_val.pt, rot_val.axis, angle);
				return;
			}
		}
	}
#endif
	if (check_top_face_agreement(cobjs)) {
		cube_t bcube(cobj);
		bcube.d[2][1] = bcube.d[2][0]; // top edge starts at cobj bottom
		bcube.d[2][0] -= 0.1*cobj_height; // shift down by 10% of the cobj height

		for (auto i = cobjs.begin(); i != cobjs.end(); ++i) {
			coll_obj const &c(coll_objects.get_cobj(*i));
			if (!c.intersects(bcube)) continue; // context intersection, not true bottom edge intersection
			if (!c.is_point_supported(center_of_mass)) continue;
			// Note: okay to call this for each interacting cobj, as this will likely result in at most one rotation,
			// assuming that cobj doesn't actually intersect (much) with cobjs, and cobjs don't intersect with each other
			rotate_to_align_with_supporting_cobj(cobj, c);
		}
	}
	if (cobjs.empty()) { // check the mesh only if there are no cobjs nearby
		point const center(cobj.get_center_pt());
		float mesh_zval(interpolate_mesh_zval(center.x, center.y, 0.0, 1, 0, 1)); // Note: uses center point, not max mesh height under the cobj; clamped xy
		if (cobj.d[2][0] < mesh_zval + 0.01*cobj_height) {rotate_to_align_with_mesh(cobj);}
	}
}


void remove_cobjs_with_same_cgroup(coll_obj const &cobj, vector<unsigned> &cobjs) {

	if (cobj.cgroup_id < 0) return; // not in a group

	for (unsigned i = 0; i < cobjs.size(); ++i) {
		if (coll_objects.get_cobj(cobjs[i]).cgroup_id == cobj.cgroup_id) {cobjs[i] = cobjs.back(); cobjs.pop_back(); --i;} // remove this cobj
	}
}


vector3d get_cobj_drop_delta(unsigned index) {

	// Note: non-const because cobj.v_fall is updated - should we pass in/return v_fall and do that update elsewhere?
	coll_obj &cobj(coll_objects.get_cobj(index));
	float const tolerance(2.0E-6); // Note: 2x larger tolerance, to exclude cobjs that are touching based on adjustment from check_push_cobj()
	float const cobj_zmin(min(czmin, zbottom));
	float const accel(-0.5*base_gravity*GRAVITY*tstep); // half gravity
	float const cobj_height(cobj.dz()), prev_v_fall(cobj.v_fall), cur_v_fall(prev_v_fall + accel);
	cobj.v_fall = 0.0; // assume the cobj stops falling; compute the correct v_fall if we reach the end without returning
	float gravity_dz(-tstep*cur_v_fall), max_dz(min(gravity_dz, cobj.d[2][0]-cobj_zmin)); // usually positive
	if (max_dz < tolerance) return zero_vector; // can't drop further
	float const test_dz(max(max_dz, 0.25f*cobj_height)); // use a min value to ensure the z-slice isn't too small, to prevent instability (for example when riding down on an elevator)
	cube_t bcube(cobj); // start at the current cobj xy
	//bcube.d[2][1]  = cobj.d[2][0]; // top = cobj bottom (Note: more efficient, but doesn't work correctly for elevators)
	bcube.d[2][0] -= test_dz; // bottom (height = test_dz)
	vector<unsigned> &cobjs(coll_objects.get_temp_cobjs());
	get_intersecting_cobjs_tree(bcube, cobjs, index, tolerance, 0, 0, -1);
	remove_cobjs_with_same_cgroup(cobj, cobjs);

	// see if this cobj's bottom edge is colliding with a platform that's moving up (elevator)
	// also, if the cobj is currently intersecting another movable cobj, try to resolve the intersection so that stacking works by moving the cobj up
	vector3d delta_max(zero_vector);
	point const center_of_mass(cobj.get_center_of_mass());

	for (auto i = cobjs.begin(); i != cobjs.end(); ++i) {
		coll_obj const &c(coll_objects.get_cobj(*i)); // Note: handles case where c is below cobj

		if (cobj.type == COLL_SPHERE && c.type == COLL_SPHERE) { // sphere-sphere case
			vector3d delta(cobj.points[0] - c.points[0]);
			float sep_dist(delta.mag() - cobj.radius - c.radius);
			if (sep_dist >= 0.0) continue; // no intersection
			sep_dist *= 1.001; // increase slightly to prevent intersections due to FP error
			
			if (sep_dist*sep_dist > delta_max.mag_sq()) { // larger - use new delta value
				if (delta.mag() < TOLERANCE) {delta_max = vector3d(0.0, 0.0, -sep_dist);} // handle spheres at the same point by moving one up
				else {delta_max = -sep_dist*delta.get_norm();}
			}
			continue;
		}
		if (cobj.type == COLL_CUBE && c.type == COLL_CUBE) { // cube-cube case
			cube_t overlap;
			cobj.cube_intersection(c, overlap);
			vector3d const overlap_sz(overlap.get_size());

			if (min(overlap_sz.x, overlap_sz.y) < 0.05*overlap_sz.z) { // push rather than stack
				int const dim(overlap_sz[1] < overlap_sz[0]);
				//bool const dir(cobj.get_cube_center()[dim] < c.get_cube_center()[dim]);
				float const delta(overlap_sz[dim]);
				if (delta*delta > delta_max.mag_sq()) {delta_max = zero_vector; delta_max[dim] = delta;}
				continue;
			}
		}
		float const dz(c.d[2][1] - cobj.d[2][0]);
		if (dz <= 0 || c.d[2][1] > cobj.d[2][1]) continue; // bottom cobj/platform edge not intersecting

		if (c.is_movable()) { // both cobjs are movable - is this a stack?
			// flat cobjs can always be stacked
			if (c.type == COLL_CUBE || c.type == COLL_CYLINDER) {}
			else if (c.type == COLL_POLYGON && (c.has_z_normal() /*|| was_a_cube()*/)) {}
			else if (dz < get_cobj_step_height()) {} // c_top - cobj_bot < step_height
			else if (c.v_fall <= 0.0) continue; // not rising (stopped or falling)
			// assume it's a stack and treat it like a moving platform
		}
		else { // handle the platform or static collision case
			if (c.platform_id < 0) { // not a platform
				if (c.type != COLL_CUBE) continue; // only non-platform cubes and handled here
				cube_t bot_bcube(cobj); bot_bcube.d[2][1] = bot_bcube.d[2][0]; // shrink to zero height
				if (!c.contains_cube(bot_bcube)) continue; // handle only cases where the botto of the cobj is completely embedded in a cube
			}
			else if (!platforms.get_cobj_platform(c).is_active()) continue; // platform is not moving (is_moving() is faster but off by one frame on platform stop/change dir)
		}
		if (!cobj.intersects_cobj(c, tolerance)) continue; // no intersection
		//if (!c.is_point_supported(center_of_mass)) continue; // center of mass is not supported - doesn't really work, doesn't agree with code below
		if (dz*dz > delta_max.mag_sq()) {delta_max = vector3d(0.0, 0.0, dz);} // larger - use new delta value
	} // for i
	if (delta_max != zero_vector) {return delta_max;}
	
	// check other cobjs and the mesh to see if this cobj can be dropped
	vector3d delta(0.0, 0.0, -test_dz);
	point const center(cobj.get_center_pt());
	
	if (!binary_step_moving_cobj_delta(cobj, cobjs, delta, tolerance)) { // stuck
		if (prev_v_fall < 4.0*accel) {gen_sound(SOUND_OBJ_FALL, center, 0.5, 1.2);}
		// check for rolling cobjs that can roll downhill
		if (cobjs.size() != 1) return zero_vector; // can only handle a single supporting cobj
		if (!is_rolling_cobj(cobj)) return zero_vector; // not rolling
		coll_obj const &c(coll_objects.get_cobj(cobjs.front()));
		if (c.is_point_supported(center_of_mass)) return zero_vector; // center of mass is resting stably, it's stuck
		vector3d move_dir((center - c.get_center_pt()).get_norm()); // FIXME: incorrect for polygon
		
		if (c.type == COLL_CUBE) { // chose +/- x|y for cubes
			if (fabs(move_dir.x) < fabs(move_dir.y)) {move_dir = ((move_dir.y < 0.0) ? -plus_y : plus_y);} // y-edge
			else                                     {move_dir = ((move_dir.x < 0.0) ? -plus_x : plus_x);} // x-edge
			if (cobj.type == COLL_SPHERE) { // more accurate for cube vs. sphere
				point p_int; // unused
				unsigned cdir(0); // unused
				sphere_cube_intersect(center, cobj.radius, c, center-delta, p_int, move_dir, cdir, 0, 1); // return value ignored; should always return 1
			}
		}
		delta = 0.05*cobj_height*move_dir; // move 5% of cobj height
		set<unsigned> seen;
		push_cobj(index, delta, seen, all_zeros); // return value is ignored
		return zero_vector; // done
	}
	float mesh_zval(interpolate_mesh_zval(center.x, center.y, 0.0, 1, 1, 1)); // Note: uses center point, not max mesh height under the cobj; clamped xy

	// check for ice
	int xpos(get_xpos(center.x)), ypos(get_ypos(center.y));
	if (is_in_ice(xpos, ypos)) { // on ice
		float const water_zval(water_matrix[ypos][xpos]);
		if (center.z > water_zval) {mesh_zval = max(mesh_zval, water_zval);} // use water zval if cobj center is above the ice
	}
	float const mesh_dz(mesh_zval - cobj.d[2][0]); // Note: can be positive if cobj is below the mesh
	if (fabs(mesh_dz) < tolerance) return zero_vector; // resting on the mesh (never happens?)
	
	if (max(delta.z, -max_dz) < mesh_dz) { // under the mesh
		if (prev_v_fall < 10.0*accel) {gen_sound(SOUND_OBJ_FALL, center, 0.2, 0.8);}
		delta.z = mesh_dz; // don't let it go below the mesh
		bool const dim(abs(delta.y) < abs(delta.x));
		float radius(0.5f*(cobj.d[dim][1] - cobj.d[dim][0])); // perpendicular to direction of movement
		if      (cobj.type == COLL_CUBE    ) {radius *= 1.4;}
		else if (cobj.type == COLL_SPHERE  ) {radius *= 0.2;} // smaller since bottom surface area is small (maybe also not-vert cylinder?)
		else if (cobj.type == COLL_CYLINDER) {radius  = min(radius, cobj.radius);}
		modify_grass_at(center, radius, 1); // crush grass
	}
	else if (delta.z <= -max_dz) { // cobj falls the entire max distance without colliding, accelerate it
		// set terminal velocity to one cobj_height per timestep to avoid falling completely through another moving cobj (such as an elevator)
		cobj.v_fall = max(cur_v_fall, -cobj_height/tstep);
	} // else |delta.z| may be > |max_dz|, but it was only falling for one frame so should not be noticeable (and should be more stable for small tstep/accel)
	else if (delta.z < 0.0) { // falling the partial distance
		cobj.v_fall = prev_v_fall; // maintain previous falling velocity (no acceleration)
	}
	// handle water
	float depth(0.0);
	point const bot_cent(center.x, center.y, cobj.d[2][0]);

	if (is_underwater(bot_cent, 0, &depth)) {
		if (temperature > W_FREEZE_POINT) { // water
			float const cobj_mass(cobj.get_mass());
			float density(cobj.cp.density);

			if (cobj.type == COLL_CUBE || cobj.type == COLL_CYLINDER) { // has a flat top surface that other cobjs can rest on (what about extruded polygons?)
				cobjs.clear();
				cube_t bcube(cobj); // start at the current cobj xy
				bcube.d[2][1] += 0.1*cobj_height; // z-slice slightly above cobj
				bcube.d[2][0]  = cobj.d[2][0]; // top of cobj
				get_intersecting_cobjs_tree(bcube, cobjs, index, tolerance, 0, 0, -1);
				//remove_cobjs_with_same_cgroup(cobj, cobjs); // ???
				float tot_mass(cobj_mass);
				if (cobj.cgroup_id >= 0) {} // Note: not meant to work with grouped cobjs - unclear what to do

				// since the z-slice is so thin/small, we simply assume that any cobj intersecting it is resting on the current cobj
				for (auto i = cobjs.begin(); i != cobjs.end(); ++i) {
					coll_obj const &c(coll_objects.get_cobj(*i));

					if (is_underwater(c.get_center_pt(), 0)) { // top cobj is also underwater
						if (c.cp.density > 1.0) {tot_mass += c.volume * (c.cp.density - 1.0);} // if c sinks, add it's net mass when submerged in water
					}
					else {tot_mass += c.get_mass();} // above the water - add the full mass
				}
				density *= tot_mass / cobj_mass; // density is effective total mass / volume
			}
			if (density <= 1.0) { // floats
				if (delta.z <= 0.0) { // falling
					float const target_z(depth - density*cobj_height); // adjust z for constant depth - cobj moves with the water surface
					delta.z     = 0.1*target_z; // smoothly transition from current z to target z
					cobj.v_fall = 0.0;
				}
			}
			else { // sinks
				if (depth > 0.5*cobj_height && mesh_dz < -0.1*cobj_height) { // sinking below the water
					for (unsigned n = 0; n < 4; ++n) {gen_bubble(global_rand_gen.gen_rand_cube_point(cobj));}
				}
				float const fall_rate((density - 1.0)/density); // falling velocity (approaches to 1.0 as density increases)
				max_dz      = min(gravity_dz*fall_rate, max_dz);
				cobj.v_fall = max(cobj.v_fall, -0.001f/tstep); // use a lower terminal velocity in water
			}
			if (prev_v_fall < 8.0*accel) {add_splash(center, xpos, ypos, min(100.0f, -5000*prev_v_fall*cobj_mass), min(2.0f*CAMERA_RADIUS, cobj.get_bsphere_radius()), 1);}
		}
		else if (depth > 0.5*cobj_height) {return zero_vector;} // stuck in ice
	}
	delta.z = max(delta.z, -max_dz); // clamp to the real max value if in freefall
	return delta;
}

void shift_cobj_up_down(coll_obj &cobj, unsigned index, vector3d const &delta) {
	cobj.shift_by(delta); // move cobj down
	cobj.cp.surfs = 0; // clear any invisible edge flags as moving may make these edges visible
	check_moving_cobj_int_with_dynamic_objs(index, delta);
}

void try_drop_movable_cobj(unsigned index, set<unsigned> &seen) {

	if (seen.find(index) != seen.end()) return; // already seen (needed for grouped cobjs)
	coll_obj &cobj(coll_objects.get_cobj(index));
	cobj_id_set_t const *group(nullptr);
	vector3d delta(get_cobj_drop_delta(index));

	if (cobj.cgroup_id >= 0) { // grouped cobj
		group = &cobj_groups.get_set(cobj.cgroup_id);
		assert(group->find(index) != group->end()); // must contain this cobj
		
		for (auto i = group->begin(); i != group->end(); ++i) { // if this cobj in a a group, we need to drop the whole group (or fail)
			seen.insert(*i); // mark as seen so we don't try to call try_drop_movable_cobj() on it again
			if (*i == index) continue; // already processed above
			vector3d const delta_i(get_cobj_drop_delta(*i));
			UNROLL_3X(delta[i_] = max(delta[i_], delta_i[i_]);) // use min fall dist or max rise dist (elevators)
			//if (delta == zero_vector) return; // not legal, because a later cobj may be on an elevator that's going up
		}
	}
	if (delta == zero_vector) return;
	shift_cobj_up_down(cobj, index, delta);

	if (group != nullptr) { // shift the group
		for (auto i = group->begin(); i != group->end(); ++i) {
			if (*i != index) {shift_cobj_up_down(coll_objects.get_cobj(*i), *i, delta);}
		}
	}
	mark_movable_cobj_smap_update();
}


int check_push_cobj(unsigned index, vector3d &delta, set<unsigned> &seen, point const &pushed_from, float &delta_z) {

	delta_z = 0.0;
	coll_obj &cobj(coll_objects.get_cobj(index));
	if (!cobj.is_movable()) return 0; // not movable
	delta.z = 0.0; // for now, objects can only be pushed in xy
	float const tolerance(1.0E-6), toler_sq(tolerance*tolerance);
	if (delta.mag_sq() < toler_sq) return 0;

	// make sure the cobj center stays within the scene bounds
	for (unsigned d = 0; d < 2; ++d) { // x/y
		if      (delta[d] > 0.0) {delta[d] = min(delta[d], ( SCENE_SIZE[d]-HALF_DXY - cobj.d[d][1]));} // pos scene edge
		else if (delta[d] < 0.0) {delta[d] = max(delta[d], (-SCENE_SIZE[d] - cobj.d[d][0]));} // neg scene edge
	}
	if (delta.mag_sq() < toler_sq) return 0;

	// determine if this cobj can be moved by checking other static objects
	// dynamic objects should move out of the way of this cobj by themselves
	cube_t bcube(cobj); // orig pos
	bcube += delta; // move to new pos
	bcube.union_with_cube(cobj); // union of original and new pos
	bcube.expand_by(-tolerance); // shrink slightly to avoid false collisions (in prticular with extruded polygons)
	vector<unsigned> cobjs;
	get_intersecting_cobjs_tree(bcube, cobjs, index, tolerance, 0, 0, -1); // duplicates should be okay
	remove_cobjs_with_same_cgroup(cobj, cobjs);
	vector3d const start_delta(delta);
	
	if (cobj.type == COLL_CUBE) { // check for horizontally stackable movable cobjs - limited to cubes for now
		seen.insert(index);

		for (unsigned i = 0; i < cobjs.size(); ++i) {
			unsigned const cid(cobjs[i]);
			coll_obj const &c(coll_objects.get_cobj(cid));
			if (!c.is_movable() || c.type != COLL_CUBE) continue;
			if (!cobj.intersects_cobj(c, -tolerance))   continue; // no initial intersection/adjacency
			if (seen.find(cid) != seen.end())           continue; // prevent infinite recursion
			seen.insert(cid);
			vector3d delta2(delta);
			if (!push_cobj(cid, delta2, seen, cobj.get_cube_center())) return 0; // can't push (recursive call)
			delta = delta2; // update with maybe reduced delta
			cobjs[i] = cobjs.back(); cobjs.pop_back(); --i; // remove this cobj
		}
	}
	if (!binary_step_moving_cobj_delta(cobj, cobjs, delta, tolerance)) { // failed to move
		// if there is a ledge (cobj z top) slightly above the bottom of the cobj, maybe we can lift it up;
		// meant to work with ramps and small steps, but not stairs or tree trunks (obviously)
		float step_height(get_cobj_step_height()), moved_height(0.0), cobj_bot(cobj.d[2][0]);
		bool has_ledge(0), has_ramp(0), success(0);
		
		if (cobj.cgroup_id >= 0) { // if this isn't on the bottom of the group, don't allow a ledge
			cobj_id_set_t const &group(cobj_groups.get_set(cobj.cgroup_id));
			for (auto i = group.begin(); i != group.end(); ++i) {cobj_bot = min(cobj_bot, coll_objects.get_cobj(*i).d[2][0]);}
		}
		for (auto i = cobjs.begin(); i != cobjs.end(); ++i) {
			coll_obj const &c(coll_objects.get_cobj(*i));
			if (c.type == COLL_POLYGON && (!c.is_thin_poly() || fabs(c.norm.z) > 0.5)) {has_ramp = 1;} // Note: can only push cobjs up polygon ramps, not cylinders
			else if (c.has_flat_top_bot() && (c.d[2][1] - cobj_bot) < step_height) {has_ledge = 1; break;} // c_top - cobj_bot < step_height
		}
		if (!has_ledge) {
			if (!has_ramp) return 0; // stuck
			step_height = min(step_height, delta.mag()); // if there is no ledge, limit step height to delta length (45 degree inclination)
		}
		for (unsigned n = 0; n < 10; ++n) { // try raising the cobj in 10 steps
			delta = start_delta; // reset to try again
			moved_height += 0.1*step_height;
			cobj.shift_by(vector3d(0.0, 0.0, 0.1*step_height)); // shift up slightly to see if it can be moved now
			if (binary_step_moving_cobj_delta(cobj, cobjs, delta, tolerance)) {success = 1; break;}
		}
		if (!success || cobj.cgroup_id >= 0) {cobj.shift_by(vector3d(0.0, 0.0, -moved_height));} // shift down to original position on failure or grouped cobjs
		if (!success) return 0; // can't move
		delta_z = moved_height;
		return 2; // success, but required moving up
	}

	// if the cobj is a cylinder or capsule, try to rotate it about its center
	// cubes and polygons don't rotate because they have more contact area on the bottom that contributes to friction
	// spheres don't rotate because they're rotationally invariant, except for their textures, which don't rotate properly anyway
	if (pushed_from == all_zeros) {} // not pushed from a valid position
	else if (cobj.cgroup_id >= 0) {} // grouped cobjs don't rotate (but could be made to, with some effort)
	else if (!cobj.is_cylinder() && cobj.type != COLL_CAPSULE) {} // not a rotatable object
	else if (cobj.cp.tid >= 0 && cobj.cp.tscale != 0.0) {} // not if textured or using tex coords (since texgen textures don't rotate)
	else if (fabs(cobj.points[0].z - cobj.points[1].z) < TOLER) { // oriented in the XY plane, rotates around z
		vector3d const cdir(cobj.points[1] - cobj.points[0]);

		if (fabs(dot_product(delta.get_norm(), cdir.get_norm())) < 0.5) { // pushed from the side, not the end
			float const line_t(get_closest_pt_on_line_t(pushed_from, cobj.points[0], cobj.points[1]));
			float const torque(CLIP_TO_pm1(2.0f*(line_t - 0.5f))); // 0.0 at center, +/-1.0 at cylinder ends
			float const abs_torque(fabs(torque));
			float const friction_factor((cobj.is_wet() || cobj.is_snow_cov()) ? 0.15 : 0.3);

			if (abs_torque > friction_factor) { // pushed near the end of the cylinder, so there is some torque
				point const closest_pt(cobj.points[0] + line_t*cdir);
				float const net_torque((abs_torque - friction_factor)/(1.0f - friction_factor)); // shift the torque curve
				float const angle(0.01*net_torque*SIGN(torque)*SIGN(cross_product(cdir, pushed_from-closest_pt).z));
				coll_obj const before_rotate(cobj);
				cobj.rotate_about(cobj.get_center_of_mass(), plus_z, angle, 0); // don't re-add yet
				cobjs.clear();
				cube_t bcube(cobj);
				float const dh(0.01*cobj.dz());
				bcube.d[2][0] += dh; bcube.d[2][1] -= dh; // shrink slightly in z to exclude cobjs above (resting on) and below (resting on)
				get_intersecting_cobjs_tree(bcube, cobjs, index, -tolerance, 0, 0, -1); // duplicates should be okay, include adjacent cobjs
				// FIXME: only partially effective
				if (intersects_any_cobj(cobj, cobjs, -tolerance)) {cobj = before_rotate;} // restore the cobj to undo the rotation
				else {
					cobj.re_add_coll_cobj(cobj.id, 0);
					delta *= 1.0 - net_torque; // this is the remaining translation component
				}
			}
		}
	}
	return 1; // can be moved
}

void move_cobj_and_update_state(unsigned index, vector3d const &cobj_delta) {
	
	coll_obj &cobj(coll_objects.get_cobj(index));
	cobj.move_cobj(cobj_delta, 1); // move the cobj instead of the player and re-add to coll structure
	cobj.cp.surfs = 0; // clear any invisible edge flags as moving may make these edges visible
	register_moving_cobj(index); // may already be there
	check_moving_cobj_int_with_dynamic_objs(index, cobj_delta);
}

bool push_cobj(unsigned index, vector3d &delta, set<unsigned> &seen, point const &pushed_from) {

	coll_obj &cobj(coll_objects.get_cobj(index));
	cobj_id_set_t const *group(nullptr);
	float delta_z(0.0);

	if (cobj.cgroup_id >= 0) { // grouped cobj
		group = &cobj_groups.get_set(cobj.cgroup_id);
		assert(group->find(index) != group->end()); // must contain this cobj
		float max_dz(0.0);
		
		for (auto i = group->begin(); i != group->end(); ++i) { // if this cobj in a a group, we need to push the whole group (or fail)
			seen.insert(*i); // prevent infinite recursion
			int const ret(check_push_cobj(*i, delta, seen, pushed_from, delta_z));
			if (ret == 0) return 0; // not pushed, not moved up
			if (ret == 2) {max_dz = max(max_dz, delta_z);}
		}
		if (max_dz > 0.0) { // must move all cobjs up together
			// FIXME: still not completely correct, since this doesn't check that it's valid to move all cobjs up by this amount (no check for top collision)
			for (auto i = group->begin(); i != group->end(); ++i) {coll_objects.get_cobj(*i).shift_by(vector3d(0.0, 0.0, max_dz));}
		}
	}
	else { // push single cobj
		if (!check_push_cobj(index, delta, seen, pushed_from, delta_z)) return 0; // not pushed (okay if moved up)
	}
	// cobj moved, see if it gets teleported
	vector3d cobj_delta(delta);
	float const radius(cobj.get_bsphere_radius());
	point const center(cobj.get_center_pt());
	point cobj_pos(center + cobj_delta); // post-move pos
	
	if (maybe_teleport_object(cobj_pos, 0.5*radius, NO_SOURCE, COLLISION)) { // was teleported (type is ignored here)
		cobj_delta = cobj_pos - center + delta.get_norm()*radius; // move radius past the teleport end position so that the player doesn't get stuck on the cobj when going through
	}
	if (group != nullptr) { // grouped cobjs
		for (auto i = group->begin(); i != group->end(); ++i) {move_cobj_and_update_state(*i, cobj_delta);} // need to teleport any other grouped cobjs
	}
	else {
		move_cobj_and_update_state(index, cobj_delta); // single cobj
	}
	mark_movable_cobj_smap_update();
	if ((rand2()%1000) == 0) {gen_sound(SOUND_SLIDING, center, 0.1, 1.0);}
	return 1; // moved
}

bool push_movable_cobj(unsigned index, vector3d &delta, point const &pushed_from) {
	set<unsigned> seen;
	return push_cobj(index, delta, seen, pushed_from);
}

bool proc_movable_cobj(point const &orig_pos, point &player_pos, unsigned index, int type) {

	if (type == CAMERA && sstates != nullptr && sstates[CAMERA_ID].jump_time > 0) return 0; // can't push while jumping (what about smileys?)
	vector3d delta(orig_pos - player_pos);
	coll_obj const &cobj(coll_objects.get_cobj(index));

	if (dot_product(delta, (cobj.get_center_pt() - player_pos)) < 0.0) {
		player_pos = orig_pos; // don't allow the player to be moved
		return 0; // this is a pull rather than a push (due to fp error?), and we don't support pulling cobjs, so ignore it
	}
	if (1) { // see if the player is standing on the cobj, or a cobj from the same group - if so, there is no traction, and the cobj can't be pushed
		point bot_pos(player_pos);
		bot_pos.z -= 1.1*CAMERA_RADIUS;
		if (type == CAMERA) {bot_pos.z -= camera_zh;}
		int cindex(-1);
		point cpos; // unused
		vector3d cnorm; // unused

		if (check_coll_line_exact_tree(player_pos, bot_pos, cpos, cnorm, cindex, -1)) {
			if (cindex == (int)index || (cobj.cgroup_id >= 0 && coll_objects.get_cobj(cindex).cgroup_id == cobj.cgroup_id)) { // this cobj, or one in the same group
				player_pos = orig_pos; // don't allow the player to be moved
				return 0;
			}
		}
	}
	if (!push_movable_cobj(index, delta, player_pos)) return 0;
	player_pos += delta; // restore player pos, at least partially
	return 1; // moved
}

void proc_moving_cobjs() {

	vector<pair<float, unsigned>> by_z1;
	set<unsigned> seen;

	for (auto i = moving_cobjs.begin(); i != moving_cobjs.end();) {
		coll_obj &cobj(coll_objects.get_cobj(*i));
		if (cobj.status != COLL_STATIC) {moving_cobjs.erase(i++); continue;} // remove if destroyed
		bool const do_update(cobj.last_coll || ((*i+frame_counter)&7) == 0); // check for cobj updates if it moved within the last few frames or every 8th frame
		if (do_update) {by_z1.push_back(make_pair(cobj.d[2][0], *i));} // not sleeping, try to drop it
		if (cobj.last_coll) {--cobj.last_coll;}
		++i;
	}
	sort(by_z1.begin(), by_z1.end()); // sort by z1 so that stacked cobjs work correctly (processed bottom to top)
	
	for (auto i = by_z1.begin(); i != by_z1.end(); ++i) {
		try_drop_movable_cobj(i->second, seen);
		check_cobj_alignment(i->second);
	}
}

