/*
Test program of PAMHD with one planet in solar wind.

Copyright 2014, 2015, 2016, 2017 Ilja Honkonen
Copyright 2018, 2019, 2022, 2023, 2024 Finnish Meteorological Institute
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
#include "boost/lexical_cast.hpp"
#include "dccrg.hpp"
#include "dccrg_cartesian_geometry.hpp"
#include "Eigen/Core" // must be included before gensimcell.hpp
#include "mpi.h" // must be included before gensimcell.hpp
#include "gensimcell.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

#include "background_magnetic_field.hpp"
#include "solar_wind_box_options.hpp"
#include "boundaries/geometries.hpp"
#include "boundaries/multivariable_boundaries.hpp"
#include "boundaries/multivariable_initial_conditions.hpp"
#include "grid/amr.hpp"
#include "grid/options.hpp"
#include "grid/variables.hpp"
#include "math/staggered.hpp"
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
#include "mhd/solve_staggered.hpp"
#include "mhd/variables.hpp"
#include "simulation_options.hpp"
#include "variables.hpp"


// data stored in every cell of simulation grid
using Cell = pamhd::mhd::Cell_Staggered;
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

// returns reference to background magnetic field on cell faces
const auto Bg_B = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::Bg_Magnetic_Field()];
};

// returns reference to total mass density in given cell
const auto Mas = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::MHD_State_Conservative()][pamhd::mhd::Mass_Density()];
};
const auto Mom = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::MHD_State_Conservative()][pamhd::mhd::Momentum_Density()];
};
const auto Nrj = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::MHD_State_Conservative()][pamhd::mhd::Total_Energy_Density()];
};
const auto Mag = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::MHD_State_Conservative()][pamhd::Magnetic_Field()];
};
const auto Face_B = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::Face_Magnetic_Field()];
};
const auto Face_dB = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::Face_dB()];
};
// divergence of magnetic field
const auto Mag_div = [](Cell& cell_data)->auto&{
	return cell_data[pamhd::Magnetic_Field_Divergence()];
};

// solver info variable for boundary logic
const auto Sol_Info = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::Solver_Info()];
};

const auto Substep = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::Substepping_Period()];
};
const auto Substep_Min = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::Substep_Min()];
};
const auto Substep_Max = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::Substep_Max()];
};

const auto Timestep = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::Timestep()];
};

const auto Max_v = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::mhd::Max_Velocity()];
};

// flux of mass density through positive x face of cell
const auto Mas_pfx = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](0, 1)[pamhd::mhd::Mass_Density()];
	};
const auto Mas_nfx = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](0, 0)[pamhd::mhd::Mass_Density()];
	};
const auto Mas_pfy = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](1, 1)[pamhd::mhd::Mass_Density()];
	};
const auto Mas_nfy = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](1, 0)[pamhd::mhd::Mass_Density()];
	};
const auto Mas_pfz = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](2, 1)[pamhd::mhd::Mass_Density()];
	};
const auto Mas_nfz = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](2, 0)[pamhd::mhd::Mass_Density()];
	};
const auto Mom_pfx = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](0, 1)[pamhd::mhd::Momentum_Density()];
	};
const auto Mom_nfx = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](0, 0)[pamhd::mhd::Momentum_Density()];
	};
const auto Mom_pfy = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](1, 1)[pamhd::mhd::Momentum_Density()];
	};
const auto Mom_nfy = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](1, 0)[pamhd::mhd::Momentum_Density()];
	};
const auto Mom_pfz = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](2, 1)[pamhd::mhd::Momentum_Density()];
	};
const auto Mom_nfz = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](2, 0)[pamhd::mhd::Momentum_Density()];
	};
const auto Nrj_pfx = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](0, 1)[pamhd::mhd::Total_Energy_Density()];
	};
const auto Nrj_nfx = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](0, 0)[pamhd::mhd::Total_Energy_Density()];
	};
const auto Nrj_pfy = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](1, 1)[pamhd::mhd::Total_Energy_Density()];
	};
const auto Nrj_nfy = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](1, 0)[pamhd::mhd::Total_Energy_Density()];
	};
const auto Nrj_pfz = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](2, 1)[pamhd::mhd::Total_Energy_Density()];
	};
const auto Nrj_nfz = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](2, 0)[pamhd::mhd::Total_Energy_Density()];
	};
const auto Mag_pfx = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](0, 1)[pamhd::Magnetic_Field()];
	};
const auto Mag_nfx = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](0, 0)[pamhd::Magnetic_Field()];
	};
const auto Mag_pfy = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](1, 1)[pamhd::Magnetic_Field()];
	};
const auto Mag_nfy = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](1, 0)[pamhd::Magnetic_Field()];
	};
const auto Mag_pfz = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](2, 1)[pamhd::Magnetic_Field()];
	};
const auto Mag_nfz = [](Cell& cell_data)->auto& {
		return cell_data[pamhd::mhd::MHD_Flux()](2, 0)[pamhd::Magnetic_Field()];
	};

// collections of above to shorten function arguments
const auto Mas_fs = std::make_tuple(
	Mas_nfx, Mas_pfx, Mas_nfy, Mas_pfy, Mas_nfz, Mas_pfz
);
const auto Mom_fs = std::make_tuple(
	Mom_nfx, Mom_pfx, Mom_nfy, Mom_pfy, Mom_nfz, Mom_pfz
);
const auto Nrj_fs = std::make_tuple(
	Nrj_nfx, Nrj_pfx, Nrj_nfy, Nrj_pfy, Nrj_nfz, Nrj_pfz
);
const auto Mag_fs = std::make_tuple(
	Mag_nfx, Mag_pfx, Mag_nfy, Mag_pfy, Mag_nfz, Mag_pfz
);

const auto Ref_min = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::grid::Target_Refinement_Level_Min()];
};
const auto Ref_max = [](Cell& cell_data)->auto& {
	return cell_data[pamhd::grid::Target_Refinement_Level_Max()];
};


template <
	class JSON
> std::tuple<
	double, double,
	std::array<double, 3>,
	std::array<double, 3>
> get_sw(
	const JSON& json,
	const double& /*sim_time*/,
	const double& proton_mass
) {
	using std::string;
	using std::invalid_argument;
	using std::to_string;

	if (not json.HasMember("number-density")) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "JSON data doesn't have a number-density key."
		);
	}
	const auto& mass_json = json["number-density"];
	if (not mass_json.IsNumber()) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "JSON item number-density is not a number."
		);
	}
	const double mass = proton_mass * json["number-density"].GetDouble();

	if (not json.HasMember("pressure")) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "JSON data doesn't have a pressure key."
		);
	}
	const auto& pressure_json = json["pressure"];
	if (not pressure_json.IsNumber()) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "JSON item pressure is not a number."
		);
	}
	const double pressure = json["pressure"].GetDouble();

	if (not json.HasMember("velocity")) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "JSON data doesn't have a velocity key."
		);
	}
	const auto& velocity_json = json["velocity"];
	if (not velocity_json.IsArray()) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "JSON item velocity is not an array."
		);
	}
	const auto& velocity_array = velocity_json.GetArray();
	if (velocity_array.Size() != 3) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "Invalid size for velocity, should be 3"
		);
	}
	const std::array<double, 3> velocity{
		velocity_array[0].GetDouble(),
		velocity_array[1].GetDouble(),
		velocity_array[2].GetDouble()
	};

	if (not json.HasMember("magnetic-field")) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "JSON data doesn't have a magnetic field key."
		);
	}
	const auto& magnetic_field_json = json["magnetic-field"];
	if (not magnetic_field_json.IsArray()) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "JSON item magnetic field is not an array."
		);
	}
	const auto& magnetic_field_array = magnetic_field_json.GetArray();
	if (magnetic_field_array.Size() != 3) {
		throw invalid_argument(
			string(__FILE__ "(") + to_string(__LINE__) + "): "
			+ "Invalid size for magnetic field, should be 3"
		);
	}
	const std::array<double, 3> magnetic_field{
		magnetic_field_array[0].GetDouble(),
		magnetic_field_array[1].GetDouble(),
		magnetic_field_array[2].GetDouble()
	};

	return std::make_tuple(mass, pressure, velocity, magnetic_field);
}


