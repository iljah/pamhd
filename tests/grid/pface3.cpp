/*
Tests primary cell face calculation of PAMHD.

Copyright 2023 Finnish Meteorological Institute
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
*/


#include "iostream"
#include "set"
#include "stdexcept"
#include "string"

#include "dccrg.hpp"
#include "dccrg_no_geometry.hpp"
#include "mpi.h" // must be included before gensimcell.hpp

#include "common.hpp"


using namespace std;

const auto PFace = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::grid::Is_Primary_Face()];
};

int main(int argc, char* argv[])
{
	if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
		cerr << "Couldn't initialize MPI." << endl;
		abort();
	}
	MPI_Comm comm = MPI_COMM_WORLD;
	float zoltan_version = -1.0;
	if (Zoltan_Initialize(argc, argv, &zoltan_version) != ZOLTAN_OK) {
		cerr << "Zoltan_Initialize failed." << endl;
		abort();
	}

	/*
	Two-cell grids with different direction, periodicities with AMR
	*/
	{Grid grid; grid
		.set_periodic(true, true, true)
		.set_initial_length({2, 1, 1})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 1) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 2 or cell.id > 16) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (cell.id % 2 == 0) {
			if (pf(-1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (cell.id == 2) {
			if (pf(+1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (pf(-2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-3) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}}

	{Grid grid; grid
		.set_periodic(true, true, false)
		.set_initial_length({2, 1, 1})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 1) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 2 or cell.id > 16) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (cell.id % 2 == 0) {
			if (pf(-1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (cell.id == 2) {
			if (pf(+1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (pf(-2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (cell.id >= 2 and cell.id <= 8) {
			if (pf(-3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-3) == true) {
				throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
			}
		}
		if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}}

	{Grid grid; grid
		.set_periodic(true, false, true)
		.set_initial_length({2, 1, 1})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 1) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 2 or cell.id > 16) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (cell.id % 2 == 0) {
			if (pf(-1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (cell.id == 2) {
			if (pf(+1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (cell.id % 8 > 0 and cell.id % 8 < 5) {
			if (pf(-2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-3) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}}

	{Grid grid; grid
		.set_periodic(false, true, true)
		.set_initial_length({2, 1, 1})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 1) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 2 or cell.id > 16) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (cell.id % 4 == 3) {
			if (pf(-1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-3) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}}

	{Grid grid; grid
		.set_periodic(false, false, false)
		.set_initial_length({2, 1, 1})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 1) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	const set<int> pf0{3,7,11,15}, pf2{3,4,11,12}, pf4{3,4,7,8};
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 2 or cell.id > 16) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-1) == false and pf0.count(cell.id) > 0) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-2) == false and pf2.count(cell.id) > 0) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-3) == false and pf4.count(cell.id) > 0) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}}

	{Grid grid; grid
		.set_periodic(false, true, true)
		.set_initial_length({2, 1, 1})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 2) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 1 or cell.id > 18) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (cell.id == 1) {
			if (pf(-1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
			if (pf(+1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else if (cell.id % 2 == 0) {
			if (pf(-1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (cell.id == 1) {
			if (pf(+1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (pf(-2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-3) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}}

	{Grid grid; grid
		.set_periodic(true, true, true)
		.set_initial_length({1, 2, 1})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 1) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 2 or cell.id > 16) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (pf(-1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (cell.id > 2 and cell.id % 8 < 5) {
			if (pf(-2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (cell.id == 2) {
			if (pf(+2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (pf(-3) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}}

	{Grid grid; grid
		.set_periodic(true, false, true)
		.set_initial_length({1, 2, 1})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 1) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 2 or cell.id > 16) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (pf(-1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (cell.id == 2 or cell.id % 8 > 4) {
			if (pf(-2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-3) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}}

	{Grid grid; grid
		.set_periodic(true, true, true)
		.set_initial_length({1, 1, 2})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 1) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 2 or cell.id > 16) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (pf(-1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (cell.id == 2 or cell.id > 6) {
			if (pf(-3) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (cell.id == 2) {
			if (pf(+3) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
	}}

	{Grid grid; grid
		.set_periodic(true, true, false)
		.set_initial_length({1, 1, 2})
		.set_neighborhood_length(0)
		.set_maximum_refinement_level(1)
		.initialize(comm);
	for (const auto& cell: grid.local_cells()) if (cell.id == 1) grid.refine_completely(cell.id);
	grid.stop_refining();
	pamhd::grid::update_primary_faces(grid.local_cells(), PFace);
	for (const auto& cell: grid.local_cells()) {
		if (cell.id < 2 or cell.id > 16) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		const auto pf = PFace(*cell.data);
		if (pf(-1) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+1) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(-2) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (pf(+2) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		if (cell.id == 2 or cell.id > 6) {
			if (pf(-3) == true) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		} else {
			if (pf(-3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
		if (pf(+3) == false) throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}}

	MPI_Finalize();
	return EXIT_SUCCESS;
}
