/*
Program for plotting MHD output of PAMHD with gnuplot.

Copyright 2014 Ilja Honkonen
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
*/

#include "array"
#include "boost/lexical_cast.hpp"
#include "boost/optional.hpp"
#include "cstdint"
#include "cstdlib"
#include "Eigen/Core" // must be included before gensimcell
#include "fstream"
#include "mpi.h"
#include "string"
#include "tuple"
#include "unordered_map"
#include "vector"

#include "dccrg_cartesian_geometry.hpp"
#include "dccrg_mapping.hpp"
#include "dccrg_topology.hpp"
#include "gensimcell.hpp"

#include "mhd/common.hpp"
#include "mhd/save.hpp"
#include "mhd/variables.hpp"

using namespace std;
using namespace pamhd::mhd;

/*
Read data of the entire file with given name.

Fills out grid info and simulation data.

On success returns whether fluxes were saved into given file,
on failure returns an uninitialized value.
*/
boost::optional<
	std::tuple<
		bool,
		std::array<double, 3>
	>
> read_data(
	dccrg::Mapping& mapping,
	dccrg::Grid_Topology& topology,
	dccrg::Cartesian_Geometry& geometry,
	unordered_map<uint64_t, Cell>& simulation_data,
	const std::string& file_name,
	const int mpi_rank
) {
	MPI_File file;
	if (
		MPI_File_open(
			MPI_COMM_SELF,
			const_cast<char*>(file_name.c_str()),
			MPI_MODE_RDONLY,
			MPI_INFO_NULL,
			&file
		) != MPI_SUCCESS
	) {
		cerr << "Process " << mpi_rank
			<< " couldn't open file " << file_name
			<< endl;
		return boost::optional<std::tuple<bool, std::array<double, 3>>>();
	}

	MPI_Offset offset = 0;

	// check whether fluxes were saved
	std::string header_data(Save::get_header_string_template() + "x\n");
	MPI_File_read_at(
		file,
		offset,
		const_cast<void*>(static_cast<const void*>(header_data.data())),
		Save::get_header_string_size(),
		MPI_BYTE,
		MPI_STATUS_IGNORE
	);
	offset += Save::get_header_string_size();

	bool have_fluxes;
	if (header_data == Save::get_header_string_template() + "y\n")  {
		have_fluxes = true;
	} else if (header_data == Save::get_header_string_template() + "n\n") {
		have_fluxes = false;
	} else {
		cerr << "Process " << mpi_rank
			<< " Invalid header in file " << file_name
			<< ": " << header_data
			<< endl;
		return boost::optional<std::tuple<bool, std::array<double, 3>>>();
	}

	// read physical constants
	std::array<double, Save::nr_header_doubles> phys_consts;
	MPI_File_read_at(
		file,
		offset,
		phys_consts.data(),
		Save::nr_header_doubles,
		MPI_DOUBLE,
		MPI_STATUS_IGNORE
	);
	offset += Save::nr_header_doubles * sizeof(double);


	Cell::set_transfer_all(true, MHD_State_Conservative());
	if (have_fluxes) {
		Cell::set_transfer_all(true, MHD_Flux_Conservative());
	}

	// skip endianness check data
	offset += sizeof(uint64_t);

	if (not mapping.read(file, offset)) {
		cerr << "Process " << mpi_rank
			<< " couldn't set cell id mapping from file " << file_name
			<< endl;
		return boost::optional<std::tuple<bool, std::array<double, 3>>>();
	}

	offset
		+= mapping.data_size()
		+ sizeof(unsigned int)
		+ topology.data_size();

	if (not geometry.read(file, offset)) {
		cerr << "Process " << mpi_rank
			<< " couldn't read geometry from file " << file_name
			<< endl;
		return boost::optional<std::tuple<bool, std::array<double, 3>>>();
	}
	offset += geometry.data_size();

	// read number of cells
	uint64_t total_cells = 0;
	MPI_File_read_at(
		file,
		offset,
		&total_cells,
		1,
		MPI_UINT64_T,
		MPI_STATUS_IGNORE
	);
	offset += sizeof(uint64_t);

	if (total_cells == 0) {
		MPI_File_close(&file);
		return boost::optional<std::tuple<bool, std::array<double, 3>>>(
			std::make_tuple(
				have_fluxes,
				phys_consts
			)
		);
	}

	// read cell ids and data offsets
	vector<pair<uint64_t, uint64_t>> cells_offsets(total_cells);
	MPI_File_read_at(
		file,
		offset,
		cells_offsets.data(),
		2 * total_cells,
		MPI_UINT64_T,
		MPI_STATUS_IGNORE
	);

	// read cell data
	for (const auto& item: cells_offsets) {
		const uint64_t
			cell_id = item.first,
			file_address = item.second;

		simulation_data[cell_id];

		/*
		dccrg writes cell data without padding so store the
		non-padded version of the cell's datatype into file_datatype
		*/
		void* memory_address = NULL;
		int memory_count = -1;
		MPI_Datatype
			memory_datatype = MPI_DATATYPE_NULL,
			file_datatype = MPI_DATATYPE_NULL;

		tie(
			memory_address,
			memory_count,
			memory_datatype
		) = simulation_data.at(cell_id).get_mpi_datatype();

		int sizeof_memory_datatype;
		MPI_Type_size(memory_datatype, &sizeof_memory_datatype);
		MPI_Type_contiguous(sizeof_memory_datatype, MPI_BYTE, &file_datatype);

		// interpret data from the file using the non-padded type
		MPI_Type_commit(&file_datatype);
		MPI_File_set_view(
			file,
			file_address,
			MPI_BYTE,
			file_datatype,
			const_cast<char*>("native"),
			MPI_INFO_NULL
		);

		MPI_Type_commit(&memory_datatype);
		MPI_File_read_at(
			file,
			0,
			memory_address,
			memory_count,
			memory_datatype,
			MPI_STATUS_IGNORE
		);

		MPI_Type_free(&memory_datatype);
		MPI_Type_free(&file_datatype);
	}

	MPI_File_close(&file);

	return boost::optional<std::tuple<bool, std::array<double, 3>>>(
		std::make_tuple(
			have_fluxes,
			phys_consts
		)
	);
}


