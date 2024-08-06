// 3D World - OpenGL CS184 Computer Graphics Project - Mesh Intersection code
// by Frank Gennari
// 3/6/06

#include "3DWorld.h"
#include "mesh.h"
#include "mesh_intersect.h"


int const FAST_VIS_CALC = 0;


bool last_int(0);
std::unique_ptr<mesh_bsp_tree> bspt;


extern int display_mode;
extern float zmin, zmax, ztop, zbottom;


// ************ mesh_intersector ************


bool mesh_intersector::line_int_surface_cached() {

	static int x_last(-1), y_last(-1);
	if (fast + FAST_VISIBILITY_CALC >= 3) return line_intersect_surface_fast(); // not exactly correct for near-vertical lines
	
	if (last_int && !point_outside_mesh(x_last, y_last)) {
		if (fabs(v1.z - v2.z) < TOLERANCE) { // straight down, not sure about this
			if (intersect_mesh_quad(x_last, y_last)) return 1;
		}
		int const xv[13] = {0,  1, -1,  0,  0,  1, -1,  1, -1,  2, -2,  0,  0};
		int const yv[13] = {0,  0,  0, -1,  1,  1,  1, -1, -1,  0,  0, -2, -2};
		int const x1(get_xpos(v1.x)), x2(get_xpos(v2.x)), y1(get_ypos(v1.y)), y2(get_ypos(v2.y));
		int const xmin(min(x1, x2)),  xmax(max(x1, x2)),  ymin(min(y1, y2)),  ymax(max(y1, y2));

		for (int i = 0; i < 13; ++i) { // search pattern starting at x_last, y_last
			int const xval(x_last + xv[i]), yval(y_last + yv[i]);
			
			if (xval >= xmin && xval <= xmax && yval >= ymin && yval <= ymax) {
				if (point_outside_mesh(xval, yval)) continue;

				if (intersect_mesh_quad(xval, yval)) {
					x_last = ret.xpos = xval;
					y_last = ret.ypos = yval;
					return 1;
				}
			}
		}
	}
	if (line_intersect_surface()) {
		last_int = 1;
		x_last   = ret.xpos;
		y_last   = ret.ypos;
		return 1;
	}
	last_int = 0;
	return 0;
}


bool mesh_intersector::line_intersect_surface() {

	if (fast + FAST_VIS_CALC >= 3) {return line_intersect_surface_fast();}
	if (bspt) {return bspt->search(v1, v2, ret);}
	if (!check_iter_clip(fast + FAST_VIS_CALC >= 1)) return 0;
	int x1(get_xpos(v1.x)), y1(get_ypos(v1.y)), x2(get_xpos(v2.x)), y2(get_ypos(v2.y));
	int &xpos(ret.xpos), &ypos(ret.ypos);
	xpos  = x1;
	ypos  = y1;
	if (x1 == x2 && y1 == y2) {return intersect_mesh_quad(x1, y1);}
	int x_steps(x2-x1), y_steps(y2-y1), xs1, xs2, ys1, ys2, xval, yval;
	double const slope((x_steps == 0) ? 1.0e6 : ((double)y_steps)/((double)x_steps));
	double const s_inv((y_steps == 0) ? 1.0e6 : ((double)x_steps)/((double)y_steps));
	int const sval(abs(x_steps) > abs(y_steps));

	if (sval) { // |slope| < 1
		double const x_stride(((double)x_steps)/(double)max(1, abs(y_steps)));
		xpos += (int)floor(0.5*x_stride + 0.5);
		ys1   = ys2 = ((y_steps > 0.0) ? 1 : -1);
		xs1   = (int)floor(x_stride);
		xs2   = (int)ceil(x_stride);
	}
	else { // |slope| >= 1
		double const y_stride(((double)y_steps)/(double)max(1, abs(x_steps)));
		ypos += (int)floor(0.5*y_stride + 0.5);
		xs1   = xs2 = ((x_steps > 0.0) ? 1 : -1);
		ys1   = (int)floor(y_stride);
		ys2   = (int)ceil(y_stride);
	}
	if (line_intersect_plane(x1-1, xpos, y1-1, ypos)) return 1; // first segment
	int line_mode(sval);
	int const x_comp(abs(x_steps)), y_comp(abs(y_steps));

	while ((abs(xpos - x1) < x_comp) && (abs(ypos - y1) < y_comp)) {
		if (line_mode == 0) { // horizontal segment
			xval = ((sval == 0 || (xpos > (x1 + ((double)ypos - y1)*s_inv))) ? xs1 : xs2);
			if (line_intersect_plane(xpos, xpos+xval, ypos-1, ypos)) return 1;
			xpos += xval;
		}
		else { // vertical segment
			yval = ((sval == 1 || (ypos > (y1 + ((double)xpos - x1)*slope))) ? ys1 : ys2);
			if (line_intersect_plane(xpos-1, xpos, ypos, ypos+yval)) return 1;
			ypos += yval;
		}
		line_mode = !line_mode;
	}
	return line_intersect_plane(xpos-1, x2, ypos-1, y2); // last segment
}


