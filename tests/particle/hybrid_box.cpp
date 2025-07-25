/*
Plain hybrid test program of PAMHD with one planet in solar wind.

Copyright 2025 Finnish Meteorological Institute
All rights reserved.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.


Author(s): Ilja Honkonen
*/


#include "array"
#include "cmath"
#include "cstdlib"
#include "fstream"
#include "iostream"
#include "limits"
#include "random"
#include "streambuf"
#include "string"
#include "vector"

#include "boost/filesystem.hpp"
#include "dccrg.hpp"
#include "dccrg_cartesian_geometry.hpp"
#include "Eigen/Core" // must be included before gensimcell.hpp
#include "mpi.h" // must be included before gensimcell.hpp
#include "gensimcell.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

#include "background_magnetic_field.hpp"
#include "boundaries/geometries.hpp"
#include "boundaries/multivariable_boundaries.hpp"
#include "boundaries/multivariable_initial_conditions.hpp"
#include "common_variables.hpp"
#include "grid/amr.hpp"
#include "grid/options.hpp"
#include "grid/solar_wind_box.hpp"
#include "grid/variables.hpp"
#include "math/interpolation.hpp"
#include "math/nabla.hpp"
#include "particle/accumulate_dccrg.hpp"
#include "particle/boundaries.hpp"
#include "particle/common.hpp"
#include "particle/initialize.hpp"
#include "particle/options.hpp"
#include "particle/save.hpp"
#include "particle/solar_wind_box.hpp"
#include "particle/solve_dccrg.hpp"
#include "particle/splitter.hpp"
#include "particle/variables.hpp"
#include "simulation_options.hpp"
#include "solar_wind_box_options.hpp"
#include "variable_getter.hpp"


// counter for assigning unique id to particles
unsigned long long int next_particle_id;

bool pamhd::particle::Volume_Ion_Current_Density::is_stale = true;
const auto Vol_Ji = pamhd::Variable_Getter<
	pamhd::particle::Volume_Ion_Current_Density>();

bool pamhd::particle::Vertex_Ion_Current_Density::is_stale = true;
const auto Vert_Ji = pamhd::Variable_Getter<
	pamhd::particle::Vertex_Ion_Current_Density>();

bool pamhd::particle::Volume_Ion_Charge_Density::is_stale = true;
const auto Vol_Qi = pamhd::Variable_Getter<
	pamhd::particle::Volume_Ion_Charge_Density>();

bool pamhd::particle::Vertex_Ion_Charge_Density::is_stale = true;
const auto Vert_Qi = pamhd::Variable_Getter<
	pamhd::particle::Vertex_Ion_Charge_Density>();

bool pamhd::particle::Volume_Magnetic_Field::is_stale = true;
const auto Vol_B = pamhd::Variable_Getter<
	pamhd::particle::Volume_Magnetic_Field>();

bool pamhd::particle::Vertex_Magnetic_Field::is_stale = true;
const auto Vert_B = pamhd::Variable_Getter<
	pamhd::particle::Vertex_Magnetic_Field>();

bool pamhd::particle::Volume_Electric_Current_Density::is_stale = true;
const auto Vol_J = pamhd::Variable_Getter<
	pamhd::particle::Volume_Electric_Current_Density>();

bool pamhd::particle::Edge_Electric_Current_Density::is_stale = true;
const auto Edge_J = pamhd::Variable_Getter<
	pamhd::particle::Edge_Electric_Current_Density>();

bool pamhd::particle::Vertex_Electric_Current_Density::is_stale = true;
const auto Vert_J = pamhd::Variable_Getter<
	pamhd::particle::Vertex_Electric_Current_Density>();

bool pamhd::particle::Vertex_Electric_Field::is_stale = true;
const auto Vert_E = pamhd::Variable_Getter<
	pamhd::particle::Vertex_Electric_Field>();

bool pamhd::particle::Volume_Electron_Velocity::is_stale = true;
const auto Vol_Ve = pamhd::Variable_Getter<
	pamhd::particle::Volume_Electron_Velocity>();

