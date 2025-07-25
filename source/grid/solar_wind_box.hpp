/*
Grid parts of solar wind box program.

Copyright 2024, 2025 Finnish Meteorological Institute
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

* Neither the name of copyright holders nor the names of their contributors
  may be used to endorse or promote products derived from this software
  without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


Author(s): Ilja Honkonen
*/

#ifndef PAMHD_GRID_SW_BOX_HPP
#define PAMHD_GRID_SW_BOX_HPP


#include "array"
#include "set"
#include "stdexcept"
#include "string"
#include "vector"

#include "simulation_options.hpp"
#include "solar_wind_box_options.hpp"
#include "common_variables.hpp"


namespace pamhd {
namespace grid {


/*! Returns whether given cell spans inner boundary

Returns tuple (a, b) where:
	a == true if some part of cell is inside inner boundary
		or if origin is inside of cell
	b == true if some part of cell is outside of boundary

TODO: support inner boundary not at 0,0,0
*/
template<
	class Grid
> auto at_inner_boundary(
	const double& inner_boundary_radius,
	const uint64_t& cell,
	const Grid& grid
) {
	using std::make_tuple;
	using std::sqrt;

	const auto [sx, sy, sz] = grid.geometry.get_min(cell);
	const auto [ex, ey, ez] = grid.geometry.get_max(cell);
	const auto
		r1 = sqrt(sx*sx + sy*sy + sz*sz),
		r2 = sqrt(sx*sx + sy*sy + ez*ez),
		r3 = sqrt(sx*sx + ey*ey + sz*sz),
		r4 = sqrt(sx*sx + ey*ey + ez*ez),
		r5 = sqrt(ex*ex + sy*sy + sz*sz),
		r6 = sqrt(ex*ex + sy*sy + ez*ez),
		r7 = sqrt(ex*ex + ey*ey + sz*sz),
		r8 = sqrt(ex*ex + ey*ey + ez*ez);
	bool inside = false, outside = false;
	if (
		r1 <= inner_boundary_radius
		or r2 <= inner_boundary_radius
		or r3 <= inner_boundary_radius
		or r4 <= inner_boundary_radius
		or r5 <= inner_boundary_radius
		or r6 <= inner_boundary_radius
		or r7 <= inner_boundary_radius
		or r8 <= inner_boundary_radius
	) inside = true;
	if (
		r1 > inner_boundary_radius
		or r2 > inner_boundary_radius
		or r3 > inner_boundary_radius
		or r4 > inner_boundary_radius
		or r5 > inner_boundary_radius
		or r6 > inner_boundary_radius
		or r7 > inner_boundary_radius
		or r8 > inner_boundary_radius
	) outside = true;

	if (
		sx <= 0 and ex >= 0
		and sy <= 0 and ey >= 0
		and sz <= 0 and ez >= 0
	) {
		return make_tuple(true, outside);
	}

	return make_tuple(inside, outside);
}


//! Refines cells near inner boundary
template<
	class Grid
> std::set<uint64_t> refine_inner_cells(
	const double& inner_bdy_radius,
	Grid& grid
) try {
	std::set<uint64_t> ret_val;
	const auto mrlvl = grid.get_maximum_refinement_level();
	for (int i = 0; i <= mrlvl; i++) {
		for (const auto& cell: grid.local_cells()) {
			const auto [inside, outside]
				= at_inner_boundary(inner_bdy_radius, cell.id, grid);
			if (inside and outside) {
				if (grid.get_refinement_level(cell.id) < mrlvl) {
					grid.refine_completely(cell.id);
				} else {
					ret_val.insert(cell.id);
				}
				continue;
			}
			const auto [cx, cy, cz]
				= grid.geometry.get_center(cell.id);
			const auto cr2 = cx*cx + cy*cy + cz*cz;
			// maybe refine if neighbor at inner boundary
			for (const auto& neighbor: cell.neighbors_of) {
				if (
					neighbor.face_neighbor == 0
					and neighbor.edge_neighbor[0] < 0
				) {
					continue;
				}
				const auto [inside, outside]
					= at_inner_boundary(inner_bdy_radius, neighbor.id, grid);
				if (inside and outside) {
					const auto [nx, ny, nz]
						= grid.geometry.get_center(neighbor.id);
					if (cr2 > nx*nx + ny*ny + nz*nz) {
						if (grid.get_refinement_level(cell.id) < mrlvl) {
							grid.refine_completely(cell.id);
						} else {
							ret_val.insert(cell.id);
						}
						break;
					}
				}
			}
		}
		grid.stop_refining();
		grid.clear_refined_unrefined_data();
	}
	return ret_val;

} catch (const std::exception& e) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + "): " + e.what());
} catch (...) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + ")");
}