template<
	class Grid,
	class JSON,
	class BG_b
> void apply_initial_condition(
	const double& inner_bdy_radius,
	Grid& grid,
	const double& sim_time,
	const JSON& json,
	const BG_b& bg_B,
	const double& adiabatic_index,
	const double& vacuum_permeability,
	const double& proton_mass
) try {
	using std::string;
	using std::invalid_argument;
	using std::to_string;

	const auto [
		mass, pressure, velocity, magnetic_field
	] = get_sw(
		json, sim_time, proton_mass
	);
	if (grid.get_rank() == 0) {
		std::cout << "Initializing run, solar wind: "
		<< mass/proton_mass << " #/m^3, "
		<< velocity << " m/s, "
		<< pressure << " Pa, "
		<< magnetic_field << " T"
		<< std::endl;
	}

	for (const auto& cell: grid.local_cells()) {
		Sol_Info(*cell.data) = 1;

		const auto [rx, ry, rz] = grid.geometry.get_center(cell.id);
		const auto [sx, sy, sz] = grid.geometry.get_min(cell.id);
		const auto [ex, ey, ez] = grid.geometry.get_max(cell.id);

		Bg_B(*cell.data)(-1) = bg_B.get_background_field(
			{sx, ry, rz},
			vacuum_permeability
		);
		Bg_B(*cell.data)(+1) = bg_B.get_background_field(
			{ex, ry, rz},
			vacuum_permeability
		);
		Bg_B(*cell.data)(-2) = bg_B.get_background_field(
			{rx, sy, rz},
			vacuum_permeability
		);
		Bg_B(*cell.data)(+2) = bg_B.get_background_field(
			{rx, ey, rz},
			vacuum_permeability
		);
		Bg_B(*cell.data)(-3) = bg_B.get_background_field(
			{rx, ry, sz},
			vacuum_permeability
		);
		Bg_B(*cell.data)(+3) = bg_B.get_background_field(
			{rx, ry, ez},
			vacuum_permeability
		);

		Mas(*cell.data) = mass;
		Mom(*cell.data) =
		Mag(*cell.data) = {0, 0, 0};
		Face_B(*cell.data)(-1) =
		Face_B(*cell.data)(+1) =
		Face_B(*cell.data)(-2) =
		Face_B(*cell.data)(+2) =
		Face_B(*cell.data)(-3) =
		Face_B(*cell.data)(+3) = 0;
		Nrj(*cell.data) = pamhd::mhd::get_total_energy_density(
			Mas(*cell.data),
			Mom(*cell.data),
			pressure,
			Mag(*cell.data),
			adiabatic_index,
			vacuum_permeability
		);

		const auto r = std::sqrt(rx*rx + ry*ry + rz*rz);
		if (r < inner_bdy_radius) {
			Mas(*cell.data) = proton_mass * 1e9;
			Nrj(*cell.data) = pamhd::mhd::get_total_energy_density(
				Mas(*cell.data),
				Mom(*cell.data),
				1e-11,
				Mag(*cell.data),
				adiabatic_index,
				vacuum_permeability
			);
		}
	}
} catch (const std::exception& e) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + "): " + e.what());
} catch (...) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + ")");
}


