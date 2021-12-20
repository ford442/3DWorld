// 3D World - u_ship implementation for universe mode - ship/object intersection code for lines and spheres
// by Frank Gennari
// 11/29/05

#include "ship.h"
#include "ship_util.h"


bool const COBJ_SHADOWS     = 1;
bool const NO_OBJ_OBJ_INT   = 0;
float const MIN_SHADOW_DIST = 0.01;


extern int display_mode;

intersect_params def_int_params;


point ship_coll_obj::get_center() const { // inefficient
	
	point c;
	float r;
	get_bounding_sphere(c, r);
	return c;
}


bool ship_cylinder::line_intersect(point const &lp1, point const &lp2, float &t, bool calc_t) const {
	return (line_int_cylinder(lp1, lp2, p1, p2, r1, r2, check_ends, t) != 0);
}

bool ship_cylinder::sphere_intersect(point const &sc, float sr, point const &p_last, point &p_int, vector3d &norm, bool calc_int) const {
	return sphere_intersect_cylinder_ipt(sc, sr, p1, p2, r1, r2, check_ends, p_int, norm, calc_int);
}

void ship_cylinder::get_bounding_sphere(point &c, float &r) const {
	point const p[2] = {p1, p2};
	cylinder_bounding_sphere(p, r1, r2, c, r);
}

void ship_cylinder::draw_svol(point const &tpos, float cur_radius, point const &spos, int ndiv, bool player, free_obj const *const obj) const {

	assert(obj);
	int npts;
	point pts_[4];
	point spos_xf(spos);
	obj->xform_point(spos_xf);
	point const center((p1 + p2)*0.5);
	vector3d const v1(center - spos_xf);
	cylinder_quad_projection(pts_, *this, v1, npts);
	upos_point_type pts[4]; // 3 or 4
	for (int i = 0; i < npts; ++i) {pts[i] = pts_[i];}
	ushadow_polygon const poly(pts, npts, tpos, cur_radius, spos, player, obj);
	if (poly.invalid) return; // if polygon is invalid, ends should be invalid as well
	poly.draw_geom(tpos);
	
	if (check_ends) {
		point const pos((r1 == r2) ? center : ((r1 < r2) ? p2 : p1)); // center if equal, otherwise end with largest radius
		ushadow_triangle_mesh(pos, max(r1, r2), (p2 - p1), ndiv, tpos, cur_radius, spos, obj).draw_geom(tpos);
	}
}


bool ship_cube::line_intersect(point const &lp1, point const &lp2, float &t, bool calc_t) const {

	float tmax(0.0);
	return get_line_clip(lp1, lp2, d, t, tmax);
}

bool ship_cube::sphere_intersect(point const &sc, float sr, point const &p_last, point &p_int, vector3d &norm, bool calc_int) const {

	if (calc_int) {
		unsigned cdir; // unused
		return sphere_cube_intersect(sc, sr, *this, p_last, p_int, norm, cdir, 1);
	}
	return sphere_cube_intersect(sc, sr, *this);
}

void ship_cube::translate(point const &p) {

	for (unsigned i = 0; i < 3; ++i) {
		for (unsigned j = 0; j < 2; ++j) {
			d[i][j] += p[i];
		}
	}
}

void ship_cube::get_bounding_sphere(point &c, float &r) const {
	c = get_cube_center();
	r = get_bsphere_radius();
}

void ship_cube::draw_svol(point const &tpos, float cur_radius, point const &spos, int ndiv, bool player, free_obj const *const obj) const {

	assert(obj);
	point spos_xf(spos);
	obj->xform_point(spos_xf);
	vector3d const shadow_dir(get_cube_center() - spos_xf);
	upos_point_type pts[4];

	for (unsigned dim = 0; dim < 3; ++dim) {
		unsigned const d0((dim+1)%3), d1((dim+2)%3);

		for (unsigned dir = 0; dir < 2; ++dir) {
			if ((shadow_dir[dim] < 0.0) ^ dir) continue; // back facing

			for (unsigned i = 0; i < 4; ++i) {
				pts[i][dim] = d[dim][dir];
				pts[i][d0]  = d[d0][i==1||i==2];
				pts[i][d1]  = d[d1][i==2||i==3];
			}
			ushadow_polygon(pts, 4, tpos, cur_radius, spos, player, obj).draw_geom(tpos);
		}
	}
}