bool pamhd::particle::Vertex_Electron_Velocity::is_stale = true;
const auto Vert_Ve = pamhd::Variable_Getter<
	pamhd::particle::Vertex_Electron_Velocity>();

using Cell = gensimcell::Cell<
	gensimcell::Optional_Transfer,
	pamhd::particle::Volume_Ion_Current_Density,
	pamhd::particle::Vertex_Ion_Current_Density,
	pamhd::particle::Volume_Ion_Charge_Density,
	pamhd::particle::Vertex_Ion_Charge_Density,
	pamhd::particle::Volume_Magnetic_Field,
	pamhd::particle::Vertex_Magnetic_Field,
	pamhd::particle::Volume_Electric_Current_Density,
	pamhd::particle::Edge_Electric_Current_Density,
	pamhd::particle::Vertex_Electric_Current_Density,
	pamhd::particle::Vertex_Electric_Field,
	pamhd::particle::Volume_Electron_Velocity,
	pamhd::particle::Vertex_Electron_Velocity,
	pamhd::MPI_Rank,
	pamhd::Bg_Magnetic_Field,
	pamhd::Resistivity,
	pamhd::Magnetic_Field_Divergence,
	pamhd::Face_Magnetic_Field,
	pamhd::Face_dB,
	pamhd::grid::Target_Refinement_Level_Max,
	pamhd::grid::Target_Refinement_Level_Min,
	pamhd::Cell_Type,
	pamhd::Timestep,
	pamhd::Substepping_Period,
	pamhd::Substep_Min,
	pamhd::Substep_Max,
	pamhd::particle::Number_Of_Particles,
	pamhd::particle::Max_Spatial_Velocity,
	pamhd::particle::Max_Angular_Velocity,
	pamhd::particle::Bdy_Number_Density,
	pamhd::particle::Bdy_Velocity,
	pamhd::particle::Bdy_Temperature,
	pamhd::particle::Bdy_Species_Mass,
	pamhd::particle::Bdy_Charge_Mass_Ratio,
	pamhd::particle::Bdy_Nr_Particles_In_Cell,
	pamhd::particle::Bulk_Mass,
	pamhd::particle::Bulk_Momentum,
	pamhd::particle::Bulk_Velocity,
	pamhd::particle::Current_Minus_Velocity,
	pamhd::particle::Bulk_Relative_Velocity2,
	pamhd::particle::Nr_Particles_Internal,
	pamhd::particle::Nr_Particles_External,
	pamhd::particle::Nr_Accumulated_To_Cells,
	pamhd::particle::Particles_Internal,
	pamhd::particle::Particles_External,
	pamhd::particle::Accumulated_To_Cells
>;

using Grid = dccrg::Dccrg<
	Cell,
	dccrg::Cartesian_Geometry,
	std::tuple<pamhd::grid::Cell_Is_Local>,
	std::tuple<
		pamhd::grid::Face_Neighbor,
		pamhd::grid::Edge_Neighbor,
		pamhd::grid::Vertex_Neighbor,
		pamhd::grid::Relative_Size,
		pamhd::grid::Neighbor_Is_Local>
>;

const auto Bg_B = pamhd::Variable_Getter<pamhd::Bg_Magnetic_Field>();
bool pamhd::Bg_Magnetic_Field::is_stale = true;

const auto Mas = pamhd::Variable_Getter<pamhd::mhd::Mass_Density>();
bool pamhd::mhd::Mass_Density::is_stale = true;

const auto Mom = pamhd::Variable_Getter<pamhd::mhd::Momentum_Density>();
bool pamhd::mhd::Momentum_Density::is_stale = true;

const auto Nrj = pamhd::Variable_Getter<pamhd::mhd::Total_Energy_Density>();
bool pamhd::mhd::Total_Energy_Density::is_stale = true;