//! Returns std::vector<> of subset of cells from:
// for (const auto& cell: grid.local_cells()) {...}
template<
	class Grid
> auto get_solar_wind_cells(
	int direction,
	const Grid& grid
) try {
	using std::invalid_argument;
	using std::to_string;

	if (direction == 0) throw invalid_argument(
		__FILE__ "(" + to_string(__LINE__) + "): direction = 0"
	);
	const size_t dim = std::abs(direction) - 1;
	if (dim > 2) throw invalid_argument(
		__FILE__ "(" + to_string(__LINE__) + "): |direction| - 1 > 2"
	);

	const auto len0 = grid.length.get();
	const auto mrlvl = grid.get_maximum_refinement_level();
	std::array<uint64_t, 3> end_indices{
		len0[0] * (uint64_t(1) << mrlvl),
		len0[1] * (uint64_t(1) << mrlvl),
		len0[2] * (uint64_t(1) << mrlvl)
	};

	std::vector<
		std::remove_cv_t<std::remove_reference_t<
			decltype(grid.cells.front())>>
	> sw_cells;
	for (const auto& cell: grid.local_cells()) {
		const auto indices = grid.mapping.get_indices(cell.id);
		const auto len = grid.mapping.get_cell_length_in_indices(cell.id);

		if (direction < 0) {
			if (indices[dim] == 0) {
				sw_cells.push_back(cell);
			}
		} else {
			if (indices[dim] == end_indices[dim] - len) {
				sw_cells.push_back(cell);
			}
		}
	}

	return sw_cells;
} catch (const std::exception& e) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + "): " + e.what());
} catch (...) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + ")");
}


/*! Returns outflow cells

of subset of cells from:
for (const auto& cell: grid.local_cells()) {...}
*/
template<
	class Grid