/*! Prepares grid for physics initialization

-maximally refines inner boundary cells + their normal neighbors
-sets default minimum and maximum target refinement levels
-sets solver info for inner and outer boundaries

Requires following transfers to be switched on:
solver info, ...
*/
template<
	class Grid,
	class Solver_Info_Getter,
	class Targer_Maximum_Refinement_Level_Getter,
	class Targer_Minimum_Refinement_Level_Getter
> auto prepare_grid(
	const pamhd::Options& options_sim,
	pamhd::grid::Options& options_grid,
	const pamhd::Solar_Wind_Box_Options& options_box,
	Grid& grid,
	const Solver_Info_Getter& SInfo,
	const Targer_Maximum_Refinement_Level_Getter& Ref_max,
	const Targer_Minimum_Refinement_Level_Getter& Ref_min
) try {
	using std::invalid_argument;
	using std::max;
	using std::min;
	using std::runtime_error;
	using std::to_string;

	using Cell = Grid::cell_data_type;

	// maximum refinement level at inner boundary
	const auto max_ref_cells = refine_inner_cells(options_box.inner_radius, grid);
	const auto mrlvl = grid.get_maximum_refinement_level();
	for (const auto& cell: grid.local_cells()) {
		if (max_ref_cells.count(cell.id) > 0) {
			Ref_min.data(*cell.data) = mrlvl;
		} else {
			Ref_min.data(*cell.data) = 0;
		}
		Ref_max.data(*cell.data) = mrlvl;
	}

	// minimal refinement level at outer boundaries
	const auto len0 = grid.length.get();
	const auto indices0 = uint64_t(1) << mrlvl;
	const std::array<uint64_t, 3> end_indices{
		len0[0] * indices0, len0[1] * indices0, len0[2] * indices0
	};
	for (const auto& cell: grid.local_cells()) {
		const auto indices = grid.mapping.get_indices(cell.id);
		int min_distance = 1 << 30; // from outer wall
		// FIXME: assumes neighborhood size of 3
		for (auto dim: {0, 1, 2}) {
			if (len0[dim] == 1) continue;
			min_distance = min(min(min_distance,
				int(indices[dim] / indices0)),
				int((end_indices[dim]-indices[dim]-1) / indices0));
		}
		const auto rlvl = grid.get_refinement_level(cell.id);
		if (min_distance < 2) {
			Ref_max.data(*cell.data) = 0;
			if (rlvl > 0) throw runtime_error(__FILE__"(" + to_string(__LINE__) + "): " + to_string(cell.id));
		} else if (min_distance < 5) {
			Ref_max.data(*cell.data) = 1;
			if (rlvl > 1) throw runtime_error(__FILE__"(" + to_string(__LINE__) + "): " + to_string(cell.id));
		} else if (min_distance < 7) {
			Ref_max.data(*cell.data) = 2;
			if (rlvl > 2) throw runtime_error(__FILE__"(" + to_string(__LINE__) + "): " + to_string(cell.id));
		} else {
			Ref_max.data(*cell.data) = min_distance - 4;
			if (rlvl > min_distance-4) throw runtime_error(__FILE__"(" + to_string(__LINE__) + "): " + to_string(cell.id));
		}
		Ref_max.data(*cell.data) = min(mrlvl, Ref_max.data(*cell.data));
	}

	pamhd::grid::set_minmax_refinement_level(
		grid.local_cells(), grid, options_grid,
		options_sim.time_start, Ref_min, Ref_max, true
	);
	adapt_grid_sw_box(grid, Ref_min, Ref_max);
	Cell::set_transfer_all(true, Ref_min.type(), Ref_max.type());
	grid.balance_load();
	Cell::set_transfer_all(false, Ref_min.type(), Ref_max.type());
	Ref_max.type().is_stale = false;
	Ref_min.type().is_stale = false;


	/*
	Classify and collect boundary cells
	*/

	for (const auto& cell: grid.local_cells()) {
		SInfo.data(*cell.data) = 1;
	}

	using cell_list_t = std::vector<
		std::remove_cv_t<std::remove_reference_t<
			decltype(grid.cells.front())>>>;

	// sw_cells must also include remote copies
	cell_list_t sw_cells;
	for (const auto& cell: grid.all_cells()) {
		const auto indices = grid.mapping.get_indices(cell.id);
		const auto len = grid.mapping.get_cell_length_in_indices(cell.id);

		if (options_box.sw_dir < 0) {
			if (indices[options_box.sw_dim] == 0) {
				sw_cells.push_back(cell);
			}
		} else {
			if (indices[options_box.sw_dim] == end_indices[options_box.sw_dim] - len) {
				sw_cells.push_back(cell);
			}
		}
	}
	for (const auto& cell: sw_cells) {
		if (cell.is_local) SInfo.data(*cell.data) = 0;
	}

	// outflow
	// boundary and normal cell share face
	pamhd::Face_Type<cell_list_t> face_bdy;
	// boundary and normal cell share edge
	pamhd::Edge_Type<cell_list_t> edge_bdy;
	// boundary and normal cell share vertex
	cell_list_t vert_bdy;
	for (const auto& cell: grid.local_cells()) {
		const auto indices = grid.mapping.get_indices(cell.id);
		const auto len = grid.mapping.get_cell_length_in_indices(cell.id);

		int x = 0, y = 0, z = 0;
		if (indices[0] == 0) x = -1;
		else if (indices[0] == end_indices[0] - len) x = +1;
		if (indices[1] == 0) y = -1;
		else if (indices[1] == end_indices[1] - len) y = +1;
		if (indices[2] == 0) z = -1;
		else if (indices[2] == end_indices[2] - len) z = +1;

		if (len0[0] == 1) x = 0;
		if (len0[1] == 1) y = 0;
		if (len0[2] == 1) z = 0;

		if (x != 0 and y != 0 and z != 0) {
			vert_bdy.push_back(cell);
		} else if (y < 0 and z < 0) {
			edge_bdy(0, -1, -1).push_back(cell);
		} else if (y < 0 and z > 0) {
			edge_bdy(0, -1, +1).push_back(cell);
		} else if (y > 0 and z < 0) {
			edge_bdy(0, +1, -1).push_back(cell);
		} else if (y > 0 and z > 0) {
			edge_bdy(0, +1, +1).push_back(cell);
		} else if (x < 0 and z < 0) {
			edge_bdy(1, -1, -1).push_back(cell);
		} else if (x < 0 and z > 0) {
			edge_bdy(1, -1, +1).push_back(cell);
		} else if (x > 0 and z < 0) {
			edge_bdy(1, +1, -1).push_back(cell);
		} else if (x > 0 and z > 0) {
			edge_bdy(1, +1, +1).push_back(cell);
		} else if (x < 0 and y < 0) {
			edge_bdy(2, -1, -1).push_back(cell);
		} else if (x < 0 and y > 0) {
			edge_bdy(2, -1, +1).push_back(cell);
		} else if (x > 0 and y < 0) {
			edge_bdy(2, +1, -1).push_back(cell);
		} else if (x > 0 and y > 0) {
			edge_bdy(2, +1, +1).push_back(cell);
		} else if (x < 0) {
			face_bdy(-1).push_back(cell);
		} else if (x > 0) {
			face_bdy(+1).push_back(cell);
		} else if (y < 0) {
			face_bdy(-2).push_back(cell);
		} else if (y > 0) {
			face_bdy(+2).push_back(cell);
		} else if (z < 0) {
			face_bdy(-3).push_back(cell);
		} else if (z > 0) {
			face_bdy(+3).push_back(cell);
		}
	}
	for (const auto& cell: vert_bdy) {
		SInfo.data(*cell.data) = 0;
	}
	for (int dir: {-3,-2,-1,+1,+2,+3}) {
		for (const auto& cell: face_bdy(dir)) {
			SInfo.data(*cell.data) = 0;
		}
	}
	for (size_t dim: {0, 1, 2})
	for (int dir1: {-1,+1})
	for (int dir2: {-1,+1}) {
		for (const auto& cell: edge_bdy(dim, dir1, dir2)) {
			SInfo.data(*cell.data) = 0;
		}
	}

	// planet
	cell_list_t _planet_cells;
	for (const auto& cell: grid.local_cells()) {
		const auto [
			inside, outside
		] = at_inner_boundary(
			options_box.inner_radius, cell.id, grid
		);
		if (inside) {
			SInfo.data(*cell.data) = 0;
			_planet_cells.push_back(cell);
			if (outside and grid.get_refinement_level(cell.id) < mrlvl) {
				throw runtime_error(__FILE__"(" + to_string(__LINE__)
					+ "): " + to_string(cell.id));
			}
		}
	}
	Cell::set_transfer_all(true, SInfo.type());
	grid.update_copies_of_remote_neighbors();
	cell_list_t planet_cells;
	for (const auto& cell: _planet_cells) {
		bool have_normal = false;
		for (const auto& neighbor: cell.neighbors_of) {
			if (
				neighbor.face_neighbor == 0
				and neighbor.edge_neighbor[0] < 0
			) {
				continue;
			}
			if (SInfo.data(*neighbor.data) == 1) {
				have_normal = true;
				break;
			}
		}
		if (not have_normal) {
			SInfo.data(*cell.data) = -1;
		} else {
			planet_cells.push_back(cell);
		}
	}
	grid.update_copies_of_remote_neighbors();
	Cell::set_transfer_all(false, SInfo.type());
	SInfo.type().is_stale = false;

	return std::make_tuple(
		sw_cells, face_bdy, edge_bdy, vert_bdy, planet_cells);

} catch (const std::exception& e) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + "): " + e.what());
} catch (...) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + ")");
}


