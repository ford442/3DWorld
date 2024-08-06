// 3D World - 3D Studio Max File Reader
// by Frank Gennari
// 11/1/2014

#include "3DWorld.h"
#include "model3d.h"
#include "file_reader.h"

bool const EXTRA_VERBOSE = 0;
bool const ALLOW_VERBOSE = 0;

extern bool reverse_3ds_vert_winding_order;


class file_reader_3ds : public base_file_reader {

protected:
	geom_xform_t cur_xf;
	float master_scale;
	string name; // unused?

	struct face_t {
		unsigned short ix[3] = {0}, flags;
		int mat;
		face_t() : flags(0), mat(-1) {}
	};

	long get_end_pos(unsigned read_len) {return (ftell(fp) + read_len - 6);}

	bool read_data(void *dest, size_t sz, size_t count, char const *const str) {
		size_t const nread(fread(dest, sz, count, fp));
		if (nread == count) return 1;
		if (str != nullptr) {cerr << "Error reading 3DS file " << str << " data: expected " << count << " elements of size " << sz << " but got " << nread << endl;}
		return 0;
	}

	bool read_chunk_header(unsigned short &chunk_id, unsigned &chunk_len, bool top_level=0) {
		// Note: we pass null at the top level to suppress the error message during EOF checking
		if (!read_data(&chunk_id, 2, 1, (top_level ? nullptr : "chunk id"))) return 0;
		if (EXTRA_VERBOSE && verbose) {printf("ChunkID: %x\n", chunk_id);}
		if (!read_data(&chunk_len, 4, 1, "chunk length")) return 0;
		assert(chunk_len < (1U<<31)); // sanity check
		if (EXTRA_VERBOSE && verbose) {printf("ChunkLength: %u\n", chunk_len);}
		return 1;
	}

	template<typename T> void ensure_size(vector<T> &v, unsigned num) {
		if (v.empty()) {v.resize(num);} else {assert(v.size() == num);}
	}

	bool read_vertex_block(vector<vert_tc_t> &verts) {
		unsigned short num;
		if (!read_data(&num, sizeof(unsigned short), 1, "vertex size")) return 0;
		ensure_size(verts, num);
		if (verbose) {printf("Number of vertices: %u\n", num);}

		for (int i = 0; i < num; i++) {
			if (!read_data(&verts[i].v.x, sizeof(float), 3, "vertex xyz")) return 0;
		}
		return 1;
	}

	void transform_vertices(vector<vert_tc_t> &verts, xform_matrix &matrix) {
		for (vector<vert_tc_t>::iterator i = verts.begin(); i != verts.end(); ++i) {
			matrix.apply_to_vector3d(i->v);
			i->v *= master_scale;
			cur_xf.xform_pos(i->v);
		}
	}

	bool read_mapping_block(vector<vert_tc_t> &verts) {
		unsigned short num;
		if (!read_data(&num, sizeof(unsigned short), 1, "mapping size")) return 0;
		ensure_size(verts, num);
		if (verbose) {printf("Number of tex coords: %u\n", num);}

		for (int i = 0; i < num; i++) {
			if (!read_data(verts[i].t, sizeof(float), 2, "mapping uv")) return 0;
		}
		return 1;
	}

	bool read_faces(vector<face_t> &faces) {
		unsigned short num;
		if (!read_data(&num, sizeof(unsigned short), 1, "number of faces")) return 0;
		ensure_size(faces, num);
		if (verbose) {printf("Number of polygons: %u\n", num);}

		for (unsigned i = 0; i < num; i++) {
			// flags: 3 LSB are edge orderings (AB BC CA): ignored
			if (!read_data(faces[i].ix, sizeof(unsigned short), 4, "face data")) return 0;
			if (reverse_3ds_vert_winding_order) {std::swap(faces[i].ix[0], faces[i].ix[2]);} // reverse triangle vertex ordering to agree with 3DWorld coordinate system
			//cout << "flags: " << faces[i].flags << endl;
		}
		return 1;
	}

