/*
Program for converting MHD output of PAMHD to ASCII format.

Copyright 2014, 2015, 2016, 2017 Ilja Honkonen
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
#include "cstdint"
#include "cstdlib"
#include "functional"
#include "fstream"
#include "string"
#include "unordered_map"
#include "vector"

#include "boost/optional.hpp"
#include "boost/program_options.hpp"
#include "dccrg_cartesian_geometry.hpp"
#include "dccrg_mapping.hpp"
#include "dccrg_topology.hpp"
#include "mpi.h" // must be included before gensimcell
#include "Eigen/Core" // must be included before gensimcell
#include "gensimcell.hpp"

#include "mhd/common.hpp"
#include "mhd/save.hpp"
#include "mhd/variables.hpp"
#include "variables.hpp"


using namespace std;

/*
Reads simulation data from given file.

Fills out grid info and simulation data.

On success returns physical constants used by
simulation, on failure returns an uninitialized value.
*/
boost::optional<std::array<double, 4>> read_data(
	dccrg::Mapping& cell_id_mapping,
	dccrg::Grid_Topology& topology,
	dccrg::Cartesian_Geometry& geometry,
	unordered_map<uint64_t, pamhd::mhd::Cell>& simulation_data,
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
		return boost::optional<std::array<double, 4>>();
	}

	MPI_Offset offset = 0;

	// read file version
	uint64_t file_version = 0;
	MPI_File_read_at( // TODO: add error checking
		file,
		offset,
		&file_version,
		1,
		MPI_UINT64_T,
		MPI_STATUS_IGNORE
	);
	offset += sizeof(uint64_t);
	if (file_version != 2) {
		cerr << "Process " << mpi_rank
			<< " Unsupported file version: " << file_version
			<< endl;
		return boost::optional<std::array<double, 4>>();
	}

	// read simulation parameters
	std::array<double, 4> simulation_parameters;
	MPI_File_read_at(
		file,
		offset,
		simulation_parameters.data(),
		4,
		MPI_DOUBLE,
		MPI_STATUS_IGNORE
	);
	offset += 4 * sizeof(double);

	// check endianness
	uint64_t endianness = 0;
	MPI_File_read_at(
		file,
		offset,
		&endianness,
		1,
		MPI_UINT64_T,
		MPI_STATUS_IGNORE
	);
	offset += sizeof(uint64_t);
	if (endianness != 0x1234567890abcdef) {
		cerr << "Process " << mpi_rank
			<< " Unsupported endianness: " << endianness
			<< ", should be " << 0x1234567890abcdef
			<< endl;
		return boost::optional<std::array<double, 4>>();
	}

	if (not cell_id_mapping.read(file, offset)) {
		cerr << "Process " << mpi_rank
			<< " couldn't set cell id mapping from file " << file_name
			<< endl;
		return boost::optional<std::array<double, 4>>();
	}

	offset
		+= cell_id_mapping.data_size()
		+ sizeof(unsigned int)
		+ topology.data_size();

	if (not geometry.read(file, offset)) {
		cerr << "Process " << mpi_rank
			<< " couldn't read geometry from file " << file_name
			<< endl;
		return boost::optional<std::array<double, 4>>();
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
		return boost::optional<std::array<double, 4>>(simulation_parameters);
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
	pamhd::mhd::Cell::set_transfer_all(
		true,
		pamhd::mhd::HD_State_Conservative(),
		pamhd::Magnetic_Field(),
		pamhd::Electric_Current_Density(),
		pamhd::mhd::Solver_Info(),
		pamhd::MPI_Rank(),
		pamhd::Resistivity(),
		pamhd::Bg_Magnetic_Field()
	);
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

	return boost::optional<std::array<double, 4>>(simulation_parameters);
}