const auto Face_B = pamhd::Variable_Getter<pamhd::Face_Magnetic_Field>();
bool pamhd::Face_Magnetic_Field::is_stale = true;

const auto Face_dB = pamhd::Variable_Getter<pamhd::Face_dB>();

const auto Div_B = pamhd::Variable_Getter<pamhd::Magnetic_Field_Divergence>();

// electric current minus bulk velocity
const auto J_m_V = pamhd::Variable_Getter<pamhd::particle::Current_Minus_Velocity>();
bool pamhd::particle::Current_Minus_Velocity::is_stale = true;

// electric field for propagating particles
const auto Vol_E = pamhd::Variable_Getter<pamhd::particle::Electric_Field>();
bool pamhd::particle::Electric_Field::is_stale = true;

/*! Solver info variable for boundary logic

-1 for cells not to be read nor written
0 for read-only cells
1 for read-write cells
*/
const auto CType = pamhd::Variable_Getter<pamhd::Cell_Type>();
bool pamhd::Cell_Type::is_stale = true;

const auto Substep = pamhd::Variable_Getter<pamhd::Substepping_Period>();
bool pamhd::Substepping_Period::is_stale = true;

const auto Substep_Min = pamhd::Variable_Getter<pamhd::Substep_Min>();
bool pamhd::Substep_Min::is_stale = true;

const auto Substep_Max = pamhd::Variable_Getter<pamhd::Substep_Max>();
bool pamhd::Substep_Max::is_stale = true;

const auto Timestep = pamhd::Variable_Getter<pamhd::Timestep>();
bool pamhd::Timestep::is_stale = true;

const auto Ref_min = pamhd::Variable_Getter<pamhd::grid::Target_Refinement_Level_Min>();
bool pamhd::grid::Target_Refinement_Level_Min::is_stale = true;

const auto Ref_max = pamhd::Variable_Getter<pamhd::grid::Target_Refinement_Level_Max>();
bool pamhd::grid::Target_Refinement_Level_Max::is_stale = true;

// list of particles in cell not moving to another cell
const auto Part_Int = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::particle::Particles_Internal()];
};

// particles moving to another cell
const auto Part_Ext = pamhd::Variable_Getter<pamhd::particle::Particles_External>();
bool pamhd::particle::Particles_External::is_stale = true;

// number of particles in above list, for allocating memory for arriving particles
const auto Nr_Ext = pamhd::Variable_Getter<pamhd::particle::Nr_Particles_External>();
bool pamhd::particle::Nr_Particles_External::is_stale = true;

const auto Nr_Int = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::particle::Nr_Particles_Internal()];
};

const auto Max_v_part = pamhd::Variable_Getter<pamhd::particle::Max_Spatial_Velocity>();
bool pamhd::particle::Max_Spatial_Velocity::is_stale = true;

const auto Max_ω_part = pamhd::Variable_Getter<pamhd::particle::Max_Angular_Velocity>();
bool pamhd::particle::Max_Angular_Velocity::is_stale = true;