bool ship_sphere::line_intersect(point const &lp1, point const &lp2, float &t, bool calc_t) const {

	point ls_int;
	vector3d const delta(lp2 - lp1);
	if (!line_sphere_int(delta.get_norm(), lp1, pos, radius, ls_int, 1)) return 0;
		
	if (calc_t) { // reverse engineer tmin from ls_int
		float dmax(0.0);
		unsigned maxi(0);
		
		for (unsigned i = 0; i < 3; ++i) { // get dimension of max delta
			float const dv(fabs(delta[i]));
			if (dv > dmax) {dmax = dv; maxi = i;}
		}
		t = ((dmax < TOLERANCE) ? 0.0 : (ls_int[maxi] - lp1[maxi])/delta[maxi]);
	}
	return 1;
}

bool ship_sphere::sphere_intersect(point const &sc, float sr, point const &p_last, point &p_int, vector3d &norm, bool calc_int) const {

	if (!dist_less_than(sc, pos, (sr + radius))) return 0;
		
	if (calc_int) {
		norm  = (sc - pos).get_norm();
		p_int = pos + norm*(sr + radius);
	}
	return 1;
}

void ship_sphere::get_bounding_sphere(point &c, float &r) const {
	c = pos;
	r = radius;
}

void ship_sphere::draw_svol(point const &tpos, float cur_radius, point const &spos, int ndiv, bool player, free_obj const *const obj) const {
	ushadow_sphere(pos, radius, tpos, cur_radius, spos, ndiv, player, obj).draw_geom(tpos);
}


bool ship_torus::line_intersect(point const &lp1, point const &lp2, float &t, bool calc_t) const {
	return line_torus_intersect_rescale(lp1, lp2, center, plus_z, ri, ro, t);
}

bool ship_torus::sphere_intersect(point const &sc, float sr, point const &p_last, point &p_int, vector3d &norm, bool calc_int) const {
	return sphere_torus_intersect(sc, sr, center, ri, ro, p_int, norm, calc_int);
}

void ship_torus::get_bounding_sphere(point &c, float &r) const {
	c = center;
	r = ri + ro;
}

void ship_torus::draw_svol(point const &tpos, float cur_radius, point const &spos, int ndiv, bool player, free_obj const *const obj) const {

	assert(obj);
	assert(ro > 0.0 && ri > 0.0 && ndiv > 0);
	int npts;
	point pts_[4];
	point spos_xf(spos);
	obj->xform_point(spos_xf);
	double const ndiv_inv(1.0/double(ndiv)), step(PI*(double)ro*ndiv_inv + 2.0*ri*sin(PI*ndiv_inv));
	bool const self_shadow(dist_less_than(obj->get_pos(), tpos, 0.01*cur_radius));
	float const min_sdist(self_shadow ? 0.1*cur_radius : 0.0);
	float const ri_mod(0.97*ri); // slightly smaller than actual ri to avoid self-shadowing artifacts

	for (int i = 0; i < ndiv; ++i) {
		double const theta(TWO_PI*(double)i*ndiv_inv);
		point const pos(ro*cos(theta), ro*sin(theta), 0.0), p(center + pos);
		vector3d const delta(cross_product(pos, plus_z).get_norm() * step);
		cylinder_quad_projection(pts_, cylinder_3dw(p-delta, p+delta, ri_mod, ri_mod), (p - spos_xf), npts);
		upos_point_type pts[4]; // 3 or 4
		for (int n = 0; n < npts; ++n) pts[n] = pts_[n];
		ushadow_polygon(pts, npts, tpos, cur_radius, spos, player, obj, min_sdist).draw_geom(tpos);
	}

	// determine the tangent points from spos to center with radius ro
	// http://mathworld.wolfram.com/CircleTangentLine.html
	double const x0((double)center.x - (double)spos_xf.x), y0((double)center.y - (double)spos_xf.y), xy_sq(x0*x0 + y0*y0), r_sq((double)ro*ro);

	if (xy_sq > r_sq) {
		double const term1(-ro*x0/xy_sq), term2(y0*sqrt(xy_sq - r_sq)/xy_sq);
		double dmin(0.0), d[4] = {};
		unsigned pmin[2] = {0, 0};
		upos_point_type tpts[4];

		for (unsigned i = 0; i < 2; ++i) { // tangents
			double const val(term1 + (1.0 - 2.0*i)*term2), sin_t(sin(acos(max(-1.0, min(1.0, val)))));
			point pt((x0 + ro*val + spos_xf.x), (y0 + ro*sin_t + spos_xf.y), center.z);

			for (unsigned j = 0; j < 2; ++j) { // too bad we can't easily limit it to 2 out of 4
				tpts[(i<<1)+j] = pt;
				d[(i<<1)+j]    = p2p_dist_sq(pt, spos_xf);
				pt.y -= 2.0*ro*sin_t;
			}
		}
		for (unsigned i = 1; i < 4; ++i) {
			for (unsigned j = 0; j < i; ++j) {
				double const dist(fabs(d[i] - d[j])); // tangent lines have equal lengths
				if (i == 1 || dist < dmin) {dmin = dist; pmin[0] = i; pmin[1] = j;}
			}
		}
		for (unsigned i = 0; i < 2; ++i) {
			ushadow_sphere(tpts[pmin[i]], ri_mod, tpos, cur_radius, spos, ndiv, player, obj, min_sdist).draw_geom(tpos);
		}
	}
}


