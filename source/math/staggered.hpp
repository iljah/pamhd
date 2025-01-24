/*
Functions for working with divergence of vector field.

Copyright 2014, 2015, 2016, 2017 Ilja Honkonen
Copyright 2018, 2019, 2022,
          2023, 2024, 2025 Finnish Meteorological Institute
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

* Neither the names of the copyright holders nor the names of their contributors
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

#ifndef PAMHD_MATH_STAGGERED_HPP
#define PAMHD_MATH_STAGGERED_HPP


#include "string"

#include "mpi.h"


namespace pamhd {
namespace math {


/*!
Same as get_divergence() but for vector variable that's stored on cell faces.

Face_Var must have same () operator as provided by grid::Face_Type.

Returns average absolute divergence in cells of all processes.
*/
template <
	class Cell_Iterator,
	class Grid,
	class Face_Var_Getter,
	class Divergence_Getter,
	class Cell_Type_Getter
> double get_divergence_staggered(
	const Cell_Iterator& cells,
	Grid& grid,
	const Face_Var_Getter& Face_Var,
	const Divergence_Getter& Divergence,
	const Cell_Type_Getter& Cell_Type
) {
	double local_divergence = 0, global_divergence = 0;
	uint64_t local_calculated_cells = 0, global_calculated_cells = 0;
	for (const auto& cell: cells) {
		if (Cell_Type.data(*cell.data) <= 0) {
			continue;
		}
		local_calculated_cells++;

		const auto cell_length = grid.geometry.get_length(cell.id);
		auto& div = Divergence.data(*cell.data);
		div = 0.0;

		for (auto dim: {0, 1, 2})
		for (auto side: {-1, +1}) {
			div += side * Face_Var.data(*cell.data)(dim, side) / cell_length[dim];
		}
		local_divergence += std::abs(div);
	}

	MPI_Comm comm = grid.get_communicator();
	MPI_Allreduce(
		&local_divergence,
		&global_divergence,
		1,
		MPI_DOUBLE,
		MPI_SUM,
		comm
	);
	MPI_Allreduce(
		&local_calculated_cells,
		&global_calculated_cells,
		1,
		MPI_UINT64_T,
		MPI_SUM,
		comm
	);
	MPI_Comm_free(&comm);

	if (global_calculated_cells == 0) {
		return 0.0;
	}
	return global_divergence / global_calculated_cells;
}


/*!
Calculates J = curl(B).
*/
template <
	class Grid,
	class Volume_Magnetic_Field_Getter,
	class Volume_Current_Density_Getter,
	class Solver_Info_Getter
> void get_curl_B(
	Grid& grid,
	const Volume_Magnetic_Field_Getter& Vol_B,
	const Volume_Current_Density_Getter& Vol_J,
	const Solver_Info_Getter& SInfo
) {
	using std::abs;
	using std::array;
	using std::cerr;
	using std::endl;

	using Cell = Grid::cell_data_type;

	bool update_copies = false;
	if (SInfo.type().is_stale) {
		update_copies = true;
		Cell::set_transfer_all(true, SInfo.type());
		SInfo.type().is_stale = false;
	}
	if (Vol_B.type().is_stale) {
		update_copies = true;
		Cell::set_transfer_all(true, Vol_B.type());
		Vol_B.type().is_stale = false;
	}
	if (update_copies) {
		grid.update_copies_of_remote_neighbors();
	}
	Cell::set_transfer_all(false, SInfo.type(), Vol_B.type());

	for (const auto& cell: grid.local_cells()) {
		if (SInfo.data(*cell.data) < 0) {
			continue;
		}

		if (grid.get_refinement_level(cell.id) != 0) {
			cerr <<  __FILE__ << "(" << __LINE__<< "): "
				<< "Adaptive mesh refinement not supported" << endl;
			abort();
		}

		const auto cell_length = grid.geometry.get_length(cell.id);

		// get distance between neighbors in same dimension
		array<double, 3>
			// distance from current cell on neg and pos side (> 0)
			neigh_neg_dist{{0, 0, 0}},
			neigh_pos_dist{{0, 0, 0}};

		// number of neighbors in each dimension
		array<size_t, 3> nr_neighbors{{0, 0, 0}};

		for (const auto& neighbor: cell.neighbors_of) {
			const auto& fn = neighbor.face_neighbor;
			if (fn == 0) continue;
			if (SInfo.data(*neighbor.data) < 0) continue;

			const size_t dim = abs(fn) - 1;
			nr_neighbors[dim]++;

			const auto neighbor_length
				= grid.geometry.get_length(neighbor.id);

			const double distance
				= (cell_length[dim] + neighbor_length[dim]) / 2.0;

			if (fn < 0) {
				neigh_neg_dist[dim] = distance;
			} else {
				neigh_pos_dist[dim] = distance;
			}
		}

		bool have_enough_neighbors = false;
		for (auto dim = 0; dim < 3; dim++) {
			if (nr_neighbors[dim] == 2) {
				have_enough_neighbors = true;
			}
		}

		auto
			&vol_b = Vol_B.data(*cell.data),
			&vol_j = Vol_J.data(*cell.data);

		vol_j[0] =
		vol_j[1] =
		vol_j[2] = 0;

		if (not have_enough_neighbors) {
			continue;
		}

		/*
		curl_0 = diff_vec_2 / diff_pos_1 - diff_vec_1 / diff_pos_2
		curl_1 = diff_vec_0 / diff_pos_2 - diff_vec_2 / diff_pos_0
		curl_2 = diff_vec_1 / diff_pos_0 - diff_vec_0 / diff_pos_1
		*/
		for (auto dim0 = 0; dim0 < 3; dim0++) {

			const auto
				dim1 = (dim0 + 1) % 3,
				dim2 = (dim0 + 2) % 3;

			// zero in dimensions with missing neighbor(s)
			if (nr_neighbors[dim1] == 2) {
				vol_j[dim0] += vol_b[dim2] * (neigh_pos_dist[dim1] - neigh_neg_dist[dim1]);
			}
			if (nr_neighbors[dim2] == 2) {
				vol_j[dim0] -= vol_b[dim1] * (neigh_pos_dist[dim2] - neigh_neg_dist[dim2]);
			}
		}

		for (const auto& neighbor: cell.neighbors_of) {
			const auto& fn = neighbor.face_neighbor;
			if (fn == 0) continue;
			if (SInfo.data(*neighbor.data) < 0) continue;

			const size_t dim0 = abs(fn) - 1;
			if (nr_neighbors[dim0] != 2) {
				continue;
			}

			const auto
				dim1 = (dim0 + 1) % 3,
				dim2 = (dim0 + 2) % 3;

			double multiplier = 0;
			if (fn < 0) {
				multiplier = -neigh_pos_dist[dim0] / neigh_neg_dist[dim0];
			} else {
				multiplier = neigh_neg_dist[dim0] / neigh_pos_dist[dim0];
			}
			multiplier /= (neigh_pos_dist[dim0] + neigh_neg_dist[dim0]);

			const auto& neigh_vec = Vol_B.data(*neighbor.data);

			vol_j[dim2] += multiplier * neigh_vec[dim1];
			vol_j[dim1] -= multiplier * neigh_vec[dim2];
		}
	}
	Vol_J.type().is_stale = true;
}

}} // namespaces

#endif // ifndef PAMHD_MATH_STAGGERED_HPP