// given a particle these return references to particle's parameters
const auto Part_Pos = [](
	pamhd::particle::Particle_Internal& particle
)->auto& {
	return particle[pamhd::particle::Position()];
};
// as above but for caller that also provides cell's data
const auto Part_Vel_Cell = [](
	Cell&, pamhd::particle::Particle_Internal& particle
)->auto& {
	return particle[pamhd::particle::Velocity()];
};
const auto Part_Vel = [](
	pamhd::particle::Particle_Internal& particle
)->auto& {
	return particle[pamhd::particle::Velocity()];
};
const auto Part_C2M = [](
	pamhd::particle::Particle_Internal& particle
)->auto& {
	return particle[pamhd::particle::Charge_Mass_Ratio()];
};
// copy of number of real particles represented by simulation particle
const auto Part_Nr = [](
	pamhd::particle::Particle_Internal& particle
)->auto {
	return particle[pamhd::particle::Mass()] / particle[pamhd::particle::Species_Mass()];
};
const auto Part_Mas = [](
	pamhd::particle::Particle_Internal& particle
)->auto& {
	return particle[pamhd::particle::Mass()];
};
// as above but for caller that also provides cell's data
const auto Part_Mas_Cell = [](
	Cell&, pamhd::particle::Particle_Internal& particle
)->auto& {
	return particle[pamhd::particle::Mass()];
};
const auto Part_Des = [](
	pamhd::particle::Particle_External& particle
)->auto& {
	return particle[pamhd::particle::Destination_Cell()];
};
// reference to mass of given particle's species
const auto Part_SpM = [](
	pamhd::particle::Particle_Internal& particle
)->auto& {
	return particle[pamhd::particle::Species_Mass()];
};
// as above but for caller that also provides cell's data
const auto Part_SpM_Cell = [](
	Cell&, pamhd::particle::Particle_Internal& particle
)->auto& {
	return particle[pamhd::particle::Species_Mass()];
};
// copy of particle's kinetic energy relative to pamhd::particle::Bulk_Velocity
const auto Part_Ekin = [](
	Cell& cell_data,
	pamhd::particle::Particle_Internal& particle
)->auto {
	return
		0.5 * particle[pamhd::particle::Mass()]
		* pamhd::norm2(
			pamhd::add(
				particle[pamhd::particle::Velocity()],
				pamhd::neg(cell_data[pamhd::particle::Bulk_Velocity()].first))
		);
};

// reference to accumulated number of particles in given cell
const auto Nr_Particles = pamhd::Variable_Getter<pamhd::particle::Number_Of_Particles>();
bool pamhd::particle::Number_Of_Particles::is_stale = true;

const auto Bulk_Mass_Getter = pamhd::Variable_Getter<pamhd::particle::Bulk_Mass>();
bool pamhd::particle::Bulk_Mass::is_stale = true;

const auto Bulk_Momentum_Getter = pamhd::Variable_Getter<pamhd::particle::Bulk_Momentum>();
bool pamhd::particle::Bulk_Momentum::is_stale = true;

const auto Bulk_Relative_Velocity2_Getter = pamhd::Variable_Getter<pamhd::particle::Bulk_Relative_Velocity2>();
bool pamhd::particle::Bulk_Relative_Velocity2::is_stale = true;

const auto Bulk_Velocity_Getter = pamhd::Variable_Getter<pamhd::particle::Bulk_Velocity>();
bool pamhd::particle::Bulk_Velocity::is_stale = true;

// list of items (variables above) accumulated from particles in given cell
const auto Accu_List_Getter = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::particle::Accumulated_To_Cells()];
};

// length of above list (for transferring between processes)
const auto Accu_List_Length_Getter = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::particle::Nr_Accumulated_To_Cells()];
};

// target cell of accumulated particle values in an accumulation list item
const auto Accu_List_Target_Getter = [](
	pamhd::particle::Accumulated_To_Cell& accu_item
)->auto& {
	return accu_item[pamhd::particle::Target()];
};

// accumulated number of particles in an accumulation list item
const auto Accu_List_Number_Of_Particles_Getter = [](
	pamhd::particle::Accumulated_To_Cell& accu_item
)->auto& {
	return accu_item[pamhd::particle::Number_Of_Particles()];
};

const auto Accu_List_Bulk_Mass_Getter = [](
	pamhd::particle::Accumulated_To_Cell& accu_item
)->auto& {
	return accu_item[pamhd::particle::Bulk_Mass()];
};

const auto Accu_List_Bulk_Velocity_Getter = [](
	pamhd::particle::Accumulated_To_Cell& accu_item
)->auto& {
	return accu_item[pamhd::particle::Bulk_Velocity()];
};

const auto Accu_List_Bulk_Relative_Velocity2_Getter = [](
	pamhd::particle::Accumulated_To_Cell& accu_item
)->auto& {
	return accu_item[pamhd::particle::Bulk_Relative_Velocity2()];
};