bool ship_bounded_cylinder::line_intersect(point const &lp1, point const &lp2, float &t, bool calc_t) const {

	float t0(1.0);

	if (bcube.line_intersect(lp1, lp2, t, calc_t) && ship_cylinder::line_intersect(lp1, lp2, t0, calc_t)) {
		t = max(t, t0);
		return 1;
	}
	return 0;
}

bool ship_bounded_cylinder::sphere_intersect(point const &sc, float sr, point const &p_last, point &p_int, vector3d &norm, bool calc_int) const {

	point pi_test(all_zeros);
	vector3d norm_test(zero_vector);

	if (bcube.sphere_intersect(sc, sr, p_last, p_int, norm, calc_int) &&
		ship_cylinder::sphere_intersect(sc, sr, p_last, pi_test, norm_test, calc_int))
	{
		if (calc_int && p2p_dist_sq(pi_test, p_last) > p2p_dist_sq(p_int, p_last)) {
			p_int = pi_test;
			norm  = norm_test;
		}
		return 1;
	}
	return 0;
}

void ship_bounded_cylinder::get_bounding_sphere(point &c, float &r) const {
	
	point center[2];
	float radius[2] = {};
	ship_cylinder::get_bounding_sphere(center[0], radius[0]);
	bcube.get_bounding_sphere(center[1], radius[1]);
	bool const rmin(radius[1] < radius[0]);
	c = center[rmin];
	r = radius[rmin];
}

void ship_bounded_cylinder::draw_svol(point const &tpos, float cur_radius, point const &spos, int ndiv, bool player, free_obj const *const obj) const {

	// should really be an AND
	ship_cylinder::draw_svol(tpos, cur_radius, spos, ndiv, player, obj);
	//bcube.draw_svol(tpos, cur_radius, spos, ndiv, player, obj);
}


bool ship_capsule::line_intersect(point const &lp1, point const &lp2, float &t, bool calc_t) const {

	t = 2.0; // start out of range
	float t0(1.0);
	bool intersected(0);
	if (line_int_cylinder(lp1, lp2, p1, p2, r1, r2, 0, t0)) {if (!calc_t) return 1; t = min(t, t0); intersected = 1;}
	ship_sphere const ss[2] = {ship_sphere(p1, r1), ship_sphere(p2, r2)};
	
	for (unsigned d = 0; d < 2; ++d) {
		if (ss[d].line_intersect(lp1, lp2, t0, calc_t)) {if (!calc_t) return 1; t = min(t, t0); intersected = 1;}
	}
	return intersected;
}

