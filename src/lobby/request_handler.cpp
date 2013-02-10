//
// request_handler.cpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2012 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/lexical_cast.hpp>

#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <map>

#include "asserts.hpp"
#include "request_handler.hpp"
#include "mime_types.hpp"
#include "name.hpp"
#include "reply.hpp"
#include "request.hpp"
#include "game_server_worker.hpp"

namespace http {
namespace server {

namespace
{
	const int64_t game_server_wait_interval_ms = 30000;

	enum start_game_errors
	{
		game_created,
		max_player_size_exceeded,
		not_enough_human_players,
		game_server_timed_out,
		game_server_failed_create,
		game_server_bad_response,
	};

	void process_start_game_message(const std::string& game_type, 
		const json_spirit::mArray& players, 
		int bots, 
		json_spirit::mObject& reply_object)
	{
		json_spirit::mObject m;
		game_server::worker& w = game_server::worker::get_server_from_game_type(game_type);
		const game_server::server_info& si = w.get_server_info();
		if(players.size() > si.max_players) {
			reply_object["type"] = "error";
			reply_object["description"] = "Maximum number of human players exceeded.";
			return;
		}
		if(players.size() < si.min_humans) {
			reply_object["type"] = "error";
			reply_object["description"] = "Not enough human players supplied.";
			return;
		}
		if(players.size() + bots < si.min_players) {
			bots = si.min_players - players.size();
		}
		if(players.size() + bots > si.max_players) {
			bots = si.max_players - players.size();
		}
		json_spirit::mArray u_ary;
		// {type: 'create_game', game_type: 'citadel', users: [{user: 'a', session_id: 1}, {user: 'b', bot: true, session_id: 2}]}
		// Add Bots
		u_ary.insert(u_ary.end(), players.begin(), players.end());
		for(int n = 0; n < bots; ++n) {
			json_spirit::mObject b_obj;
			b_obj["user"] = name::generate_bot_name();
			b_obj["bot"] = true;
			b_obj["session_id"] = game_server::shared_data::make_session_id();
			u_ary.push_back(b_obj);
		}
		m["type"] = "create_game";
		m["game_type"] = game_type;
		m["users"] = u_ary;
		// Push 'create_game' message to the game server.
		game_server::message msg;
		msg.msg = json_spirit::write(m);
		msg.reply = game_server::string_queue_ptr(new game_server::string_queue);
		w.get_queue()->push(msg);
		std::string reply;
		if(msg.reply->wait_and_pop(reply, game_server_wait_interval_ms)) {
			json_spirit::mValue rv;
			json_spirit::read(reply, rv);
			json_spirit::mObject ro = rv.get_obj();
			if(ro.find("type") != ro.end()) {
				const std::string& type = ro["type"].get_str();
				if(type == "create_game_failed") {
					reply_object["type"] = "error";
					reply_object["description"] = "Server replied: Create game failed.";
				} else if(type == "game_created") {

					for(auto it : ro) {
						reply_object[it.first] = it.second;
					}
				} else {
					reply_object["type"] = "error";
					reply_object["description"] = "Didn't understand server response. 'type' unknown: " + type;
				}
			} else {
				reply_object["type"] = "error";
				reply_object["description"] = "Didn't understand server response. No 'type' attribute";
			}
		} else {
			// XXX we could really do a fail-over and retry the another server with this.
			reply_object["type"] = "error";
			reply_object["description"] = "Timeout waiting for game server to reply.";
		}
	}
}

request_handler::request_handler(const std::string& doc_root, game_server::shared_data& data)
  : doc_root_(doc_root), data_(data)
{
}

void request_handler::handle_request(const request& req, reply& rep)
{
	if(req.method == "POST") {
		handle_post(req, rep);
	} else if(req.method == "GET") {
		handle_get(req, rep);
	} else {
		// handled
		std::cerr << "Unhandled request: " << req.method << std::endl;
		rep = reply::stock_reply(reply::not_implemented);
	}
}

void request_handler::handle_get(const request& req, reply& rep)
{
  // Decode url to path.
  std::string request_path; 
  if (!url_decode(req.uri, request_path))
  {
    rep = reply::stock_reply(reply::bad_request);
    return;
  }

  // Request path must be absolute and not contain "..".
  if (request_path.empty() || request_path[0] != '/'
      || request_path.find("..") != std::string::npos)
  {
    rep = reply::stock_reply(reply::bad_request);
    return;
  }

  // If path ends in slash (i.e. is a directory) then add "index.html".
  if (request_path[request_path.size() - 1] == '/')
  {
    request_path += "index.html";
  }

  // Determine the file extension.
  std::size_t last_slash_pos = request_path.find_last_of("/");
  std::size_t last_dot_pos = request_path.find_last_of(".");
  std::string extension;
  if (last_dot_pos != std::string::npos && last_dot_pos > last_slash_pos)
  {
    extension = request_path.substr(last_dot_pos + 1);
  }

  // Open the file to send back.
  std::string full_path = doc_root_ + request_path;
  std::unique_ptr<std::istream> is = std::unique_ptr<std::istream>(new std::ifstream(full_path.c_str(), std::ios::in | std::ios::binary));
  if(is == nullptr || !(*is))
  {
	rep = reply::stock_reply(reply::not_found);
	return;
  }

  // Fill out the reply to be sent to the client.
  rep.status = reply::ok;
  char buf[512];
  while (is->read(buf, sizeof(buf)).gcount() > 0)
    rep.content.append(buf, unsigned(is->gcount()));
  rep.headers.resize(2);
  rep.headers[0].name = "Content-Length";
  rep.headers[0].value = boost::lexical_cast<std::string>(rep.content.size());
  rep.headers[1].name = "Content-Type";
  rep.headers[1].value = mime_types::extension_to_type(extension);
}

bool request_handler::url_decode(const std::string& in, std::string& out)
{
  out.clear();
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size(); ++i)
  {
    if (in[i] == '%')
    {
      if (i + 3 <= in.size())
      {
        int value = 0;
        std::istringstream is(in.substr(i + 1, 2));
        if (is >> std::hex >> value)
        {
          out += static_cast<char>(value);
          i += 2;
        }
        else
        {
          return false;
        }
      }
      else
      {
        return false;
      }
    }
    else if (in[i] == '+')
    {
      out += ' ';
    }
    else
    {
      out += in[i];
    }
  }
  return true;
}

void request_handler::create_json_reply(const json_spirit::mValue& value, reply& rep)
{
	rep.content = json_spirit::write(value);
	rep.status = reply::ok;
	rep.headers.resize(2);
	rep.headers[0].name = "Content-Length";
	rep.headers[0].value = boost::lexical_cast<std::string>(rep.content.size());
	rep.headers[1].name = "Content-Type";
	rep.headers[1].value = "application/json";
}

void request_handler::handle_post(const request& req, reply& rep)
{
	json_spirit::mObject obj;
	if(req.uri != "/tbs") {
		rep = reply::stock_reply(reply::not_found);
		return;
	}
	std::cerr << "POST /tbs" << std::endl;
	std::cerr << "BODY: " << req.body << std::endl;

	json_spirit::mValue value;
	json_spirit::read(req.body, value);

	if(value.type() != json_spirit::obj_type) {
		rep = reply::bad_request_with_detail("Not valid JSON data. Data was: <pre>" + req.body + "</pre>");
		return;
	}
	auto req_obj = value.get_obj();
	auto it = req_obj.find("type");
	if(it == req_obj.end()) {
		obj["type"] = "error";
		obj["description"] = "No 'type' field in request";
		return create_json_reply(json_spirit::mValue(obj), rep);
	}
	const std::string& type = it->second.get_str();

	int session_id = -1;
	it = req_obj.find("session_id");
	if(it != req_obj.end()) {
		if(it->second.type() == json_spirit::int_type) {
			session_id = it->second.get_int();
		} else {
			obj["type"] = "error";
			obj["error"] = "'session_id' attribute must be integer type";
			return create_json_reply(json_spirit::mValue(obj), rep);
		}
	}

	it = req_obj.find("user");
	if(it == req_obj.end()) {
		obj["type"] = "error";
		obj["error"] = "No 'user' field in request";
		return create_json_reply(json_spirit::mValue(obj), rep);
	}
	if(it->second.type() != json_spirit::str_type) {
		obj["type"] = "error";
		obj["error"] = "'user' attribute must be string type";
		return create_json_reply(json_spirit::mValue(obj), rep);
	}
	const std::string& user = it->second.get_str();
	if(user.empty()) {
		obj["type"] = "error";
		obj["error"] = "'user' field is empty.";
		return create_json_reply(json_spirit::mValue(obj), rep);
	}

	std::string pass;
	it = req_obj.find("password");
	if(it != req_obj.end()) {
		if(it->second.type() == json_spirit::str_type) {
			pass = it->second.get_str();
		}
	}

	if(type == "login") {
		game_server::shared_data::action action;
		game_server::client_info ci;
		boost::tie(action, ci) = data_.process_user(user, pass, session_id);
		if(action == game_server::shared_data::bad_session_id) {
			obj["type"] = "error";
			obj["description"] = "Bad session ID";
		} else if(action == game_server::shared_data::send_salt) {
			obj["type"] = "password_request";
			obj["salt"] = ci.salt;
			obj["session_id"] = ci.session_id;
		} else if(action == game_server::shared_data::user_not_found) {
			obj["type"] = "error";
			obj["description"] = "User not found";
		} else if(action == game_server::shared_data::password_failed) {
			obj["type"] = "error";
			obj["description"] = "Password failed.";
		} else if(action == game_server::shared_data::login_success) {
			obj["type"] = "login_success";
			obj["session_id"] = ci.session_id;
		} else {
			ASSERT_LOG(false, "Unhandled state: " << static_cast<int>(action));
		}
	} else if(type == "get_status") {
		json_spirit::mArray ary;
		data_.get_status_list(&ary);
		obj["type"] = "status";
		obj["clients"] = ary;
	} else if(type == "get_server_info") {
		json_spirit::mObject si_obj;
		game_server::worker::get_server_info(&si_obj);
		obj["type"] = "server_info";
		obj["servers"] = si_obj;
	} else if(type == "start_game") {
		std::string game_type = req_obj["game_type"].get_str();
		json_spirit::mArray players = req_obj["players"].get_array();
		int bots = req_obj["bots"].get_int();
		process_start_game_message(game_type, players, bots, obj);
	} else if(type == "quit") {
		if(data_.sign_off(user, session_id)) {
			obj["type"] = "bye";
		}
		// We just return no content if session id and username not valid.
	} else {
		obj["type"] = "error";
		obj["description"] = "Unknown type of request";
	}
	create_json_reply(json_spirit::mValue(obj), rep);
}

} // namespace server
} // namespace http
