#pragma once
#include "../Shared.hpp"

#include <fstream>
#include <filesystem>
#include <ranges>
#include <unordered_set>

// Will set to the ip of the machine running the server
constexpr static const char* ip = "0.0.0.0";

class Server {
private:
    socket_t server_socket_ = {};
    std::unordered_set<socket_t> clients_ = {};
    std::unordered_set<socket_t> socket_set_ = {};
    std::unordered_map<socket_t, std::string> users_ = {};

    std::mutex clients_mutex_ = {};
    std::mutex user_mutex_ = {};

    fd_set master_set_ = {};
public:
    Server( ) {
#ifdef _WIN32
		// Initialize Winsock
        WSADATA wsaData = {};
        if ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 ) {
            throw std::runtime_error( "WSAStartup failed" );
        }
#endif
		// Create a socket
        server_socket_ = socket( AF_INET, SOCK_STREAM, 0 );
		// Check if the socket was created successfully
        if ( server_socket_ == INVALID_SOCKET_VAL ) {
            // Cleanup and throw error
            CLOSESOCKET( server_socket_ );
#ifdef _WIN32
            WSACleanup( );
#endif
            throw std::runtime_error( "Could not create socket" );
        }

		// Set up the server address structure
        sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;

		// Convert the IP address from string to binary form
        if ( inet_pton( AF_INET, ip, &server_addr.sin_addr ) <= 0 ) {
            // Cleanup and throw error
            CLOSESOCKET( server_socket_ );
#ifdef _WIN32
            WSACleanup( );
#endif
            throw std::runtime_error( "Invalid IP address" );
        }
		// Set the port number in network byte order
        server_addr.sin_port = htons( port );

		// Bind the socket to the address and port
        if ( bind( server_socket_, reinterpret_cast< struct sockaddr* >( &server_addr ), sizeof( server_addr ) ) < 0 ) {
            // Cleanup and throw error
            CLOSESOCKET( server_socket_ );
#ifdef _WIN32
            WSACleanup( );
#endif
            throw std::runtime_error( "Bind failed" );
        }

		// Start listening for incoming connections
        if ( listen( server_socket_, SOMAXCONN ) < 0 ) {
            // Cleanup and throw error
            CLOSESOCKET( server_socket_ );
#ifdef _WIN32
            WSACleanup( );
#endif
            throw std::runtime_error( "Listen failed" );
        }

        //  Set the socket as non blocking
#ifdef _WIN32
        u_long non_blocking = 1;
        if ( ioctlsocket( server_socket_, FIONBIO, &non_blocking ) != 0 ) {
            // Cleanup and throw error
            CLOSESOCKET( server_socket_ );
            WSACleanup( );
            throw std::runtime_error( "Failed to set non-blocking mode" );
        }
#else
        int flags = fcntl( server_socket_, F_GETFL, 0 );
        fcntl( server_socket_, F_SETFL, flags | O_NONBLOCK );
#endif

		// Initialize the master set for select and add the server socket to the socket set
        FD_ZERO( &master_set_ );
        FD_SET( server_socket_, &master_set_ );
        socket_set_.insert( server_socket_ );
    }

    ~Server( ) {
        // Notify all threads to shut down
        Shared::end_mt( );
		// Shutdown the server socket
        shutdown( server_socket_, SD_BOTH );
        // Close the client socket
        CLOSESOCKET( server_socket_ );
#ifdef _WIN32
        // Cleanup Winsock
        WSACleanup( );
#endif
    }
private:
    void cleanup_client( socket_t client_socket, std::string username, bool http_request );
    void handle_client( socket_t client_socket );
    void accept_new_client( );
public:
    void run( );
};