// Note: doesn't always calculate first/nearest intersection point
bool ship_capsule::sphere_intersect(point const &sc, float sr, point const &p_last, point &p_int, vector3d &norm, bool calc_int) const {

	if (sphere_intersect_cylinder_ipt(sc, sr, p1, p2, r1, r2, 0, p_int, norm, calc_int)) {return 1;}
	ship_sphere const ss[2] = {ship_sphere(p1, r1), ship_sphere(p2, r2)};

	for (unsigned d = 0; d < 2; ++d) {
		if (ss[d].sphere_intersect(sc, sr, p_last, p_int, norm, calc_int)) {return 1;}
	}
	return 0;
}

void ship_capsule::get_bounding_sphere(point &c, float &r) const {
	
	point const p[2] = {p1, p2};
	cylinder_bounding_sphere(p, r1, r2, c, r); // same as a cylinder?
}

void ship_capsule::draw_svol(point const &tpos, float cur_radius, point const &spos, int ndiv, bool player, free_obj const *const obj) const {

	ship_cylinder(*this, 0).draw_svol(tpos, cur_radius, spos, ndiv, player, obj);
	ship_sphere const ss[2] = {ship_sphere(p1, r1), ship_sphere(p2, r2)};
	for (unsigned d = 0; d < 2; ++d) {ss[d].draw_svol(tpos, cur_radius, spos, ndiv, player, obj);}
}


void ship_triangle_list::translate(point const &p) {

	ship_sphere::translate(p);
	for (vector<triangle>::iterator i = triangles.begin(); i != triangles.end(); ++i) {*i += p;}
}

bool ship_triangle_list::line_intersect(point const &lp1, point const &lp2, float &t, bool calc_t) const {

	float t_int(1.0);
	bool intersected(0);

	for (vector<triangle>::const_iterator i = triangles.begin(); i != triangles.end(); ++i) {
		if (line_poly_intersect(lp1, lp2, i->pts, 3, i->get_normal(), t_int)) {
			if (!calc_t) return 1;
			if (t_int < t) t = t_int; // closer intersection point
			intersected = 1;
		}
	}
	return intersected;
}

bool ship_triangle_list::sphere_intersect(point const &sc, float sr, point const &p_last, point &p_int, vector3d &norm, bool calc_int) const {
	return ship_sphere::sphere_intersect(sc, sr, p_last, p_int, norm, calc_int);
}

void ship_triangle_list::draw_svol(point const &tpos, float cur_radius, point const &spos, int ndiv, bool player, free_obj const *const obj) const {

	point spos_xf(spos);
	obj->xform_point(spos_xf);
	vector3d const shadow_dir(pos - spos_xf);

	for (vector<triangle>::const_iterator i = triangles.begin(); i != triangles.end(); ++i) {
		if (dot_product(i->get_normal(), shadow_dir) > 0.0) continue; // back facing
		upos_point_type pts[3];
		UNROLL_3X(pts[i_] = i->pts[i_];)
		ushadow_polygon(pts, 3, tpos, cur_radius, spos, player, obj).draw_geom(tpos);
	}
}


// Note: Similar to above code, except it calculates the intersection point as well.
bool u_ship::line_int_obj(point const &p1, point const &p2, point *p_int, float *dscale) const {

	assert(radius >= 0.0);
	cobj_vector_t const &cobjs(get_cobjs());
	if (cobjs.empty()) return 1;
	point p[2] = {p1, p2};
	xform_point_x2(p[0], p[1]);
	float tmin(1.0);
	bool intersects(0);
	bool const calc_t(p_int != NULL);

	for (unsigned i = 0; i < cobjs.size(); ++i) {
		assert(cobjs[i]);
		float tmin0(1.0);
		
		if (cobjs[i]->line_intersect(p[0], p[1], tmin0, calc_t)) {
			if (!calc_t && !dscale) return 1;
			intersects = 1;
			
			if (tmin0 < tmin) {
				tmin = tmin0;
				if (dscale) *dscale = cobjs[i]->get_dscale();
			}
		}
	}
	if (intersects && calc_t) {
		*p_int = p[0] + (p[1] - p[0])*tmin;
		xform_point_inv(*p_int); // else shouldn't get here?
	}
	return intersects;
}


