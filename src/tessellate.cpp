// 3D World - GLU Polygon Tessellation code
// by Frank Gennari
// 5/29/06

#include "3DWorld.h"
#include "collision_detect.h"
#include "subdiv.h"


#ifdef _WIN32
#define fgCALLBACK CALLBACK
#else
#define fgCALLBACK GLAPIENTRY
#endif


triangle_vntc cur_triangle;
deque<triangle_vntc> triangles;
deque<vert_norm_tc> added_pts;
bool mode_valid(0), self_int(0), has_tess_error(0);
int mode(0);
unsigned vertex(0);


void fgCALLBACK tess_error(GLenum errno) {
	cout << "Polygon tesselation error " << errno << " has occurred: " << gluErrorString(errno) << endl;
	has_tess_error = 1;
}


void fgCALLBACK do_combine(GLdouble c[3], vert_norm_tc *vertex_data[4], GLfloat weight[4], vert_norm_tc **out) { // self-intersecting polygon

	self_int = 1;
	vector3d norm(zero_vector);
	float tc[2] = {0.0, 0.0};

	for (unsigned i = 0; i < 4; ++i) {
		if (!vertex_data[i]) { // the spec says this can't happen, but it does seem to occur
			assert(weight[i] == 0.0);
			continue;
		}
		norm  += vertex_data[i]->n    * weight[i];
		tc[0] += vertex_data[i]->t[0] * weight[i];
		tc[1] += vertex_data[i]->t[1] * weight[i];
	}
	added_pts.emplace_back(point(c[0], c[1], c[2]), norm, tc);
	*out = &added_pts.back();
}


void fgCALLBACK do_coord(vert_norm_tc *coord) {

	assert(coord);
	assert(mode_valid);
	assert(vertex < 3);
	cur_triangle.pts[vertex] = *coord;
	bool const emit_tri(vertex == 2);
	if (emit_tri) {triangles.push_back(cur_triangle);}

	switch (mode) {
	case GL_TRIANGLES:
		if (emit_tri) {vertex = 0;}
		break;
	case GL_TRIANGLE_STRIP:
		if (emit_tri) {for (unsigned i = 0; i < 2; ++i) {cur_triangle.pts[i] = cur_triangle.pts[i+1];}}
		break;
	case GL_TRIANGLE_FAN:
		if (emit_tri) {cur_triangle.pts[1] = cur_triangle.pts[2];}
		break;
	default:
		assert(0);
	}
	if (!emit_tri) {++vertex;}
}


void fgCALLBACK do_begin(int type) {

	assert(!mode_valid);
	mode       = type;
	mode_valid = 1;
	vertex     = 0;
}


void fgCALLBACK do_end() {

	assert(mode_valid);
	assert(!triangles.empty());
	mode_valid = 0;

	switch (mode) {
	case GL_TRIANGLES:
		assert(vertex == 0);
		break;
	case GL_TRIANGLE_STRIP:
	case GL_TRIANGLE_FAN:
		assert(vertex == 2);
		break;
	default:
		assert(0);
	}
}


GLUtesselator *init_tess() {

	GLUtesselator *tobj(gluNewTess());
	assert(tobj != NULL);
	gluTessCallback(tobj, GLU_TESS_VERTEX,  (void (fgCALLBACK *)(void))do_coord);
	gluTessCallback(tobj, GLU_TESS_BEGIN,   (void (fgCALLBACK *)(void))do_begin);
	gluTessCallback(tobj, GLU_TESS_END,     (void (fgCALLBACK *)(void))do_end);
	gluTessCallback(tobj, GLU_TESS_ERROR,   (void (fgCALLBACK *)(void))tess_error);
	gluTessCallback(tobj, GLU_TESS_COMBINE, (void (fgCALLBACK *)(void))do_combine);
	return tobj;
}