	bool read_null_term_string(string &str) {
		while (1) {
			char l_char;
			if (!read_data(&l_char, 1, 1, "string char")) return 0;
			if (l_char == '\0') return 1;
			str.push_back(l_char); // add the null terminator or not?
		}
		return 0; // never gets here
	}

	bool read_color(colorRGB &color) {
		unsigned short chunk_id;
		unsigned chunk_len;
		if (!read_chunk_header(chunk_id, chunk_len)) return 0;

		if (chunk_id == 0x0010) {
			assert(chunk_len == 18);
			return read_data(&color.R, sizeof(float), 3, "RGB float color");
		}
		else if (chunk_id == 0x0011) {
			assert(chunk_len == 9);
			color_wrapper cw;
			if (!read_data(cw.c, sizeof(unsigned char), 3, "RGB 24-bit color")) return 0;
			color = cw.get_c3();
			return 1;
		}
		return 0; // invalid chunk_id
	}

	bool read_percentage(unsigned read_len, float &val) {
		unsigned short chunk_id, ival;
		unsigned chunk_len;
		long const end_pos(get_end_pos(read_len));
		if (!read_chunk_header(chunk_id, chunk_len)) return 0;
		
		switch (chunk_id) {
		case 0x0030: // short percentage
			if (!read_data(&ival, sizeof(unsigned short), 1, "short percentage")) return 0;
			val = ival/100.0;
			break;
		case 0x0031: // float percentage
			if (!read_data(&val, sizeof(float), 1, "float percentage")) return 0;
			val /= 100.0;
			break;
		default:
			assert(0);
		} // end switch
		assert(ftell(fp) == end_pos);
		return 1;
	}

	bool read_matrix(xform_matrix &matrix, unsigned short chunk_len) {
		assert(chunk_len == 54); // header + 4*3 floats
		float m[12];
		if (!read_data(m, sizeof(float), 12, "transform matrix")) return 0;
#if 0
		float *mp(matrix.get_ptr());

		for (unsigned i = 0; i < 4; ++i) {
			for (unsigned j = 0; j < 3; ++j) {mp[4*i+j] = m[3*i+j];}
		}
		//UNROLL_3X(mp[12+i_] = m[9+i_];) // copy translate only
#endif
		return 1;
	}

	void skip_chunk(unsigned chunk_len) {
		if (fseek(fp, chunk_len-6, SEEK_CUR) != 0) {
			cerr << "Error: fseek() call failed" << endl;
			exit(1); // not sure if/when this can fail; if it does, it's likely an internal error
		}
	}

	// return value: 0 = error, 1 = processed, 2 = can't handle/skip
	virtual int proc_other_chunks(unsigned short chunk_id, unsigned chunk_len) {return 2;}

public:
	file_reader_3ds(string const &fn) : base_file_reader(fn), master_scale(1.0) {}
	virtual ~file_reader_3ds() {}

	bool read(geom_xform_t const &xf, bool verbose_) {
		verbose = verbose_;
		cur_xf = xf;
		if (!open_file(1)) return 0; // binary file
		cout << "Reading 3DS file " << filename << endl;
		unsigned short chunk_id;
		unsigned chunk_len;

		while (read_chunk_header(chunk_id, chunk_len, 1)) { // read each chunk from the file 
			switch (chunk_id) {
				// MAIN3DS: Main chunk, contains all the other chunks; length: 0 + sub chunks
			case 0x4d4d:
				break;
				// EDIT3DS: 3D Editor chunk, objects layout info; length: 0 + sub chunks
			case 0x3d3d:
				break;
				// EDIT_OBJECT: Object block, info for each object; length: len(object name) + sub chunks
			case 0x4000:
				if (!read_null_term_string(name)) return 0;
				break;
				// master scale factor
			case 0x0100:
				if (!read_data(&master_scale, sizeof(float), 1, "master scale")) return 0;
				break;
			default: // send to derived class reader, and skip chunk if it isn't handled
				{
					int const ret(proc_other_chunks(chunk_id, chunk_len));
					if (ret == 0) return 0; // error
					else if (ret == 2) {skip_chunk(chunk_len);} // skip
				}
			} // end switch
		}
		return 1;
	}
};


