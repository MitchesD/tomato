#pragma once

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>
#include <stdexcept>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/noncopyable.hpp>
#include <enet/enet.h>

//#include "world.hh"
class World;

#define DEFAULT_PORT 1234

class Server: public boost::noncopyable {
  public:
	Server(World* world, int port = DEFAULT_PORT): m_quit(false), m_world(world) {
		m_address.host = ENET_HOST_ANY;
		m_address.port = port;
		// Create host at address, max_conns, unlimited up/down bandwith
		m_server = enet_host_create(&m_address, 16, 0, 0);
		if (m_server == NULL)
			throw std::runtime_error("An error occurred while trying to create an ENet server host.");
		// Start listener thread
		m_thread = boost::thread(boost::bind(&Server::listen, boost::ref(*this)));
	}

	~Server() {
		enet_host_destroy(m_server);
	}

	void listen() {
		ENetEvent e;
		while (!m_quit) {
			enet_host_service(m_server, &e, 1000);
			switch (e.type) {
			case ENET_EVENT_TYPE_CONNECT:
				std::cout << "Client connected from " << e.peer->address.host << ":" << e.peer->address.port << std::endl;
				// TODO: Create player
				e.peer->data = NULL;
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				// TODO: Handle receive
				printf ("A packet of length %u containing %s was received on channel %u.\n",
						e.packet -> dataLength,
						e.packet -> data,
						e.channelID);

				enet_packet_destroy (e.packet); // Clean-up
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				std::cout << "Client disconnected." << std::endl;
				// TODO: Delete player
				e.peer->data = NULL;
				break;
			default:
				break;
			}
		}

	}

	/// Send a string
	void send_to_all(std::string msg, int flag = 0) {
		ENetPacket* packet = enet_packet_create (msg.c_str(), msg.length(), flag);
		enet_host_broadcast(m_server, 0, packet); // Send through channel 0 to all peers
		enet_host_flush (m_server); // Don't dispatch events
	}

	/// Send a char
	void send_to_all(char ch, int flag = 0) {
		send_to_all(std::string(ch, 1), flag);
	}

	void terminate() { m_quit = true; }

  private:
	bool m_quit;
	World* m_world;
	ENetAddress m_address;
	ENetHost* m_server;
	boost::thread m_thread;
};


class Client: public boost::noncopyable {
  public:
	/// Construct new
	Client(std::string host = "localhost", int port = DEFAULT_PORT): m_quit(false) {
		m_client = enet_host_create(NULL, 2, 0, 0);
		if (m_client == NULL)
			throw std::runtime_error("An error occurred while trying to create an ENet server host.");

		enet_address_set_host(&m_address, host.c_str());
		m_address.port = port;
		// Initiate the connection, allocating the two channels 0 and 1.
		m_peer = enet_host_connect(m_client, &m_address, 2);
		if (m_peer == NULL)
			throw std::runtime_error("No available peers for initiating an ENet connection.");
		// Wait up to 5 seconds for the connection attempt to succeed.
		ENetEvent event;
		if (enet_host_service (m_client, &event, 5000) > 0 &&
		  event.type == ENET_EVENT_TYPE_CONNECT) {
			// Start listener thread
			m_thread = boost::thread(boost::bind(&Client::listen, boost::ref(*this)));
		} else { // Failure
			enet_peer_reset(m_peer);
			throw std::runtime_error(std::string("Connection to ") + host + " failed!");
		}
	}

	~Client() {
		enet_host_destroy(m_client);
	}

	void listen() {
		ENetEvent e;
		while (!m_quit) {
			enet_host_service(m_client, &e, 1000);
			switch (e.type) {
			case ENET_EVENT_TYPE_RECEIVE:
				// TODO: Handle receive
				printf ("A packet of length %u containing %s was received on channel %u.\n",
						e.packet -> dataLength,
						e.packet -> data,
						e.channelID);

				enet_packet_destroy (e.packet); // Clean-up
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				std::cout << "Server disconnected." << std::endl;
				// TODO: Handle
				e.peer->data = NULL;
				break;
			default:
				break;
			}
		}

	}

	/// Send a string
	void send(std::string msg, int flag = 0) {
		ENetPacket* packet = enet_packet_create (msg.c_str(), msg.length(), flag);
		enet_peer_send (m_peer, 0, packet); // Send through channel 0
		enet_host_flush (m_client); // Don't dispatch events
	}

	/// Send a char
	void send(char ch, int flag = 0) {
		send(std::string(ch, 1), flag);
	}

	void terminate() { m_quit = true; }

  private:
	bool m_quit;
	World* m_world;
	ENetAddress m_address;
	ENetHost* m_client;
	ENetPeer* m_peer;
	boost::thread m_thread;
};

