#ifdef _MSC_VER
#include "targetver.h"
#endif

#include <iostream>
#include <fstream>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <json_spirit.h>
#if defined(USE_SQLITE)
#include "sqlite3.h"
#endif

#include "asserts.hpp"
#include "server.hpp"
#include "shared_data.hpp"
#include "game_server_worker.hpp"

namespace 
{
	const int64_t default_polling_interval = 5000;
	const std::string default_lobby_config_file = "lobby-config.cfg";
}


int main(int argc, char* argv[])
{
	std::vector<std::string> args;
	std::string lobby_config_file = default_lobby_config_file;

	// Push the command-line arguments into an array
	for(int i = 1; i < argc; ++i) {
		args.push_back(argv[i]);
	}

	for(auto a : args) {
		std::vector<std::string> seperated_args;
		boost::split(seperated_args, a, boost::lambda::_1 == '=');
		if(seperated_args[0] == "--config-file" || seperated_args[0] == "-n") {
			lobby_config_file = seperated_args[1];
		}
	}

	std::ifstream is(lobby_config_file);
	json_spirit::mValue value;
	json_spirit::read(is, value);
	ASSERT_LOG(value.type() == json_spirit::obj_type, "lobby-config.cfg should be an object.");
	auto cfg_obj = value.get_obj();
	if(cfg_obj.find("arguments") != cfg_obj.end()) {
		for(const auto& v : cfg_obj["arguments"].get_array()) {
			args.push_back(v.get_str());
		}
	}

#if defined(USE_SQLITE)
	sqlite3* db = nullptr;
	int rc = sqlite3_open("lobby-data.db", &db);
	if(rc) {
		std::cerr << "Can't open database!" << sqlite3_errmsg(db) << std::endl;
		sqlite3_close(db);
		db = nullptr;
	}
#endif

	int64_t polling_interval = default_polling_interval;
	auto it = cfg_obj.find("server_polling_interval");
	if(it != cfg_obj.end() && (it->second.type() == json_spirit::real_type 
		|| it->second.type() == json_spirit::int_type)) {
		polling_interval = int64_t(it->second.get_real() * 1000.0);
	}

	std::vector<std::pair<boost::shared_ptr<game_server::worker>, boost::shared_ptr<boost::thread> > > server_thread_list;

	try {
		auto listen_obj = cfg_obj["listen"].get_obj();
		std::size_t num_threads = cfg_obj["threads"].get_int();
		std::string file_path = cfg_obj["file_path"].get_str();

		game_server::shared_data shared_data;

		auto gs_ary = cfg_obj["game_server"].get_array();
			// Create a tasks to poll the game servers
		for(auto game_servers : gs_ary) {
			auto gs_obj = game_servers.get_obj();
			std::string gs_addr = gs_obj["address"].get_str();
			std::string gs_port = gs_obj["port"].get_str();

			boost::shared_ptr<game_server::worker> game_server_reader = boost::shared_ptr<game_server::worker>(new game_server::worker(polling_interval, shared_data, gs_addr, gs_port));
			boost::shared_ptr<boost::thread> game_server_thread = boost::shared_ptr<boost::thread>(new boost::thread(*game_server_reader));
			server_thread_list.push_back(std::pair<boost::shared_ptr<game_server::worker>, boost::shared_ptr<boost::thread> >(game_server_reader, game_server_thread));
		}
	
		// Initialise the server.
		http::server::server s(listen_obj["address"].get_str(), 
			listen_obj["port"].get_str(), 
			file_path, 
			num_threads, 
			shared_data);

		// Run the server until stopped.
		s.run();

		// Abort thread and wait till it finishes
		for(auto p : server_thread_list) {
			p.first->abort();
			p.second->join();
		}
	} catch(std::exception& e) {
		ASSERT_LOG(false, "exception: " << e.what());
	}

#if defined(USE_SQLITE)
	if(db) {
		sqlite3_close(db);
	}
#endif
	return 0;
}