// ************************************************


class file_reader_3ds_triangles : public file_reader_3ds {

	colorRGBA def_color;
	vector<coll_tquad> *ppts;

	virtual int proc_other_chunks(unsigned short chunk_id, unsigned chunk_len) {
		assert(ppts != nullptr);

		switch (chunk_id) {
			// OBJ_TRIMESH: Triangular mesh, contains chunks for 3d mesh info; length: 0 + sub chunks
		case 0x4100:
			return read_mesh(chunk_len); // handled
		} // end switch
		return 2; // skip
	}

	bool read_mesh(unsigned read_len) {
		unsigned short chunk_id;
		unsigned chunk_len;
		long const end_pos(get_end_pos(read_len));
		vector<vert_tc_t> verts;
		vector<face_t> faces;
		xform_matrix matrix;

		while (ftell(fp) < end_pos) { // read each chunk from the file 
			if (!read_chunk_header(chunk_id, chunk_len)) return 0;

			switch (chunk_id) {
				// TRI_FACEL1: Polygons (faces) list
				// Chunk Length: 1 x unsigned short (# polygons) + 3 x unsigned short (polygon points) x (# polygons) + sub chunks
			case 0x4120:
				if (!read_faces(faces)) return 0;
				break;
				// TRI_VERTEXL: Vertices list
				// Chunk Length: 1 x unsigned short (# vertices) + 3 x float (vertex coordinates) x (# vertices) + sub chunks
			case 0x4110:
				if (!read_vertex_block(verts)) return 0;
				break;
				// TRI_MAPPINGCOORS: Vertices list
				// Chunk Length: 1 x unsigned short (# mapping points) + 2 x float (mapping coordinates) x (# mapping points) + sub chunks
			case 0x4140:
				if (!read_mapping_block(verts)) return 0;
				break;
				// mesh matrix
			case 0x4160:
				read_matrix(matrix, chunk_len);
				break;
			default:
				skip_chunk(chunk_len);
			} // end switch
		} // end while
		assert(ftell(fp) == end_pos);
		transform_vertices(verts, matrix);
		triangle tri;

		for (vector<face_t>::const_iterator i = faces.begin(); i != faces.end(); ++i) {
			for (unsigned n = 0; n < 3; ++n) {
				unsigned const ix(i->ix[n]);
				assert(ix < verts.size());
				tri.pts[n] = verts[ix].v;
			}
			ppts->push_back(coll_tquad(tri, def_color));
		}
		return 1;
	}

public:
	file_reader_3ds_triangles(string const &fn) : file_reader_3ds(fn), ppts(nullptr) {}

	bool read(vector<coll_tquad> *ppts_, geom_xform_t const &xf, colorRGBA const &def_c, bool verbose) {
		assert(ppts_ != nullptr);
		ppts = ppts_;
		def_color = def_c;
		return file_reader_3ds::read(xf, verbose);
	}
};


// ************************************************


class file_reader_3ds_model : public file_reader_3ds, public model_from_file_t {

	int use_vertex_normals;
	unsigned obj_id;

	virtual int proc_other_chunks(unsigned short chunk_id, unsigned chunk_len) {

		// smoothing groups and shininess?
		switch (chunk_id) {
			// OBJ_TRIMESH: Triangular mesh, contains chunks for 3d mesh info; length: 0 + sub chunks
		case 0x4100:
			return read_mesh(chunk_len); // handled

		case 0xAFFF: // material
			{
				// since the material properties may be defined before its name, we can't get the material by name and fill it in;
				// instead, we create a temporary material, fill it in, then look it up by name and overwrite the material in the model with cur_mat
				material_t cur_mat("", filename);
				if (!read_material(chunk_len, cur_mat)) {cerr << "Error reading material " << cur_mat.name << endl; return 0;}
				int const cur_mat_id(model.get_material_ix(cur_mat.name, filename, 0)); // okay if exists
				model.get_material(cur_mat_id) = cur_mat; // overwrite if needed (or should this be an error?)
				return 1; // handled
			}
		} // end switch
		return 2; // skip
	}

