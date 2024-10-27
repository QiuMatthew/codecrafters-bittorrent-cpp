#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "lib/nlohmann/json.hpp"
#include "lib/sha1.hpp"

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
	index = end_index + 1;
	return decoded_value;
}

json decode_bencoded_list(const std::string& encoded_value, size_t& index) {
	std::vector<json> decoded_value;

	index++;  // skip first 'l'
	while (index < encoded_value.size() - 1) {
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

	index++;  // skip ending e
	return json(decoded_value);
}

json decode_bencoded_dict(const std::string& encoded_value, size_t& index) {
	json dict = json::object();

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

		dict[key.get<std::string>()] = val;
	}

	index++;  // skip ending 'e'
	return dict;
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

std::string read_file(const std::string& file_path) {
	std::ifstream file(file_path, std::ios::binary);
	std::stringstream buffer;
	if (file) {
		buffer << file.rdbuf();
		file.close();
		return buffer.str();
	} else {
		throw std::runtime_error("Failed to open file: " + file_path);
	}
}

json parse_torrent_file(const std::string& filename) {
	std::string encoded_torrent_meta = read_file(filename);
	json decoded_torrent_meta = decode_bencoded_value(encoded_torrent_meta);

	return decoded_torrent_meta;
}

std::string json_to_bencode(const json& j) {
	std::ostringstream os;
	if (j.is_object()) {
		os << 'd';
		for (auto& el : j.items()) {
			os << el.key().size() << ':' << el.key()
			   << json_to_bencode(el.value());
		}
		os << 'e';
	} else if (j.is_array()) {
		os << 'l';
		for (const json& item : j) {
			os << json_to_bencode(item);
		}
		os << 'e';
	} else if (j.is_number_integer()) {
		os << 'i' << j.get<int>() << 'e';
	} else if (j.is_string()) {
		const std::string& value = j.get<std::string>();
		os << value.size() << ':' << value;
	}
	return os.str();
}

std::string sha1_hash(const std::string& message) {
	SHA1 sha1;
	sha1.update(message);
	return sha1.final();
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
		std::string encoded_value = argv[2];
		json decoded_value = decode_bencoded_value(encoded_value);
		std::cout << decoded_value.dump() << std::endl;
	} else if (command == "info") {
		if (argc < 3) {
			std::cerr << "Usage: " << argv[0] << " info <filename>"
					  << std::endl;
			return 1;
		}
		std::string filename = argv[2];
		json decoded_meta = parse_torrent_file(filename);
		// get tracker URL
		std::string tracker_url = decoded_meta["announce"];
		std::cout << "Tracker URL: " + tracker_url + "\n";
		// get length
		std::int64_t length = decoded_meta["info"]["length"];
		std::cout << "Length: " + std::to_string(length) + "\n";
		// get info, bencode it and hash it
		json info = decoded_meta["info"];
		std::string encoded_info = json_to_bencode(info);
		std::cout << "Encoded Info: " << encoded_info << std::endl;
		std::string info_hash = sha1_hash(encoded_info);
		std::cout << "Info Hash: " << info_hash << std::endl;
		// get piece length
		std::int64_t piece_length = decoded_meta["info"]["piece length"];
		std::cout << "Piece Length: " + std::to_string(piece_length) + "\n";
		// get piece hashes and print them in hex
		std::string pieces = decoded_meta["info"]["pieces"];
		std::cout << "Pieces: " << pieces << std::endl;
		std::vector<std::string> piece_hashes;
		for (size_t i = 0; i < pieces.size(); i += 20) {
			piece_hashes.push_back(pieces.substr(i, 20));
		}
		std::cout << "Piece Hashes: " << std::endl;
	} else {
		std::cerr << "unknown command: " << command << std::endl;
		return 1;
	}

	return 0;
}