/*
Writes given data in ascii format to given file appended with .txt.
*/
void convert(
	const dccrg::Cartesian_Geometry& geometry,
	const unordered_map<uint64_t, pamhd::mhd::Cell>& simulation_data,
	const std::string& output_file_name_prefix,
	const double adiabatic_index,
	const double vacuum_permeability
) {
	std::vector<uint64_t> cells;
	for (const auto& i: simulation_data) {
		const auto cell_id = i.first;
		if (geometry.mapping.get_refinement_level(cell_id) > 0) {
			std::cerr << "Refined mesh not supported." << std::endl;
			abort();
		}
		cells.push_back(cell_id);
	}

	std::ofstream ascii_file(output_file_name_prefix + ".txt");
	ascii_file <<
		"# MHD data created by PAMHD\n"
		"# Format of each line (lines are ordered randomly):\n"
		"# x, y and z coordinates of cell's center\n"
		"# mass density\n"
		"# momentum density\n"
		"# thermal pressure\n"
		"# x, y and z components of magnetic field\n"
		"# x, y and z components of background magnetic field on +x face\n"
		"# x, y and z components of electric current density\n#\n"
		"# Physical constants:\n"
		"# adiabatic index: " << adiabatic_index <<
		"\n# vacuum permeability: " << vacuum_permeability << "\n";

	for (const auto& cell_id: cells) {
		const auto cell_center = geometry.get_center(cell_id);
		ascii_file
			<< cell_center[0] << " "
			<< cell_center[1] << " "
			<< cell_center[2] << " ";

		const auto& hd_data = simulation_data.at(cell_id)[pamhd::mhd::HD_State_Conservative()];

		const auto& density = hd_data[pamhd::mhd::Mass_Density()];
		ascii_file << density << " ";

		const auto& momentum = hd_data[pamhd::mhd::Momentum_Density()];
		if (not std::isnormal(density) or density < 0) {
			ascii_file << "0 0 0\n";
		} else {
			ascii_file
				<< momentum[0] / density << " "
				<< momentum[1] / density << " "
				<< momentum[2] / density << " ";
		}

		const auto pressure
			= pamhd::mhd::get_pressure(
				hd_data[pamhd::mhd::Mass_Density()],
				hd_data[pamhd::mhd::Momentum_Density()],
				hd_data[pamhd::mhd::Total_Energy_Density()],
				simulation_data.at(cell_id)[pamhd::Magnetic_Field()],
				adiabatic_index,
				vacuum_permeability
			);
		ascii_file << pressure << " ";

		const auto& magnetic_field = simulation_data.at(cell_id)[pamhd::Magnetic_Field()];
		ascii_file
			<< magnetic_field[0] << " "
			<< magnetic_field[1] << " "
			<< magnetic_field[2] << " ";

		const auto& background_magnetic_field = simulation_data.at(cell_id)[pamhd::Bg_Magnetic_Field()](0,0);
		ascii_file
			<< background_magnetic_field[0] << " "
			<< background_magnetic_field[1] << " "
			<< background_magnetic_field[2] << " ";

		const auto& current = simulation_data.at(cell_id)[pamhd::Electric_Current_Density()];
		ascii_file
			<< current[0] << " "
			<< current[1] << " "
			<< current[2] << "\n";
	}
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

	// program options
	bool verbose = false;
	std::vector<std::string> input_files;

	boost::program_options::options_description
		options("Usage: program_name [options], where options are");
	options.add_options()
		("help", "Print this help message")
		("verbose", "Print run time information")
		("input-file",
			boost::program_options::value<std::vector<std::string>>(&input_files),
			"Results to plot, the --input-file part can be omitted");

	boost::program_options::positional_options_description positional_options;
	positional_options.add("input-file", -1);

	boost::program_options::variables_map var_map;
	try {
		boost::program_options::store(
			boost::program_options::command_line_parser(argc, argv)
				.options(options)
				.positional(positional_options)
				.run(),
			var_map
		);
	} catch (std::exception& e) {
		if (rank == 0) {
			std::cerr <<  __FILE__ << "(" << __LINE__ << "): "
				<< "Couldn't parse command line options: " << e.what()
				<< std::endl;
		}
		abort();
	}
	boost::program_options::notify(var_map);

	if (var_map.count("help") > 0) {
		if (rank == 0) {
			cout
				<< "Converts given .dc files from MHD test program to ASCII format\n"
				<< options
				<< endl;
		}
		MPI_Finalize();
		return EXIT_SUCCESS;
	}

	if (input_files.size() == 0) {
		MPI_Finalize();
		return EXIT_SUCCESS;
	}

	if (var_map.count("verbose") > 0) {
		verbose = true;
	}

	for (size_t i = 0; i < input_files.size(); i++) {
		if (int(i) % comm_size != rank) {
			continue;
		}

		if (verbose) {
			std::cout << "MPI rank " << rank
				<< " processing file " << input_files[i]
				<< std::endl;
		}

		dccrg::Mapping cell_id_mapping;
		dccrg::Grid_Topology topology;
		dccrg::Cartesian_Geometry geometry(cell_id_mapping.length, cell_id_mapping, topology);
		unordered_map<uint64_t, pamhd::mhd::Cell> simulation_data;

		boost::optional<std::array<double, 4>> header
			= read_data(
				cell_id_mapping,
				topology,
				geometry,
				simulation_data,
				input_files[i],
				rank
			);
		if (not header) {
			std::cerr <<  __FILE__ << "(" << __LINE__<< "): "
				<< "Couldn't read simulation data from file " << input_files[i]
				<< std::endl;
			continue;
		}

		convert(
			geometry,
			simulation_data,
			input_files[i].substr(0, input_files[i].size() - 3),
			(*header)[1],
			(*header)[3]
		);
	}

	MPI_Finalize();

	return EXIT_SUCCESS;
}