template <
	class Grid,
	class Target_Refinement_Level_Min_Getter,
	class Target_Refinement_Level_Max_Getter
> void adapt_grid_sw_box(
	Grid& grid,
	const Target_Refinement_Level_Min_Getter& Ref_min,
	const Target_Refinement_Level_Max_Getter& Ref_max
) {
	using std::to_string;
	using std::runtime_error;

	for (const auto& cell: grid.local_cells()) {
		const auto ref_lvl = grid.get_refinement_level(cell.id);
		if (ref_lvl < Ref_min.data(*cell.data)) {
			grid.refine_completely(cell.id);
		} else if (ref_lvl == Ref_min.data(*cell.data)) {
			grid.dont_unrefine(cell.id);
		} else if (ref_lvl > Ref_max.data(*cell.data)) {
			grid.unrefine_completely(cell.id);
		}
	}
	const auto new_cells = grid.stop_refining();
	for (auto cell: new_cells) {
		auto* const cell_data = grid[cell];
		if (cell_data == nullptr) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		auto* const parent_data = grid[grid.get_parent(cell)];
		if (parent_data == nullptr) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}
	const auto removed_cells = grid.get_removed_cells();
	for (auto cell: removed_cells) {
		auto* const removed_data = grid[cell];
		if (removed_data == nullptr) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto parent = grid.mapping.get_parent(cell);
		auto* const parent_data = grid[parent];
		if (parent_data == nullptr) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}
}


}} // namespaces

#endif // ifndef PAMHD_GRID_SW_BOX_HPP