/*
Plots 1d data from given list in that order.
*/
int plot_1d(
	const dccrg::Cartesian_Geometry& geometry,
	const unordered_map<uint64_t, Cell>& simulation_data,
	const std::vector<uint64_t>& cells,
	const std::string& output_file_name_prefix,
	const double adiabatic_index,
	const double vacuum_permeability,
	const bool have_fluxes
) {
	const string gnuplot_file_name(output_file_name_prefix + ".dat");
	ofstream gnuplot_file(gnuplot_file_name);

	const size_t tube_dim
		= [&](){
			for (size_t i = 0; i < geometry.length.get().size(); i++) {
				if (geometry.length.get()[i] > 1) {
					return i;
				}
			}
			return size_t(999);
		}();

	const double
		tube_start = geometry.get_start()[tube_dim],
		tube_end = geometry.get_end()[tube_dim];

	// mass density & pressure
	gnuplot_file
		<< "set term png enhanced\nset output '"
		<< output_file_name_prefix + ".png"
		<< "'\nset xlabel 'Dimension 1 (m)'\nset xrange ["
		<< boost::lexical_cast<std::string>(tube_start)
		<< " : " << boost::lexical_cast<std::string>(tube_end)
		<< "]\nset ylabel 'Mass density (kg m^{-3})'\n"
		   "set y2label 'Pressure (Pa)'\n"
		   "set key horizontal outside bottom\n"
		   "set ytics nomirror\n"
		   "set yrange [0 : *]\n"
		   "set y2range [0 : *]\n"
		   "set y2tics auto\n"
		   "plot "
		     "'-' using 1:2 axes x1y1 with line linewidth 2 title 'density', "
		     "'-' u 1:2 axes x1y2 w l lw 2 t 'pressure'\n";

	for (const auto& cell_id: cells) {
		const auto& mhd_data
			= simulation_data.at(cell_id)[MHD_State_Conservative()];
		const double x = geometry.get_center(cell_id)[tube_dim];
		gnuplot_file << x << " " << mhd_data[Mass_Density()] << "\n";
	}
	gnuplot_file << "end\n";

	for (const auto& cell_id: cells) {
		const auto& mhd_data
			= simulation_data.at(cell_id)[MHD_State_Conservative()];
		const double x = geometry.get_center(cell_id)[tube_dim];
		gnuplot_file
			<< x << " "
			<< get_pressure<
				MHD_State_Conservative::data_type,
				Mass_Density,
				Momentum_Density,
				Total_Energy_Density,
				Magnetic_Field
			>(mhd_data, adiabatic_index, vacuum_permeability)
			<< "\n";
	}
	gnuplot_file << "end\n";

	// flux of mass density & pressure
	if (have_fluxes) {
		gnuplot_file
			<< "set output '"
			<< output_file_name_prefix + "_flux.png"
			<< "'\nset ylabel 'Mass density flux'\n"
			   "set y2label 'Pressure flux'\n"
			   "set yrange [* : *]\n"
			   "set y2range [* : *]\n"
			   "plot "
			     "'-' using 1:2 axes x1y1 with line linewidth 2 title 'density', "
			     "'-' u 1:2 axes x1y2 w l lw 2 t 'pressure'\n";

		for (const auto& cell_id: cells) {
			const auto& mhd_data
				= simulation_data.at(cell_id)[MHD_Flux_Conservative()];
			const double x = geometry.get_center(cell_id)[tube_dim];
			gnuplot_file << x << " " << mhd_data[Mass_Density()] << "\n";
		}
		gnuplot_file << "end\n";

		for (const auto& cell_id: cells) {
			const auto& mhd_data
				= simulation_data.at(cell_id)[MHD_Flux_Conservative()];
			const double x = geometry.get_center(cell_id)[tube_dim];
			gnuplot_file
				<< x << " "
				<< get_pressure<
					MHD_State_Conservative::data_type,
					Mass_Density,
					Momentum_Density,
					Total_Energy_Density,
					Magnetic_Field
				>(mhd_data, adiabatic_index, vacuum_permeability)
				<< "\n";
		}
		gnuplot_file << "end\n";
	}

	// velocity
	gnuplot_file
		<< "set term png enhanced\nset output '"
		<< output_file_name_prefix + "_V.png"
		<< "'\nset ylabel 'Velocity (m s^{-1})'\n"
		   "set yrange [* : *]\n"
		   "unset y2label\n"
		   "unset y2tics\n"
		   "set ytics mirror\n"
		   "plot "
		     "'-' u 1:2 w l lw 2 t 'v_1', "
		     "'-' u 1:2 w l lw 2 t 'v_2', "
		     "'-' u 1:2 w l lw 2 t 'v_3'\n";

	for (const auto& cell_id: cells) {
		const auto& mhd_data
			= simulation_data.at(cell_id)[MHD_State_Conservative()];
		const auto& m = mhd_data[Momentum_Density()];
		const auto& rho = mhd_data[Mass_Density()];
		const auto v(m / rho);
		const double x = geometry.get_center(cell_id)[tube_dim];
		gnuplot_file << x << " " << v[0] << "\n";
	}
	gnuplot_file << "end\n";

	for (const auto& cell_id: cells) {
		const auto& mhd_data
			= simulation_data.at(cell_id)[MHD_State_Conservative()];
		const auto& m = mhd_data[Momentum_Density()];
		const auto& rho = mhd_data[Mass_Density()];
		const auto v(m / rho);
		const double x = geometry.get_center(cell_id)[tube_dim];
		gnuplot_file << x << " " << v[1] << "\n";
	}
	gnuplot_file << "end\n";

	for (const auto& cell_id: cells) {
		const auto& mhd_data
			= simulation_data.at(cell_id)[MHD_State_Conservative()];
		const auto& m = mhd_data[Momentum_Density()];
		const auto& rho = mhd_data[Mass_Density()];
		const auto v(m / rho);
		const double x = geometry.get_center(cell_id)[tube_dim];
		gnuplot_file << x << " " << v[2] << "\n";
	}
	gnuplot_file << "end\n";

	// momentum flux
	if (have_fluxes) {
		gnuplot_file
			<< "set term png enhanced\nset output '"
			<< output_file_name_prefix + "_M_flux.png"
			<< "'\nset ylabel 'Momentum'\n"
			   "set yrange [* : *]\n"
			   "unset y2label\n"
			   "unset y2tics\n"
			   "set ytics mirror\n"
			   "plot "
			     "'-' u 1:2 w l lw 2 t 'p_1 flux', "
			     "'-' u 1:2 w l lw 2 t 'p_2 flux', "
			     "'-' u 1:2 w l lw 2 t 'p_3 flux'\n";

		for (const auto& cell_id: cells) {
			const auto& mhd_data
				= simulation_data.at(cell_id)[MHD_Flux_Conservative()];
			const auto& m = mhd_data[Momentum_Density()];
			const double x = geometry.get_center(cell_id)[tube_dim];
			gnuplot_file << x << " " << m[0] << "\n";
		}
		gnuplot_file << "end\n";

		for (const auto& cell_id: cells) {
			const auto& mhd_data
				= simulation_data.at(cell_id)[MHD_Flux_Conservative()];
			const auto& m = mhd_data[Momentum_Density()];
			const double x = geometry.get_center(cell_id)[tube_dim];
			gnuplot_file << x << " " << m[1] << "\n";
		}
		gnuplot_file << "end\n";

		for (const auto& cell_id: cells) {
			const auto& mhd_data
				= simulation_data.at(cell_id)[MHD_Flux_Conservative()];
			const auto& m = mhd_data[Momentum_Density()];
			const double x = geometry.get_center(cell_id)[tube_dim];
			gnuplot_file << x << " " << m[2] << "\n";
		}
		gnuplot_file << "end\n";
	}

	// magnetic field
	gnuplot_file
		<< "set term png enhanced\nset output '"
		<< output_file_name_prefix + "_B.png"
		<< "'\nset ylabel 'Magnetic field (T)'\n"
		   "plot "
		     "'-' u 1:2 w l lw 2 t 'B_1', "
		     "'-' u 1:2 w l lw 2 t 'B_2', "
		     "'-' u 1:2 w l lw 2 t 'B_3'\n";

	for (const auto& cell_id: cells) {
		const auto& mhd_data
			= simulation_data.at(cell_id)[MHD_State_Conservative()];
		const auto& B = mhd_data[Magnetic_Field()];
		const double x = geometry.get_center(cell_id)[tube_dim];
		gnuplot_file << x << " " << B[0] << "\n";
	}
	gnuplot_file << "end\n";

	for (const auto& cell_id: cells) {
		const auto& mhd_data
			= simulation_data.at(cell_id)[MHD_State_Conservative()];
		const auto& B = mhd_data[Magnetic_Field()];
		const double x = geometry.get_center(cell_id)[tube_dim];
		gnuplot_file << x << " " << B[1] << "\n";
	}
	gnuplot_file << "end\n";

	for (const auto& cell_id: cells) {
		const auto& mhd_data
			= simulation_data.at(cell_id)[MHD_State_Conservative()];
		const auto& B = mhd_data[Magnetic_Field()];
		const double x = geometry.get_center(cell_id)[tube_dim];
		gnuplot_file << x << " " << B[2] << "\n";
	}
	gnuplot_file << "end\n";

	// magnetic field flux
	if (have_fluxes) {
		gnuplot_file
			<< "set term png enhanced\nset output '"
			<< output_file_name_prefix + "_B.png"
			<< "'\nset ylabel 'Magnetic field (T)'\n"
			   "plot "
			     "'-' u 1:2 w l lw 2 t 'B_1 flux', "
			     "'-' u 1:2 w l lw 2 t 'B_2 flux', "
			     "'-' u 1:2 w l lw 2 t 'B_3 flux'\n";

		for (const auto& cell_id: cells) {
			const auto& mhd_data
				= simulation_data.at(cell_id)[MHD_Flux_Conservative()];
			const auto& B = mhd_data[Magnetic_Field()];
			const double x = geometry.get_center(cell_id)[tube_dim];
			gnuplot_file << x << " " << B[0] << "\n";
		}
		gnuplot_file << "end\n";

		for (const auto& cell_id: cells) {
			const auto& mhd_data
				= simulation_data.at(cell_id)[MHD_Flux_Conservative()];
			const auto& B = mhd_data[Magnetic_Field()];
			const double x = geometry.get_center(cell_id)[tube_dim];
			gnuplot_file << x << " " << B[1] << "\n";
		}
		gnuplot_file << "end\n";

		for (const auto& cell_id: cells) {
			const auto& mhd_data
				= simulation_data.at(cell_id)[MHD_Flux_Conservative()];
			const auto& B = mhd_data[Magnetic_Field()];
			const double x = geometry.get_center(cell_id)[tube_dim];
			gnuplot_file << x << " " << B[2] << "\n";
		}
		gnuplot_file << "end\n";
	}

	gnuplot_file.close();

	return system(("gnuplot " + gnuplot_file_name).c_str());
}