> auto get_outflow_cells(
	const Grid& grid
) try {
	using std::cout;
	using std::endl;
	using std::string;
	using std::invalid_argument;
	using std::to_string;

	const auto len0 = grid.length.get();
	const auto mrlvl = grid.get_maximum_refinement_level();
	const std::array<uint64_t, 3> end_indices{
		len0[0] * (uint64_t(1) << mrlvl),
		len0[1] * (uint64_t(1) << mrlvl),
		len0[2] * (uint64_t(1) << mrlvl)
	};

	using cell_list_t = std::vector<
		std::remove_cv_t<std::remove_reference_t<
			decltype(grid.cells.front())>>
	>;
	// boundary and normal cell share face
	pamhd::grid::Face_Type<cell_list_t> face_bdy;
	// boundary and normal cell share edge
	pamhd::grid::Edge_Type<cell_list_t> edge_bdy;
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

	for (int dir: {-3,-2,-1,+1,+2,+3}) {
		for (const auto& cell: face_bdy(dir)) {
			Sol_Info(*cell.data) = 0;
		}
	}
	for (int dim: {0, 1, 2})
	for (int dir1: {-1, +1})
	for (int dir2: {-1, +1}) {
		for (const auto& cell: edge_bdy(dim, dir1, dir2)) {
			Sol_Info(*cell.data) = 0;
		}
	}
	for (const auto& cell: vert_bdy) {
		Sol_Info(*cell.data) = 0;
	}
	return std::make_tuple(face_bdy, edge_bdy, vert_bdy);

} catch (const std::exception& e) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + "): " + e.what());
} catch (...) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + ")");
}


/*! Returns planetary boundary cells

Transfer of solver info between processes must be switched on.
*/
template<
	class Grid
> auto get_planet_cells(
	const double& inner_bdy_radius,
	Grid& grid
) try {
	std::set<uint64_t> planet_candidates;
	for (const auto& cell: grid.local_cells()) {
		const auto [rx, ry, rz] = grid.geometry.get_center(cell.id);
		const auto r = std::sqrt(rx*rx + ry*ry + rz*rz);
		if (r < inner_bdy_radius) {
			planet_candidates.insert(cell.id);
			Sol_Info(*cell.data) = 0;
		}
	}
	grid.update_copies_of_remote_neighbors();

	std::vector<
		std::remove_cv_t<std::remove_reference_t<
			decltype(grid.cells.front())>>
	> planet_cells;
	for (const auto& cell: grid.local_cells()) {
		if (planet_candidates.count(cell.id) == 0) continue;
		// include only outer layer of planet boundary cells
		bool have_normal = false;
		for (const auto& neighbor: cell.neighbors_of) {
			if (Sol_Info(*neighbor.data) == 1) {
				have_normal = true;
				break;
			}
		}
		if (not have_normal) {
			Sol_Info(*cell.data) = -1;
		} else {
			planet_cells.push_back(cell);
		}
	}
	grid.update_copies_of_remote_neighbors();
	return planet_cells;

} catch (const std::exception& e) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + "): " + e.what());
} catch (...) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + ")");
}


template<
	class JSON,
	class Cells
> void apply_solar_wind_boundaries(
	const JSON& json,
	const Cells& solar_wind_cells,
	const int& solar_wind_dir,
	const double& sim_time,
	const double& adiabatic_index,
	const double& vacuum_permeability,
	const double& proton_mass
) try {
	const auto [
		mass, pressure, velocity, magnetic_field
	] = get_sw(
		json, sim_time, proton_mass
	);
	for (const auto& cell: solar_wind_cells) {
		Mas(*cell.data) = mass;
		Mom(*cell.data) = {
			mass*velocity[0],
			mass*velocity[1],
			mass*velocity[2]
		};
		// prevent div(B) at solar wind boundary
		if (solar_wind_dir != abs(1)) {
			Mag(*cell.data)[0]     =
			Face_B(*cell.data)(-1) =
			Face_B(*cell.data)(+1) = magnetic_field[0];
		}
		if (solar_wind_dir != abs(2)) {
			Mag(*cell.data)[1]     =
			Face_B(*cell.data)(-2) =
			Face_B(*cell.data)(+2) = magnetic_field[1];
		}
		if (solar_wind_dir != abs(3)) {
			Mag(*cell.data)[2]     =
			Face_B(*cell.data)(-3) =
			Face_B(*cell.data)(+3) = magnetic_field[2];
		}
		Nrj(*cell.data) = pamhd::mhd::get_total_energy_density(
			mass, velocity, pressure, magnetic_field,
			adiabatic_index, vacuum_permeability
		);
	}
} catch (const std::exception& e) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + "): " + e.what());
} catch (...) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + ")");
}