bool mesh_intersector::line_intersect_surface_fast() { // DDA

	if (!check_iter_clip(0)) return 0;
	int const x1(get_xpos(v1.x)), y1(get_ypos(v1.y)), x2(get_xpos(v2.x)), y2(get_ypos(v2.y));
	int const dx(x2 - x1), dy(y2 - y1), steps(max(1, max(abs(dx), abs(dy))));
	double const dz((double)v2.z - (double)v1.z), xinc(dx/(double)steps), yinc(dy/(double)steps), zinc(dz/(double)steps);
	double x(x1), y(y1), z(v1.z - zinc); // -zinc is kind of strange but necessary for proper functioning
	double const z_min(min(v1.z, v2.z)), z_max(max(v1.z, v2.z));

	for (int k = 0; k <= steps; ++k) {
		int const ix((int)x), iy((int)y);

		// check x+0.5, y+0.5 (could use mesh_height)
		if (!point_outside_mesh(ix, iy) && z_min_matrix[iy][ix] > z && z >= z_min && z <= z_max) {
			ret.xpos = ix; ret.ypos = iy; ret.zval = z;
			return 1;
		}
		x += xinc; y += yinc; z += zinc;
	}
	return 0;
}


bool mesh_intersector::check_iter_clip(int fast2) {

	float minz(max(zmin, zbottom)), maxz(min(zmax, ztop));
	if (!do_line_clip_scene(v1, v2, minz, maxz)) return 0;
	if (!fast2) return 1;
	float zmax0(minz);
	int const xa(get_xpos(v1.x)), ya(get_ypos(v1.y)), xb(get_xpos(v2.x)), yb(get_ypos(v2.y));
	int const dx(xb - xa), dy(yb - ya), steps(max(abs(dx), abs(dy)));
	double const xinc(dx/(double)steps), yinc(dy/(double)steps);
	double x(xa), y(ya);

	for (int k = 0; k <= steps; ++k) { // DDA algorithm
		int const xpos(int(x + 0.5)), ypos(int(y + 0.5));
		if (!point_outside_mesh(xpos, ypos)) zmax0 = max(mesh_height[ypos][xpos], zmax0);
		x += xinc;
		y += yinc;
	}
	assert(zmax0 <= maxz && zmax0 >= minz); // too strict?
	maxz = min(maxz, zmax0);
	return do_line_clip_scene(v1, v2, minz, maxz);
}


bool mesh_intersector::line_intersect_plane(int x1, int x2, int y1, int y2) {

	int const dj((x1 > x2) ? -1 : 1), di((y1 > y2) ? -1 : 1);
	
	for (int i = y1; i != y2+di; i += di) {
		for (int j = x1; j != x2+dj; j += dj) {
			if (intersect_mesh_quad(j, i)) { // vertex_normals[i][j]?
				ret.xpos = j;
				ret.ypos = i;
				assert(!point_outside_mesh(ret.xpos, ret.ypos));
				return 1;
			}
		}
	}
	return 0;
}

bool line_poly_intersect(vector3d const &p1, point const &p2, point const *points, unsigned npts, float &t) {
	vector3d norm;
	get_normal(points[0], points[1], points[2], norm, 0); // doesn't require norm to be normalized
	return line_poly_intersect(p1, p2, points, npts, norm, t);
}
bool mesh_intersector::intersect_mesh_quad(int x, int y) {

	if (x < 0 || y < 0 || x >= MESH_X_SIZE-1 || y >= MESH_Y_SIZE-1) return 0;
	double const xv(get_xval(x)), yv(get_yval(y));
	point const pts[4] = {
		point(xv,        yv,        mesh_height[y  ][x  ]),
		point(xv,        yv+DY_VAL, mesh_height[y+1][x  ]),
		point(xv+DX_VAL, yv,        mesh_height[y  ][x+1]),
		point(xv+DX_VAL, yv+DY_VAL, mesh_height[y+1][x+1])};
	float t(0.0);
	
	if (line_poly_intersect(v1, v2, pts, 3, t) || line_poly_intersect(v1, v2, pts+1, 3, t)) {
		ret.zval = float(v1.z + (v2.z - v1.z)*t);
		return 1;
	}
	return 0;
}


