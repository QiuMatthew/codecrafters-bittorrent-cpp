#include <arpa/inet.h>
#include <sys/socket.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "lib/hash/sha1.hpp"
#include "lib/http/HTTPRequest.hpp"
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

std::string hex_string_to_bytes(const std::string& hex_string) {
	std::string bytes;
	for (size_t i = 0; i < hex_string.size(); i += 2) {
		std::string byte_string = hex_string.substr(i, 2);
		char byte = (char)std::strtol(byte_string.c_str(), nullptr, 16);
		bytes.push_back(byte);
	}
	return bytes;
}

std::string byte_string_to_hex(const std::string& byte_string) {
	std::ostringstream hex;
	for (unsigned char c : byte_string) {
		hex << std::hex << std::setw(2) << std::setfill('0') << (int)c;
	}
	return hex.str();
}

std::string url_encode(const std::string& input) {
	std::ostringstream encoded;
	for (unsigned char c : input) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			encoded << c;
		} else {
			encoded << '%' << std::hex << std::setw(2) << std::setfill('0')
					<< (int)c;
		}
	}
	return encoded.str();
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
		// get piece hashes
		std::string pieces = decoded_meta["info"]["pieces"];
		std::cout << "Pieces: " << pieces << std::endl;
		std::vector<std::string> piece_hashes;
		for (size_t i = 0; i < pieces.size(); i += 20) {
			piece_hashes.push_back(pieces.substr(i, 20));
		}
		// print in hex, filling with leading 0
		std::cout << "Piece Hashes: " << std::endl;
		for (const std::string& piece_hash : piece_hashes) {
			for (unsigned char c : piece_hash) {
				std::cout << std::hex << std::setw(2) << std::setfill('0')
						  << (int)c;
			}
			std::cout << std::endl;
		}
	} else if (command == "peers") {
		if (argc < 3) {
			std::cerr << "Usage: " << argv[0] << " peer <filename>"
					  << std::endl;
			return 1;
		}
		std::string filename = argv[2];
		json decoded_meta = parse_torrent_file(filename);
		// get tracker URL
		std::string tracker_url = decoded_meta["announce"];
		std::cout << "Tracker URL: " + tracker_url + "\n";
		// get info, bencode it and hash it
		json info = decoded_meta["info"];
		std::string encoded_info = json_to_bencode(info);
		std::cout << "Encoded Info: " << encoded_info << std::endl;
		std::string info_hash_hex = sha1_hash(encoded_info);
		std::cout << "Info Hash: " << info_hash_hex << std::endl;
		std::string info_hash_bytes = hex_string_to_bytes(info_hash_hex);
		std::cout << "Info Hash Bytes: " << info_hash_bytes << std::endl;
		std::string info_hash = url_encode(info_hash_bytes);
		std::cout << "Info Hash URL Encoded: " << info_hash << std::endl;
		// get peer id
		std::string peer_id = "12345678901234567890";
		std::cout << "Peer ID: " << peer_id << std::endl;
		// get port
		std::int64_t port = 6881;
		std::cout << "Port: " << port << std::endl;
		// get length
		std::int64_t length = decoded_meta["info"]["length"];
		std::cout << "Length: " + std::to_string(length) + "\n";
		// send request to tracker
		std::string request_url =
			tracker_url + "?info_hash=" + info_hash + "&peer_id=" + peer_id +
			"&port=" + std::to_string(port) +
			"&uploaded=0&downloaded=0&left=" + std::to_string(length) +
			"&compact=1";
		std::cout << "Request URL: " << request_url << std::endl;
		http::Request request(request_url);
		http::Response response = request.send("GET");
		std::string response_body{response.body.begin(), response.body.end()};
		std::cout << "Response: " << response_body << std::endl;
		json decoded_response = decode_bencoded_value(response_body);
		// if request failed, return 1
		if (decoded_response.find("failure reason") != decoded_response.end()) {
			std::cerr << "Failed to get peers: "
					  << decoded_response["failure reason"] << std::endl;
			return 1;
		}
		// get peers
		std::string peers = decoded_response["peers"];
		std::cout << "Peers: " << peers << std::endl;
		std::cout << "Peers: " << std::endl;
		for (size_t i = 0; i < peers.size(); i += 6) {
			std::string ip = std::to_string((unsigned char)peers[i]) + "." +
							 std::to_string((unsigned char)peers[i + 1]) + "." +
							 std::to_string((unsigned char)peers[i + 2]) + "." +
							 std::to_string((unsigned char)peers[i + 3]);
			std::int64_t port =
				(unsigned char)peers[i + 4] << 8 | (unsigned char)peers[i + 5];
			std::cout << ip << ":" << port << std::endl;
		}
	} else if (command == "handshake") {
		if (argc < 4) {
			std::cerr << "Usage: " << argv[0]
					  << " handshake <filename> <peer_ip:port>" << std::endl;
			return 1;
		}
		std::string filename = argv[2];
		std::string peer_ip_port = argv[3];
		std::string peer_ip;
		std::int64_t peer_port;
		size_t colon_index = peer_ip_port.find(':');
		if (colon_index != std::string::npos) {
			peer_ip = peer_ip_port.substr(0, colon_index);
			peer_port = std::stoll(peer_ip_port.substr(colon_index + 1));
		} else {
			std::cerr << "Invalid peer IP:Port: " << peer_ip_port << std::endl;
			return 1;
		}
		// create a socket
		int sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) {
			std::cerr << "Failed to create socket" << std::endl;
			return 1;
		}
		// define the server address
		struct sockaddr_in serv_addr;
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(peer_port);
		if (inet_pton(AF_INET, peer_ip.c_str(), &serv_addr.sin_addr) <= 0) {
			std::cerr << "Invalid address: " << peer_ip << std::endl;
			return 1;
		}
		// connect to the server
		if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) <
			0) {
			std::cerr << "Failed to connect to peer" << std::endl;
			return 1;
		}
		// prepare handshake message
		json decoded_meta = parse_torrent_file(filename);
		std::string info_hash_hex =
			sha1_hash(json_to_bencode(decoded_meta["info"]));
		std::string info_hash_bytes = hex_string_to_bytes(info_hash_hex);
		std::vector<char> handshake_message;
		char protocol_length = 19;
		handshake_message.push_back(protocol_length);
		std::string protocol = "BitTorrent protocol";
		for (char c : protocol) {
			handshake_message.push_back(c);
		}
		for (int i = 0; i < 8; i++) {
			handshake_message.push_back(0);
		}
		for (char c : info_hash_bytes) {
			handshake_message.push_back(c);
		}
		std::string peer_id = "12345678901234567890";
		for (char c : peer_id) {
			handshake_message.push_back(c);
		}
		// send handshake message
		if (send(sockfd, handshake_message.data(), handshake_message.size(),
				 0) < 0) {
			std::cerr << "Failed to send handshake message" << std::endl;
			return 1;
		}
		// receive handshake message
		std::vector<char> handshake_response(68);
		if (recv(sockfd, handshake_response.data(), handshake_response.size(),
				 0) < 0) {
			std::cerr << "Failed to receive handshake response" << std::endl;
			return 1;
		}
		// check handshake response
		if (handshake_response[0] != 19) {
			std::cerr << "Invalid protocol length: "
					  << (int)handshake_response[0] << std::endl;
			return 1;
		}
		std::string protocol_response(handshake_response.begin() + 1,
									  handshake_response.begin() + 20);
		if (protocol_response != "BitTorrent protocol") {
			std::cerr << "Invalid protocol: " << protocol_response << std::endl;
			return 1;
		}
		std::string info_hash_response(handshake_response.begin() + 28,
									   handshake_response.begin() + 48);
		if (info_hash_response != info_hash_bytes) {
			std::cerr << "Invalid info hash: " << info_hash_response
					  << std::endl;
			return 1;
		}
		std::string peer_id_response(handshake_response.begin() + 48,
									 handshake_response.begin() + 68);
		std::string peer_id_hex = byte_string_to_hex(peer_id_response);
		std::cout << "Handshake successful" << std::endl;
		std::cout << "Peer ID: " << peer_id_hex << std::endl;
	} else {
		std::cerr << "unknown command: " << command << std::endl;
		return 1;
	}

	return 0;
}