template<
	class Grid,
	class JSON,
	class SW_Cells,
	class Face_Cells,
	class Edge_Cells,
	class Vert_Cells,
	class Planet_Cells
> void apply_boundaries(
	Grid& grid,
	const double& sim_time,
	const JSON& json,
	const int& solar_wind_dir,
	const SW_Cells& solar_wind_cells,
	const Face_Cells& face_cells,
	const Edge_Cells& edge_cells,
	const Vert_Cells& vert_cells,
	const Planet_Cells& planet_cells,
	const double& adiabatic_index,
	const double& vacuum_permeability,
	const double& proton_mass
) try {
	// boundary and normal cell share face
	for (int dir: {-3,-2,-1,+1,+2,+3}) {
		for (const auto& cell: face_cells(dir)) {
			for (const auto& neighbor: cell.neighbors_of) {
				const auto& fn = neighbor.face_neighbor;
				if (fn != -dir) continue;
				Mas(*cell.data) = Mas(*neighbor.data);
				Mom(*cell.data) = Mom(*neighbor.data);
				Nrj(*cell.data) = Nrj(*neighbor.data);
				Mag(*cell.data) = Mag(*neighbor.data);
				for (int dir2: {-3,-2,-1,+1,+2,+3}) {
					if (dir2 == fn or dir2 == -fn) {
						Face_B(*cell.data)(dir2) = Face_B(*neighbor.data)(-fn);
					} else {
						Face_B(*cell.data)(dir2) = Face_B(*neighbor.data)(dir2);
					}
				}
			}
		}
	}

	// boundary and normal cell share edge
	for (int dim: {0, 1, 2})
	for (int dir1: {-1, +1})
	for (int dir2: {-1, +1}) {
		for (const auto& cell: edge_cells(dim, dir1, dir2)) {
			for (const auto& neighbor: cell.neighbors_of) {
				const auto& en = neighbor.edge_neighbor;
				if (en[0] != dim or en[1] != -dir1 or en[2] != -dir2) continue;
				Mas(*cell.data) = Mas(*neighbor.data);
				Mom(*cell.data) = Mom(*neighbor.data);
				Nrj(*cell.data) = Nrj(*neighbor.data);
				Mag(*cell.data) = Mag(*neighbor.data);
				if (dim == 0) {
					Face_B(*cell.data)(-1) = Face_B(*neighbor.data)(-1);
					Face_B(*cell.data)(+1) = Face_B(*neighbor.data)(+1);
					if (dir1 < 0) {
						Face_B(*cell.data)(-2) =
						Face_B(*cell.data)(+2) = Face_B(*neighbor.data)(-2);
					} else {
						Face_B(*cell.data)(-2) =
						Face_B(*cell.data)(+2) = Face_B(*neighbor.data)(+2);
					}
					if (dir2 < 0) {
						Face_B(*cell.data)(-3) =
						Face_B(*cell.data)(+3) = Face_B(*neighbor.data)(-3);
					} else {
						Face_B(*cell.data)(-3) =
						Face_B(*cell.data)(+3) = Face_B(*neighbor.data)(+3);
					}
				}
				if (dim == 1) {
					Face_B(*cell.data)(-2) = Face_B(*neighbor.data)(-2);
					Face_B(*cell.data)(+2) = Face_B(*neighbor.data)(+2);
					if (dir1 < 0) {
						Face_B(*cell.data)(-1) =
						Face_B(*cell.data)(+1) = Face_B(*neighbor.data)(-1);
					} else {
						Face_B(*cell.data)(-1) =
						Face_B(*cell.data)(+1) = Face_B(*neighbor.data)(+1);
					}
					if (dir2 < 0) {
						Face_B(*cell.data)(-3) =
						Face_B(*cell.data)(+3) = Face_B(*neighbor.data)(-3);
					} else {
						Face_B(*cell.data)(-3) =
						Face_B(*cell.data)(+3) = Face_B(*neighbor.data)(+3);
					}
				}
				if (dim == 2) {
					Face_B(*cell.data)(-3) = Face_B(*neighbor.data)(-3);
					Face_B(*cell.data)(+3) = Face_B(*neighbor.data)(+3);
					if (dir1 < 0) {
						Face_B(*cell.data)(-1) =
						Face_B(*cell.data)(+1) = Face_B(*neighbor.data)(-1);
					} else {
						Face_B(*cell.data)(-1) =
						Face_B(*cell.data)(+1) = Face_B(*neighbor.data)(+1);
					}
					if (dir2 < 0) {
						Face_B(*cell.data)(-2) =
						Face_B(*cell.data)(+2) = Face_B(*neighbor.data)(-2);
					} else {
						Face_B(*cell.data)(-2) =
						Face_B(*cell.data)(+2) = Face_B(*neighbor.data)(+2);
					}
				}
			}
		}
	}

	// boundary and normal cell share vertex
	for (const auto& cell: vert_cells) {
		for (const auto& neighbor: cell.neighbors_of) {
			const auto& fn = neighbor.face_neighbor;
			if (fn != 0) continue;
			const auto& en = neighbor.edge_neighbor;
			if (en[0] >= 0) continue;

			Mas(*cell.data) = Mas(*neighbor.data);
			Mom(*cell.data) = Mom(*neighbor.data);
			Nrj(*cell.data) = Nrj(*neighbor.data);
			Mag(*cell.data) = Mag(*neighbor.data);
			if (neighbor.x < 0) {
				Face_B(*cell.data)(-1) =
				Face_B(*cell.data)(+1) = Face_B(*neighbor.data)(+1);
			} else {
				Face_B(*cell.data)(-1) =
				Face_B(*cell.data)(+1) = Face_B(*neighbor.data)(-1);
			}
			if (neighbor.y < 0) {
				Face_B(*cell.data)(-2) =
				Face_B(*cell.data)(+2) = Face_B(*neighbor.data)(+2);
			} else {
				Face_B(*cell.data)(-2) =
				Face_B(*cell.data)(+2) = Face_B(*neighbor.data)(-2);
			}
			if (neighbor.z < 0) {
				Face_B(*cell.data)(-3) =
				Face_B(*cell.data)(+3) = Face_B(*neighbor.data)(+3);
			} else {
				Face_B(*cell.data)(-3) =
				Face_B(*cell.data)(+3) = Face_B(*neighbor.data)(-3);
			}
		}
	}

	// planetary boundary cells
	for (const auto& cell: planet_cells) {
		Mas(*cell.data) = proton_mass * 1e9;
		Mom(*cell.data) = {0, 0, 0};
		Mag(*cell.data) = {0, 0, 0};
		Nrj(*cell.data) = pamhd::mhd::get_total_energy_density(
			Mas(*cell.data),
			Mom(*cell.data),
			1e-11,
			Mag(*cell.data),
			adiabatic_index,
			vacuum_permeability
		);
		Face_B(*cell.data)(-1) =
		Face_B(*cell.data)(+1) =
		Face_B(*cell.data)(-2) =
		Face_B(*cell.data)(+2) =
		Face_B(*cell.data)(-3) =
		Face_B(*cell.data)(+3) = 0;

		pamhd::grid::Face_Type<bool> have_value{false, false, false, false, false, false};
		// corrections to Face_B from normal neighbors
		for (const auto& neighbor: cell.neighbors_of) {
			if (Sol_Info(*neighbor.data) < 1) continue;

			const auto& fn = neighbor.face_neighbor;
			const auto& en = neighbor.edge_neighbor;

			if (fn != 0) {
				Face_B(*cell.data)(fn) = Face_B(*neighbor.data)(-fn);
				have_value(fn) = true;
			} else if (en[0] >= 0) {
				// TODO
			} else {
				// TODO
			}
		}
		for (int dir: {-3,-2,-1,+1,+2,+3}) {
			if (not have_value(dir)) {
				if (have_value(-dir)) {
					Face_B(*cell.data)(dir) = Face_B(*cell.data)(-dir);
				} else {
				}
			}
		}
		Mag(*cell.data) = {
			0.5*(Face_B(*cell.data)(-1) + Face_B(*cell.data)(+1)),
			0.5*(Face_B(*cell.data)(-2) + Face_B(*cell.data)(+2)),
			0.5*(Face_B(*cell.data)(-3) + Face_B(*cell.data)(+3))
		};
		Nrj(*cell.data) = pamhd::mhd::get_total_energy_density(
			Mas(*cell.data),
			Mom(*cell.data),
			1e-11,
			Mag(*cell.data),
			adiabatic_index,
			vacuum_permeability
		);
	}

	apply_solar_wind_boundaries(
		json, solar_wind_cells,
		solar_wind_dir, sim_time,
		adiabatic_index,
		vacuum_permeability,
		proton_mass
	);
	grid.update_copies_of_remote_neighbors();

} catch (const std::exception& e) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + "): " + e.what());
} catch (...) {
	throw std::runtime_error(__FILE__ "(" + std::to_string(__LINE__) + ")");
}