// returns 1 only if can terminate the collision test
bool u_ship::do_sphere_int(point const &sc, float sr, intersect_params &ip, bool &intersects, point const &p_last, cobj_vector_t const &cobjs) const {

	for (unsigned i = 0; i < cobjs.size(); ++i) {
		assert(cobjs[i]);
		point pi_test(all_zeros);
		vector3d norm_test(zero_vector);

		if (cobjs[i]->sphere_intersect(sc, sr, p_last, pi_test, norm_test, ip.calc_int)) {
			if (!ip.calc_int && !ip.calc_dscale) return 1;
			
			if (!intersects || p2p_dist_sq(pi_test, p_last) < p2p_dist_sq(ip.p_int, p_last)) {
				ip.update_int_pn(pi_test, norm_test, cobjs[i]->get_dscale());
			}
			intersects = 1;
		}
	}
	return 0; // this should return 0
}


// Note: Similar to above code, except it calculates the intersection point and normal as well.
bool u_ship::sphere_int_obj(point const &c, float r, intersect_params &ip) const {

	assert(radius > 0.0);
	cobj_vector_t const &cobjs(get_cobjs());
	if (cobjs.empty()) return 1;
	point p(c), p_last(ip.p_last);
	if (ip.calc_int) {xform_point_x2(p, p_last);} else {xform_point(p);}
	r /= radius; // scale to 1.0
	bool intersects(0);
	if (do_sphere_int(p, r, ip, intersects, p_last, cobjs)) return 1;

	if (intersects && ip.calc_int) {
		xform_point_inv(ip.p_int);
		rotate_point_inv(ip.norm); // ip.norm will be normalized
	}
	return intersects;
}


bool free_obj::ship_int_obj(u_ship const *const ship, intersect_params &ip) const {
	assert(ship);
	return ship->sphere_int_obj(pos, c_radius, ip);
}

bool free_obj::obj_int_obj(free_obj const *const obj, intersect_params &ip) const {

	assert(obj);
	return obj->sphere_int_obj(pos, c_radius, ip);
}


bool u_ship::obj_int_obj(free_obj const *const obj, intersect_params &ip) const {
	assert(obj);
	return obj->ship_int_obj(this, ip);
}

bool u_ship::ship_int_obj(u_ship const *const ship, intersect_params &ip) const {

	assert(ship);
	cobj_vector_t const &cobjs1(get_cobjs());
	if (cobjs1.empty()) return ship->sphere_int_obj(pos, c_radius, ip);
	cobj_vector_t const &cobjs2(ship->get_cobjs());
	if (cobjs2.empty()) return sphere_int_obj(ship->get_pos(), ship->get_c_radius(), ip);
	if (!sphere_int_obj(ship->get_pos(), ship->get_c_radius())) return 0; // bounding sphere test, no intersect calc
	if (!ship->sphere_int_obj(pos, c_radius, (NO_OBJ_OBJ_INT ? ip : def_int_params))) return 0; // bounding sphere test
	if (NO_OBJ_OBJ_INT) return 1;
	return (cobjs_int_obj(cobjs2, ship) && ship->cobjs_int_obj(cobjs1, this)); // *** add ip to call? ***
}


bool u_ship::cobjs_int_obj(cobj_vector_t const &cobjs2, free_obj const *const obj, intersect_params &ip) const { // incomplete

	cobj_vector_t const &cobjs1(get_cobjs());
	assert(!cobjs1.empty() && !cobjs2.empty()); // shouldn't be called in this case
	point p_last(ip.p_last);
	if (ip.calc_int) xform_point(p_last);
	bool intersects(0);
	float const obj_radius((obj ? obj->get_radius() : 1.0)), sr_scale(obj_radius/get_radius());

	for (unsigned i = 0; i < cobjs2.size(); ++i) {
		assert(cobjs2[i]);
		float sr;
		point sc;
		cobjs2[i]->get_bounding_sphere(sc, sr);
		if (obj) obj->xform_point_inv(sc); // obj local to global
		if (!dist_less_than(sc, pos, (sr*obj_radius + get_c_radius()))) continue;
		xform_point(sc); // global to current local
		sr *= sr_scale;
		if (do_sphere_int(sc, sr, ip, intersects, p_last, cobjs1)) return 1;
		// *** more detailed code, use ip.calc_int ***
	}
	if (intersects && ip.calc_int) {
		xform_point_inv(ip.p_int);
		rotate_point_inv(ip.norm);
	}
	return intersects;
}


