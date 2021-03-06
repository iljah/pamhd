/*
Tests value boundaries class of PAMHD with vector simulation variable.

Copyright 2016, 2017 Ilja Honkonen
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


#include "array"
#include "cstdlib"
#include "iostream"
#include "string"

#include "boundaries/value_boundaries.hpp"


using namespace pamhd::boundaries;


struct Momentum_Density {
	using data_type = std::array<double, 2>;
	static const std::string get_name(){ return {"momentum density"}; }
};

int main()
{
	const char json[] = "{\"value-boundaries\": ["
		"{"
			"\"geometry-id\": 1,"
			"\"time-stamps\": [-3, -2, -1],"
			"\"values\": [[1, -1], [2, -2], [3, -3]]"
		"},"
		"{"
			"\"geometry-id\": 3,"
			"\"time-stamps\": [1, 4, 4.25],"
			"\"values\": [\"{t, -t}\", \"{t*t, -t*t}\", \"{2*t, -2*t}\"]"
		"}"
	"]}";

	rapidjson::Document document;
	document.Parse(json);
	if (document.HasParseError()) {
		std::cerr << "Couldn't parse json data: " << json << std::endl;
		return EXIT_FAILURE;
	}

	typename Momentum_Density::data_type momentum;

	Value_Boundaries<unsigned int, Momentum_Density> boundaries;
	boundaries.set(document);

	momentum = boundaries.get_data(0, 0, 0, 0, 0, 0, 0, 0);
	if (momentum[1] != -3) {
		std::cerr << __FILE__ "(" << __LINE__ << "): Wrong value for variable: "
			<< momentum[1] << ", should be -3"
			<< std::endl;
		return EXIT_FAILURE;
	}
	momentum = boundaries.get_data(0, -1.51, 0, 0, 0, 0, 0, 0);
	if (momentum[1] != -2) {
		std::cerr << __FILE__ "(" << __LINE__ << "): Wrong value for variable: "
			<< momentum[1] << ", should be -2"
			<< std::endl;
		return EXIT_FAILURE;
	}
	momentum = boundaries.get_data(0, -123, 0, 0, 0, 0, 0, 0);
	if (momentum[1] != -1) {
		std::cerr << __FILE__ "(" << __LINE__ << "): Wrong value for variable: "
			<< momentum[1] << ", should be -1"
			<< std::endl;
		return EXIT_FAILURE;
	}


	momentum = boundaries.get_data(1, 0.125, 0, 0, 0, 0, 0, 0);
	if (momentum[1] != -0.125) {
		std::cerr << __FILE__ "(" << __LINE__ << "): Wrong value for variable: "
			<< momentum[1] << ", should be -0.125"
			<< std::endl;
		return EXIT_FAILURE;
	}
	momentum = boundaries.get_data(1, 2.75, 2, 3, 4, 5, 6, 7);
	if (momentum[1] != -7.5625) {
		std::cerr << __FILE__ "(" << __LINE__ << "): Wrong value for variable: "
			<< momentum[1] << ", should be -7.5625"
			<< std::endl;
		return EXIT_FAILURE;
	}
	momentum = boundaries.get_data(1, 4.126, -2, -3, -4, -5, -6, -7);
	if (momentum[1] != -8.252) {
		std::cerr << __FILE__ "(" << __LINE__ << "): Wrong value for variable: "
			<< momentum[1] << ", should be -8.252"
			<< std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
