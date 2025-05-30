/*
Staggered version of get2d_div.cpp.

Copyright 2014, 2015, 2016, 2017 Ilja Honkonen
Copyright 2018, 2022, 2023,
          2024, 2025 Finnish Meteorological Institute
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


#include "array"
#include "cstdlib"
#include "iostream"
#include "limits"
#include "vector"

#include "dccrg.hpp"
#include "dccrg_cartesian_geometry.hpp"
#include "gensimcell.hpp"

#include "grid/amr.hpp"
#include "grid/variables.hpp"
#include "math/nabla.hpp"
#include "tests/math/common.hpp"
#include "variable_getter.hpp"


// assigned to y component of vector field
double function(const double x, const double y)
{
	return x*x/y;
}

double div_of_function(const double x, const double y)
{
	return -std::pow(x/y, 2);
}


/*!
Returns maximum norm if p == 0
*/
template<class Grid> double get_diff_lp_norm(
	const Grid& grid,
	const double p,
	const double cell_volume
) {
	double local_norm = 0, global_norm = 0;
	for (const auto& cell: grid.local_cells()) {
		if ((*cell.data)[Type()] != 1) {
			continue;
		}

		const auto center = grid.geometry.get_center(cell.id);

		const auto div_of = div_of_function(center[0], center[1]);

		if (p == 0) {
			local_norm = std::max(
				local_norm,
				std::fabs((*cell.data)[Divergence()] - div_of)
			);
		} else {
			local_norm += std::pow(
				std::fabs((*cell.data)[Divergence()] - div_of),
				p
			);
		}
	}
	local_norm *= cell_volume;

	if (p == 0) {
		MPI_Comm comm = grid.get_communicator();
		MPI_Allreduce(&local_norm, &global_norm, 1, MPI_DOUBLE, MPI_MAX, comm);
		MPI_Comm_free(&comm);
		return global_norm;
	} else {
		MPI_Comm comm = grid.get_communicator();
		MPI_Allreduce(&local_norm, &global_norm, 1, MPI_DOUBLE, MPI_SUM, comm);
		MPI_Comm_free(&comm);
		return std::pow(global_norm, 1.0 / p);
	}
}


int main(int argc, char* argv[])
{
	if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
		std::cerr << "Couldn't initialize MPI." << std::endl;
		abort();
	}

	MPI_Comm comm = MPI_COMM_WORLD;

	int rank = 0, comm_size = 0;
	if (MPI_Comm_rank(comm, &rank) != MPI_SUCCESS) {
		std::cerr << "Couldn't obtain MPI rank." << std::endl;
		abort();
	}
	if (MPI_Comm_size(comm, &comm_size) != MPI_SUCCESS) {
		std::cerr << "Couldn't obtain size of MPI communicator." << std::endl;
		abort();
	}


	// intialize Zoltan
	float zoltan_version;
	if (Zoltan_Initialize(argc, argv, &zoltan_version) != ZOLTAN_OK) {
		std::cerr << "Zoltan_Initialize failed." << std::endl;
		abort();
	}

	const unsigned int neighborhood_size = 0;
	const int max_refinement_level = 0;

	double old_norm = std::numeric_limits<double>::max();
	size_t old_nr_of_cells = 0;
	for (size_t nr_of_cells = 8; nr_of_cells <= 256; nr_of_cells *= 2) {

		dccrg::Dccrg<
			Cell,
			dccrg::Cartesian_Geometry,
			std::tuple<>,
			std::tuple<
				pamhd::grid::Face_Neighbor,
				pamhd::grid::Relative_Size>
		> grid;

		const std::array<uint64_t, 3> grid_size{{nr_of_cells + 2, nr_of_cells + 2, 1}};

		grid
			.set_load_balancing_method("RANDOM")
			.set_initial_length(grid_size)
			.set_neighborhood_length(neighborhood_size)
			.set_maximum_refinement_level(max_refinement_level)
			.initialize(comm)
			.balance_load();

		const std::array<double, 3>
			cell_length{{
				double(5) / (grid_size[0] - 2),
				double(5) / (grid_size[1] - 2),
				1
			}},
			grid_start{{1, 1, 0}};

		const double cell_volume
			= cell_length[0] * cell_length[1] * cell_length[2];

		dccrg::Cartesian_Geometry::Parameters geom_params;
		geom_params.start = grid_start;
		geom_params.level_0_cell_length = cell_length;
		grid.set_geometry(geom_params);

		for (const auto& cell: grid.local_cells()) {
			const auto
				center = grid.geometry.get_center(cell.id),
				length = grid.geometry.get_length(cell.id);

			auto& vec = (*cell.data)[Vector()];
			vec = {0, 0, 0, 0, 0, 0};
			vec(1, +1) = function(center[0], center[1] + length[1]/2);
			vec(1, -1) = function(center[0], center[1] - length[1]/2);
		}
		grid.update_copies_of_remote_neighbors();

		for (const auto& cell: grid.local_cells()) {
			const auto index = grid.mapping.get_indices(cell.id);
			if (
				index[0] > 0
				and index[0] < grid_size[0] - 1
				and index[1] > 0
				and index[1] < grid_size[1] - 1
			) {
				(*cell.data)[Type()] = 1;
			} else {
				(*cell.data)[Type()] = 0;
			}
		}

		const auto Vector_Getter = pamhd::Variable_Getter<Vector>();
		const auto Divergence_Getter = pamhd::Variable_Getter<Divergence>();
		const auto Type_Getter = pamhd::Variable_Getter<Type>();
		pamhd::math::get_divergence_face2volume(
			grid.local_cells(),
			grid,
			Vector_Getter,
			Divergence_Getter,
			Type_Getter
		);

		const double
			p_of_norm = 2,
			norm = get_diff_lp_norm(grid, p_of_norm, cell_volume);

		if (norm > old_norm) {
			if (grid.get_rank() == 0) {
				std::cerr << __FILE__ << ":" << __LINE__
					<< ": Norm with " << nr_of_cells
					<< " cells " << norm
					<< " is larger than with " << nr_of_cells / 2
					<< " cells " << old_norm
					<< std::endl;
			}
			abort();
		}

		if (old_nr_of_cells > 16) {
			const double order_of_accuracy
				= -log(norm / old_norm)
				/ log(double(nr_of_cells) / old_nr_of_cells);

			if (order_of_accuracy < 1.6) {
				if (grid.get_rank() == 0) {
					std::cerr << __FILE__ << ":" << __LINE__
						<< ": Order of accuracy from "
						<< old_nr_of_cells << " to " << nr_of_cells
						<< " is too low: " << order_of_accuracy
						<< std::endl;
				}
				abort();
			}
		}

		old_nr_of_cells = nr_of_cells;
		old_norm = norm;
	}

	MPI_Finalize();

	return EXIT_SUCCESS;
}