int main(int argc, char* argv[]) {
	using std::ceil;
	using std::cerr;
	using std::cout;
	using std::endl;
	using std::flush;
	using std::max;
	using std::min;
	using std::runtime_error;
	using std::to_string;

	/*
	Initialize MPI
	*/

	if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
		cerr << "Couldn't initialize MPI." << endl;
		abort();
	}

	MPI_Comm comm = MPI_COMM_WORLD;

	int rank = 0, comm_size = 0;
	if (MPI_Comm_rank(comm, &rank) != MPI_SUCCESS) {
		cerr << "Couldn't obtain MPI rank." << endl;
		abort();
	}
	if (MPI_Comm_size(comm, &comm_size) != MPI_SUCCESS) {
		cerr << "Couldn't obtain size of MPI communicator." << endl;
		abort();
	}

	next_particle_id = 1 + rank;

	// initialize Zoltan
	float zoltan_version;
	if (Zoltan_Initialize(argc, argv, &zoltan_version) != ZOLTAN_OK) {
		cerr << "Zoltan_Initialize failed." << endl;
		abort();
	}

	/*
	Parse configuration file
	*/

	if (argc != 2) {
		if (argc < 2 and rank == 0) {
			cerr
				<< "Name of configuration file required."
				<< endl;
		}
		if (argc > 2 and rank == 0) {
			cerr
				<< "Too many arguments given to " << argv[0]
				<< ": " << argc - 1 << ", should be 1"
				<< endl;
		}
		MPI_Finalize();
		return EXIT_FAILURE;
	}

	std::ifstream json_file(argv[1]);
	if (not json_file.good()) {
		if (rank == 0) {
			cerr << "Couldn't open configuration file " << argv[1] << endl;
		}
		MPI_Finalize();
		return EXIT_FAILURE;
	}

	std::string json{
		std::istreambuf_iterator<char>(json_file),
		std::istreambuf_iterator<char>()
	};

	rapidjson::Document document;
	document.Parse(json.c_str());
	if (document.HasParseError()) {
		if (rank == 0) {
			cerr << "Couldn't parse json data in file "
				<< argv[1] << " at character position "
				<< document.GetErrorOffset() << ": "
				<< rapidjson::GetParseError_En(document.GetParseError())
				<< endl;
		}
		MPI_Finalize();
		return EXIT_FAILURE;
	}

	pamhd::Options options_sim{document};
	pamhd::grid::Options options_grid{document};
	pamhd::particle::Options options_particle{document};
	pamhd::Solar_Wind_Box_Options options_box{document};

	if (rank == 0 and options_sim.output_directory != "") {
		cout << "Saving results into directory " << options_sim.output_directory << endl;
		try {
			boost::filesystem::create_directories(options_sim.output_directory);
		} catch (const boost::filesystem::filesystem_error& e) {
			cerr <<  __FILE__ << "(" << __LINE__ << ") "
				"Couldn't create output directory "
				<< options_sim.output_directory << ": "
				<< e.what()
				<< endl;
			abort();
		}
	}

	pamhd::Background_Magnetic_Field<
		double,
		pamhd::Magnetic_Field::data_type
	> background_B;
	background_B.set(document);

	/*
	Initialize simulation grid
	*/
	const unsigned int neighborhood_size = 3;
	const auto& number_of_cells = options_grid.get_number_of_cells();
	const size_t min_cell0_count = 5 + 2*options_grid.get_max_ref_lvl();
	for (auto dim: {0, 1, 2}) {
		if (number_of_cells[dim] == 1) continue;
		if (number_of_cells[dim] < min_cell0_count) {
			cout << "Number of initial cells in dimension " << dim
				<< " must be at least " << min_cell0_count
				<< " but " << number_of_cells[dim] << " given" << endl;
			abort();
		}
	}
	const auto& periodic = options_grid.get_periodic();

	Grid grid; grid
		.set_initial_length(number_of_cells)
		.set_neighborhood_length(neighborhood_size)
		.set_periodic(periodic[0], periodic[1], periodic[2])
		.set_load_balancing_method(options_sim.lb_name.c_str())
		.set_maximum_refinement_level(options_grid.get_max_ref_lvl())
		.initialize(comm);

	// set grid geometry
	const std::array<double, 3>
		simulation_volume
			= options_grid.get_volume(),
		cell_volume{
			simulation_volume[0] / number_of_cells[0],
			simulation_volume[1] / number_of_cells[1],
			simulation_volume[2] / number_of_cells[2]
		};

	dccrg::Cartesian_Geometry::Parameters geom_params;
	geom_params.start = options_grid.get_start();
	geom_params.level_0_cell_length = cell_volume;

	try {
		grid.set_geometry(geom_params);
	} catch (...) {
		cerr << __FILE__ << ":" << __LINE__
			<< ": Couldn't set grid geometry."
			<< endl;
		abort();
	}

	if (rank == 0) {
		cout << "Adapting and balancing grid at time "
			<< options_sim.time_start << "...  " << flush;
	}

	auto [
		solar_wind_cells, face_cells,
		edge_cells, vert_cells, planet_cells
	] = pamhd::grid::prepare_grid(
		options_sim, options_grid, options_box,
		grid, CType, Ref_max, Ref_min
	);
	if (rank == 0) {
		cout << "done" << endl;
	}

	for (const auto& cell: solar_wind_cells) {
		if (CType.data(*cell.data) != 0) {
			throw runtime_error(
				__FILE__"(" + to_string(__LINE__)
				+ ") Solar wind cell " + to_string(cell.id)
				+ " is of type " + to_string(CType.data(*cell.data))
			);
		}
		if (Ref_max.data(*cell.data) != 0)
			throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}
	for (int dir: {-3,-2,-1,+1,+2,+3}) {
		for (const auto& cell: face_cells(dir)) {
			if (CType.data(*cell.data) != 0) {
				throw runtime_error(
					__FILE__"(" + to_string(__LINE__)
					+ ") Face boundary cell " + to_string(cell.id)
					+ " is of type " + to_string(CType.data(*cell.data))
				);
			}
			if (Ref_max.data(*cell.data) != 0)
				throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
	}
	for (int dim: {0, 1, 2})
	for (int dir1: {-1, +1})
	for (int dir2: {-1, +1}) {
		for (const auto& cell: edge_cells(dim, dir1, dir2)) {
			if (CType.data(*cell.data) != 0) {
				throw runtime_error(
					__FILE__"(" + to_string(__LINE__)
					+ ") Edge boundary cell " + to_string(cell.id)
					+ " is of type " + to_string(CType.data(*cell.data))
				);
			}
			if (Ref_max.data(*cell.data) != 0)
				throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
		}
	}
	for (const auto& cell: vert_cells) {
		if (CType.data(*cell.data) != 0) {
			throw runtime_error(
				__FILE__"(" + to_string(__LINE__)
				+ ") Vertex boundary cell " + to_string(cell.id)
				+ " is of type " + to_string(CType.data(*cell.data))
			);
		}
		if (Ref_max.data(*cell.data) != 0)
			throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}
	for (const auto& cell: planet_cells) {
		if (CType.data(*cell.data) != 0) {
			throw runtime_error(
				__FILE__"(" + to_string(__LINE__)
				+ ") Planet boundary cell " + to_string(cell.id)
				+ " is of type " + to_string(CType.data(*cell.data))
			);
		}
		if (Ref_max.data(*cell.data) != grid.get_maximum_refinement_level())
			throw runtime_error(__FILE__"(" + to_string(__LINE__) + ")");
	}

	for (const auto& cell: grid.local_cells()) {
		(*cell.data)[pamhd::MPI_Rank()] = rank;
		Substep.data(*cell.data)     =
		Substep_Max.data(*cell.data) =
		Substep_Min.data(*cell.data) = 1;
		Max_v_part.data(*cell.data) =
		Max_ω_part.data(*cell.data) = -1;
	}
	Max_v_part.type().is_stale = true;
	Max_ω_part.type().is_stale = true;
	Substep.type().is_stale = true;
	Substep_Max.type().is_stale = true;
	Substep_Min.type().is_stale = true;

	/*
	Simulate
	*/

	const double time_end = options_sim.time_start + options_sim.time_length;
	double
		simulation_time = options_sim.time_start,
		next_particle_save = options_particle.save_n;

	if (rank == 0) {
		cout << "Initializing... " << endl;
	}

	std::mt19937_64 random_source;
	uint64_t next_particle_id = pamhd::particle::initialize_plasma(
		grid, simulation_time, options_sim, options_box,
		options_particle, random_source, Part_Int
	);

	uint64_t simulation_step = 0;
	next_particle_id = pamhd::particle::apply_boundaries_sw_box(
		next_particle_id,
		simulation_step,
		grid,
		simulation_time,
		options_sim,
		options_box,
		options_particle,
		solar_wind_cells,
		face_cells,
		edge_cells,
		vert_cells,
		planet_cells,
		random_source,
		Part_Int,
		CType
	);

	// final init with timestep of 0
	pamhd::particle::timestep_hyb(
		options_sim.time_start, grid, options_sim, Part_Int,
		Part_Pos, Part_Mas, Part_Mas_Cell, Part_SpM,
		Part_SpM_Cell, Part_Vel, Part_Vel_Cell, Part_Ekin,
		Nr_Particles, Part_Nr,
		Accu_List_Number_Of_Particles_Getter,
		Accu_List_Bulk_Mass_Getter,
		Accu_List_Bulk_Velocity_Getter,
		Accu_List_Bulk_Relative_Velocity2_Getter,
		Accu_List_Target_Getter,
		Accu_List_Length_Getter,
		Accu_List_Getter,
		pamhd::particle::Nr_Accumulated_To_Cells(),
		pamhd::particle::Accumulated_To_Cells(),
		pamhd::particle::Bulk_Velocity(), CType,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		options_sim.temp2nrj,
		Vol_J, Edge_J, Vert_J, Vert_E, Nr_Ext, Max_v_part, Max_ω_part,
		Part_Ext, Part_C2M, Part_Des, Face_dB, Bg_B,
		Substep, Substep_Min,
		Substep_Max, Face_B, background_B,
		Timestep, 0,
		options_particle.gyroperiod_time_step_factor, Vol_Qi, Vol_Ji, Vert_B
	);
	if (rank == 0) {
		cout << "done" << endl;
	}
	constexpr uint64_t file_version = 4;
	if (options_particle.save_n >= 0) {
		if (rank == 0) {
			cout << "Saving particles at time " << simulation_time << endl;
		}
		// update number of internal particles
		for (const auto& cell: grid.local_cells()) {
			Nr_Int(*cell.data) = Part_Int(*cell.data).size();
		}
		if (
			not pamhd::particle::save_hyb(
				boost::filesystem::canonical(
					boost::filesystem::path(options_sim.output_directory)
				).append("particle_").generic_string(),
				grid, file_version,
				simulation_step, simulation_time,
				options_sim.adiabatic_index,
				options_sim.proton_mass,
				options_sim.temp2nrj
			)
		) {
			cerr <<  __FILE__ << "(" << __LINE__ << "): "
				"Couldn't save particle result."
				<< endl;
			abort();
		}
	}

	while (simulation_time < time_end) {
		simulation_step++;

		// don't step over the final simulation time
		const double
			until_end = time_end - simulation_time,
			dt = pamhd::particle::timestep_hyb(
				simulation_time, grid, options_sim, Part_Int,
				Part_Pos, Part_Mas, Part_Mas_Cell, Part_SpM,
				Part_SpM_Cell, Part_Vel, Part_Vel_Cell, Part_Ekin,
				Nr_Particles, Part_Nr,
				Accu_List_Number_Of_Particles_Getter,
				Accu_List_Bulk_Mass_Getter,
				Accu_List_Bulk_Velocity_Getter,
				Accu_List_Bulk_Relative_Velocity2_Getter,
				Accu_List_Target_Getter,
				Accu_List_Length_Getter,
				Accu_List_Getter,
				pamhd::particle::Nr_Accumulated_To_Cells(),
				pamhd::particle::Accumulated_To_Cells(),
				pamhd::particle::Bulk_Velocity(), CType,
				options_sim.adiabatic_index,
				options_sim.vacuum_permeability,
				options_sim.temp2nrj,
				Vol_J, Edge_J, Vert_J, Vert_E, Nr_Ext, Max_v_part, Max_ω_part,
				Part_Ext, Part_C2M, Part_Des, Face_dB, Bg_B,
				Substep, Substep_Min,
				Substep_Max, Face_B, background_B,
				Timestep, until_end,
				options_particle.gyroperiod_time_step_factor, Vol_Qi, Vol_Ji, Vert_B
			);

		if (rank == 0) {
			cout << "Solution calculated at time " << simulation_time
				<< " s, timestep " << dt << " s" << flush;
		}

		simulation_time += dt;

		const auto avg_div = pamhd::math::get_divergence_face2volume(
			grid.local_cells(), grid,
			Face_B, Div_B, CType
		);
		if (rank == 0) {
			cout << ", average divergence " << avg_div;
		}

		const uint64_t splits_local = pamhd::particle::split_particles(
			options_particle.min_particles, random_source,
			grid, Part_Int, Part_Pos, Part_Mas, CType
		);
		uint64_t splits_global = 0;
		if (MPI_Reduce(
			&splits_local, &splits_global, 1,
			MPI_UINT64_T, MPI_SUM, 0, comm
		) != MPI_SUCCESS) {
			std::cerr << __FILE__ "(" << __LINE__
				<< "): Couldn't reduce max substep." << std::endl;
			abort();
		}
		if (rank == 0) {
			if (splits_global > 0) {
				cout << ", " << splits_global << " particle(s) split" << endl;
			} else {
				cout << endl;
			}
		}

		next_particle_id = pamhd::particle::apply_boundaries_sw_box(
			next_particle_id,
			simulation_step,
			grid,
			simulation_time,
			options_sim,
			options_box,
			options_particle,
			solar_wind_cells,
			face_cells,
			edge_cells,
			vert_cells,
			planet_cells,
			random_source,
			Part_Int,
			CType
		);

		if (
			(options_particle.save_n >= 0 and simulation_time >= time_end)
			or (options_particle.save_n > 0 and simulation_time >= next_particle_save)
		) {
			if (next_particle_save <= simulation_time) {
				next_particle_save
					+= options_particle.save_n
					* ceil(max(options_particle.save_n, simulation_time - next_particle_save) / options_particle.save_n);
			}

			if (rank == 0) {
				cout << "Saving particles at time " << simulation_time << "... " << endl;
			}

			// update number of internal particles
			for (const auto& cell: grid.local_cells()) {
				Nr_Int(*cell.data) = Part_Int(*cell.data).size();
			}
			constexpr uint64_t file_version = 4;
			if (
				not pamhd::particle::save_hyb(
					boost::filesystem::canonical(
						boost::filesystem::path(options_sim.output_directory)
					).append("particle_").generic_string(),
					grid,
					file_version,
					simulation_step,
					simulation_time,
					options_sim.adiabatic_index,
					options_sim.proton_mass,
					options_sim.temp2nrj
				)
			) {
				cerr <<  __FILE__ << "(" << __LINE__ << "): "
					"Couldn't save particle result."
					<< endl;
				abort();
			}
		}
	}

	if (rank == 0) {
		cout << "Simulation finished at time " << simulation_time << endl;
	}
	MPI_Finalize();

	return EXIT_SUCCESS;
}