	void get_triangle_pts(face_t const &face, vector<vert_tc_t> const &verts, point pts[3]) {
		for (unsigned n = 0; n < 3; ++n) {
			unsigned const ix(face.ix[n]);
			assert(ix < verts.size());
			pts[n] = verts[ix].v;
		}
	}

	bool read_mesh(unsigned read_len) {
		unsigned short chunk_id;
		unsigned chunk_len;
		long const end_pos(get_end_pos(read_len));
		vector<vert_tc_t> verts;
		vector<face_t> faces;
		vector<unsigned> sgroups;
		typedef map<int, vector<unsigned short> > face_mat_map_t;
		face_mat_map_t face_materials;
		xform_matrix matrix;

		while (ftell(fp) < end_pos) { // read each chunk from the file 
			if (!read_chunk_header(chunk_id, chunk_len)) return 0;

			switch (chunk_id) {
				// TRI_FACEL1: Polygons (faces) list
				// Chunk Length: 1 x unsigned short (# polygons) + 3 x unsigned short (polygon points) x (# polygons) + sub chunks
			case 0x4120:
				if (!read_faces(faces)) return 0;
				break;
			// faces data
			case 0x4130: // Faces Material: asciiz name, short nfaces, short face_ids (after faces)
				{
					// read and process material name
					string mat_name;
					if (!read_null_term_string(mat_name)) return 0;
					int const mat_id(model.get_material_ix(mat_name, filename, 1));
					vector<unsigned short> &faces_mat(face_materials[mat_id]);
					unsigned const start_sz(faces_mat.size()); // append if nonempty - is this correct?
					// read and process face materials
					unsigned short num;
					if (!read_data(&num, sizeof(unsigned short), 1, "number of faces for material")) return 0;
					faces_mat.resize(start_sz + num);
					if (num > 0 && !read_data((faces_mat.data() + start_sz), sizeof(unsigned short), num, "faces for material")) return 0;
					if (verbose && num > 0) {cout << "Material " << mat_name << " is used for " << num << " faces" << endl;}
					break;
				}
			case 0x4150: // Smoothing Group List (after mapping coords)
				// nfaces*4bytes: Long int where the nth bit indicates if the face belongs to the nth smoothing group
				assert(chunk_len == sizeof(unsigned)*faces.size() + 6);
				sgroups.resize(faces.size());
				if (!read_data(&sgroups.front(), sizeof(unsigned), faces.size(), "smoothing groups")) return 0;
				break;
				// TRI_VERTEXL: Vertices list
				// Chunk Length: 1 x unsigned short (# vertices) + 3 x float (vertex coordinates) x (# vertices) + sub chunks
			case 0x4110:
				if (!read_vertex_block(verts)) return 0;
				break;
				// TRI_MAPPINGCOORS: Vertices list
				// Chunk Length: 1 x unsigned short (# mapping points) + 2 x float (mapping coordinates) x (# mapping points) + sub chunks
			case 0x4140:
				if (!read_mapping_block(verts)) return 0;
				break;
				// mesh matrix
			case 0x4160:
				read_matrix(matrix, chunk_len);
				break;
			default:
				skip_chunk(chunk_len);
			} // end switch
		} // end while
		assert(ftell(fp) == end_pos);
		transform_vertices(verts, matrix);
		
		// assign materials to faces
		for (face_mat_map_t::const_iterator i = face_materials.begin(); i != face_materials.end(); ++i) {
			for (vector<unsigned short>::const_iterator f = i->second.begin(); f != i->second.end(); ++f) {
				assert(*f < faces.size());
				assert(faces[*f].mat == -1 || faces[*f].mat == i->first); // material not yet assigned
				faces[*f].mat = i->first;
			}
		}
		vector<unsigned short> &def_mat(face_materials[-1]); // create default material

		for (unsigned i = 0; i < faces.size(); ++i) {
			if (faces[i].mat == -1) {def_mat.push_back(i);} // faces not assigned to a material get the default material
		}
		vector<counted_normal> normals; // weighted_normal can also be used, but doesn't work well

		if (use_vertex_normals) {
			// build vertex lists and compute face normals
			normals.resize(verts.size());

			for (vector<face_t>::const_iterator i = faces.begin(); i != faces.end(); ++i) {
				unsigned sgroup(0); // smooth group; represents a bit mask; defaults to 0 if there's no smooth group chunk

				if (!sgroups.empty()) {
					unsigned const face_id(i - faces.begin());
					assert(face_id < sgroups.size());
					sgroup = sgroups[face_id];
				}
				if (sgroup > 0) {} // future work
				point pts[3];
				get_triangle_pts(*i, verts, pts);
				vector3d normal(get_poly_norm(pts));
				if (use_vertex_normals > 1) {normal *= polygon_area(pts, 3);} // weight normal by face area
				UNROLL_3X(normals[i->ix[i_]].add_normal(normal);)
			} // for i
			model3d::proc_model_normals(normals, use_vertex_normals); // use_vertex_normals = 1 or 2 here
		}
		// add triangles to model for each material
		polygon_t tri;
		tri.resize(3);

		for (face_mat_map_t::const_iterator i = face_materials.begin(); i != face_materials.end(); ++i) {
			vntc_map_t vmap[2]; // average_normals=0
			vntct_map_t vmap_tan[2]; // average_normals=0

			for (vector<unsigned short>::const_iterator f = i->second.begin(); f != i->second.end(); ++f) {
				point pts[3];
				get_triangle_pts(faces[*f], verts, pts);
				vector3d const face_n(get_poly_norm(pts));
				unsigned short const *const ixs(faces[*f].ix);

				for (unsigned j = 0; j < 3; ++j) {
					unsigned const ix(ixs[j]);
					vector3d const normal((use_vertex_normals == 0 || (face_n != zero_vector && !normals[ix].is_valid())) ? face_n : normals[ix]);
					tri[j] = vert_norm_tc(pts[j], normal, verts[ix].t[0], verts[ix].t[1]);
				}
				model.add_polygon(tri, vmap, vmap_tan, i->first, obj_id);
			} // for f
		} // for i
		++obj_id;
		return 1;
	}