int main(int argc, char* argv[]) {
	using std::abs;
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
	pamhd::Solar_Wind_Box_Options options_sw{document};
	const int solar_wind_dir = [&options_sw](){
		if (options_sw.sw_dir == "-x") return -1;
		else if (options_sw.sw_dir == "+x") return +1;
		else if (options_sw.sw_dir == "-y") return -2;
		else if (options_sw.sw_dir == "+y") return +2;
		else if (options_sw.sw_dir == "-z") return -3;
		else if (options_sw.sw_dir == "+z") return +3;
		else {
			std::cerr << "Invalid solar wind boundary direction: "
				<< options_sw.sw_dir << std::endl;
			MPI_Finalize();
			return EXIT_FAILURE;
		}
	}();

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
	std::array<bool, 3> periodic;
	for (size_t dim: {0, 1, 2}) {
		if (number_of_cells[dim] <= 2) {
			periodic[dim] = true;
		} else {
			periodic[dim] = false;
		}
	}
	if (abs(solar_wind_dir) == 1) {
		periodic[0] = false;
	} else if (abs(solar_wind_dir) == 2) {
		periodic[1] = false;
	} else if (abs(solar_wind_dir) == 3) {
		periodic[2] = false;
	}

	Grid grid; grid
		.set_initial_length(number_of_cells)
		.set_neighborhood_length(neighborhood_size)
		.set_periodic(periodic[0], periodic[1], periodic[2])
		.set_load_balancing_method(options_sim.lb_name)
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
		options_sim.time_start, Ref_min, Ref_max);
	pamhd::grid::adapt_grid(
		grid, Ref_min, Ref_max,
		pamhd::grid::New_Cells_Handler(Ref_min, Ref_max),
		pamhd::grid::Removed_Cells_Handler(Ref_min, Ref_max));

	Cell::set_transfer_all(true,
		pamhd::grid::Target_Refinement_Level_Min(),
		pamhd::grid::Target_Refinement_Level_Max());
	grid.balance_load();
	Cell::set_transfer_all(false,
		pamhd::grid::Target_Refinement_Level_Min(),
		pamhd::grid::Target_Refinement_Level_Max());
	if (rank == 0) {
		cout << "done" << endl;
	}

	for (const auto& cell: grid.local_cells()) {
		(*cell.data)[pamhd::MPI_Rank()] = rank;
		Substep(*cell.data) = 1;
		Max_v(*cell.data) = {-1, -1, -1, -1, -1, -1};
	}
	pamhd::mhd::set_minmax_substepping_period(
		options_sim.time_start, grid,
		options_mhd, Substep_Min, Substep_Max);
	Cell::set_transfer_all(true,
		pamhd::mhd::Max_Velocity(),
		pamhd::mhd::Substepping_Period(),
		pamhd::mhd::Substep_Min(),
		pamhd::mhd::Substep_Max(),
		pamhd::MPI_Rank()
	);
	grid.update_copies_of_remote_neighbors();
	Cell::set_transfer_all(false,
		pamhd::mhd::Max_Velocity(),
		pamhd::mhd::Substepping_Period(),
		pamhd::mhd::Substep_Min(),
		pamhd::mhd::Substep_Max(),
		pamhd::MPI_Rank()
	);

	/*
	Simulate
	*/

	const double time_end = options_sim.time_start + options_sim.time_length;
	double
		simulation_time = options_sim.time_start,
		next_mhd_save = options_mhd.save_n;

	// initialize MHD
	if (rank == 0) {
		cout << "Initializing MHD... " << endl;
	}

	Cell::set_transfer_all(true,
		pamhd::Face_Magnetic_Field(),
		pamhd::mhd::MHD_State_Conservative(),
		pamhd::Bg_Magnetic_Field(),
		pamhd::mhd::Solver_Info()
	);
	apply_initial_condition(
		options_sw.inner_bdy_radius,
		grid, simulation_time,
		document, background_B,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		options_sim.proton_mass
	);
	grid.update_copies_of_remote_neighbors();
	pamhd::mhd::update_B_consistency(
		0, grid.local_cells(),
		Mas, Mom, Nrj, Mag, Face_B,
		Sol_Info, Substep,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		true
	);
	grid.update_copies_of_remote_neighbors();

	const auto solar_wind_cells
		= get_solar_wind_cells(solar_wind_dir, grid);
	const auto [face_cells, edge_cells, vert_cells]
		= get_outflow_cells(grid);
	const auto planet_cells
		= get_planet_cells(options_sw.inner_bdy_radius, grid);

	apply_boundaries(
		grid, simulation_time, document,
		solar_wind_dir, solar_wind_cells,
		face_cells, edge_cells, vert_cells,
		planet_cells,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		options_sim.proton_mass
	);
	grid.update_copies_of_remote_neighbors();

	pamhd::mhd::update_B_consistency(
		0, grid.local_cells(),
		Mas, Mom, Nrj, Mag, Face_B,
		Sol_Info, Substep,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		true
	);
	grid.update_copies_of_remote_neighbors();
	Cell::set_transfer_all(false,
		pamhd::Face_Magnetic_Field(),
		pamhd::mhd::MHD_State_Conservative(),
		pamhd::Bg_Magnetic_Field(),
		pamhd::mhd::Solver_Info()
	);

	if (rank == 0) {
		cout << "Done initializing MHD" << endl;
	}

	// final init with timestep of 0
	pamhd::mhd::timestep(
		mhd_solver, grid, options_mhd, options_sim.time_start,
		0, options_mhd.time_step_factor,
		options_sim.adiabatic_index,
		options_sim.vacuum_permeability,
		Mas, Mom, Nrj, Mag, Face_B, Face_dB, Bg_B,
		Mas_fs, Mom_fs, Nrj_fs, Mag_fs, Sol_Info,
		Timestep, Substep, Substep_Min, Substep_Max, Max_v
	);
	size_t simulation_step = 0;
	constexpr uint64_t file_version = 3;
	if (options_mhd.save_n >= 0) {
		if (rank == 0) {
			cout << "Saving MHD at time " << simulation_time << endl;
		}
		if (
			not pamhd::mhd::save_staggered(
				boost::filesystem::canonical(
					boost::filesystem::path(options_sim.output_directory)
				).append("mhd_staggered_").generic_string(),
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

		// don't step over the final simulation time
		double until_end = time_end - simulation_time;
		const double dt = pamhd::mhd::timestep(
			mhd_solver, grid, options_mhd, simulation_time,
			until_end, options_mhd.time_step_factor,
			options_sim.adiabatic_index,
			options_sim.vacuum_permeability,
			Mas, Mom, Nrj, Mag, Face_B, Face_dB, Bg_B,
			Mas_fs, Mom_fs, Nrj_fs, Mag_fs, Sol_Info,
			Timestep, Substep, Substep_Min, Substep_Max, Max_v
		);
		if (rank == 0) {
			cout << "Solved MHD at time " << simulation_time
				<< " s with time step " << dt << " s" << flush;
		}
		simulation_time += dt;

		const auto avg_div = pamhd::math::get_divergence_staggered(
			grid.local_cells(), grid,
			Face_B, Mag_div, Sol_Info
		);
		if (rank == 0) {
			cout << " average divergence " << avg_div << endl;
		}
		Cell::set_transfer_all(true, pamhd::Magnetic_Field_Divergence());
		grid.update_copies_of_remote_neighbors();
		Cell::set_transfer_all(false, pamhd::Magnetic_Field_Divergence());

		Cell::set_transfer_all(true,
			pamhd::Face_Magnetic_Field(),
			pamhd::mhd::MHD_State_Conservative(),
			pamhd::Bg_Magnetic_Field(),
			pamhd::mhd::Solver_Info()
		);
		apply_boundaries(
			grid, simulation_time, document,
			solar_wind_dir, solar_wind_cells,
			face_cells, edge_cells, vert_cells,
			planet_cells,
			options_sim.adiabatic_index,
			options_sim.vacuum_permeability,
			options_sim.proton_mass
		);
		grid.update_copies_of_remote_neighbors();
		pamhd::mhd::update_B_consistency(
			0, grid.local_cells(),
			Mas, Mom, Nrj, Mag, Face_B,
			Sol_Info, Substep,
			options_sim.adiabatic_index,
			options_sim.vacuum_permeability,
			true
		);
		grid.update_copies_of_remote_neighbors();
		Cell::set_transfer_all(false,
			pamhd::Face_Magnetic_Field(),
			pamhd::mhd::MHD_State_Conservative(),
			pamhd::Bg_Magnetic_Field(),
			pamhd::mhd::Solver_Info()
		);

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
				not pamhd::mhd::save_staggered(
					boost::filesystem::canonical(
						boost::filesystem::path(options_sim.output_directory)
					).append("mhd_staggered_").generic_string(),
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
