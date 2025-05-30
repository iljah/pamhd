/*
MHD test program of PAMHD.

Copyright 2014, 2015, 2016, 2017 Ilja Honkonen
Copyright 2018, 2019, 2022,
          2023, 2024, 2025 Finnish Meteorological Institute
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
#include "grid/amr.hpp"
#include "grid/options.hpp"
#include "grid/variables.hpp"
#include "math/nabla.hpp"
#include "mhd/amr.hpp"
#include "mhd/boundaries.hpp"
#include "mhd/common.hpp"
#include "mhd/hll_athena.hpp"
#include "mhd/hlld_athena.hpp"
#include "mhd/initialize.hpp"
#include "mhd/initialize_staggered.hpp"
#include "mhd/options.hpp"
#include "mhd/roe_athena.hpp"
#include "mhd/rusanov.hpp"
#include "mhd/save.hpp"
#include "mhd/solve.hpp"
#include "mhd/variables.hpp"
#include "simulation_options.hpp"
#include "variable_getter.hpp"
#include "common_variables.hpp"


// data stored in every cell of simulation grid
using Cell = pamhd::mhd::Cell;
using Grid = dccrg::Dccrg<
	Cell,
	dccrg::Cartesian_Geometry,
	std::tuple<pamhd::grid::Cell_Is_Local>,
	std::tuple<
		pamhd::grid::Face_Neighbor,
		pamhd::grid::Edge_Neighbor,
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

const auto Vol_B = pamhd::Variable_Getter<pamhd::Magnetic_Field>();
bool pamhd::Magnetic_Field::is_stale = true;

const auto Face_B = pamhd::Variable_Getter<pamhd::Face_Magnetic_Field>();
bool pamhd::Face_Magnetic_Field::is_stale = true;

const auto Face_dB = pamhd::Variable_Getter<pamhd::Face_dB>();

const auto Div_B = pamhd::Variable_Getter<pamhd::Magnetic_Field_Divergence>();

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

const auto Max_v = pamhd::Variable_Getter<pamhd::mhd::Max_Velocity>();
bool pamhd::mhd::Max_Velocity::is_stale = true;

const auto MHDF = pamhd::Variable_Getter<pamhd::mhd::MHD_Flux>();
const auto Mas_f = [](Cell& cell_data, const int dir)->auto& {
	return MHDF.data(cell_data)(dir)[pamhd::mhd::Mass_Density()];
};
const auto Mom_f = [](Cell& cell_data, const int dir)->auto& {
	return MHDF.data(cell_data)(dir)[pamhd::mhd::Momentum_Density()];
};
const auto Nrj_f = [](Cell& cell_data, const int dir)->auto& {
	return MHDF.data(cell_data)(dir)[pamhd::mhd::Total_Energy_Density()];
};
const auto Mag_f = [](Cell& cell_data, const int dir)->auto& {
	return MHDF.data(cell_data)(dir)[pamhd::Magnetic_Field()];
};

const auto FInfo = pamhd::Variable_Getter<pamhd::mhd::Face_Boundary_Type>();
bool pamhd::mhd::Face_Boundary_Type::is_stale = true;

const auto Ref_min = pamhd::Variable_Getter<pamhd::grid::Target_Refinement_Level_Min>();
bool pamhd::grid::Target_Refinement_Level_Min::is_stale = true;

const auto Ref_max = pamhd::Variable_Getter<pamhd::grid::Target_Refinement_Level_Max>();
bool pamhd::grid::Target_Refinement_Level_Max::is_stale = true;


int main(int argc, char* argv[]) {
	using std::ceil;
	using std::cerr;
	using std::cout;
	using std::endl;
	using std::flush;
	using std::max;
	using std::min;

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
	pamhd::mhd::Options options_mhd{document};

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

	using geometry_id_t = unsigned int;

	pamhd::boundaries::Geometries<
		geometry_id_t,
		std::array<double, 3>,
		double,
		uint64_t
	> geometries;
	geometries.set(document);

	pamhd::boundaries::Multivariable_Initial_Conditions<
		geometry_id_t,
		pamhd::mhd::Number_Density,
		pamhd::mhd::Velocity,
		pamhd::mhd::Pressure,
		pamhd::Magnetic_Field
	> initial_conditions_mhd;
	initial_conditions_mhd.set(document);

	pamhd::boundaries::Multivariable_Boundaries<
		uint64_t,
		geometry_id_t,
		pamhd::mhd::Number_Density,
		pamhd::mhd::Velocity,
		pamhd::mhd::Pressure,
		pamhd::Magnetic_Field
	> boundaries_mhd;
	boundaries_mhd.set(document);

	pamhd::Background_Magnetic_Field<
		double,
		pamhd::Magnetic_Field::data_type
	> background_B;
	background_B.set(document);

	const auto mhd_solver
		= [&options_mhd, &background_B, &rank](){
			if (options_mhd.solver == "rusanov") {
				return pamhd::mhd::Solver::rusanov;
			} else if (options_mhd.solver == "hll-athena") {
				return pamhd::mhd::Solver::hll_athena;
			} else if (options_mhd.solver == "hlld-athena") {
				if (background_B.exists() and rank == 0) {
					cout << "NOTE: background magnetic field ignored by hlld-athena solver." << endl;
				}
				return pamhd::mhd::Solver::hlld_athena;
			} else if (options_mhd.solver == "roe-athena") {
				if (background_B.exists() and rank == 0) {
					cout << "NOTE: background magnetic field ignored by roe-athena solver." << endl;
				}
				return pamhd::mhd::Solver::roe_athena;
			} else {
				cerr <<  __FILE__ << "(" << __LINE__ << "): "
					<< "Unsupported solver: " << options_mhd.solver
					<< endl;
				abort();
			}
		}();

	/*
	Initialize simulation grid
	*/
	const unsigned int neighborhood_size = 3;
	const auto& number_of_cells = options_grid.get_number_of_cells();
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
	pamhd::grid::set_minmax_refinement_level(
		grid.local_cells(), grid, options_grid,
		options_sim.time_start, Ref_min, Ref_max, false);
	pamhd::grid::adapt_grid(
		grid, Ref_min, Ref_max,
		pamhd::grid::New_Cells_Handler(Ref_min, Ref_max),
		pamhd::grid::Removed_Cells_Handler(Ref_min, Ref_max));
	Cell::set_transfer_all(true, Ref_min.type(), Ref_max.type());
	grid.balance_load();
	Cell::set_transfer_all(false, Ref_min.type(), Ref_max.type());
	if (rank == 0) {
		cout << "done" << endl;
	}

	for (const auto& cell: grid.local_cells()) {
		(*cell.data)[pamhd::MPI_Rank()] = rank;
		Substep.data(*cell.data) = 1;
		Max_v.data(*cell.data) = {-1, -1, -1, -1, -1, -1};
	}
	Max_v.type().is_stale = true;
	Substep.type().is_stale = true;

	// assign cells into boundary geometries
	for (const auto& gid: geometries.get_geometry_ids()) {
		geometries.clear_cells(gid);
	}
	for (const auto& cell: grid.local_cells()) {
		geometries.overlaps(
			grid.geometry.get_min(cell.id),
			grid.geometry.get_max(cell.id),
			cell.id);
	}

	/*
	Simulate
	*/

	const double time_end = options_sim.time_start + options_sim.time_length;
	double
		simulation_time = options_sim.time_start,
		next_mhd_save = options_mhd.save_n,
		next_amr = options_grid.amr_n;

	// initialize MHD
	if (rank == 0) {
		cout << "Initializing... " << endl;
	}
	pamhd::mhd::initialize_magnetic_field_staggered<pamhd::Magnetic_Field>(
		geometries, initial_conditions_mhd, background_B,
		grid, simulation_time, options_sim.vacuum_permeability,
		Face_B, Mag_f, Bg_B
	);

	pamhd::mhd::update_B_consistency(
		0, grid.local_cells(), grid,
		Mas, Mom, Nrj, Vol_B, Face_B,
		CType, Substep,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		false // fluid not initialized yet
	);

	pamhd::mhd::initialize_fluid_staggered(
		geometries, initial_conditions_mhd,
		grid, simulation_time,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		options_sim.proton_mass, true,
		Mas, Mom, Nrj, Vol_B,
		Mas_f, Mom_f, Nrj_f
	);

	// classify cells & faces into normal, boundary and dont_solve
	pamhd::mhd::set_solver_info(grid, boundaries_mhd, geometries, CType);
	pamhd::mhd::classify_faces(grid, CType, FInfo);

	pamhd::mhd::apply_boundaries(
		grid, geometries, boundaries_mhd,
		simulation_time, options_sim.proton_mass,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		Mas, Mom, Nrj, Vol_B,
		Face_B, CType, FInfo, Substep
	);

	// final init with timestep of 0
	pamhd::mhd::timestep(
		mhd_solver, grid, options_sim, options_sim.time_start,
		0, options_mhd.time_step_factor,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		Mas, Mom, Nrj, Vol_B, Face_B, Face_dB, Bg_B,
		Mas_f, Mom_f, Nrj_f, Mag_f, CType, Timestep,
		Substep, Substep_Min, Substep_Max, Max_v
	);
	if (rank == 0) {
		cout << "Initialization done" << endl;
	}

	size_t simulation_step = 0;
	constexpr uint64_t file_version = 4;
	if (options_mhd.save_n >= 0) {
		if (rank == 0) {
			cout << "Saving MHD at time " << simulation_time << endl;
		}
		if (
			not pamhd::mhd::save(
				boost::filesystem::canonical(
					boost::filesystem::path(options_sim.output_directory)
				).append("mhd_").generic_string(),
				grid,
				file_version,
				simulation_step,
				simulation_time,
				options_sim.adiabatic_index,
				options_sim.proton_mass,
				options_sim.vacuum_permeability
			)
		) {
			cerr <<  __FILE__ << "(" << __LINE__ << "): "
				"Couldn't save mhd result."
				<< endl;
			abort();
		}
	}

	while (simulation_time < time_end) {
		simulation_step++;

		const double
			// don't step over the final simulation time
			until_end = time_end - simulation_time,
			dt = pamhd::mhd::timestep(
				mhd_solver, grid, options_sim, simulation_time,
				until_end, options_mhd.time_step_factor,
				options_sim.adiabatic_index,
				options_sim.vacuum_permeability,
				Mas, Mom, Nrj, Vol_B, Face_B, Face_dB, Bg_B,
				Mas_f, Mom_f, Nrj_f, Mag_f, CType, Timestep,
				Substep, Substep_Min, Substep_Max, Max_v
			);
		if (rank == 0) {
			cout << "Solved MHD at time " << simulation_time
				<< " s with time step " << dt << " s" << flush;
		}
		simulation_time += dt;

		if (options_grid.amr_n > 0 and simulation_time >= next_amr) {
			if (rank == 0) {
				cout << "...\nAdapting grid at time " << simulation_time << "..." << flush;
			}
			next_amr
				+= options_grid.amr_n
				* ceil((simulation_time - next_amr) / options_grid.amr_n);
			pamhd::mhd::adapt_grid(
				grid, options_grid, options_mhd,
				geometries, boundaries_mhd, background_B,
				simulation_time, options_sim.proton_mass,
				options_sim.adiabatic_index,
				options_sim.vacuum_permeability,
				Mas, Mom, Nrj, Vol_B, Face_B, Bg_B,
				CType, FInfo, Ref_min, Ref_max,
				Substep, Max_v
			);
		}

		pamhd::mhd::apply_boundaries(
			grid, geometries, boundaries_mhd,
			simulation_time, options_sim.proton_mass,
			options_sim.adiabatic_index,
			options_sim.vacuum_permeability,
			Mas, Mom, Nrj, Vol_B,
			Face_B, CType, FInfo, Substep
		);

		const auto avg_div = pamhd::math::get_divergence_face2volume(
			grid.local_cells(), grid,
			Face_B, Div_B, CType
		);
		if (rank == 0) {
			cout << " average divergence " << avg_div << endl;
		}

		if (
			(options_mhd.save_n >= 0 and simulation_time >= time_end)
			or (options_mhd.save_n > 0 and simulation_time >= next_mhd_save)
		) {
			if (rank == 0) {
				cout << "Saving MHD at time " << simulation_time << endl;
			}
			if (next_mhd_save <= simulation_time) {
				next_mhd_save
					+= options_mhd.save_n
					* ceil(max(options_mhd.save_n, simulation_time - next_mhd_save) / options_mhd.save_n);
			}
			if (
				not pamhd::mhd::save(
					boost::filesystem::canonical(
						boost::filesystem::path(options_sim.output_directory)
					).append("mhd_").generic_string(),
					grid,
					file_version,
					simulation_step,
					simulation_time,
					options_sim.adiabatic_index,
					options_sim.proton_mass,
					options_sim.vacuum_permeability
				)
			) {
				cerr <<  __FILE__ << "(" << __LINE__ << "): "
					"Couldn't save mhd result."
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