bool mesh_intersector::get_intersection(int &xpos_, int &ypos_, float &zval_, bool cached) {
	
	bool const retval(cached ? line_int_surface_cached() : line_intersect_surface());
	xpos_ = ret.xpos;
	ypos_ = ret.ypos;
	zval_ = ret.zval;
	return retval;
}


bool mesh_intersector::get_intersection() {

	float const v1z(v1.z), v2z(v2.z); // cache the starting z values (before clipping)
	ret.zval = zmin;

	if (line_int_surface_cached()) {
		if (ret.zval > min(v1z, v2z) && ret.zval < max(v1z, v2z)) return 1;
		last_int = 0; // not a true intersection
	}
	return 0;
}


bool mesh_intersector::get_any_non_intersection(point const *const pts, unsigned npts) {

	assert(pts);

	for (unsigned i = 0; i < npts; ++i) {
		v1 = pts[i];
		if (!get_intersection()) return 1;
	}
	return 0;
}


// ************ mesh intersection drivers ************


bool sphere_visible_to_pt(point const &pt, point const &center, float radius) {
	if (radius == 0.0) return line_intersect_mesh(center, pt, 1);
	point qp[4];
	unsigned const num_pts((radius < 0.5*HALF_DXY) ? 2 : 4);
	get_sphere_border_pts(qp, center, pt, radius, num_pts);
	mesh_intersector mint(pt, pt, 1);
	return mint.get_any_non_intersection(qp, num_pts); 
}


bool line_intersect_mesh(point const &v1, point const &v2, int fast) {
	mesh_intersector mint(v1, v2, fast);
	return mint.get_intersection();
}
bool line_intersect_mesh(point const &v1, point const &v2, int &xpos, int &ypos, float &zval, int fast, bool cached) {
	mesh_intersector mint(v1, v2, fast);
	return mint.get_intersection(xpos, ypos, zval, cached);
}
bool line_intersect_mesh(point const &v1, point const &v2, point &cpos, int fast, bool cached) {
	int xpos, ypos; // unused
	float zval;
	if (!line_intersect_mesh(v1, v2, xpos, ypos, zval, fast, cached)) return 0;
	float const t((zval - v1.z)/(v2.z - v1.z));
	cpos = v1 + (v2 - v1)*t;
	return 1;
}


bool is_visible_from_light(point const &pos, point const &lpos, int fast) {
	if (lpos.x == 0.0 && lpos.y == 0.0 && lpos.z == 0.0) return 0;
	if (lpos.z > ztop && pos.z > ztop) return 1; // above the mesh
	return !line_intersect_mesh(lpos, pos, fast);
}


// ************ mesh_bsp_search ************


bool is_pow_2(int val) { // slow
	unsigned num_ones(0);
	for (; val != 0; val >>= 1) {if (val & 1) ++num_ones;}
	return (num_ones == 1);
}


bool mesh_size_ok_for_bsp_tree() {

	if (!is_pow_2(MESH_X_SIZE) || !is_pow_2(MESH_Y_SIZE))           return 0; // relax this later?
	if (MESH_X_SIZE > 2*MESH_Y_SIZE || MESH_Y_SIZE > 2*MESH_X_SIZE) return 0; // aspect ratio must be in the range [0.5,2.0]
	return 1;
}