// ***** SHADOW CODE *****


ushadow_sphere::ushadow_sphere(upos_point_type const &sobj_pos, float sobj_r, upos_point_type const &cur_pos, float cur_radius,
							   point const &sun_pos, int ndiv, bool player, free_obj const *const obj, float rmin) : nsides(ndiv), pmap(NULL)
{
	rad[0] = rad[1] = 0.0;
	assert(sobj_r > TOLERANCE && cur_radius > TOLERANCE);
	float sphere_r(sobj_r);
	upos_point_type sphere_pos(sobj_pos);
	
	if (obj) {
		obj->xform_point_inv(sphere_pos);
		sphere_r *= obj->get_radius();
	}
	// reduce nsides if sphere_r is small?
	vector3d_d const shadow_dir((sphere_pos - sun_pos).get_norm());
	double const rsum((double)cur_radius + (double)sphere_r);

	if (!sphere_test_comp((upos_point_type)sun_pos, cur_pos, -shadow_dir, rsum*rsum)) { // self shadow test?
		invalid = 1;
		return;
	}
	double const dist_to_sun(max(TOLERANCE, p2p_dist(sphere_pos, upos_point_type(sun_pos))));
	double const dist(dot_product_ptv(shadow_dir, cur_pos, sphere_pos));
	double const min_dist(max(rmin, MIN_SHADOW_DIST*sobj_r));
	assert(dist_to_sun > TOLERANCE);

	for (unsigned i = 0; i < 2; ++i) {
		double delta((i ? -1 : 1)*max(min_dist, 1.25*cur_radius));
		if (!player || !i) delta += dist;
		delta = max(0.0, delta);
		rad[i]  = sphere_r*(dist_to_sun + delta)/dist_to_sun;
		spos[i] = sphere_pos + shadow_dir*delta;
	}
	if (dist_less_than(spos[0], spos[1], TOLERANCE)) {
		invalid = 1;
		return;
	}
	if (min(rad[0], rad[1])/max(rad[0], rad[1]) > 0.94) {rad[0] = rad[1] = 0.5*(rad[0] + rad[1]);}
}


void ushadow_sphere::draw(upos_point_type const &pos) const {

	assert(!invalid);
	draw_shadow_cylinder((spos[0] - pos), (spos[1] - pos), rad[0], rad[1], nsides, 1, pmap);
}


ushadow_polygon::ushadow_polygon(upos_point_type const *const pts, unsigned np, upos_point_type const &cur_pos,
	float cur_radius, point const &sun_pos, bool player, free_obj const *const obj, float rmin) : npts(np)
{
	assert(npts == 3 || npts == 4);
	upos_point_type pts_[4];
	for (unsigned i = 0; i < npts; ++i) pts_[i] = pts[i];
	if (obj) {obj->xform_point_inv_multi(pts_, npts);}
	upos_point_type norm;
	upos_point_type const center(get_center(pts_, npts));
	float radius(0.0);
		
	for (unsigned i = 0; i < npts; ++i) {
		radius = max(radius, p2p_dist_sq(center, pts_[i]));
	}
	radius = sqrt(radius);

	if (!dist_less_than(center, cur_pos, cur_radius)) { // not a self-shadow
		vector3d_d const shadow_dir((center - sun_pos).get_norm());
		double const rsum((double)cur_radius + (double)radius);
		
		if (!sphere_test_comp((upos_point_type)sun_pos, cur_pos, -shadow_dir, rsum*rsum)) {
			invalid = 1;
			return;
		}
	}
	get_normal(pts_[0], pts_[1], pts_[2], norm, 0);
	double const dp(dot_product(norm, (center - sun_pos)));
	double const min_dist(max(rmin, MIN_SHADOW_DIST*radius));

	for (unsigned i = 0; i < npts; ++i) {
		upos_point_type const &pos(pts_[(dp > 0.0) ? (npts - i - 1) : i]);
		vector3d_d const shadow_dir((pos - sun_pos).get_norm());
		double const dist(dot_product_ptv(shadow_dir, cur_pos, pos));

		for (unsigned j = 0; j < 2; ++j) {
			p[j][i] = pos + shadow_dir*max(min_dist, (dist + (1.2 - 2.4*j)*cur_radius));
		}
	}
}