void tessellate_polygon(polygon_t const &poly) {

	assert(!has_tess_error);
	unsigned const size((unsigned)poly.size());
	assert(size >= 3);
	static GLUtesselator *tobj = NULL;
	if (tobj == NULL) tobj = init_tess();
	mode_valid = 0;
	self_int   = 0;
	vertex     = 0;
	added_pts.clear();
	vector3d const norm(poly.get_planar_normal());
	gluTessNormal(tobj, norm.x, norm.y, norm.z);
	gluTessBeginPolygon(tobj, (void *)(&poly.front()));
	gluTessBeginContour(tobj);
	double coord[3];

	for (unsigned i = 0; i < size; ++i) {
		UNROLL_3X(coord[i_] = poly[i].v[i_];)
		gluTessVertex(tobj, coord, (void *)&poly[i]);
	}
	gluTessEndContour(tobj);
	gluTessEndPolygon(tobj);
	assert(self_int == !added_pts.empty());

	if (has_tess_error) { // must delete and recreate tess object
		has_tess_error = 0;
		gluDeleteTess(tobj);
		tobj = NULL;
	}
	else {
		assert(!mode_valid);
	}
	if (self_int) {
		static bool had_self_int_warning(0);

		if (!had_self_int_warning) {
			cout << "* Warning: Self-intersecting polygon." << endl;
			had_self_int_warning = 1;
		}
	}
}


template<typename T> bool split_polygon(polygon_t const &poly, vector<T> &ppts, float coplanar_thresh, bool allow_quads) {

	unsigned const npts((unsigned)poly.size());
	assert(npts >= 3);
	
	if (npts <= 4 && (npts == 3 || (allow_quads && poly.is_coplanar(coplanar_thresh) && poly.is_convex()))) { // triangle or convex/coplanar quad
		if (!poly.is_valid()) return 0; // invalid zero area polygon - skip
		ppts.push_back(poly);
		return 1;
	}
	tessellate_polygon(poly); // could special case convex quads, but that might not help much

	// calculate polygon normal (assuming planar polygon)
	vector3d n(poly.get_planar_normal()), cp_sum(zero_vector);
	for (unsigned i = 0; i < npts; ++i) {cp_sum += cross_product(poly[i].v, poly[(i+1)%npts].v);}
	if (dot_product(n, cp_sum) < 0.0) {n *= -1.0;}
	static polygon_t new_poly;
	new_poly.resize(3);

	// triangles can be empty if they're all small fragments that get dropped
	for (unsigned i = 0; i < triangles.size(); ++i) {
		UNROLL_3X(new_poly[i_] = triangles[i].pts[i_];)
		if (!new_poly.is_valid()) continue; // invalid zero area triangle - skip
		if (dot_product(new_poly.get_planar_normal(), n) < 0.0) {swap(new_poly[0], new_poly[2]);} // invert draw order
		ppts.push_back(new_poly);
	}
	// triangles and split_polygons can be empty here if they're all small fragments that get dropped
	triangles.clear();
	return 1;
}

template bool split_polygon<polygon_t >(polygon_t const &poly, vector<polygon_t > &ppts, float coplanar_thresh, bool allow_quads);
template bool split_polygon<coll_tquad>(polygon_t const &poly, vector<coll_tquad> &ppts, float coplanar_thresh, bool allow_quads);


void split_polygon_to_cobjs(coll_obj const &cobj, coll_obj_group &split_polygons, vector<point> const &poly_pts) {

	if (poly_pts.size() == 3 && is_poly_valid(&poly_pts.front())) { // optimization
		split_polygons.push_back(cobj);
		split_polygons.back().set_from_pts(&poly_pts.front(), 3);
		return;
	}
	static polygon_t poly;
	static vector<coll_tquad> ppts;
	ppts.resize(0);
	poly.from_points(poly_pts);
	split_polygon(poly, ppts, 0.0, 1); // allow_quads=1

	for (vector<coll_tquad>::const_iterator i = ppts.begin(); i != ppts.end(); ++i) {
		split_polygons.push_back(cobj);
		copy_tquad_to_cobj(*i, split_polygons.back()); // Note: poly color is ignored
	}
}