/*
Plots data of given cells as a 2d color map.
*/
int plot_2d(
	const dccrg::Cartesian_Geometry& geometry,
	const unordered_map<uint64_t, Cell>& simulation_data,
	const std::vector<uint64_t>& cells,
	const std::string& output_file_name_prefix,
	const double adiabatic_index,
	const double vacuum_permeability,
	const bool /*have_fluxes*/
) {
	const auto& grid_length = geometry.length;
	const string gnuplot_file_name(output_file_name_prefix + ".dat");

	ofstream gnuplot_file(gnuplot_file_name);

	// mass density
	gnuplot_file
		<< "set term png enhanced\nset output '"
		<< output_file_name_prefix + "_rho.png"
		<< "'\nset ylabel 'Dimension 2'\n"
		   "set xlabel 'Dimension 1'\n"
		   "set format cb '%.2e'\n"
		   "plot '-' matrix with image title 'Mass density (kg / m^3)'\n";

	for (size_t i = 0; i < cells.size(); i++) {
		const auto& mhd_data
			= simulation_data.at(cells[i])[MHD_State_Conservative()];

		gnuplot_file << mhd_data[Mass_Density()] << " ";
		if (i % grid_length.get()[0] == grid_length.get()[0] - 1) {
			gnuplot_file << "\n";
		}
	}
	gnuplot_file << "\nend\n";

	// pressure
	gnuplot_file
		<< "set output '"
		<< output_file_name_prefix + "_P.png"
		<< "'\nplot '-' matrix with image title 'Pressure (Pa)'\n";

	for (size_t i = 0; i < cells.size(); i++) {
		const auto& mhd_data
			= simulation_data.at(cells[i])[MHD_State_Conservative()];

		gnuplot_file
			<< get_pressure<
				MHD_State_Conservative::data_type,
				Mass_Density,
				Momentum_Density,
				Total_Energy_Density,
				Magnetic_Field
			>(mhd_data, adiabatic_index, vacuum_permeability)
			<< " ";
		if (i % grid_length.get()[0] == grid_length.get()[0] - 1) {
			gnuplot_file << "\n";
		}
	}
	gnuplot_file << "\nend\n";

	// vx
	gnuplot_file
		<< "set output '"
		<< output_file_name_prefix + "_vx.png"
		<< "'\nplot '-' matrix with image title 'V_1 (m / s)'\n";

	for (size_t i = 0; i < cells.size(); i++) {
		const auto& mhd_data
			= simulation_data.at(cells[i])[MHD_State_Conservative()];

		gnuplot_file
			<< (mhd_data[Momentum_Density()] / mhd_data[Mass_Density()])[0]
			<< " ";
		if (i % grid_length.get()[0] == grid_length.get()[0] - 1) {
			gnuplot_file << "\n";
		}
	}
	gnuplot_file << "\nend\n";

	// vy
	gnuplot_file
		<< "set output '"
		<< output_file_name_prefix + "_vy.png"
		<< "'\nplot '-' matrix with image title 'V_y (m / s)'\n";

	for (size_t i = 0; i < cells.size(); i++) {
		const auto& mhd_data
			= simulation_data.at(cells[i])[MHD_State_Conservative()];

		gnuplot_file
			<< (mhd_data[Momentum_Density()] / mhd_data[Mass_Density()])[1]
			<< " ";
		if (i % grid_length.get()[0] == grid_length.get()[0] - 1) {
			gnuplot_file << "\n";
		}
	}
	gnuplot_file << "\nend\n";

	// vz
	gnuplot_file
		<< "set output '"
		<< output_file_name_prefix + "_vz.png"
		<< "'\nplot '-' matrix with image title 'V_z (m / s)'\n";

	for (size_t i = 0; i < cells.size(); i++) {
		const auto& mhd_data
			= simulation_data.at(cells[i])[MHD_State_Conservative()];

		gnuplot_file
			<< (mhd_data[Momentum_Density()] / mhd_data[Mass_Density()])[2]
			<< " ";
		if (i % grid_length.get()[0] == grid_length.get()[0] - 1) {
			gnuplot_file << "\n";
		}
	}
	gnuplot_file << "\nend\n";


	/*
	Scale vector lengths
	*/

	double max_v = 0, max_B = 0;
	for (size_t i = 0; i < cells.size(); i++) {
		const auto& mhd_data
			= simulation_data.at(cells[i])[MHD_State_Conservative()];

		const double
			v = (mhd_data[Momentum_Density()] / mhd_data[Mass_Density()]).norm(),
			B = mhd_data[Magnetic_Field()].norm();

		if (max_v < v) {
			max_v = v;
		}
		if (max_B < B) {
			max_B = B;
		}
	}
	if (max_v == 0) {
		max_v = 1;
	}
	if (max_B == 0) {
		max_B = 1;
	}

	const double
		vectors_per_screen = 10,
		screen_length = geometry.get_end()[0] - geometry.get_start()[0],
		vector_length = screen_length / vectors_per_screen,
		velocity_scaling = vector_length / max_v,
		B_scaling = vector_length / max_B;

	// velocity
	gnuplot_file
		<< "set output '"
		<< output_file_name_prefix + "_V.png"
		<< "'\nset ylabel 'Y'\n"
		   "set xlabel 'X'\n"
		   "set title 'Velocity'\n"
		   "plot '-' u 1:2:3:4 w vectors t ''\n";

	for (size_t i = 0; i < cells.size(); i++) {
		const auto& mhd_data
			= simulation_data.at(cells[i])[MHD_State_Conservative()];

		const auto& m = mhd_data[Momentum_Density()];
		const auto& rho = mhd_data[Mass_Density()];
		const auto v(m / rho);
		const double
			x = geometry.get_center(cells[i])[0],
			y = geometry.get_center(cells[i])[1];
		gnuplot_file << x << " " << y << " "
			<< velocity_scaling * v[0] << " " << velocity_scaling * v[1] << "\n";
	}
	gnuplot_file << "end\n";

	// magnetic field
	gnuplot_file
		<< "set output '"
		<< output_file_name_prefix + "_B.png"
		<< "'\nset title 'Magnetic field'\n"
		   "plot '-' u 1:2:3:4 w vectors t ''\n";

	for (size_t i = 0; i < cells.size(); i++) {
		const auto& mhd_data
			= simulation_data.at(cells[i])[MHD_State_Conservative()];

		const auto& B = mhd_data[Magnetic_Field()];
		const double
			x = geometry.get_center(cells[i])[0],
			y = geometry.get_center(cells[i])[1];
		gnuplot_file << x << " " << y << " "
			<< B_scaling * B[0] << " " << B_scaling * B[1] << "\n";
	}
	gnuplot_file << "end\n";


	gnuplot_file.close();

	return system(("gnuplot " + gnuplot_file_name).c_str());
}


