#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "lib/nlohmann/json.hpp"

using json = nlohmann::json;

json decode_bencoded_string(const std::string& encoded_value, size_t& index) {
	// Example: "5:hello" -> "hello"
	size_t colon_index = encoded_value.find(':', index);
	if (colon_index != std::string::npos) {
		std::string number_string =
			encoded_value.substr(index, colon_index - index);
		int64_t number = std::atoll(number_string.c_str());
		std::string str = encoded_value.substr(colon_index + 1, number);
		index = colon_index + number + 1;
		return json(str);
	} else {
		throw std::runtime_error("Invalid encoded value: " + encoded_value);
	}
}

json decode_bencoded_integer(const std::string& encoded_value, size_t& index) {
	size_t end_index = encoded_value.find('e', index + 1);
	json decoded_value =
		json(stoll(encoded_value.substr(index + 1, end_index - index - 1)));
	// std::cout << "test" << std::endl;
	index = end_index + 1;
	return decoded_value;
}

json decode_bencoded_list(const std::string& encoded_value, size_t& index) {
	std::vector<json> decoded_value;

	index++;  // skip first 'l'
	while (index < encoded_value.size() - 1) {
		// std::cout << "index = " << index << std::endl;
		if (encoded_value[index] == 'e') {
			// list ends
			break;
		}
		if (std::isdigit(encoded_value[index])) {
			decoded_value.push_back(
				decode_bencoded_string(encoded_value, index));
		} else if (encoded_value[index] == 'i') {
			decoded_value.push_back(
				decode_bencoded_integer(encoded_value, index));
		} else if (encoded_value[index] == 'l') {
			decoded_value.push_back(decode_bencoded_list(encoded_value, index));
		}
	}

	index++;
	return json(decoded_value);
}

json decode_bencoded_dict(const std::string& encoded_value, size_t& index) {
	nlohmann::ordered_map<json, json> map;

	index++;  // skip leading 'd'
	while (index < encoded_value.size() - 1) {
		if (encoded_value[index] == 'e') {
			// list ends
			break;
		}
		json key = decode_bencoded_string(encoded_value, index);
		json val;
		if (std::isdigit(encoded_value[index])) {
			val = decode_bencoded_string(encoded_value, index);
		} else if (encoded_value[index] == 'i') {
			val = decode_bencoded_integer(encoded_value, index);
		} else if (encoded_value[index] == 'l') {
			val = decode_bencoded_list(encoded_value, index);
		} else if (encoded_value[index] == 'd') {
			val = decode_bencoded_dict(encoded_value, index);
		}

		map.push_back({key, val});
	}

	index++;  // skip ending 'e'
	return json(map);
}

json decode_bencoded_value(const std::string& encoded_value) {
	size_t index = 0;
	if (std::isdigit(encoded_value[0])) {
		return decode_bencoded_string(encoded_value, index);
	} else if (encoded_value[0] == 'i') {
		return decode_bencoded_integer(encoded_value, index);
	} else if (encoded_value[0] == 'l') {
		return decode_bencoded_list(encoded_value, index);
	} else if (encoded_value[0] == 'd') {
		return decode_bencoded_dict(encoded_value, index);
	} else {
		throw std::runtime_error("Unhandled encoded value: " + encoded_value);
	}
}

int main(int argc, char* argv[]) {
	// Flush after every std::cout / std::cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " decode <encoded_value>"
				  << std::endl;
		return 1;
	}

	std::string command = argv[1];

	if (command == "decode") {
		if (argc < 3) {
			std::cerr << "Usage: " << argv[0] << " decode <encoded_value>"
					  << std::endl;
			return 1;
		}
		// You can use print statements as follows for debugging, they'll be
		// visible when running tests.
		// std::cout << "Logs from your program will appear here!" << std::endl;

		// Uncomment this block to pass the first stage
		std::string encoded_value = argv[2];
		json decoded_value = decode_bencoded_value(encoded_value);
		std::cout << decoded_value.dump() << std::endl;
	} else {
		std::cerr << "unknown command: " << command << std::endl;
		return 1;
	}

	return 0;
}