// could use back face culling, but the driver call inside draw_verts() dominates so it wouldn't help much
void ushadow_polygon::draw(upos_point_type const &pos) const {

	assert(!invalid);
	assert(npts == 3 || npts == 4);
	vert_wrap_t verts[36];
	unsigned ix(0);

	for (unsigned i = 0; i < 2; ++i) { // ends (cull faces?)
		for (unsigned j = 0; j < npts; ++j) {
			verts[ix++].v = (p[i][i ? j : (npts-j-1)] - pos);
		}
		if (npts == 4) { // complete the triangles from the quads
			verts[ix++].v = (p[i][i ? 0 : 3] - pos);
			verts[ix++].v = (p[i][i ? 2 : 1] - pos);
		}
	}
	for (unsigned i = 0; i < npts; ++i) { // sides
		for (unsigned j = 0; j < 2; ++j) {
			for (unsigned k = 0; k < 2; ++k) {
				verts[ix++].v = (p[j][(i+(k^j))%npts] - pos);
			}
		}
		verts[ix++].v = (p[0][i] - pos); // complete the triangles from the quads
		verts[ix++].v = (p[1][(i+1)%npts] - pos);
	}
	draw_verts(verts, ix, GL_TRIANGLES);
}


ushadow_triangle_mesh::tri_vect_t ushadow_triangle_mesh::tris;


// voxel triangles constructor
ushadow_triangle_mesh::ushadow_triangle_mesh(vector<triangle> const &triangles, upos_point_type const &cur_pos,
	float cur_radius, point const &sun_pos, float obj_radius, point const &obj_center, free_obj const *const obj)
{
	tris.resize(triangles.size());
	point center(obj_center);
	if (obj) {obj->xform_point_inv(center);}
	upos_point_type const dir(center - sun_pos);
	double const min_dist(MIN_SHADOW_DIST*(double)obj_radius);

	for (unsigned t = 0; t < triangles.size(); ++t) {
		upos_point_type pts[3];
		UNROLL_3X(pts[i_] = triangles[t].pts[i_];)
		if (obj) obj->xform_point_inv_multi(pts, 3);
		upos_point_type norm;
		get_normal(pts[0], pts[1], pts[2], norm, 0);
		if (dot_product(norm, dir) > 0.0) {swap(pts[0], pts[1]);}
		set_triangle(t, pts, sun_pos, cur_pos, min_dist, cur_radius);
	}
}


// circle constructor
ushadow_triangle_mesh::ushadow_triangle_mesh(point const &circle_center, float circle_radius, vector3d const &circle_normal,
	unsigned ndiv, upos_point_type const &cur_pos, float cur_radius, point const &sun_pos, free_obj const *const obj)
{
	tris.resize(ndiv);
	point center(circle_center);
	vector3d normal(circle_normal);
	
	if (obj) {
		obj->xform_point_inv(center);
		obj->rotate_point_inv(normal);
		circle_radius *= obj->get_radius();
	}
	vector3d const dir(center - sun_pos);
	double const dp(dot_product(normal, dir)), min_dist(MIN_SHADOW_DIST*(double)circle_radius);
	vector3d vref(zero_vector);
	vref[fabs(normal.x) > fabs(normal.y)] = 1.0;
	point const cp1(cross_product(normal, vref).get_norm()), cp2(cross_product(normal, cp1).get_norm());
	upos_point_type last(center);
	float const css(TWO_PI/(float)ndiv), sin_ds(sin(css)), cos_ds(cos(css));
	float sin_s(0.0), cos_s(1.0);

	for (unsigned t = 0; t <= ndiv; ++t) {
		float const s(sin_s), c(cos_s);
		upos_point_type const pos(center + circle_radius*(c*cp1 + s*cp2));
		sin_s = s*cos_ds + c*sin_ds;
		cos_s = c*cos_ds - s*sin_ds;

		if (t > 0) { // last is not valid
			upos_point_type pts[3] = {pos, last, center};
			if (dp < 0.0) {swap(pts[0], pts[1]);}
			set_triangle(t-1, pts, sun_pos, cur_pos, min_dist, cur_radius);
		}
		last = pos;
	}
}