	bool read_and_proc_texture(unsigned chunk_len, int &tid, char const *const name, bool invert_alpha=0) {
		string tex_fn;
		unsigned short map_tiling;
		if (!read_texture(chunk_len, tex_fn, map_tiling)) {cerr << "Error reading texture " << name << endl; return 0;}
		if (verbose) {cout << name << ": " << tex_fn << ", map_tiling: " << map_tiling << endl;}
		bool const mirror((map_tiling & 0x2) != 0), wrap(map_tiling == 0); // 0x10 for decal (not mirrored or wrapped)
		check_and_bind(tid, tex_fn, 0, verbose, invert_alpha, wrap, mirror);
		return 1;
	}

	bool read_material(unsigned read_len, material_t &cur_mat) {
		unsigned short chunk_id;
		unsigned chunk_len;
		long const end_pos(get_end_pos(read_len));

		while (ftell(fp) < end_pos) { // read each chunk from the file 
			if (!read_chunk_header(chunk_id, chunk_len)) return 0;

			switch (chunk_id) {
			case 0xA000: // material name
				if (!read_null_term_string(cur_mat.name)) return 0;
				break;
			case 0xA010: // material ambient color
				if (!read_color(cur_mat.ka)) return 0;
				//cur_mat.ka = WHITE; // uncomment to force white color (if color was set to black, etc.)
				break;
			case 0xA020: // material diffuse color
				if (!read_color(cur_mat.kd)) return 0;
				//cur_mat.kd = WHITE; // uncomment to force white color (if color was set to black, etc.)
				break;
			case 0xA030: // material specular color
				if (!read_color(cur_mat.ks)) return 0;
				break;
			case 0xA040: // material shininess
				if (!read_percentage(chunk_len, cur_mat.ns)) return 0;
				//cur_mat.ns = 1.0 - cur_mat.ns;
				cur_mat.ns *= 100.0;
				break;
			case 0xA050: // material transparency
				if (!read_percentage(chunk_len, cur_mat.alpha)) return 0;
				cur_mat.alpha = 1.0 - cur_mat.alpha; // convert from transparency to opacity
				break;
			case 0xA200: // texture map 1
				if (!read_and_proc_texture(chunk_len, cur_mat.d_tid, "texture map 1")) return 0;
				break;
			case 0xA204: // specular map
				if (!read_and_proc_texture(chunk_len, cur_mat.s_tid, "specular map")) return 0;
				break;
			case 0xA210: // opacity map
				if (!read_and_proc_texture(chunk_len, cur_mat.alpha_tid, "opacity map", 1)) return 0; // invert alpha values
				break;
			case 0xA230: // bump map
				if (!read_and_proc_texture(chunk_len, cur_mat.bump_tid, "bump map")) return 0;
				break;
			case 0xA220: // reflection map
				if (!read_and_proc_texture(chunk_len, cur_mat.refl_tid, "reflection map")) return 0;
				break;
			default:
				skip_chunk(chunk_len);
			} // end switch
		} // end while
		assert(ftell(fp) == end_pos);

		if (cur_mat.bump_tid >= 0 && cur_mat.bump_tid == cur_mat.d_tid) {
			cout << "Bump map texture is the same as the diffuse texture; ignoring." << endl;
			cur_mat.bump_tid = -1; // why are so many of the files I find online wrong?
		}
		if (verbose) {cout << "Read material " << cur_mat.name << endl;}
		return 1;
	}