mesh_bsp_tree::mesh_bsp_tree() {

	assert(mesh_size_ok_for_bsp_tree());
	dir0    = (MESH_X_SIZE < MESH_Y_SIZE);
	nlevels = unsigned(floor(log2(double(XY_MULT_SIZE))));
	unsigned const tot_alloc(XY_MULT_SIZE << 1);
	bsp_data.resize(tot_alloc);
	tree.resize(nlevels+1);
	unsigned cur(0);

	for (unsigned i = 0; i <= nlevels; ++i) {
		tree[i] = &bsp_data[cur];
		cur    += (XY_MULT_SIZE >> (nlevels - i));
	}
	assert(cur < tot_alloc);
	if (is_pow_2(MESH_X_SIZE) && is_pow_2(MESH_Y_SIZE)) {assert(cur == (tot_alloc-1));}

	// fill in bottom level (individual mesh quads)
	bsp_tree_node *leaves(tree[nlevels]);
	float const tolerance(0.01*DZ_VAL); // adjustment to allow for intersections with a flat mesh where mzmin == mzmax

	for (int y = 0; y < MESH_Y_SIZE; ++y) {
		for (int x = 0; x < MESH_X_SIZE; ++x) {
			float mzmin(zmax), mzmax(zmin);
			
			for (int yy = y; yy < min(y+2, MESH_Y_SIZE); ++yy) {
				for (int xx = x; xx < min(x+2, MESH_X_SIZE); ++xx) {
					mzmin = min(mzmin, mesh_height[yy][xx]);
					mzmax = max(mzmax, mesh_height[yy][xx]);
				}
			}
			assert(mzmin <= mzmax);
			leaves[y*MESH_X_SIZE + x].c = cube_t(get_xval(x), get_xval(x+1), get_yval(y), get_yval(y+1), mzmin-tolerance, mzmax+tolerance);
		}
	}
	bool const inv(!bool(nlevels&1));

	// fill in higher levels bottom up
	for (int level = nlevels-1; level >= 0; --level) {
		unsigned const bsx((nlevels-level+!(dir0^inv)) >> 1), bsy((nlevels-level+(dir0^inv)) >> 1);
		unsigned const xsize(MESH_X_SIZE >> bsx), ysize(MESH_Y_SIZE >> bsy);
		unsigned const last_size((xsize*ysize) << 1);
		unsigned const dim(bool(level&1) ^ dir0 ^ inv), delta(dim ? xsize : 1); // either one scanline from the previous level or 1
		assert(xsize > 0 && ysize > 0);
		if (level == 0) {assert(xsize == 1 && ysize == 1);}
		bsp_tree_node const *last_level(tree[level+1]);
		bsp_tree_node *cur_level(tree[level]);

		for (unsigned y = 0; y < ysize; ++y) {
			unsigned const yoff(y*xsize);

			for (unsigned x = 0; x < xsize; ++x) {
				unsigned const src_ix((yoff<<1) + (x<<(dim^1)));
				assert((src_ix + delta) < last_size);
				bsp_tree_node &cur(cur_level[yoff + x]);
				cur.c = last_level[src_ix].c;
				cur.c.union_with_cube(last_level[src_ix + delta].c);
			}
		}
	}
}


bool mesh_bsp_tree::search_recur(point v1, point v2, unsigned x, unsigned y, unsigned level, mesh_query_ret &ret) const { // recursive

	assert(level <= nlevels);
	unsigned const xsize(MESH_X_SIZE >> ((nlevels-level+unsigned(!dir0)) >> 1)), ix(y*xsize + x);
	assert(unsigned((tree[level] - tree[0]) + ix) < bsp_data.size());
	if (!do_line_clip(v1, v2, tree[level][ix].c.d)) return 0;

	if (level == nlevels) { // base case - at a leaf, now verify plane intersection
		assert(x < unsigned(MESH_X_SIZE) && y < unsigned(MESH_Y_SIZE));
		if (mesh_draw != nullptr && !mesh_draw[y][x]) return 0;
		mesh_intersector intersector(v1, v2, 0);
		if (!intersector.intersect_mesh_quad(x, y)) return 0; // use the intersector's line-plane intersection function
		ret.zval = intersector.get_ret().zval;
		ret.xpos = x;
		ret.ypos = y;
		return 1;
	}
	unsigned const dim((level&1) ^ dir0 ^ (!bool(nlevels&1))), xv(x << (dim^1)), yv(y << dim);
	unsigned const i0(dim ? (v1.y > v2.y) : (v1.x > v2.x)); // determine which bin to examine first

	for (unsigned i = 0; i < 2; ++i) {
		unsigned const x2(xv + ((i^i0)&(dim^1))), y2(yv + ((i^i0)&dim));
		if (search_recur(v1, v2, x2, y2, level+1, ret)) return 1;
	}
	return 0;
}


// ************ BSP tree drivers ************


void gen_mesh_bsp_tree() {

	if (!mesh_size_ok_for_bsp_tree()) return;
	//RESET_TIME;
	bspt.reset(new mesh_bsp_tree()); // must be created after mesh size is read from config file
	//PRINT_TIME("BSP Tree");
}