void ushadow_triangle_mesh::set_triangle(unsigned t, upos_point_type const pts[3], point const &sun_pos,
	upos_point_type const &cur_pos, double min_dist, double cur_radius)
{
	assert(t < tris.size());

	for (unsigned i = 0; i < 3; ++i) {
		upos_point_type const shadow_dir((pts[i] - sun_pos).get_norm());
		double const dist(dot_product_ptv(shadow_dir, cur_pos, pts[i]));

		for (unsigned j = 0; j < 2; ++j) {
			tris[t].p[j][i] = pts[i] + shadow_dir*max(min_dist, (dist + (1.2 - 2.4*j)*cur_radius));
		}
	}
}


void ushadow_triangle_mesh::draw(upos_point_type const &pos) const {

	assert(!invalid);
	if (tris.empty()) return;
	static vector<vert_wrap_t> verts;
	verts.resize(0);

	for (tri_vect_t::const_iterator t = tris.begin(); t != tris.end(); ++t) {
		for (unsigned i = 0; i < 2; ++i) { // ends (cull faces?)
			for (unsigned j = 0; j < 3; ++j) {verts.emplace_back(t->p[i][i ? j : (2-j)] - pos);}
		}
		for (unsigned j = 0; j < 2; ++j) {
			for (unsigned k = 0; k < 2; ++k) {verts.emplace_back(t->p[j][(k^j)%3] - pos);}
		}
		verts.emplace_back(t->p[0][0] - pos); // need to draw as triangles (0 and 2)
		verts.emplace_back(t->p[1][1] - pos);
	}
	draw_verts(verts, GL_TRIANGLES);
}


void free_obj::draw_shadow_volumes_from(uobject const *sobj, point const &sun_pos, float dscale, int ndiv) const {

	assert(sobj);
	assert(radius > TOLERANCE);
	
	if (COBJ_SHADOWS && sobj->casts_detailed_shadow()) {
		sobj->draw_shadow_volumes(pos, c_radius, sun_pos, ndiv);
		return;
	}
	// insert custom drawn-from-sun's-point-of-view code in here...
	float const sobj_radius(sobj->get_radius());
	bool const player(sobj == &player_ship());
	int const ndiv_raw(int(sqrt(16.0*dscale*sobj_radius/radius)));
	int nsides((player ? 2 : 1)*min(2*N_CYL_SIDES, min(max(N_CYL_SIDES, 4*ndiv), max(3, ndiv_raw))));
	if (nsides > 32) nsides &= ~7; // remove the last three bits
	ushadow_sphere uss(sobj->get_pos(), sobj_radius, pos, c_radius, sun_pos, nsides, player, NULL);
	float const *const pmap(sobj->get_sphere_shadow_pmap(sun_pos, pos, nsides));
	if (pmap) uss.set_pmap(pmap);
	uss.draw_geom(pos);
}


void u_ship::draw_shadow_volumes(point const &targ_pos, float cur_radius, point const &sun_pos, int ndiv) const {

	cobj_vector_t const &cobjs(get_cobjs());
	if (cobjs.empty()) return; // should never get here
	bool const player(is_player_ship());

	for (unsigned i = 0; i < cobjs.size(); ++i) {
		assert(cobjs[i]);
		cobjs[i]->draw_svol(targ_pos, cur_radius, sun_pos, ndiv, player, this);
	}
}