	bool read_texture(unsigned read_len, string &tex_name, unsigned short &map_tiling) {
		unsigned short chunk_id;
		unsigned chunk_len;
		long const end_pos(get_end_pos(read_len));
		map_tiling = 0; // in case it's not specified

		while (ftell(fp) < end_pos) { // read each chunk from the file 
			if (!read_chunk_header(chunk_id, chunk_len)) return 0;

			switch (chunk_id) {
			case 0xA300: // mapping filename
				if (!read_null_term_string(tex_name)) return 0;
				break;
			case 0xA351: // mapping parameters
				if (!read_data(&map_tiling, sizeof(unsigned short), 1, "texture map tiling flags")) return 0;
				break;
			default:
				skip_chunk(chunk_len);
			} // end switch
		} // end while
		assert(ftell(fp) == end_pos);
		return 1;
	}

public:
	file_reader_3ds_model(string const &fn, int use_vertex_normals_, model3d &model_) :
	  file_reader_3ds(fn), model_from_file_t(fn, model_), use_vertex_normals(use_vertex_normals_), obj_id(0) {}

	bool read(geom_xform_t const &xf, bool verbose) {
		if (!file_reader_3ds::read(xf, verbose)) return 0;
		model.finalize(); // optimize vertices, remove excess capacity, compute bounding sphere, subdivide, compute LOD blocks
		model.load_all_used_tids();
		if (verbose) {cout << "bcube: " << model.get_bcube().str() << endl; model.show_stats();}
		return 1;
	}
};


bool read_3ds_file_model(string const &filename, model3d &model, geom_xform_t const &xf, int use_vertex_normals, bool verbose) {
	timer_t timer("Read 3DS Model");
	file_reader_3ds_model reader(filename, use_vertex_normals, model);
	return reader.read(xf, (ALLOW_VERBOSE && verbose));
}

bool read_3ds_file_pts(string const &filename, vector<coll_tquad> *ppts, geom_xform_t const &xf, colorRGBA const &def_c, bool verbose) {
	file_reader_3ds_triangles reader(filename);
	return reader.read(ppts, xf, def_c, (ALLOW_VERBOSE && verbose));
}