int main(int argc, char* argv[])
{
	if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
		cerr << "Coudln't initialize MPI." << endl;
		abort();
	}

	MPI_Comm comm = MPI_COMM_WORLD;

	int rank = 0, comm_size = 0;
	MPI_Comm_rank(comm, &rank);
	MPI_Comm_size(comm, &comm_size);


	for (int i = 1; i < argc; i++) {

		if ((i - 1) % comm_size != rank) {
			continue;
		}

		const string argv_string(argv[i]);

		dccrg::Mapping mapping;
		dccrg::Grid_Topology topology;
		dccrg::Cartesian_Geometry geometry(mapping.length, mapping, topology);
		unordered_map<uint64_t, Cell> simulation_data;

		boost::optional<
			std::tuple<
				bool,
				std::array<double, 3>
			>
		> header = read_data(
			mapping,
			topology,
			geometry,
			simulation_data,
			argv_string,
			rank
		);
		if (not header) {
			std::cerr <<  __FILE__ << "(" << __LINE__<< "): "
				<< "Couldn't read simulation data from file " << argv_string
				<< std::endl;
			continue;
		}


		// cell coordinates increase with id (without AMR)
		std::vector<uint64_t> cells;
		cells.reserve(simulation_data.size());

		for (const auto& item: simulation_data) {
			cells.push_back(item.first);
		}
		if (cells.size() == 0) {
			continue;
		}

		std::sort(cells.begin(), cells.end());

		// get number of dimensions
		const auto grid_length = mapping.length.get();
		const size_t dimensions
			= [&](){
				size_t ret_val = 0;
				for (size_t i = 0; i < grid_length.size(); i++) {
					if (grid_length[i] > 1) {
						ret_val++;
					}
				}
				return ret_val;
			}();

		switch(dimensions) {
		case 1:
			plot_1d(
				geometry,
				simulation_data,
				cells,
				argv_string.substr(0, argv_string.size() - 3),
				std::get<1>(*header)[0],
				std::get<1>(*header)[2],
				std::get<0>(*header)
			);
			break;
		case 2:
			plot_2d(
				geometry,
				simulation_data,
				cells,
				argv_string.substr(0, argv_string.size() - 3),
				std::get<1>(*header)[0],
				std::get<1>(*header)[2],
				std::get<0>(*header)
			);
			break;
		default:
			std::cerr <<  __FILE__ << "(" << __LINE__<< "): "
				<< "Unsupported number of dimensions in file " << argv_string
				<< std::endl;
			continue;
			break;
		}
	}

	MPI_Finalize();

	return EXIT_SUCCESS;
}